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

project(test_osx)

set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# TODO: Need a more automatic way to refer to the halide library and header
set(HALIDE_LIB_PATH "" CACHE PATH "Full path of the file libHalide.a")
set(HALIDE_INCLUDE_PATH "" CACHE PATH "Path to the directory containing Halide.h")

# Set the string for the bundle identifier in ccmake. The value passed here will
# work for testing, change "com.yourcompany." if desired.
set(HALIDE_BUNDLE_IDENTIFIER "com.yourcompany.\${PRODUCT_NAME:rfc1034identifier}" CACHE STRING "Set the build identifier for your company")

set(generator_func "example")

halide_generator_output_path(${generator_func} out_dir)

# To add files to a Copy to Bundle Resources build phase, the files must be
# passed to add_executable and to the RESOURCE property in set_target_properties
set(resources
  index.html
  )

add_executable(test_osx MACOSX_BUNDLE
  main.m
  AppDelegate.h
  AppDelegate.m
  AppProtocol.h
  AppProtocol.mm
  BufferT.h
  BufferT.m
  ../generator/test_example_generator.cpp
  ${resources}
  )

# This test harness will load Halide generated symbols from the
# executable using dlsym. The harness code does not refer to the
# symbols explicitly so we have to make sure that the linker does
# not remove them from the executable.
set(exported_symbols "-Xlinker -exported_symbol -Xlinker _${generator_func}")
set(exported_symbols "${exported_symbols} -Xlinker -exported_symbol -Xlinker _${generator_func}_jit_wrapper")

# Export the test symbols
set(exported_symbols "${exported_symbols} -Xlinker -exported_symbol -Xlinker _test_example_generator")

# Other frameworks passed on the link line
set(frameworks "-framework CoreGraphics -framework Foundation -framework Cocoa -framework WebKit")

set_target_properties(test_osx PROPERTIES
  MACOSX_BUNDLE_EXECUTABLE_NAME "\${EXECUTABLE_NAME}"
  MACOSX_BUNDLE_BUNDLE_NAME "\${PRODUCT_NAME}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${HALIDE_BUNDLE_IDENTIFIER}"
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  RESOURCE "${resources}"

  # The XCODE_ATTTRIBUTE_* variable feature sets an Xcode build setting based on
  # the suffix of the variable name. Here we set some default values of OS X app
  # development
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
  XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS YES
  XCODE_ATTRIBUTE_VALID_ARCHS "i386 x86_64"
  XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
  XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD c++0x
  XCODE_ATTRIBUTE_GCC_ENABLE_CPP_RTTI NO

  LINK_FLAGS "${exported_symbols} ${frameworks}"
  )

target_include_directories(test_osx PUBLIC
  ${HALIDE_INCLUDE_PATH}
  )

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

# Add a build step to call the generator
halide_add_generator_dependency(test_osx example example example -e html target=host)
