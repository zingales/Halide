#include "HalideRuntime.h"

#include <stdlib.h>
#include <strings.h>

#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED
#include <stdint.h>
typedef struct buffer_t {
  uint64_t dev;
  uint8_t* host;
  int32_t extent[4];
  int32_t stride[4];
  int32_t min[4];
  int32_t elem_size;
  bool host_dirty;
  bool dev_dirty;
} buffer_t;
#endif

// This function outputs the buffer in text form in a platform specific manner.
extern "C"
int halide_buffer_print(const buffer_t* buffer);

// This function outputs the buffer as an image in a platform specific manner.
// For example, in a web based application the buffer contents might be
// displayed as a png image.
extern "C"
int halide_buffer_display(const buffer_t* buffer);

extern "C"
int example(const float _runtime_factor, buffer_t *_g);

extern "C"
int example_glsl(const float _runtime_factor, buffer_t *_g);

extern "C"
bool test_example_generator() {

  halide_print(NULL,"Running filter example\n");

  int N = 256;
  int C = 4;

  float compiletime_factor = 1.0f;
  float runtime_factor = 2.0f;
  unsigned char* data = (unsigned char*)malloc(N*N*C);

  // TODO: Add other functions to create input images
  // buffer_t g = halide_buffer_with_url("");

  buffer_t g = {
    .dev = 0,
    .host = (uint8_t*)data,
    .extent = { N, N, C, 0 },
    .stride = { 1, N, N*N, C*N*N },
    .min = { 0, 0, 0, 0 },
    .elem_size = sizeof(data[0]),
    .host_dirty = 1,
    .dev_dirty = 0,
  };

  halide_print(NULL,"CPU target\n");
  example(runtime_factor,&g);

  halide_copy_to_host(NULL,&g);

  int errors = 0;
  for (int c = 0; c < C; c++) {
    for (int y = 0; y < N; y++) {
      for (int x = 0; x < N; x++) {
        unsigned char expected = (unsigned char)((float)(x > y ? x : y) *
                                                 (float)c *
                                                 compiletime_factor *
                                                 runtime_factor);
        unsigned char actual   = g.host[c*N*N + y*N + x];
        if (expected != actual) {
          errors++;
        }
      }
    }
  }

  halide_buffer_display(&g);
  free(g.host);

  halide_print(NULL,"GPU target\n");

  g.host = (unsigned char*)malloc(N*N*C);
  g.host_dirty = 1;
  g.dev_dirty = 0;

  example_glsl(runtime_factor,&g);

  halide_copy_to_host(NULL, &g);

  for (int c = 0; c < C; c++) {
    for (int y = 0; y < N; y++) {
      for (int x = 0; x < N; x++) {
        unsigned char expected = (unsigned char)((float)(x > y ? x : y) *
                                                 (float)c *
                                                 compiletime_factor *
                                                 runtime_factor);
        unsigned char actual   = g.host[c*N*N + y*N + x];
        if (expected != actual) {
          errors++;
        }
      }
    }
  }

  halide_buffer_display(&g);

  return (bool)errors;
}

