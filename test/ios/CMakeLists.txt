# This file contains a CMake build for a basic iOS app that runs a series of
# test functions and displays the results.

# This project contains targets that build for both iOS and Mac OS X.

cmake_minimum_required(VERSION 2.8.5)

include("${CMAKE_CURRENT_SOURCE_DIR}/../HalideGenerator.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../src/HalideRuntime.cmake")

# In iOS Halide development, (for ahead-of-time compilation) Halide is built for
# OS X and these tests must be built for iOS. This is accomplished below by
# setting several Xcode attributes below to build one target as a device app
# bundle and the rest as host command line tools.

project(test_ios)

# We want all of our targets to put there binaries in a common path, not based
# on the platform name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# TODO: Need a more automatic way to refer to the halide library and header
set(HALIDE_LIB_PATH "" CACHE PATH "Full path of the file libHalide.a")
set(HALIDE_INCLUDE_PATH "" CACHE PATH "Path to the directory containing Halide.h")

# Set the string for the bundle identifier in ccmake. The value passed here will
# work for testing, change "com.yourcompany." if desired.
set(HALIDE_IOS_BUNDLE_IDENTIFIER "com.yourcompany.\${PRODUCT_NAME:rfc1034identifier}" CACHE STRING "Set the build identifier for your company")

set(generator_func "example")

# Set the developer identiy in ccmake if the default for your build machine is
# not sufficient.
set(HALIDE_IOS_CODE_SIGNING_IDENTITY "iPhone Developer" CACHE STRING "Set the developer identity")

set(CMAKE_OSX_SYSROOT "iphoneos")
set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)")
set(CMAKE_XCODE_EFFECTIVE_PLATFORMS "-iphoneos;-iphonesimulator")

# To add files to a Copy to Bundle Resources build phase, the files must be
# passed to add_executable and to the RESOURCE property in set_target_properties
set(RESOURCES
  index.html
  )

add_executable(test_ios MACOSX_BUNDLE
  AppDelegate.h
  AppDelegate.m
  AppProtocol.h
  AppProtocol.mm
  BufferT.h
  BufferT.m
  ../generator/test_example_generator.cpp
  main.m
  ViewController.h
  ViewController.m
  ${RESOURCES}
  )

# This test harness will load Halide generated symbols from the
# executable using dlsym. The harness code does not refer to the
# symbols explicitly so we have to make sure that the linker does
# not remove them from the executable.
set(exported_symbols "-Xlinker -exported_symbol -Xlinker _${generator_func}")

# Export the test symbols
set(exported_symbols "${exported_symbols} -Xlinker -exported_symbol -Xlinker _test_example_generator")

# Other frameworks passed on the link line
set(frameworks "-framework CoreGraphics -framework Foundation -framework UIKit")

set_target_properties(test_ios PROPERTIES
  MACOSX_BUNDLE_EXECUTABLE_NAME "\${EXECUTABLE_NAME}"
  MACOSX_BUNDLE_BUNDLE_NAME "\${PRODUCT_NAME}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${HALIDE_IOS_BUNDLE_IDENTIFIER}"
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  RESOURCE ${RESOURCES}

  # The XCODE_ATTTRIBUTE_* variable feature sets an Xcode build setting based on
  # the suffix of the variable name. Here we set some default values of iOS app
  # development
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${HALIDE_IOS_CODE_SIGNING_IDENTITY}"
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
  XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS YES
  XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 8.0
  XCODE_ATTRIBUTE_VALID_ARCHS "arm64 armv7 armv7s x86_64"
  XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphonesimulator iphoneos"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH YES

  LINK_FLAGS "${exported_symbols} ${frameworks}"
  )

target_include_directories(test_ios PUBLIC
  ${HALIDE_INCLUDE_PATH}
  )

# Add a Halide Generator to the test app project.

# Add a helper program required by HalideRuntime.cmake
add_executable(bitcode2cpp ../../tools/bitcode2cpp.cpp)

# Use Xcode attributes to setup this target as a host command line tool even
# though the other targets in the project are iOS apps.
set_target_properties(bitcode2cpp PROPERTIES
  XCODE_ATTRIBUTE_SDKROOT "macosx"
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
  XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS YES
  XCODE_ATTRIBUTE_VALID_ARCHS "i386 x86_64"
  XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
  XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD c++0x
  XCODE_ATTRIBUTE_GCC_ENABLE_CPP_RTTI NO
  )

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
halide_add_generator_dependency(test_ios example example example -e html target=ios-arm-64)
# halide_add_generator_dependency(test_ios example example example -e html target=host)

# Use Xcode attributes to setup this target as a host command line tool even
# though the other targets in the project are iOS apps.
set_target_properties(example PROPERTIES
  XCODE_ATTRIBUTE_SDKROOT "macosx"
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
  XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS YES
  XCODE_ATTRIBUTE_VALID_ARCHS "i386 x86_64"
  XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
  XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD c++0x
  XCODE_ATTRIBUTE_GCC_ENABLE_CPP_RTTI NO
  )










