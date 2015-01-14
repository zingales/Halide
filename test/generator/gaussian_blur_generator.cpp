#include "Halide.h"
#include "../test/performance/clock.h"

using namespace Halide;

namespace {

class GaussianBlur : public Halide::Generator<GaussianBlur> {
public:
    ImageParam input{ Float(32), 3, "input" };
    Param<float> sigma{ "sigma", 1.0f };

    Var x, y, c;

    Func blur(Func f, Func coeff, Expr size) {
        Func blurred;
        blurred(x, y, c) = undef<float>();

        // Warm up
        blurred(x, 0, c) = coeff(0) * f(x, 0, c);
        blurred(x, 1, c) = (coeff(0) * f(x, 1, c) +
                            coeff(1) * blurred(x, 0, c));
        blurred(x, 2, c) = (coeff(0) * f(x, 2, c) +
                            coeff(1) * blurred(x, 1, c) +
                            coeff(2) * blurred(x, 0, c));

        // Top to bottom
        RDom fwd(3, size - 3);
        blurred(x, fwd, c) = (coeff(0) * f(x, fwd, c) +
                              coeff(1) * blurred(x, fwd - 1, c) +
                              coeff(2) * blurred(x, fwd - 2, c) +
                              coeff(3) * blurred(x, fwd - 3, c));

        // Tail end
        Expr padding = cast<int>(ceil(4*sigma) + 3);
        RDom tail(size, padding);
        blurred(x, tail, c) = (coeff(1) * blurred(x, tail - 1, c) +
                               coeff(2) * blurred(x, tail - 2, c) +
                               coeff(3) * blurred(x, tail - 3, c));

        // Bottom to top
        Expr last = size + padding - 1;
        RDom backwards(0, last - 2);
        Expr b = last - 3 - backwards; // runs from last - 3 down to zero
        blurred(x, b, c) = (coeff(0) * blurred(x, b, c) +
                            coeff(1) * blurred(x, b + 1, c) +
                            coeff(2) * blurred(x, b + 2, c) +
                            coeff(3) * blurred(x, b + 3, c));
        return blurred;
    }

    Func blur_then_transpose(Func f, Func coeff, Expr size) {

        Func blurred = blur(f, coeff, size);

        // Also compute attenuation due to zero boundary condition by
        // blurring an image of ones in the same way. This gives a
        // boundary condition equivalent to reweighting the Gaussian
        // near the edge. (TODO: add a generator param to select
        // different boundary conditions).
        Func ones;
        ones(x, y, c) = 1.0f;
        Func attenuation = blur(ones, coeff, size);

        // Invert the attenuation so we can multiply by it. The
        // attenuation is the same for every row/channel so we only
        // need one column.
        Func inverse_attenuation;
        inverse_attenuation(y) = 1.0f / attenuation(0, y, 0);

        // Transpose it
        Func transposed;
        transposed(x, y, c) = blurred(y, x, c);

        // Correct for attenuation
        Func out;
        out(x, y, c) = transposed(x, y, c) * inverse_attenuation(x);

        // Schedule it.
        Var yi, xi, yii, xii;

        attenuation.compute_root();
        inverse_attenuation.compute_root().vectorize(y, 8);
        out.compute_root()
            .tile(x, y, xi, yi, 8, 32)
            .tile(xi, yi, xii, yii, 8, 8)
            .vectorize(xii).unroll(yii).parallel(y);
        blurred.compute_at(out, y);
        transposed.compute_at(out, xi).vectorize(y).unroll(x);

        for (int i = 0; i < blurred.num_update_definitions(); i++) {
            RDom r = blurred.reduction_domain(i);
            if (r.defined()) {
                blurred.update(i).reorder(x, r);
            }
            blurred.update(i).vectorize(x, 8).unroll(x);
        }

        return out;
    }

