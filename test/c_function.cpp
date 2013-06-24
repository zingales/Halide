#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_c_function_hl.h"
#include "../apps/support/static_image.h"
#endif

#include <stdio.h>

// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols
// This is not supported by the C PseudoJIT backend.

int call_counter = 0;
extern "C" float my_func(int x, float y) {
    call_counter++;
    return x*y;
}
#if !HALIDE_STATIC_RUN
HalideExtern_2(float, my_func, int, float);
#endif

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Var x, y;
    Func f;

    f(x, y) = my_func(x, cast<float>(y));
#endif

#if HALIDE_STATIC_COMPILE
    f.compile_to_file("hl_c_function_hl");
#endif

#if HALIDE_STATIC_RUN
    Image<float> imf(32, 32);
    hl_c_function_hl(imf);
#endif

#if HALIDE_JIT
    Image<float> imf = f.realize(32, 32);
#endif

#if !HALIDE_STATIC_COMPILE
    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (float)(i*j);
	    float delta = imf(i, j) - correct;
            if (delta < -0.001 || delta > 0.001) {
                printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                return -1;
            }
        }
    }

    if (call_counter != 32*32) {
        printf("C function was called %d times instead of %d\n", call_counter, 32*32);
        return -1;
    }

    printf("Success!\n");
#endif

    return 0;
}
