#define KERNEL_SIZE 16
#define TILE_W 32
#define DIM 4096
#define CLAMP 0
#if CLAMP
#define CLAMP_START clamp(
#define CLAMP_END_I , 0, tile_w-1)
#define CLAMP_END_O , 0, (DIM/tile_w) - 1)
#else
#define CLAMP_START
#define CLAMP_END_I
#define CLAMP_END_O
#endif
#define INC_X 1
#define INC_Y 1

#include <Halide.h>
#include <cstdio>
#include <stdio.h>
#include <sys/time.h>

timeval t1, t2;
#define begin_timing gettimeofday(&t1, NULL); for (int i = 0; i < 10; i++) {
#define end_timing } gettimeofday(&t2, NULL);

using namespace Halide;

Func make_blur_diag(Func in, int kernel_size, int tile_w, int inc_x, int inc_y) {
    Func blur_diag("blur_diag"), in_dup("in_dup");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    // algorithm

    in_dup(x, y) = in(x, y);

    Expr blur_diag_expr = in_dup(x, y) / kernel_size;
    for (int i = 1; i < kernel_size; i++) {
	blur_diag_expr += in_dup(x + inc_x*i, y + inc_y*i) / kernel_size;
    }
    blur_diag(x, y) = blur_diag_expr;

    // schedule
    
    in_dup.compute_root();
    //blur_diag.tile(x, y, xi, yi, tile_w, tile_w).vectorize(x, 4).parallel(y);
    //blur_diag.tile(x, y, xi, yi, tile_w, tile_w);
    blur_diag.vectorize(x, 8);//.parallel(y);

    return blur_diag;
}

Func make_blur_diag_tiled(Func in, int kernel_size, int tile_w, int inc_x, int inc_y) {
    Func in_tiled("in_tiled"), blur_diag_tiled("blur_diag_tiled");
    Var x("x"), y("y"), xi("xi"), xo("xo"), yi("yi"), yo("yo"), z("z");
     
    // algorithm

    in_tiled(xi, yi, xo, yo) = in(xo*tile_w + xi, yo*tile_w + yi);
    
    Expr blur_diag_tiled_expr = in_tiled(CLAMP_START x % tile_w CLAMP_END_I,
					 CLAMP_START y % tile_w CLAMP_END_I,
					 CLAMP_START x / tile_w CLAMP_END_O,
					 CLAMP_START y / tile_w CLAMP_END_O
					 ) / kernel_size;
    for (int i = 1; i < kernel_size; i++) {
	blur_diag_tiled_expr += in_tiled(CLAMP_START (x+inc_x*i) % tile_w CLAMP_END_I,
					 CLAMP_START (y+inc_y*i) % tile_w CLAMP_END_I,
					 CLAMP_START (x+inc_x*i) / tile_w CLAMP_END_O,
					 CLAMP_START (y+inc_y*i) / tile_w CLAMP_END_O
					 ) / kernel_size;
    }

    blur_diag_tiled(x, y) = blur_diag_tiled_expr;

    // schedule

    in_tiled.compute_root();
    //blur_diag_tiled.tile(x, y, xi, yi, tile_w, tile_w).vectorize(x, 8).parallel(y);
    //blur_diag_tiled.tile(x, y, xi, yi, tile_w, tile_w).vectorize(x, 4).parallel(y);
    //blur_diag_tiled.tile(x, y, xi, yi, tile_w, tile_w);
    blur_diag_tiled.vectorize(x, 8);//.parallel(y);

    return blur_diag_tiled;
}

int main(int argc, char **argv) {

    int repeat = 1;
    if (argc > 1) repeat = atoi(argv[1]);
    printf("running with %d repeats and averaging\n", repeat);

    //ImageParam input(UInt(16), 2);

    Func sq("sq"), radial("radial");
    Var x("x"), y("y"), z("z");

    // The algorithm(s)

    sq(z) = z*z;
    radial(x, y) = sq(x) + sq(y);
    //radial.compute_root();

    int tile_widths[] = {1, 2, 4, 8, 16, 32, 64, 128};
    int num_widths = sizeof(tile_widths)/sizeof(tile_widths[0]);
    float kernel_sizes[] = {1, 2, 4, 8, 16, 32};
    int num_sizes = sizeof(kernel_sizes)/sizeof(kernel_sizes[0]);
    int kernels[][2] = { {1, 1} };//{1, 0}, {1, 1}, {0, 1} };
    int num_kernels = sizeof(kernels)/sizeof(kernels[0]);

    for (int k = 0; k < num_kernels; k++) {
	printf("\n\nkernel %d %d\n\n", kernels[k][0], kernels[k][1]);
	// table headers
	printf("%16s %16s %16s %16s\n", "tile width", "kernel width", "ref time", "tile strg time");
	for (int j = 0; j < num_sizes; j++) {
	    for (int i = 0; i < num_widths; i++) {
		int tile_w = tile_widths[i];
		int kernel_size = kernel_sizes[j];
		int inc_x = kernels[k][0];
		int inc_y = kernels[k][1];
		
		// with storage tiling
		Func blur_diag_tiled = make_blur_diag_tiled(radial, kernel_size, tile_w, inc_x, inc_y);
		blur_diag_tiled.compile_jit();
		
		begin_timing;
		for (int r = 0; r < repeat; r++)
		    Image<int> diag_tiled = blur_diag_tiled.realize(DIM, DIM);
		end_timing;
		float tiled_time = (1.0/repeat)*((t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0f);

		// without storage tiling
		Func blur_diag = make_blur_diag(radial, kernel_size, tile_w, inc_x, inc_y);
		blur_diag.compile_jit();
		
		begin_timing;
		for (int r = 0; r < repeat; r++)
		    Image<int> diag = blur_diag.realize(DIM, DIM);
		end_timing;
		float ref_time = (1.0/repeat)*((t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0f);

		// print to table
		printf("%16d %16d %16f %16f\n", tile_w, kernel_size, ref_time, tiled_time);
	    }
	}
    }
        
    /*
    for (int y = 0; y < DIM; y++) {
	for (int x = 0; x < DIM; x++) {
	    if (diag(x, y) != diag_tiled(x, y))
		printf("difference at (%d,%d): %d %d\n", x, y,
		       diag(x, y), diag_tiled(x, y));
	}
    }
    */
    return 0;
}
