#include <stdio.h>
#include <Halide.h>
#include <iostream>

#define DIM 32

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g");

    printf("Defining function...\n");

    g(x, y) = 0.0f + x + y;
    f(x, y) = 0.0f + g(x, y)*g(x, y); // + 0.01f;

    char *target = getenv("HL_TARGET");
    g.compute_root();
    if (target && std::string(target) == "opengl") {
        f.cuda_tile(x, y, DIM, DIM);
    }
 
    printf("Realizing function...\n");

    Image<float> imf = f.realize(DIM, DIM);

    // Check the result was what we expected
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            if ((float) imf(i, j) != (i + j)*(i + j)) {
                printf("imf[%d, %d] = %f\n", i, j, imf(i, j));
                return -1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}
