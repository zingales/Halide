#include <vector>
#include "Halide.h"

using namespace Halide;

namespace {

// Generator class for BLAS gemm operations.
template<class T>
class GEMMGenerator :
        public Generator<GEMMGenerator<T>> {
  public:
    typedef Generator<GEMMGenerator<T>> Base;
    using Base::target;
    using Base::get_target;
    using Base::natural_vector_size;

    GeneratorParam<bool> assertions_enabled_ = {"assertions_enabled", false};
    GeneratorParam<bool> use_fma_ = {"use_fma", false};
    GeneratorParam<bool> vectorize_ = {"vectorize", true};
    GeneratorParam<bool> parallel_ = {"parallel", true};
    GeneratorParam<int>  block_size_ = {"block_size", 1 << 5};
    GeneratorParam<bool> transpose_A_ = {"transpose_A", false};
    GeneratorParam<bool> transpose_B_ = {"transpose_B", false};

    // Standard ordering of parameters in GEMM functions.
    Param<T>   a_ = {"a", 1.0};
    ImageParam A_ = {type_of<T>(), 2, "A"};
    ImageParam B_ = {type_of<T>(), 2, "B"};
    Param<T>   b_ = {"b", 1.0};
    ImageParam C_ = {type_of<T>(), 2, "C"};

    void SetupTarget() {
        if (!assertions_enabled_) {
            target.set(get_target()
                       .with_feature(Target::NoAsserts)
                       .with_feature(Target::NoBoundsQuery));
        }

        if (use_fma_) {
            target.set(get_target().with_feature(Target::FMA));
        }
    }

    Func build() {
        SetupTarget();

        const int vec_size = vectorize_? natural_vector_size(type_of<T>()): 1;
        const Expr num_rows = transpose_A_? A_.height(): A_.width();
        const Expr num_cols = transpose_B_? B_.width(): B_.height();
        const Expr sum_size = transpose_A_? A_.width(): A_.height();
        const Expr proxy_size = ((sum_size + block_size_ - 1) / block_size_) * block_size_;

        Var i("i"), j("j"), n("n");
        Func result("result");
        Func At("At"), Bt("Bt");
        Func A, B;
        At(i, j) = A_(j, i);
        Bt(i, j) = B_(j, i);
        A(i, j) = transpose_A_?
                select(j < sum_size, At(i, clamp(j, 0, sum_size)), cast<T>(0)):
                select(j < sum_size, A_(i, clamp(j, 0, sum_size)), cast<T>(0));
        B(i, j) = transpose_B_?
                select(i < sum_size, Bt(clamp(i, 0, sum_size), j), cast<T>(0)):
                select(i < sum_size, B_(clamp(i, 0, sum_size), j), cast<T>(0));

        RDom k(0, proxy_size);
        Func prod("prod");
        prod(i, j) = b_ * C_(i, j);
        prod(i, j) += a_ * A(i, k) * B(k, j);
        result(i, j) = prod(i, j);

        if (vectorize_) {
          Var ii("ii"), ji("ji");
          result.specialize(num_rows >= block_size_ && num_cols >= block_size_)
              .tile(i, j, ii, ji, block_size_, block_size_).parallel(j)
              .vectorize(ii, vec_size).unroll(ii);

          result.specialize(num_rows >= vec_size).vectorize(i, vec_size);

          RVar ki("ki");
          prod.compute_at(result, i);
          prod.vectorize(i, vec_size).unroll(i);
          prod.update(0)
              .split(k, k, ki, block_size_)
              .reorder(i, j, ki, k)
              .vectorize(i, vec_size).unroll(i);

          if (transpose_A_) {
            At.compute_at(prod, k).vectorize(j, vec_size).unroll(i);
          }

          A_.set_min(0, 0).set_min(1, 0);
          B_.set_bounds(0, 0, sum_size).set_min(1, 0);
          C_.set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);
          result.output_buffer().set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);
        }

        return result;
    }
};

RegisterGenerator<GEMMGenerator<float>>    register_sgemm("sgemm");
RegisterGenerator<GEMMGenerator<double>>   register_dgemm("dgemm");

}  // namespace
