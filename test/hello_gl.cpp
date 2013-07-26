#include <stdio.h>
#include <Halide.h>
#include <iostream>

#define DIM 4
#define TILES 2

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g");

    printf("Defining function...\n");

    g(x, y) = x + y + 0.0f; //0.01f*y;
    //f(x, y) = g(x,y) + 0.0f;
    f(x, y) = 0.0f + 17.0f + g(x, y) *
        //0.0001f * 
        g(x + 1, y + 1) * g(x - 1, y - 1);
        

    char *target = getenv("HL_TARGET");
    g.compute_root();
    if (target && std::string(target) == "opengl") {
        f.cuda_tile(x, y, DIM/TILES, DIM/TILES);
    }
 
    printf("Realizing function...\n");

    Image<float> imf = f.realize(DIM, DIM);

    // Check the result was what we expected
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            if ((float) imf(i, j) != 17 + (i + j) * (i + j + 2) * (i + j - 2)) {
                printf("imf[%d, %d] = %f\n", i, j, imf(i, j));
            }
        }
    }
    printf("Success!\n");
    return 0;
}
