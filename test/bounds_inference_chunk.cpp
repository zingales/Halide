#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_bounds_inference_chunk_hl.h"
#include "../apps/support/static_image.h"
#endif

#include <stdio.h>

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Func f, g, h; Var x, y;
    
    h(x, y) = x + y;
    g(x, y) = (h(x-1, y-1) + h(x+1, y+1))/2;
    f(x, y) = (g(x-1, y-1) + g(x+1, y+1))/2;

    h.compute_root();
    g.compute_at(f, y);

    //f.trace();
#endif

#if HALIDE_STATIC_COMPILE
    f.compile_to_file("hl_bounds_inference_chunk_hl");
#endif

#if HALIDE_STATIC_RUN
    Image<int> out(32, 32); // WHY DOES THIS CRASH IF ARGS NOT PROVIDED?
    hl_bounds_inference_chunk_hl(out);
#endif

#if HALIDE_JIT
    Image<int> out = f.realize(32, 32);
#endif

#if !HALIDE_STATIC_COMPILE
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (out(x, y) != x + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x+y);
                return -1;
            }
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
