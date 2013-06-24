#if !HALIDE_STATIC_RUN
#include <Halide.h>
using namespace Halide;
#endif

#include <stdio.h>

int main(int argc, char **argv) {
#if !HALIDE_STATIC_RUN
    // Recursive functions can create circular references. These could
    // cause leaks. Run this test under valgrind to check.
    for (int i = 0; i < 10000; i++) {
        Func f;
        Var x;
        RDom r(0, 10);
        f(x) = x;
        f(r) = f(r-1) + f(r+1);
    }
#endif

    printf("Success!\n");
    return 0;

}
