#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_compare_vars_hl.h"
#include "../apps/support/static_image.h"
#endif

#include <stdio.h>

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Func f;
    Var x, y;
    f(x, y) = select(x == y, 1, 0);
#endif

#if HALIDE_STATIC_COMPILE
    f.compile_to_file("hl_compare_vars_hl");
#endif

#if HALIDE_STATIC_RUN
    Image<int> im(10, 10);
    hl_compare_vars_hl(im);
#endif

#if HALIDE_JIT
    Image<int> im = f.realize(10, 10);
#endif

#if !HALIDE_STATIC_COMPILE
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            int correct = (x == y) ? 1 : 0;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
#endif

    return 0;
}
