#include <stdio.h>
#include <iostream>

#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_bounds_hl_f.h"
#include "hl_bounds_hl_g.h"
#include "hl_bounds_hl_h.h"
#include "../apps/support/static_image.h"
#endif

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    printf("Defining function...\n");

    f(x, y) = max(x, y);
    g(x, y) = min(x, y);
    h(x, y) = clamp(x+y, 20, 100);

    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        f.cuda_tile(x, y, 8, 8);
        g.cuda_tile(x, y, 8, 8);
        h.cuda_tile(x, y, 8, 8);
    }
    if (target && std::string(target) == "opencl") {
        f.cuda_tile(x, y, 32, 1);
        g.cuda_tile(x, y, 32, 1);
        h.cuda_tile(x, y, 32, 1);
    }
#endif
 
#if HALIDE_STATIC_COMPILE
    printf("Compiling functions...\n");

    f.compile_to_file("hl_bounds_hl_f");
    g.compile_to_file("hl_bounds_hl_g");
    h.compile_to_file("hl_bounds_hl_h");
#endif

#if HALIDE_JIT
    printf("Realizing function...\n");

    Image<int> imf = f.realize(32, 32);
    Image<int> img = g.realize(32, 32);
    Image<int> imh = h.realize(32, 32);
#endif

#if HALIDE_STATIC_RUN
    Image<int> imf(32, 32);
    hl_bounds_hl_f(imf);
    Image<int> img(32, 32);
    hl_bounds_hl_g(img);
    Image<int> imh(32, 32);
    hl_bounds_hl_h(imh);
#endif

#if !HALIDE_STATIC_COMPILE
    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != (i > j ? i : j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return -1;
            }
            if (img(i, j) != (i < j ? i : j)) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return -1;
            }
            int href = i+j;
            if (href < 20) href = 20;
            if (href > 100) href = 100;
            if (imh(i, j) != href) {
                printf("imh[%d, %d] = %d (not %d)\n", i, j, imh(i, j), href);
                return -1;
            }
        }
    }

    printf("Success!\n");
#endif

    return 0;
}
