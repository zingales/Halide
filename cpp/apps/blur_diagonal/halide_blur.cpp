#define KERNEL_LENGTH 200

#include <Halide.h>
#include <cstdio>
using namespace Halide;

int main(int argc, char **argv) {

    ImageParam input(UInt(16), 3);
    Func blur_diag("blur_diag");
    Var x("x"), y("y"), c("c");
    
    // The algorithm
    Expr foo = input(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c)
	/ KERNEL_LENGTH;
    for (int i = 1; i < KERNEL_LENGTH; i++) {
	foo = foo + (input(clamp(x + i, 0, input.width()-1),
			   clamp(y + i, 0, input.height()-1), c) / 
		     KERNEL_LENGTH);
    }
    blur_diag(x, y, c) = foo;

    setenv("HL_ENABLE_CLAMPED_VECTOR_LOAD", "1", 1);
    blur_diag.compile_to_file("halide_blur", input);

    return 0;
}
