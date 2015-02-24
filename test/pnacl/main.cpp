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

#include <GLES2/gl2.h>
#include "ppapi/lib/gl/gles2/gl2ext_ppapi.h"
#include "ppapi/cpp/graphics_3d.h"

using namespace pp;

static Instance *inst = NULL;

// There is no way to dlsym a runtime specified symbol in pepper, so we define
// the test function symbol to load at compile time (via -DTEST_SYMBOL=<...>) It
// is declared here and called below.
extern "C" bool TEST_SYMBOL ();

extern "C" void halide_error(void */* user_context */, const char *msg) {
    printf("%s\n",msg);
    inst->PostMessage(msg);
}

extern "C" void halide_print(void */* user_context */, const char *msg) {
    printf("%s\n",msg);
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

extern "C" void *halide_opengl_get_proc_address(void *user_context, const char *name_cstr) {

  // All of these symbols are defined in the global namespace, but pnacl does
  // not support dlsym, so we string compare and return the appropriate pointer
  // using the symbol name.
  std::string name = name_cstr;

#define GLFUNC(dec,def) \
  if (name == std::string("gl" #def)) {\
  return (void*)gl##def;\
}
  GLFUNC(PFNGLDELETETEXTURESPROC, DeleteTextures);
  GLFUNC(PFNGLGENTEXTURESPROC, GenTextures);
  GLFUNC(PFNGLBINDTEXTUREPROC, BindTexture);
  GLFUNC(PFNGLGETERRORPROC, GetError);
  GLFUNC(PFNGLVIEWPORTPROC, Viewport);
  GLFUNC(PFNGLGENBUFFERSPROC, GenBuffers);
  GLFUNC(PFNGLDELETEBUFFERSPROC, DeleteBuffers);
  GLFUNC(PFNGLBINDBUFFERPROC, BindBuffer);
  GLFUNC(PFNGLBUFFERDATAPROC, BufferData);
  GLFUNC(PFNGLTEXPARAMETERIPROC, TexParameteri);
  GLFUNC(PFNGLTEXIMAGE2DPROC, TexImage2D);
  GLFUNC(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D);
  GLFUNC(PFNGLDISABLEPROC, Disable);
  GLFUNC(PFNGLCREATESHADERPROC, CreateShader);
  GLFUNC(PFNGLACTIVETEXTUREPROC, ActiveTexture);
  GLFUNC(PFNGLSHADERSOURCEPROC, ShaderSource);
  GLFUNC(PFNGLCOMPILESHADERPROC, CompileShader);
  GLFUNC(PFNGLGETSHADERIVPROC, GetShaderiv);
  GLFUNC(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
  GLFUNC(PFNGLDELETESHADERPROC, DeleteShader);
  GLFUNC(PFNGLCREATEPROGRAMPROC, CreateProgram);
  GLFUNC(PFNGLATTACHSHADERPROC, AttachShader);
  GLFUNC(PFNGLLINKPROGRAMPROC, LinkProgram);
  GLFUNC(PFNGLGETPROGRAMIVPROC, GetProgramiv);
  GLFUNC(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);
  GLFUNC(PFNGLUSEPROGRAMPROC, UseProgram);
  GLFUNC(PFNGLDELETEPROGRAMPROC, DeleteProgram);
  GLFUNC(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation);
  GLFUNC(PFNGLUNIFORM1IVPROC, Uniform1iv);
  GLFUNC(PFNGLUNIFORM2IVPROC, Uniform2iv);
  GLFUNC(PFNGLUNIFORM2IVPROC, Uniform4iv);
  GLFUNC(PFNGLUNIFORM1FVPROC, Uniform1fv);
  GLFUNC(PFNGLUNIFORM1FVPROC, Uniform4fv);
  GLFUNC(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers);
  GLFUNC(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers);
  GLFUNC(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus);
  GLFUNC(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer);
  GLFUNC(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D);
  GLFUNC(PFNGLGETATTRIBLOCATIONPROC, GetAttribLocation);
  GLFUNC(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer);
  GLFUNC(PFNGLDRAWELEMENTSPROC, DrawElements);
  GLFUNC(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray);
  GLFUNC(PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray);
  GLFUNC(PFNGLPIXELSTOREIPROC, PixelStorei);
  GLFUNC(PFNGLREADPIXELS, ReadPixels);
  GLFUNC(PFNGLGETSTRINGPROC, GetString);
  GLFUNC(PFNGLGETINTEGERV, GetIntegerv);

  return NULL;
}

// This function must be define in the client app because the pp::Instance
// object is needed to create a pp::Graphics3D context.
extern "C" int halide_opengl_create_context(void *user_context) {

  if (!glInitializePPAPI(pp::Module::Get()->get_browser_interface())) {
    halide_error(user_context, "Unable to initialize GL PPAPI!\n");
    return -1;
  }

  const int32_t attrib_list[] = {
    PP_GRAPHICS3DATTRIB_RED_SIZE, 8,
    PP_GRAPHICS3DATTRIB_GREEN_SIZE, 8,
    PP_GRAPHICS3DATTRIB_BLUE_SIZE, 8,
    PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,

    // The default width and height are both zero, and if we use that value
    // renderbuffer creation on the context will fail. The off screen buffer
    // does not fail if we pass a non-zero value.
    PP_GRAPHICS3DATTRIB_WIDTH, 1,
    PP_GRAPHICS3DATTRIB_HEIGHT, 1,
    PP_GRAPHICS3DATTRIB_NONE
  };

  // Create an off screen graphics context for Halide.
  static Graphics3D context_ = pp::Graphics3D(inst, attrib_list);

  glSetCurrentContextPPAPI(context_.pp_resource());
  return 0;
}
