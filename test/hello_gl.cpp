#include <stdio.h>
#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g");

    printf("Defining function...\n");

    g(x, y) = x + y;
    f(x, y) = g(x+1,y)*g(x+1,y);

    char *target = getenv("HL_TARGET");
    g.compute_root();
    if (target && std::string(target) == "opengl") {
        f.cuda_tile(x, y, 32, 32);
    }
 
    printf("Realizing function...\n");

    Image<float> imf = f.realize(32, 32);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != (i + j)*(i + j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}