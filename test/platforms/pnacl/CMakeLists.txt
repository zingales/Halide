# This file contains a CMake build for a basic PNACL app that runs a series of
# Generator tests and displays the results. Generators must be
# added to the app below.

cmake_minimum_required(VERSION 2.8.5)

include("../HalideAppTests.cmake")

project(test_pnacl)

set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# TODO: Need a more automatic way to refer to the halide library and header
set(HALIDE_LIB_PATH "" CACHE PATH "Full path of the file libHalide.a")
set(HALIDE_INCLUDE_PATH "" CACHE PATH "Path to the directory containing Halide.h")

# Create a target for the PNACL app. This function will setup all of the app
# state and compile and link in the specified test source files. Generators are
# added to the test below.
halide_add_pnacl_app(TARGET test_pnacl
  TEST_SOURCES ../tests/test_example_generator.cpp
  TEST_FUNCTIONS test_example_generator
  )

# Create a generator executable and use it to generate a halide function for the
# host target.
halide_add_pnacl_generator_to_app(TARGET test_pnacl
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example
  GENERATOR_SOURCES ../../generator/example_generator.cpp
  GENERATOR_ARGS -e html target=pnacl-32-nacl
  )

# Pass the same generator target name with a different GENERATED_FUNCTION and
# Halide target string. This will cause the generator to be run a second time
# with a different target enabled. The test code referenced above uses both of
# these halide generated functions.
halide_add_pnacl_generator_to_app(TARGET test_pnacl
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example_glsl
  GENERATOR_SOURCES ../../generator/example_generator.cpp
  GENERATOR_ARGS -e html target=pnacl-32-nacl-opengl
  )
