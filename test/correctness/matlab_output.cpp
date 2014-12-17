#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    ImageParam in(Float(32), 2);
    Param<float> scale;

    Var x("x"), y("y");

    Func result;
    result(x, y) = in(x, y) * scale;

    std::vector<Argument> args;
    args.push_back(in);
    args.push_back(scale);
    result.compile_to(output_matlab("mex_scale"), args, "scale_fn");

    printf("Success!\n");

    return 0;

}
