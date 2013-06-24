#include <stdio.h>

#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_bound_hl_f.h"
#include "hl_bound_hl_g.h"
#include "../apps/support/static_image.h"
#endif

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Var x, y, c;
    Func f, g, h;

    f(x, y) = max(x, y);
    g(x, y, c) = f(x, y) * c;
    
    g.bound(c, 0, 3);
#endif
    
#if HALIDE_STATIC_COMPILE
    f.compile_to_file("hl_bound_hl_f");
    g.compile_to_file("hl_bound_hl_g");
#endif

#if HALIDE_STATIC_RUN
    Image<int> imf(32, 32);
    hl_bound_hl_f(imf);
    Image<int> img(32, 32, 3);
    hl_bound_hl_g(img);
#endif

#if HALIDE_JIT
    Image<int> imf = f.realize(32, 32);
    Image<int> img = g.realize(32, 32, 3);
#endif

#if !HALIDE_STATIC_COMPILE
    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != (i > j ? i : j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return -1;
            }
            for (int c = 0; c < 3; c++) {
                if (img(i, j, c) != c*(i > j ? i : j)) {
                    printf("img[%d, %d, %d] = %d\n", i, j, c, img(i, j, c));
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
#endif

    return 0;
}
