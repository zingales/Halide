#include <Halide.h>
#include <stdio.h>
#include "../common/random_expr.h"
#include "fuzz_codegen_common.h"

using namespace Halide;

Var x, y;

template <typename T>
Func random_exprs() {
    RandomExprGenerator fuzz;
    Image<T> params(param_count, sample_count);
    for (int i = 0; i < param_count; i++) {
        for (int j = 0; j < sample_count; j++) {
            params(i, j) = rand() % 256 - 128;
        }
        fuzz.leafs.push_back(params(i, y));
    }

    Func f;
    f(x, y) = undef<T>();
    for (int i = 0; i < expr_count; i++) {
        f(i, y) = fuzz.random_expr(type_of<T>(), 4);
    }

    return f;
}

template <typename T>
Expr test(Func f, Realization R, int dim) {
    Image<T> ref = R[dim];
    return select(f(x, y)[dim] == ref(x, y), 1, 0);
}

int main(int argc, char **argv) {
    // We want different fuzz tests every time, to increase coverage.
    // We also report the seed to enable reproducing failures.
    int fuzz_seed = time(NULL);
    srand(fuzz_seed);
    std::cout << "Simplify fuzz test seed: " << fuzz_seed << '\n';

    std::vector<Func> exprs;
    exprs.push_back(random_exprs<uint8_t>());
    exprs.push_back(random_exprs<uint16_t>());
    exprs.push_back(random_exprs<uint32_t>());
    exprs.push_back(random_exprs<int8_t>());
    exprs.push_back(random_exprs<int16_t>());
    exprs.push_back(random_exprs<int32_t>());
    //exprs.push_back(random_exprs<float>());

    std::vector<Expr> defs;
    for (size_t i = 0; i < exprs.size(); i++) {
        defs.push_back(exprs[i](x, y));
    }

    // Make a func with all of the type exprs stored in a Tuple.
    Func f;
    f(x, y) = Tuple(defs);

    // Realize the tester using the JIT target. Presumably, this is
    // going to be an x86 target.
    Realization R = f.realize(expr_count, sample_count, get_jit_target_from_environment());

    std::vector<Expr> tests;
    tests.push_back(test<uint8_t>(f, R, 0));
    tests.push_back(test<uint16_t>(f, R, 1));
    tests.push_back(test<uint32_t>(f, R, 2));
    tests.push_back(test<int8_t>(f, R, 3));
    tests.push_back(test<int16_t>(f, R, 4));
    tests.push_back(test<int32_t>(f, R, 5));
    //tests.push_back(test<float>(f, R, 6));

    Var z;
    Func g;
    g(x, y, z) = select(z == 0, test<uint8_t>(f, R, 0),
                        z == 1, test<uint16_t>(f, R, 1),
                        z == 2, test<uint32_t>(f, R, 2),
                        z == 3, test<int8_t>(f, R, 3),
                        z == 4, test<int16_t>(f, R, 4),
                                test<int32_t>(f, R, 5));

    g.compile_to_file("fuzz_codegen");
    return 0;
}
