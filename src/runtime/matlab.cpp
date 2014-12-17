#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "../buffer_t.h"

// We never use mxArray as a concrete type, so this is OK.
typedef void* mxArray_ptr;

enum mxClassID {
    mxUNKNOWN_CLASS = 0,
    mxCELL_CLASS,
    mxSTRUCT_CLASS,
    mxLOGICAL_CLASS,
    mxCHAR_CLASS,
    mxVOID_CLASS,
    mxDOUBLE_CLASS,
    mxSINGLE_CLASS,
    mxINT8_CLASS,
    mxUINT8_CLASS,
    mxINT16_CLASS,
    mxUINT16_CLASS,
    mxINT32_CLASS,
    mxUINT32_CLASS,
    mxINT64_CLASS,
    mxUINT64_CLASS,
    mxFUNCTION_CLASS,
    mxOPAQUE_CLASS,
    mxOBJECT_CLASS,
#if defined(_LP64) || defined(_WIN64)
    mxINDEX_CLASS = mxUINT64_CLASS,
#else
    mxINDEX_CLASS = mxUINT32_CLASS,
#endif

    mxSPARSE_CLASS = mxVOID_CLASS
};

enum mxComplexity {
    mxREAL,
    mxCOMPLEX
};

// Declare all the matlab APIs we need.
#define MEX_FN(ret, fn, args) extern "C" ret fn args
#define MEX_FN_730(ret, fn, fn_730, args) extern "C" ret fn_730 args

MEX_FN(int, mexPrintf, (const char*, ...));
MEX_FN(void, mexErrMsgIdAndTxt, (const char *, const char*, ...));
MEX_FN_730(size_t, mxGetNumberOfDimensions, mxGetNumberOfDimensions_730, (const mxArray_ptr));
MEX_FN_730(const size_t*, mxGetDimensions, mxGetDimensions_730, (const mxArray_ptr));
MEX_FN(void*, mxGetData, (const mxArray_ptr));
MEX_FN(size_t, mxGetElementSize, (const mxArray_ptr));
MEX_FN(mxClassID, mxGetClassID, (const mxArray_ptr));
MEX_FN(double, mxGetScalar, (const mxArray_ptr));

#define mxGetDimensions mxGetDimensions_730
#define mxGetNumberOfDimensions mxGetNumberOfDimensions_730

// Set the halide print and error handlers to use mexPrintf. We can't
// use mexErrMsgIdAndTxt from within Halide because it crashes matlab.
extern "C" WEAK void halide_matlab_printf(void *, const char *msg) {
    mexPrintf("%s\n", msg);
}

extern "C" WEAK void halide_matlab_init() {
    halide_set_custom_print(halide_matlab_printf);
    halide_set_error_handler(halide_matlab_printf);
}

// Convert an mxArray to a buffer_t.
extern "C" WEAK int halide_matlab_to_buffer_t(int n, const mxArray_ptr a, buffer_t *buf) {
    if (a == NULL) {
        mexErrMsgIdAndTxt("Matlab:Halide", "Argument %d is NULL.\n", n);
        return -1;
    }
    halide_assert(NULL, buf != NULL);
    memset(buf, 0, sizeof(buffer_t));

    int dim_count = mxGetNumberOfDimensions(a);
    if (dim_count > 4) {
        mexErrMsgIdAndTxt("Matlab:Halide", "Argument %d has %d dimensions.\n", n, dim_count);
        return -1;
    }

    buf->host = (uint8_t *)mxGetData(a);
    buf->elem_size = mxGetElementSize(a);

    const size_t *dims = mxGetDimensions(a);
    for (int i = 0; i < dim_count; i++) {
        buf->extent[i] = static_cast<int32_t>(dims[i]);
    }

    // Compute dense strides.
    buf->stride[0] = 1;
    for (int i = 1; i < dim_count; i++) {
        buf->stride[i] = buf->extent[i - 1] * buf->stride[i - 1];
    }

    mxGetScalar(a);

    return 0;
}
