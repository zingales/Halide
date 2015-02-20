// This file contains a thin test app wrapper for pnacl-*-nacl halide targets.
// To run the test app, build the project containing this file and then serve
// the output binary directory on a webserver, such as the localhost server
// included in the nacl SDK:
//
// ccmake -GXcode ../path/to/test/pnacl
// xcodebuild -target pnacl_test
// nacl_sdk/pepper_41/tools/httpd.py --no-dir-check -C bin
//
// Then open http://localhost:5103/ in Google Chrome

#include <string>
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_array_buffer.h"
#include "ppapi/cpp/var_dictionary.h"
#include <sstream>

#include <HalideRuntime.h>

using namespace pp;

static Instance *inst = NULL;

// There is no way to dlsym a runtime specified symbol in pepper, so we define
// the test function symbol to load at compile time (via -DTEST_SYMBOL=<...>) It
// is declared here and called below.
extern "C" bool TEST_SYMBOL ();

extern "C" void halide_error(void */* user_context */, const char *msg) {
    inst->PostMessage(msg);
}

extern "C" void halide_print(void */* user_context */, const char *msg) {
    inst->PostMessage(msg);
}

extern "C" int halide_buffer_print(const buffer_t* buffer) {
    std::ostringstream oss;
    oss << "<pre class='data'>";

    int extent3 = buffer->extent[3] ? buffer->extent[3] : 1;
    for (int z = 0; z != extent3; ++z) {
        for (int y = 0; y != buffer->extent[2]; ++y) {
            for (int x = 0; x != buffer->extent[1]; ++x) {
                for (int c = 0; c != buffer->extent[0]; ++c) {
                    int idx =
                    z*buffer->stride[3] +
                    y*buffer->stride[2] +
                    x*buffer->stride[1] +
                    c*buffer->stride[0];
                    switch (buffer->elem_size) {
                    case 1:
                        // Cast the result to int so that the value is printed as a number
                        // instead of an ascii character.
                        oss << ((int)((unsigned char*)buffer->host)[idx]);
                        break;
                    case 4:
                        oss << ((float*)buffer->host)[idx];
                        break;
                    }
                }
                oss << "\n";
            }
            oss << "\n";
        }
        oss << "\n\n";
    }
    oss << "</pre>\n";

    inst->PostMessage(oss.str().c_str());

    return 0;
}

// This function outputs the buffer as an image in a platform specific manner.
extern "C" int halide_buffer_display(const buffer_t* buffer) {

    if (!inst)
        return 1;

    void* data_ptr = buffer->host;

    size_t width            = buffer->extent[0];
    size_t height           = buffer->extent[1];
    size_t channels         = buffer->extent[2];
    size_t bitsPerComponent = buffer->elem_size*8;

    // For planar data, there is one channel across the row
    size_t src_bytesPerRow      = width*buffer->elem_size;
    size_t dst_bytesPerRow      = width*channels*buffer->elem_size;

    size_t totalBytes = width*height*channels*buffer->elem_size;

    // Javascrip ImageData does not support planar image formats so we must
    // interleave the image.
    unsigned char* src_buffer = (unsigned char*)data_ptr;

    VarArrayBuffer array_buffer(totalBytes);

    unsigned char* dst_buffer = (unsigned char*)array_buffer.Map();

    // Interleave the data
    for (size_t c=0;c!=buffer->extent[2];++c) {
        for (size_t y=0;y!=buffer->extent[1];++y) {
            for (size_t x=0;x!=buffer->extent[0];++x) {
                size_t src = x + y*src_bytesPerRow + c * (height*src_bytesPerRow);
                size_t dst = c + x*channels + y*dst_bytesPerRow;
                dst_buffer[dst] = src_buffer[src];
            }
        }
    }

    VarDictionary dict;
    dict.Set("width", (int32_t)width);
    dict.Set("height", (int32_t)height);
    dict.Set("host", array_buffer);

    inst->PostMessage(dict);

    return 0;
}

class HalideInstance : public Instance {
public:
    explicit HalideInstance(PP_Instance instance) :
        Instance(instance) {
        // Initialize the singleton instance used by the halide runtime callbacks.
        inst = this;
    }

    virtual ~HalideInstance() {
        // Remove the singleton
        delete inst;
    }

    virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]) {
        PostMessage("Halide test loaded\n<br>");

        // Launch the test
        bool result = TEST_SYMBOL();

        // Output the result. The test should return true on error.
        std::ostringstream oss;
        oss << "<br>Halide test returned value: " << result << "<br>\n";
        PostMessage(oss.str());

        return true;
    }
};

class HalideModule : public Module {
public:
    HalideModule() : Module() {}
    virtual ~HalideModule() {}

    virtual Instance* CreateInstance(PP_Instance instance) {
        return new HalideInstance(instance);
    }
};

namespace pp {
    Module* CreateModule() {
        return new HalideModule();
    }
}  // namespace pp
