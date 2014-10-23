#include <fuzz_codegen.h>
#include <static_image.h>
#include <stdio.h>
#include <HalideRuntime.h>
#include <assert.h>

#include "fuzz_codegen_common.h"

int main(int argc, char **argv) {
    Image<int> result(expr_count, sample_count, 6);
    fuzz_codegen(result);

    for (int i = 0; i < expr_count; i++) {
        for (int j = 0; j < sample_count; j++) {
            for (int k = 0; k < 6; k++) {
                if (result(i, j, k) == 0) {
                    printf("Error at expr %d, sample %d, type %d\n", i, j, k);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