    Func build() override {

        // Compute IIR coefficients using the method of Young and Van Vliet.
        Func coeff;
        Expr q = select(sigma < 2.5f,
                        3.97156f - 4.14554f*sqrt(1 - 0.26891f*sigma),
                        q = 0.98711f*sigma - 0.96330f);
        Expr denom = 1.57825f + 2.44413f*q + 1.4281f*q*q + 0.422205f*q*q*q;
        coeff(x) = undef<float>();
        coeff(1) = (2.44413f*q + 2.85619f*q*q + 1.26661f*q*q*q)/denom;
        coeff(2) = -(1.4281f*q*q + 1.26661f*q*q*q)/denom;
        coeff(3) = (0.422205f*q*q*q)/denom;
        coeff(0) = 1 - (coeff(1) + coeff(2) + coeff(3));
        coeff.compute_root();

        Func f;
        f(x, y, c) = input(x, y, c);
        f = blur_then_transpose(f, coeff, input.height());
        f = blur_then_transpose(f, coeff, input.width());
        return f;
    }

    // Build a naive gaussian blur to use in the test.
    Func naive_blur() {

        // Add a ones channel at c = -1.
        Func with_alpha;
        with_alpha(x, y, c) = select(c < 0, 1.0f, input(x, y, max(0, c)));

        // Add a zero boundary condition.
        Func in = BoundaryConditions::constant_exterior(with_alpha, 0.0f,
                                                        0, input.width(),
                                                        0, input.height());

        // Define the Gaussian kernel.
        Func kernel;
        kernel(x) = exp(-x*x/(2*sigma*sigma));

        Expr radius = ceil(4*sigma);
        RDom r(-radius, 2*radius+1);

        // Blur.
        Func blurx;
        blurx(x, y, c) = sum(in(x + r, y, c) * kernel(r));

        Func blury;
        blury(x, y, c) = sum(blurx(x, y + r, c) * kernel(r));

        // Renormalize.
        Func normalized;
        normalized(x, y, c) = blury(x, y, c) / blury(x, y, -1);

        blurx.compute_root().parallel(y, 8).vectorize(x, 8);
        normalized.compute_root().parallel(y, 8).vectorize(x, 8);

        return normalized;

    }

    bool test() {
        Func f = build();

        // Make some noise input
        Image<float> in(1024, 768, 3);
        lambda(x, y, c, random_float()).realize(in);
        input.set(in);

        // Allocate an output buffer.
        Image<float> out(1024, 768, 3);

        // Check correctness.
        {
            // Also do a naive Gaussian at the same sigma with the same boundary condition and compare.
            Func reference = naive_blur();

            // Use a small sigma so that the naive version doesn't take forever.
            sigma.set(3.0f);

            Image<float> ref = reference.realize(1024, 768, 3);
            f.realize(out);
            RDom r(out);

            // Compute maximum absolute error.
            float error_max = evaluate<float>(maximum(abs(ref(r.x, r.y, r.z) - out(r.x, r.y, r.z))));

            // Compute average absolute error.
            float error_avg = evaluate<float>(sum(abs(ref(r.x, r.y, r.z) - out(r.x, r.y, r.z))));
            error_avg /= 1024 * 768 * 3;

            printf("Average error: %f\n"
                   "Maximum error: %f\n", error_avg, error_max);
            if (error_avg > 0.002f) {
                printf("Average error too high: %f\n", error_avg);
                return false;
            }
            if (error_max > 0.01f) {
                printf("Max error too high: %f\n", error_max);
                return false;
            }
        }

        // Report performance.
        {

            // Use a large sigma to demonstrate that the runtime of the algorithm doesn't depend much on sigma.
            sigma.set(20.0f);

            float megapixels = (in.width() * in.height() * in.channels()) / (1024.0f * 1024.0f);
            const int attempts = 5;
            const int iters = 10;
            double best_time = 1e20;
            for (int j = 0; j < attempts; j++) {
                double t1 = current_time();
                for (int i = 0; i < iters; i++) {
                    f.realize(out);
                }
                double t2 = current_time();
                best_time = std::min(best_time, t2 - t1);
            }
            printf("%f megapixels per millisecond\n", (megapixels*iters)/best_time);
        }

        return true;
    }

    void help(std::ostream &out) {
        out << "Floating-point Gaussian blur using an IIR. The kernel is truncated and "
            << "renormalized at the image edges.\n";
        GeneratorBase::help(out);
    }
};

Halide::RegisterGenerator<GaussianBlur> register_my_gen{"gaussian_blur"};

}  // namespace
