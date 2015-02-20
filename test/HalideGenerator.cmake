# This function returns the intermediate output directory where the Halide
# generator output will be placed. This path is automatically added to the
# library path and include path of the specified target. This function can be
# used to determine the location of the other output files like the bit code and
# html.
function(halide_generator_output_path gen_name result_var)
  # Convert the binary dir to a native path
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/" NATIVE_INT_DIR)

  # Create a directory to contain generator specific intermediate files
  set(scratch_dir "${NATIVE_INT_DIR}scratch_${gen_name}")
  file(MAKE_DIRECTORY "${scratch_dir}")

  # Set the output variable
  set(${result_var} "${scratch_dir}" PARENT_SCOPE)
endfunction(halide_generator_output_path)

# This function adds custom build steps to invoke a Halide generator exectuable,
# produce a static library containing the generated code, and then link that
# static library to the specified target. The generator executable must be
# produced separately, e.g. using a call to the function halide_project(...)
# You may specify additional arguments passed to the generator executable after
# the named function arguments. For example, any -e options and the target=<..>
# parameter
function(halide_add_generator_dependency target gen_target gen_name func_name)

  # Determine a scratch directory to build and execute the generator. ${target}
  # will include the generated header from this directory.
  halide_generator_output_path(${gen_name} SCRATCH_DIR)

  # Determine the name of the output files
  # CMake 2.8 doesn't have string(CONCAT), so fake it like so:
  string(REPLACE ".lib" "${CMAKE_STATIC_LIBRARY_SUFFIX}" FILTER_LIB "${func_name}.lib" )
  string(REPLACE ".h" ".h" FILTER_HDR "${func_name}.h" )

  # Check to see if the target includes pnacl
  if ("${ARGN}" MATCHES ".*pnacl.*")
    set(FILTER_LIB "${func_name}.bc")
    set(target_is_pnacl TRUE)
  endif()

  # Add a custom target to output the Halide generated static library
  if (WIN32)
    # TODO(srj): this has not yet been tested on Windows.
    add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
      DEPENDS "${gen_target}"
      COMMAND "${CMAKE_BINARY_DIR}/bin/${BUILD_TYPE}/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}
      COMMAND "lib.exe" "/OUT:${FILTER_LIB}" "${SCRATCH_DIR}\\${func_name}.o"
      WORKING_DIRECTORY "${SCRATCH_DIR}"
      )
  elseif(XCODE)
    if (NOT target_is_pnacl)
      add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
        DEPENDS "${gen_target}"

        # The generator executable will be placed in a configuration specific
        # directory, so the Xcode variable $(CONFIGURATION) is passed in the custom
        # build script.
        COMMAND "${CMAKE_BINARY_DIR}/bin/$(CONFIGURATION)/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}

        # If we are building an ordinary executable, use libtool to create the
        # static library.
        COMMAND libtool -static -o "${FILTER_LIB}" "${SCRATCH_DIR}/${func_name}.o"
        WORKING_DIRECTORY "${SCRATCH_DIR}"
        )
    else()
      # For pnacl targets, there is no libtool step
      add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
        DEPENDS "${gen_target}"
        COMMAND "${CMAKE_BINARY_DIR}/bin/$(CONFIGURATION)/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}
        WORKING_DIRECTORY "${SCRATCH_DIR}"
        )
    endif()

  else()
    if (NOT target_is_pnacl)
      add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
        DEPENDS "${gen_target}"
        COMMAND "${CMAKE_BINARY_DIR}/bin/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}

        # Create an archive using ar (or similar)
        COMMAND "${CMAKE_AR}" q "${FILTER_LIB}" "${SCRATCH_DIR}/${func_name}.o"
        WORKING_DIRECTORY "${SCRATCH_DIR}"
        )
    else()
      # No archive step for pnacl targets
      add_custom_command(OUTPUT "${FILTER_LIB}" "${FILTER_HDR}"
        DEPENDS "${gen_target}"
        COMMAND "${CMAKE_BINARY_DIR}/bin/${gen_target}${CMAKE_EXECUTABLE_SUFFIX}" "-g" "${gen_name}" "-f" "${func_name}" "-o" "${SCRATCH_DIR}" ${ARGN}
        WORKING_DIRECTORY "${SCRATCH_DIR}"
        )
    endif()
  endif()

  # Use a custom target to force it to run the generator before the
  # object file for the runner. The target name will start with the prefix
  #  "exec_generator_"
  add_custom_target("exec_generator_${gen_name}_${func_name}"
                    DEPENDS "${FILTER_LIB}" "${FILTER_HDR}"
                    )

  # Place the target in a special folder in IDEs
  set_target_properties("exec_generator_${gen_name}_${func_name}" PROPERTIES
                        FOLDER "generator"
                        )

  # Make the generator execution target a dependency of the specified
  # application target and link to the generated library

  if (TARGET "${target}")

    # exec_generator_foo must build before $target
    add_dependencies("${target}" "exec_generator_${gen_name}_${func_name}")

    target_link_libraries("${target}" "${SCRATCH_DIR}/${FILTER_LIB}")

    # Add the scratch directory to the include path for ${target}. The generated
    # header may be included via #include "${gen_name}.h"
    target_include_directories("${target}" PRIVATE "${SCRATCH_DIR}")

  endif()

endfunction(halide_add_generator_dependency)
