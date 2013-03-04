#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
  Var x("x"), y("y");
  Func f("f"), g("g");

    printf("Defining function...\n");
#if 0
    f(x) = 5;
    g(x) = f(x + 5);
#elif 0
    f(x, y) = 5;
    g(x, y) = f(x+5,y+17);
#elif 0
    f(x) = 5;
    g(x) = f(clamp(x, -3, 7));
#elif 1
    f(x, y) = 5;
    g(x, y) = f(clamp(x, 0, 33), clamp(y, 0, 1));
#else
    f(x, y) = 5;
    g(x, y) = f(clamp(x, 0, 33), y);
#endif
#if 0
    printf("Realizing function...\n");

    Image<int> imf = f.realize(32, 32);
    Image<int> img = g.realize(32, 32);
    Image<int> imh = h.realize(32, 32);

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
#endif
    f.compute_root(); //vectorize(x,2);
    g.vectorize(x, 4);
    g.compile_to_assembly("/tmp/simple_asm2.s", {});
    g.compile_to_bitcode("/tmp/simple_asm2.bc", {});


    printf("Success!\n");
    return 0;
}
