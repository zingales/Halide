// Compile the halide module like so:
// make -C ../../../FImage/cpp_bindings/ FImage.a && g++-4.6 -std=c++0x halide_blur.cpp -I ../../../FImage/cpp_bindings/ ../../../FImage/cpp_bindings/FImage.a && ./a.out && opt -O3 -always-inline halide_blur.bc | llc -filetype=obj > halide_blur.o

// Then compile this file like so:
// g++-4.6 -Wall -ffast-math -O3 -fopenmp test.cpp halide_blur.o

#include <emmintrin.h>
#include <math.h>
#include <sys/time.h>
#include <stdint.h>

#define cimg_display 0
#include "CImg.h"
using namespace cimg_library;

// TODO: fold into module
extern "C" { typedef struct CUctx_st *CUcontext; }
namespace FImage { CUcontext cuda_ctx = 0; }

timeval t1, t2;
#define begin_timing gettimeofday(&t1, NULL); for (int i = 0; i < 10; i++) {
#define end_timing } gettimeofday(&t2, NULL);

typedef CImg<uint16_t> Image;

extern "C" {
#include "halide_blur.h"
#include "halide_blur_tiled.h"
}

// Convert a CIMG image to a buffer_t for halide
buffer_t halideBufferOfImage(Image &im) {
    buffer_t buf = {(uint8_t *)im.data(), 0, false, false, 
                    {im.width(), im.height(), 1, 1}, 
                    {1, im.width(), 0, 0}, 
                    {0, 0, 0, 0}, 
                    sizeof(int16_t)};
    return buf;
}

Image blur_halide(Image &in) {
    Image out(in.width(), in.height());

    //buffer_t inbuf = halideBufferOfImage(in);
    buffer_t outbuf = halideBufferOfImage(out);

    // Call it once to initialize the halide runtime stuff
    halide_blur(&outbuf);

    begin_timing;
    
    // Compute the same region of the output as blur_fast (i.e., we're
    // still being sloppy with boundary conditions)
    halide_blur(&outbuf);

    end_timing;

    return out;
}

Image blur_halide_tiled(Image &in) {
    Image out(in.width(), in.height());

    //buffer_t inbuf = halideBufferOfImage(in);
    buffer_t outbuf = halideBufferOfImage(out);

    // Call it once to initialize the halide runtime stuff
    halide_blur_tiled(&outbuf);

    begin_timing;
    
    // Compute the same region of the output as blur_fast (i.e., we're
    // still being sloppy with boundary conditions)
    halide_blur_tiled(&outbuf);

    end_timing;

    return out;
}

int main(int argc, char **argv) {

    Image input(2048, 2048);

    for (int y = 0; y < input.height(); y++) {
	for (int x = 0; x < input.width(); x++) {
	    input(x, y) = rand() & 0xfff;
	}
    }

    Image halide = blur_halide(input);
    float halide_time = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0f;

    printf("not dead yet\n");

    Image halide_tiled = blur_halide_tiled(input);
    float halide_tiled_time = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0f;
    
    printf("still not dead yet\n");

    printf("times (halide, halide_tiled): %f %f\n", halide_time, halide_tiled_time);

    printf("still still not dead yet\n");

    /*for (int y = 0; y < input.height(); y++) {
	for (int x = 0; x < input.width(); x++) {
	    if (halide(x, y) != halide_tiled(x, y))
		printf("difference at (%d,%d): %d %d\n", x, y,
		       halide(x, y), halide_tiled(x, y));
	}
	}*/
    
    return 0;
}
