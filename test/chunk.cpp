#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_chunk_hl.h"
#include "../apps/support/static_image.h"
#endif

#include <stdio.h>

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Var x, y;
    Var xo, xi, yo, yi;
    
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = cast<float>(x);
    g(x, y) = f(x+1, y) + f(x-1, y);

    
    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        Var xi, yi;
        g.cuda_tile(x, y, 8, 8);
        f.compute_at(g, Var("blockidx")).cuda_threads(x, y);
    } else {    
        g.tile(x, y, xo, yo, xi, yi, 8, 8);
        f.compute_at(g, xo);
    }
#endif

#if HALIDE_STATIC_COMPILE
    g.compile_to_file("hl_chunk_hl");
#endif

#if HALIDE_STATIC_RUN
    printf("Calling function...\n");
    Image<float> im(32, 32);
    hl_chunk_hl(im);
#endif

#if HALIDE_JIT
    printf("Realizing function...\n");

    Image<float> im = g.realize(32, 32);
#endif

#if !HALIDE_STATIC_COMPILE
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (im(i,j) != 2*i) {
                printf("im[%d, %d] = %f\n", i, j, im(i,j));
                return -1;
            }
        }
    }

    printf("Success!\n");
#endif

    return 0;
}
