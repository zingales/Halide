# This file contains a CMake build for a basic OSX app that runs a series of
# Generator tests and displays the results. Generators must be
# added to the app below and then configured in an html file loaded at
# runtime.

cmake_minimum_required(VERSION 2.8.5)

include("${CMAKE_CURRENT_SOURCE_DIR}/../HalideGenerator.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../src/HalideRuntime.cmake")

# CMake will only build for a single platform per project, it is not possible to
# build for both iOS and MacOSX in the same project. In iOS Halide development,
# (for ahead-of-time compilation) Halide is built for OS X and these tests must
# be built for iOS.

project(test_pnacl)

set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# TODO: Need a more automatic way to refer to the halide library and header
set(HALIDE_LIB_PATH "" CACHE PATH "Full path of the file libHalide.a")
set(HALIDE_INCLUDE_PATH "" CACHE PATH "Path to the directory containing Halide.h")

set(generator_func "example")

halide_generator_output_path(${generator_func} out_dir)

# Add a Halide Generator to the test app project.

# Add a helper program required by HalideRuntime.cmake
add_executable(bitcode2cpp ../../tools/bitcode2cpp.cpp)

# Then create the generator executable
add_executable(example
  ../generator/example_generator.cpp
  generator_main.cpp
  )

target_link_libraries(example
  ${HALIDE_LIB_PATH}
  )

target_include_directories(example PUBLIC
  ${HALIDE_INCLUDE_PATH}
  )

set_target_properties(example PROPERTIES
  # Halide::Generator requires C++11
  XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD c++0x
  XCODE_ATTRIBUTE_GCC_ENABLE_CPP_RTTI NO
  )

# Add a build step to call the generator, not that so far we have not added a
# test_nacl target to the project.
halide_add_generator_dependency(test_pnacl example example example target=pnacl-32-nacl)

# Determine PNACL SDK environment
set(NACL_SDK_ROOT "" CACHE PATH "Full path to the nacl_sdk directory")
set(NACL_PEPPER_VERSION "41" CACHE PATH "Name of the pepper version ")

set(nacl_pepper_root "${NACL_SDK_ROOT}/pepper_${NACL_PEPPER_VERSION}")
set(nacl_include_flags "-I${nacl_pepper_root}/include" "-I${HALIDE_INCLUDE_PATH}")
set(nacl_linker_flags "-L${nacl_pepper_root}/lib/pnacl/Release")

# It is not possible to dlsym the test symbol in pepper newlib, so we must
# specify the function names to test a compile time instead of via a javascript
# variable.
set(nacl_test_defines "-DTEST_SYMBOL=test_example_generator")

if(APPLE)
  set(nacl_toolchain "${nacl_pepper_root}/toolchain/mac_pnacl")
endif()

# Compile the module and instance code to a pnacl bc file
add_custom_command(OUTPUT ${out_dir}/main.bc
COMMAND ${nacl_toolchain}/bin/pnacl-clang++ -o ${out_dir}/main.bc ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp -c -O2 ${nacl_include_flags} ${nacl_test_defines}
  MAIN_DEPENDENCY main.cpp
  )

# Compile the test code to a pnacl bc file
add_custom_command(OUTPUT ${out_dir}/test_example_generator.bc
  COMMAND ${nacl_toolchain}/bin/pnacl-clang++ -o ${out_dir}/test_example_generator.bc ${CMAKE_CURRENT_SOURCE_DIR}/../generator/test_example_generator.cpp -c -O2 ${nacl_include_flags}
  MAIN_DEPENDENCY ../generator/test_example_generator.cpp
  )

# Add a build step to generate a .pexe target
add_custom_target(test_pnacl
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ${nacl_toolchain}/bin/pnacl-clang++ -o ${out_dir}/test_pnacl.pexe ${out_dir}/main.bc ${out_dir}/test_example_generator.bc ${out_dir}/example.bc -O2 ${nacl_linker_flags} -lppapi_cpp -lppapi
  COMMAND ${nacl_toolchain}/bin/pnacl-finalize -o ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_pnacl.pexe ${out_dir}/test_pnacl.pexe
  COMMAND cp index.html ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
  COMMAND cp test_pnacl.nmf ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
  DEPENDS ${out_dir}/main.bc
          ${out_dir}/test_example_generator.bc
          ${out_dir}/example.bc
          index.html
          test_pnacl.nmf
  SOURCES main.cpp
          index.html
          test_pnacl.nmf
  )



