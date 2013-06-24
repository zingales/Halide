#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_chunk_sharing_hl.h"
#include "../apps/support/static_image.h"
#endif

#include <stdio.h>

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Var x("x"), y("y"), i("i"), j("j");
    Func a("a"), b("b"), c("c"), d("d");

    printf("Defining function...\n");

    a(i, j) = i + j;
    b(i, j) = a(i, j) + 1;
    c(i, j) = a(i, j) * 2;
    d(x, y) = b(x, y) + c(x, y);

    c.compute_at(d, y);
    b.compute_at(d, y);
    a.compute_at(d, y);
#endif

#if HALIDE_STATIC_COMPILE
    d.compile_to_file("hl_chunk_sharing_hl");
#endif

#if HALIDE_STATIC_RUN
    printf("Calling function...\n");

    Image<int> im(32, 32);
    hl_chunk_sharing_hl(im);
#endif

#if HALIDE_JIT
    printf("Realizing function...\n");

    Image<int> im = d.realize(32, 32);
#endif

#if !HALIDE_STATIC_COMPILE
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int a = x + y;
            int b = a + 1;
            int c = a * 2;
            int d = b + c;
            if (im(x, y) != d) {
                printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), d);
                return -1;
            }
        }
    }
#endif

    printf("Success!\n");
    return 0;
}
