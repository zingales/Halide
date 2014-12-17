#ifndef HALIDE_MATLAB_OUTPUT_H
#define HALIDE_MATLAB_OUTPUT_H

/** \file
 *
 */

#include "Output.h"

namespace Halide {

/** Create an output that generates matlab mex libraries from compiled
 * Funcs. */
EXPORT Output output_matlab(const std::string &filename);

}

#endif
