
# This function takes a .cpp source file containing part of the Halide runtime
# compiles it into a LLVM bitcode file, and then encodes the bitcode file as a
# extern unsigned char[] symbol in an output .cpp file. This .cpp file is
# included in libHalide. During Halide AOT or JIT compilation, the symbol is
# used to access the runtime module code for a specific target platform and
# architecture.
#
# Arguments:
# prefix - the prefix used for targets, e.g. "_initmod_"
# source - the path to the source file
# arch   - the architecture to compile, either "32" or "64"
# output_cpp_list - the output argument on to which the path to the generated
#           source files is appended
# ...    - other arguments are passed to the CLANG command line, e.g. "-I ..."
#
function(halide_runtime_module prefix source arch output_cpp_list)

  # TODO: Allow the runtime to be compiled with other triples or remove this
  # check.
  # -m64 isn't respected unless we also use a 64-bit target.
  IF (ARCHS EQUAL 32)
    set(TARGET "i386-unknown-unknown-unknown")
  ELSE()
    set(TARGET "x86_64-unknown-unknown-unknown")
  ENDIF()

  # This function might be called from outside the libHalide project build, e.g.
  # in a test wrapper app build within the same source tree. In that case we
  # need to make sure ${CLANG} is defined.
  if (NOT CLANG)
    set(LLVM_BIN "llvm3.2/bin/Debug/" CACHE PATH "Path to LLVM bin directory")
    file(TO_NATIVE_PATH "${LLVM_BIN}/llvm-as${CMAKE_EXECUTABLE_SUFFIX}" LLVM_AS)
    file(TO_NATIVE_PATH "${LLVM_BIN}/clang${CMAKE_EXECUTABLE_SUFFIX}" CLANG)
  endif()

  # Check to see if the bitcode2cpp target has been created
  if (NOT TARGET bitcode2cpp)
    message(FATAL_ERROR "bitcode2cpp target not found")
  endif()

  # Determine a build directory to put intermediate files and convert it from a
  # cmake path to a native path (e.g. on Windows)
  file(TO_NATIVE_PATH
       "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${CMAKE_CFG_INTDIR}/"
       build_dir_native)

  # Determine the name of this source file, excluding the directory and
  # extension.
  GET_FILENAME_COMPONENT(name ${source} NAME_WE)

  # Determine the file paths for the intermediate .ll and .bc files for both
  # Release and Debug builds. Note that this is independent of how libHalide is
  # built- Halide will use the debug modules if the Halide target contains
  # :debug
  set(LL_D "${build_dir_native}/${prefix}.${name}_${arch}_debug.ll")
  set(LL   "${build_dir_native}/${prefix}.${name}_${arch}.ll")
  set(BC_D "${build_dir_native}/${prefix}.${name}_${arch}_debug.bc")
  set(BC   "${build_dir_native}/${prefix}.${name}_${arch}.bc")

  # Determine the file paths for the output .cpp files for both Release and
  # Debug
  set(INITMOD_D "${build_dir_native}/${prefix}.${name}_${arch}_debug.cpp")
  set(INITMOD   "${build_dir_native}/${prefix}.${name}_${arch}.cpp")

  # Compile the input source file to a .ll file for debug and release
  set(ll_cmd_line ${CXX_WARNING_FLAGS} -fno-ms-compatibility -ffreestanding -fno-blocks -fno-exceptions -m${arch} -target "${TARGET}" "-DLLVM_VERSION=${LLVM_VERSION}" -DBITS_${arch} -emit-llvm -S "${source}" ${ARGN})

  add_custom_command(OUTPUT "${LL_D}"
  DEPENDS "${source}"
  COMMAND ${CLANG}  ${RUNTIME_DEBUG_FLAG} -DDEBUG_RUNTIME ${ll_cmd_line} -o "${LL_D}"
  COMMENT "${source} -> ${LL_D}")

  add_custom_command(OUTPUT "${LL}"
  DEPENDS "${source}"
  COMMAND ${CLANG} ${ll_cmd_line} -o "${LL}"
  COMMENT "${source} -> ${LL}")

  # Assemble the .ll files into .bc files.
  add_custom_command(OUTPUT "${BC_D}"
  DEPENDS "${LL_D}"
  COMMAND "${LLVM_AS}" "${LL_D}" -o "${BC_D}"
  COMMENT "${LL_D} -> ${BC_D}")
  add_custom_command(OUTPUT "${BC}"
  DEPENDS "${LL}"
  COMMAND "${LLVM_AS}" "${LL}" -o "${BC}"
  COMMENT "${LL} -> ${BC}")

  # Run the bitcode2cpp program to encode the .bc files into extern unsigned
  # char[] symbols in .cpp files.
  add_custom_command(OUTPUT "${INITMOD_D}"
  DEPENDS "${BC_D}"
  COMMAND bitcode2cpp "${name}_${arch}_debug" < "${BC_D}" > "${INITMOD_D}"
  COMMENT "${BC_D} -> ${INITMOD_D}")
  add_custom_command(OUTPUT "${INITMOD}"
  DEPENDS "${BC}"
  COMMAND bitcode2cpp "${name}_${arch}" < "${BC}" > "${INITMOD}"
  COMMENT "${BC} -> ${INITMOD}")

  # Add the generated cpp files to the output list
  set(${output_cpp_list} ${${output_cpp_list}} ${INITMOD} ${INITMOD_D} PARENT_SCOPE)

endfunction(halide_runtime_module)
