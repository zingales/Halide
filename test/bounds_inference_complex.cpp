#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_bounds_inference_complex_hl.h"
#include "../apps/support/static_image.h"
#endif

#include <stdio.h>

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    const int K = 8;

    Func f[K]; Var x, y;

    if (argc > 1) srand(atoi(argv[1]));
    else srand(0);
    
    f[0](x, y) = x+y;
    f[1](x, y) = x*y;
    for (int i = 2; i < K; i++) {
        int j1 = rand() % i;
        int j2 = rand() % i;
        int j3 = rand() % i; 
        f[i](x, y) = f[j1](x-1, y-1) + f[j2](x+1, clamp(f[j3](x+1, y-1), 0, 7));

        if (rand() & 1) {
            f[i].compute_root();
            f[i].vectorize(x, 4);
        }
    }
#endif

#if HALIDE_STATIC_COMPILE
    f[K-1].compile_to_file("hl_bounds_inference_complex_hl");
#endif

#if HALIDE_STATIC_RUN
    Image<int> out(32, 32);
    hl_bounds_inference_complex_hl(out);
#endif

#if HALIDE_JIT
    Image<int> out = f[K-1].realize(32, 32);
#endif

    printf("Success!\n");
    return 0;
}

