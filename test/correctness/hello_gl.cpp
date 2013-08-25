#include <stdio.h>
#include <Halide.h>
#include <iostream>

#define DIM 4
#define TILES 2

using namespace Halide;



int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    printf("Defining function...\n");

    f(x, y) = 0.0002f + x + 200.0f*y;
    h(x, y) = 0.001f + 0.5f*f(x, y);

    char *target = getenv("HL_TARGET");
    g.compute_root();
    if (target && std::string(target) == "opengl") {
        f.cuda_tile(x, y, DIM/TILES, DIM/TILES).compute_root();
        h.cuda_tile(x, y, DIM/TILES, DIM/TILES);
    }
 
    printf("Realizing function...\n");
    Image<float> imf = f.realize(DIM, DIM);
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            float expected = 0.0002f + i + 200.0f*j;//0.01f + 0.5f*(i + j);
            //float expected = 0.01f + 0.5f*(2.0f + i + 2.0f*j);
            if (abs((float) imf(i, j) - (expected)) > 1e-4) {
                printf("imf[%d, %d] = %f\n", i, j, imf(i, j));
                printf("(expected %f)\n", expected);
            }
        }
    }
    printf("Success!\n\n\n\n\n");

    Image<float> imf2 = h.realize(DIM, DIM);
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            float expected = 0.001f + 0.5f*(0.0002f + i + 200.0f*j);//0.01f + 0.5f*(i + j);
            //float expected = 0.01f + 0.5f*(2.0f + i + 2.0f*j);
            if (abs((float) imf2(i, j) - (expected)) > 1e-4) {
                printf("imf[%d, %d] = %f\n", i, j, imf2(i, j));
                printf("(expected %f)\n", expected);
            }
        }
    }
    printf("Success(2)!!\n");

    // Check the result was what we expected
    return 0;
}
