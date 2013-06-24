#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#else
#include "hl_bad_elem_size_hl.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool error_occurred;
extern "C" void halide_error(char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    Var x, y;
    Func f;
    f(x, y) = x+y;
#endif

#if HALIDE_STATIC_COMPILE
    f.compile_to_file("hl_bad_elem_size_hl");
#else

#if HALIDE_STATIC_RUN
    void (*function)(const buffer_t *) = /*(void (*)(const buffer_t *))*/(hl_bad_elem_size_hl);
#endif

#if HALIDE_JIT
    // Dig out the raw function pointer so we can use it as if we were
    // compiling statically
    void (*function)(const buffer_t *) = (void (*)(const buffer_t *))(f.compile_jit());
    f.set_error_handler(&halide_error);
#endif

    buffer_t out;
    memset(&out, 0, sizeof(out));
    out.host = (uint8_t *)malloc(10*10);
    out.elem_size = 1; // should be 4!
    out.extent[0] = 10;
    out.extent[1] = 10;
    out.stride[0] = 1;
    out.stride[1] = 10;

    error_occurred = false;
    function(&out);

    if (error_occurred) {
        printf("Success!\n");
        return 0;
    } else {
        printf("There should have been a runtime error\n");
        return -1;
    }
#endif
}
