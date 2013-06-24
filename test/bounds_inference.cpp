#include <stdio.h>

#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_bounds_inference_hl.h"
#include "../apps/support/static_image.h"
#endif

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Func f, g, h; Var x, y;
    
    h(x) = x;
    g(x) = h(x-1) + h(x+1);
    f(x, y) = (g(x-1) + g(x+1)) + y;

    h.compute_root();
    g.compute_root();

    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        f.cuda_tile(x, y, 16, 16);
        g.cuda_tile(x, 128);
        h.cuda_tile(x, 128);
    }
#endif

#if HALIDE_STATIC_COMPILE
    f.compile_to_file("hl_bounds_inference_hl");
#endif

#if HALIDE_STATIC_RUN
    Image<int> out(32, 32);
    hl_bounds_inference_hl(out);
#endif

#if HALIDE_JIT
    Image<int> out = f.realize(32, 32);
#endif

#if !HALIDE_STATIC_COMPILE
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (out(x, y) != x*4 + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x*4+y);
                return -1;
            }
        }
    }

    printf("Success!\n");
#endif
    return 0;
}
