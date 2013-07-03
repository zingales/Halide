/* build test

g++ -c -o runtime.opengl_host.o runtime.opengl_host.cpp -I/usr/include
gcc -o test-gl runtime.opengl_host.o -L/usr/lib -lGL -lglut -lGLEW -lm

*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../buffer_t.h"

// The PTX host extends the x86 target
// But compilation fails with these, and I don't know what they do,
// so let's ignore for now
#if 0
#include "posix_allocator.cpp"
#include "posix_clock.cpp"
#include "posix_error_handler.cpp"
#include "write_debug_image.cpp"
#include "posix_io.cpp"
#include "posix_math.cpp"
#ifdef _WIN32
#include "fake_thread_pool.cpp"
#else
#ifdef __APPLE__
#include "gcd_thread_pool.cpp"
#else
#include "posix_thread_pool.cpp"
#endif
#endif
#endif

#include <GL/glew.h>

#define WEAK __attribute__((weak))

extern "C" {

// -------------------------- BEGIN OPENGL CODE ---------------------------------//

#include <stdlib.h>
// GLEW, also pulls in system OpenGL headers
#include <GL/glew.h>
// Apple has GLUT framework headers in weird place
#ifdef __APPLE__
#  include <GLUT/glut.h> 
#else
#  include <GL/glut.h>
#endif

#include <math.h>
#include <stdio.h>

#ifndef NDEBUG
#define CHECK_ERROR()                                                   \
    { GLenum errCode;                                                   \
        if((errCode = glGetError()) != GL_NO_ERROR)                     \
            fprintf (stderr, "OpenGL Error at line %d: 0x%04x\n", __LINE__, (unsigned int) errCode); \
    }
#define SAY_HI() printf("hi %s()!\n",  __PRETTY_FUNCTION__)
#else
#define CHECK_ERROR()
#define SAY_HI() 
#endif

//------------------------------------ non open GL helper functions ---------------------//

static float *empty_image(int dim0, int dim1, int dim2, int dim3) {
    SAY_HI();
    return (float *) calloc(dim0*dim1*dim2*dim3, sizeof(float));
}

static float *random_image(int dim0, int dim1, int dim2, int dim3) {
    SAY_HI();
    int sz = dim0*dim1*dim2*dim3;
    float *img = (float*) malloc(sz*sizeof(float));
    for (int i = 0; i < sz; i++) {
        img[i] = rand();
    }
    return img;
}

static int compare_images(float *ref, float* copy, int w, int h) {
    SAY_HI();
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            for (int c = 0; c < 3; c++) {
                int idx = y*w*3 + x*3 + c;
                float ref_c = ref[idx];
                float copy_c = copy[idx];
                if (ref_c!=copy_c) {
                    printf("mismatch at (%d, %d, %d): expected %f but found %f\n",
                           x, y, c, ref_c, copy_c);
                    return -1;
                }
           }
        }
    }
    return 0;
}

//------------------------------------ open GL helper functions --------------------------//

/** make texture
 *  if data is NULL texture will be empty
 */
static GLuint make_texture(int w,
                           int h,
                           void *data) {

    SAY_HI();

    // generate texture object name
    GLuint texture;
    // void glGenTextures(GLsizei  n, GLuint *  textures);
    glGenTextures(1, &texture);
    CHECK_ERROR();

    // create texture object
    glBindTexture(GL_TEXTURE_RECTANGLE, texture);
    CHECK_ERROR();

    // set parameters to use this texture as an image - use NN and clamp edges
    // should use i instead of f here? or shouldn't make difference
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK_ERROR();

    // it looks like we can bind NULL data with glTexImage2D if we intend to
    // render into this buffer
    // it's unclear if we can split this into two operations
    // maybe would make sense to generate the names and count that as allocation?
    // void TexImage2D( enum target, int level,
    //                  int internalformat, sizei width, sizei height,
    //                  int border, enum format, enum type, void *data );
    // TODO: fix data format
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGB32F, w, h,
                 0, GL_RGB, GL_FLOAT, data);
    CHECK_ERROR();

    return texture;
}

/** make a framebuffer and bind it to a texture
 */
static GLuint make_framebuffer(GLuint texture) {

    SAY_HI();

    // generate buffer object name
    GLuint framebuffer;

    // void glGenBuffers(GLsizei  n, GLuint *  buffers);
    glGenFramebuffers(1, &framebuffer);
    CHECK_ERROR();

    // framebuffer is created by binding an unused name to target framebuffer
    // void BindFramebuffer( enum target, uint framebuffer );
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    CHECK_ERROR();

    // http://stackoverflow.com/questions/4264775/opengl-trouble-with-render-to-texture-framebuffer-object
    // suggests that we should do this because we don't need the depth buffer
    // so long, depth buffer, and thanks for all the fish
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    CHECK_ERROR();

    // specify texture as color attachment to framebuffer
    glBindTexture(GL_TEXTURE_RECTANGLE, texture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE, texture, 0);
    CHECK_ERROR();

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE) {
        printf("framebuffer =)\n");
    } else {
        printf("framebuffer =(\n");
    }

    //unbind?
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return framebuffer;
}

/* 
 * Handles shader errors
 */
static void show_info_log(GLuint object,
			  PFNGLGETSHADERIVPROC glGet__iv,
                          PFNGLGETSHADERINFOLOGPROC glGet__InfoLog) {
    GLint log_length;
    char *log;
    
    glGet__iv(object, GL_INFO_LOG_LENGTH, &log_length);
    log = (char*) malloc(log_length);
    glGet__InfoLog(object, log_length, NULL, log);
    fprintf(stderr, "%s", log);
    free(log);
}

/*
 * Make shader from string
 */
static GLuint make_shader(GLenum type, const char *source, GLint *length)
{
    GLuint shader;
    GLint shader_ok;
    
    if (!source)
        return 0;
    
    // GLSL builds the shader from source every time. Here we read our 
    // shader source out of a separate file, which lets us change the
    // shader source without recompiling our C.
    shader = glCreateShader(type);
    glShaderSource(shader, 1, (const GLchar**)&source, length);
    glCompileShader(shader);
    
    glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (!shader_ok) {
        fprintf(stderr, "Failed to compile shader\n");
        show_info_log(shader, glGetShaderiv, glGetShaderInfoLog);
        glDeleteShader(shader);
        return 0;
    } else {
        show_info_log(shader, glGetShaderiv, glGetShaderInfoLog);
    }
    return shader;
}

/*
 * make buffer for storing vertex arrays
 */
static GLuint make_buffer(GLenum target,
			  const void *buffer_data,
			  GLsizei buffer_size) {
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(target, buffer);
    glBufferData(target, buffer_size, buffer_data, GL_STATIC_DRAW);
    return buffer;
}

/*
 * make a program from a vertex and fragment shader
 */
static GLuint make_program(GLuint vertex_shader, GLuint fragment_shader)
{
    GLint program_ok;
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    glGetProgramiv(program, GL_LINK_STATUS, &program_ok);
    if (!program_ok) {
        fprintf(stderr, "Failed to link shader program:\n");
        show_info_log(program, glGetProgramiv, glGetProgramInfoLog);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static const GLfloat g_vertex_buffer_data[] = { 
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f
};

static const GLushort g_element_buffer_data[] = { 0, 1, 2, 3 };

const char* vertex_shader_src = \
"#version 410                                    \n"\
"in vec2 position;                               \n"\
"out vec2 texcoord;                              \n"\
"void main()                                     \n"\
"{                                               \n"\
"    gl_Position = vec4(position, 0.0, 1.0);     \n"\
"    texcoord = position*vec2(0.5) + vec2(0.5);  \n"\
"}                                               \n\0";

const char* fragment_shader_src = \
"#version 410                                    \n"\
"uniform float fade_factor;                      \n"\
"uniform sampler2DRect input;                    \n"\
"uniform ivec2 input_dim;                        \n"\
"in vec2 texcoord;                               \n"\
"out vec4 output;                                \n"\
"void main()                                     \n"\
"{                                               \n"\
"    vec2 input_coord = input_dim*texcoord;      \n"\
"    vec4 tex_val = texture(input, input_coord); \n"\
"    output = fade_factor*tex_val*tex_val;       \n"\
"}                                               \n\0";

int main_old(int argc, char** argv)
{
    SAY_HI();
    
    // prep GLUT - will initialize the GLUT library and negotiate 
    // a session with the window system
    glutInit(&argc, argv);
    // specify what buffers default framebuffer should have
    // this specifies a colour buffer with double buffering
    //glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    // set window size to 400x300
    GLint w = 4;
    GLint h = 10;
    //glutInitWindowSize(w, h);
    // create window (arg is char* name)
    // this is necessary for initializing openGL
    glutCreateWindow("Hello World");
    // set display callback for current window
    // takes function pointer to void function w/ no args
    // void glutDisplayFunc(void (*func)(void))
    //glutDisplayFunc(&render);
    // idle callback is continuously called when events are not being received
    // this will update the fade factor between the two images over time
    //glutIdleFunc(&update_fade_factor);
    
    // sets a bunch of flags based on what extensions and OpenGL versions
    // are available
    glewInit();
    // check flag for new enough version of OpenGL
    if (!GLEW_VERSION_4_0) {
        fprintf(stderr, "OpenGL 4.0 not available\n");
        return 1;
    }

    /*
     * Data used to seed our vertex array and element array buffers.
     * glBufferData just sees this as a stream of bytes.
     * Specify vertices and ordering to make triangles out of them.
     */

    
    float *data = random_image(w, h, 3, 1);

    // generate texture object name
    GLuint input_texture = make_texture(w, h, (void *) data);
    CHECK_ERROR();
    
    // generate buffer object name
    GLuint input_framebuffer = make_framebuffer(input_texture);
    CHECK_ERROR();

    // generate texture object name
    GLuint output_texture = make_texture(w, h, NULL);
    CHECK_ERROR();
    
    // generate buffer object name
    GLuint output_framebuffer = make_framebuffer(output_texture);
    CHECK_ERROR();

    // somehow we know what the current framebufffer is
    glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
    glBindTexture(GL_TEXTURE_RECTANGLE, output_texture);
    const GLenum bufs[] = {GL_COLOR_ATTACHMENT0};
    CHECK_ERROR();

    
    // Specifies a list of color buffers to be drawn into.
    // void glDrawBuffers(GLsizei  n, // number of buffers in bufs
    //                    const GLenum * bufs);
    // The fragment shader output value is written into the nth color attachment
    // of the current framebuffer. n may range from 0 to the value of GL_MAX_COLOR_ATTACHMENTS.
    glDrawBuffers(1, bufs);
    CHECK_ERROR();
    
    // render into entire framebuffer
    glViewport(0, 0, w, h);
    
    // render

    // make buffers
    GLuint vertex_buffer = make_buffer(GL_ARRAY_BUFFER,
                                       g_vertex_buffer_data,
                                       sizeof(g_vertex_buffer_data));
    GLuint element_buffer = make_buffer(GL_ELEMENT_ARRAY_BUFFER,
                                        g_element_buffer_data,
                                        sizeof(g_element_buffer_data));;
    CHECK_ERROR();
    // make shaders
    GLint length;
    GLchar *shader_src = (GLchar *) vertex_shader_src;
    GLuint vertex_shader = make_shader(GL_VERTEX_SHADER, shader_src, NULL);
    if (vertex_shader == 0) { return 0; }
    CHECK_ERROR();
    printf("here!\n");
    shader_src = (GLchar *) fragment_shader_src;
    GLuint fragment_shader = make_shader(GL_FRAGMENT_SHADER, shader_src, NULL);
    if (fragment_shader == 0) { return 0; }
    CHECK_ERROR();

    // make program
    GLuint program = make_program(vertex_shader, fragment_shader);
    GLint fade_factor = glGetUniformLocation(program, "fade_factor");
    GLint input = glGetUniformLocation(program, "input");
    GLint input_dim = glGetUniformLocation(program, "input_dim");
    GLint position = glGetAttribLocation(program, "position");
    //input_dim[1] = glGetAttribLocation(program, "input_dim[1]");
    CHECK_ERROR();

    float fade_factor_val = 1.0;

    // render
    glUseProgram(program);
    // location, value
    glUniform1f(fade_factor, fade_factor_val);
    CHECK_ERROR();
    glUniform2i(input_dim, (GLint) w, (GLint) h);
    CHECK_ERROR();
    glActiveTexture(GL_TEXTURE0);
    CHECK_ERROR();
    glBindTexture(GL_TEXTURE_RECTANGLE, input_texture);
    CHECK_ERROR();
    // i think the 0 matches texture 0
    glUniform1i(input, 0);
    CHECK_ERROR();

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glVertexAttribPointer(position,  /* attribute */
                          2,                                /* size */
                          GL_FLOAT,                         /* type */
                          GL_FALSE,                         /* normalized? */
                          sizeof(GLfloat)*2,                /* stride */
                          (void*)0                          /* array buffer offset */
                          );
    glEnableVertexAttribArray(position);
    CHECK_ERROR();    

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
    glDrawElements(GL_TRIANGLE_STRIP,  /* mode */
                   4,                  /* count */
                   GL_UNSIGNED_SHORT,  /* type */
                   (void*)0            /* element array buffer offset */
                   );
    
    glDisableVertexAttribArray(position);
    
    glFinish();
    
    // copy back texture
    float *img = empty_image(w, h, 3, 1);
    // void glGetTexImage(GLenum  target,
    //		   GLint  level,
    //		   GLenum  format,
    //		   GLenum  type,
    //		   GLvoid *  img);
    CHECK_ERROR();
    glBindTexture(GL_TEXTURE_RECTANGLE, output_texture);
    glGetTexImage(GL_TEXTURE_RECTANGLE, 0, GL_RGB, GL_FLOAT, (void *) img);
    glFlush();
    glFinish();
    CHECK_ERROR();
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 3; c++) {
                int idx = y*w*3 + x*3 + c;
                float tex_val = ((float *) data)[idx];
                float ref = fade_factor_val*tex_val*tex_val;
                float result = ((float *) img)[idx];
                if (ref != result) {
                    printf("mismatch at (%d, %d, %d), ref: %f  result: %f\n",
                           x, y, c, ref, result);
                }
            }
        }
    }

    printf("success!\n");
    free(img);
    
    // clean up
    glDeleteFramebuffers(1, &input_framebuffer);
    glDeleteFramebuffers(1, &output_framebuffer);
    glDeleteTextures(1, &input_texture);
    glDeleteTextures(1, &output_texture);

    // suppresses compiler warnings
    return 0;
}


// -------------------------- END OPENGL CODE ---------------------------------//

// Used to create buffer_ts to track internal allocations caused by our runtime
// For now exactly the same as the openCL version
// TODO: add checks specific to the sorts of images that OpenGL can handle
buffer_t* WEAK __make_buffer(uint8_t* host, size_t elem_size,
                        size_t dim0, size_t dim1,
                        size_t dim2, size_t dim3)
{
    buffer_t* buf = (buffer_t*)malloc(sizeof(buffer_t));
    buf->host = host;
    buf->dev = 0;
    buf->extent[0] = dim0;
    buf->extent[1] = dim1;
    buf->extent[2] = dim2;
    buf->extent[3] = dim3;
    buf->elem_size = elem_size;
    buf->host_dirty = false;
    buf->dev_dirty = false;
    return buf;
}

// functions expected by CodeGen_GPU_Host

WEAK void halide_dev_free(buffer_t* buf) {
    // should delete framebuffer and texture associated with it
}
WEAK void halide_dev_malloc(buffer_t* buf) {
    // should create framebuffer and bind it to a texture
}

// Used to generate correct timings when tracing
// If all went well with OpenGl, this won't die
WEAK void halide_dev_sync() {  
    // glFinish does not return until the effects of all previously called GL commands are complete.
    // Such effects include all changes to GL state, all changes to connection state, and all changes
    // to the frame buffer contents.
    glFinish();
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    // should copy pixels from cpu memory to texture
}
WEAK void halide_copy_to_host(buffer_t* buf) {
    // should copy pixels from texture to cpu memory
}
WEAK void halide_dev_run(
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[])
{
}
// fragment shader is the kernel here - we'll need a different one for each operation

// vertex shader seems like it should always be the same

/*
int f( buffer_t *input, buffer_t *result, int N )
{
    const char* entry_name = "knl";

    int threadsX = 128;
    int threadsY =  1;
    int threadsZ =  1;
    int blocksX = N / threadsX;
    int blocksY = 1;
    int blocksZ = 1;


    threadsX = 8;
    threadsY =  1;
    threadsZ =  1;
    blocksX = 4;
    blocksY = 4;
    blocksZ = 1;

    // Invoke
    size_t argSizes[] = { sizeof(cl_mem), sizeof(cl_mem), sizeof(int), 0 };
    void* args[] = { &input->dev, &result->dev, &N, 0 };
    halide_dev_run(
        entry_name,
        blocksX,  blocksY,  blocksZ,
        threadsX, threadsY, threadsZ,
        1, // sharedMemBytes
        argSizes,
        args
    );

    return 0;
}
*/

// this should probably also be the same as the openCL version
int main(int argc, char* argv[]) {

    printf("hello world!\n");

    main_old(0, NULL);
    /*
    halide_init_kernels(src);

    const int N = 2048;
    buffer_t in, out;

    in.dev = 0;
    in.host = (uint8_t*)malloc(N*sizeof(float));
    in.elem_size = sizeof(float);
    in.extent[0] = N; in.extent[1] = 1; in.extent[2] = 1; in.extent[3] = 1;

    out.dev = 0;
    out.host = (uint8_t*)malloc(N*sizeof(float));
    out.elem_size = sizeof(float);
    out.extent[0] = N; out.extent[1] = 1; out.extent[2] = 1; out.extent[3] = 1;

    for (int i = 0; i < N; i++) {
        ((float*)in.host)[i] = i / 2.0;
    }
    in.host_dirty = true;

    halide_dev_malloc(&in);
    halide_dev_malloc(&out);
    halide_copy_to_dev(&in);

    f( &in, &out, N );

    out.dev_dirty = true;
    halide_copy_to_host(&out);

    for (int i = 0; i < N; i++) {
        float a = ((float*)in.host)[i];
        float b = ((float*)out.host)[i];
        if (b != a*a) {
            printf("[%d] %f != %f^2\n", i, b, a);
        }
    }
    */
}

} // extern C linkage


// OLD OPEN CL STUFF

#if 0

/*
 * Build standalone test (on Mac) with:
 *
 *   clang -framework OpenCL -DTEST_STUB runtime.opencl_host.cpp
 */


extern "C" {

// #define NDEBUG // disable logging/asserts for performance

#ifdef NDEBUG
#define CHECK_ERR(e,str)
#define CHECK_CALL(c,str) (c)
#define TIME_START()
#define TIME_CHECK(str)
#else // DEBUG
#define CHECK_ERR(err,str) fprintf(stderr, "Do %s\n", str); \
                         if (err != CL_SUCCESS)           \
                            fprintf(stderr, "CL: %s returned non-success: %d\n", str, err); \
                         assert(err == CL_SUCCESS)
#define CHECK_CALL(c,str) {                                                 \
    fprintf(stderr, "Do %s\n", str);                                        \
    int err = (c);                                                          \
    if (err != CL_SUCCESS)                                                  \
        fprintf(stderr, "CL: %s returned non-success: %d\n", str, err);  \
    assert(err == CL_SUCCESS);                                              \
} halide_current_time() // just *some* expression fragment after which it's legal to put a ;
#if 0
#define TIME_START() cuEventRecord(__start, 0)
#define TIME_CHECK(str) {\
    cuEventRecord(__end, 0);                                \
    cuEventSynchronize(__end);                              \
    float msec;                                             \
    cuEventElapsedTime(&msec, __start, __end);              \
    printf(stderr, "Do %s\n", str);                         \
    printf("   (took %fms, t=%d)\n", msec, halide_current_time());  \
} halide_current_time() // just *some* expression fragment after which it's legal to put a ;
#else
#define TIME_START()
#define TIME_CHECK(str)
#endif
#endif //NDEBUG



// Device, Context, Module, and Function for this entrypoint are tracked locally
// and constructed lazily on first run.
// TODO: make __f, __mod into arrays?
// static vector<CUfunction> __f;
}
extern "C" {
cl_context WEAK cl_ctx = 0;
cl_command_queue WEAK cl_q = 0;

static cl_program __mod;
// static CUevent __start, __end;

// Used to create buffer_ts to track internal allocations caused by our runtime
buffer_t* WEAK __make_buffer(uint8_t* host, size_t elem_size,
                        size_t dim0, size_t dim1,
                        size_t dim2, size_t dim3)
{
    buffer_t* buf = (buffer_t*)malloc(sizeof(buffer_t));
    buf->host = host;
    buf->dev = 0;
    buf->extent[0] = dim0;
    buf->extent[1] = dim1;
    buf->extent[2] = dim2;
    buf->extent[3] = dim3;
    buf->elem_size = elem_size;
    buf->host_dirty = false;
    buf->dev_dirty = false;
    return buf;
}

WEAK void __release_buffer(buffer_t* buf)
{
    free(buf);
}
WEAK buffer_t* __malloc_buffer(int32_t size)
{
    return __make_buffer((uint8_t*)malloc(size), sizeof(uint8_t), size, 1, 1, 1);
}

    /*
WEAK bool halide_validate_dev_pointer(buffer_t* buf, size_t size=0) {

    size_t real_size;
    cl_int result = clGetMemObjectInfo((cl_mem)buf->dev, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result) {
        fprintf(stderr, "Bad device pointer %p: clGetMemObjectInfo returned %d\n", (void *)buf->dev, result);
        return false;
    }
    fprintf(stderr, "validate %p: asked for %zu, actual allocated %zu\n", (void*)buf->dev, size, real_size);
    if (size) assert(real_size >= size && "Validating pointer with insufficient size");
    return true;
}
    */


WEAK void halide_dev_free(buffer_t* buf) {

    /*
      buffer on device is framebuffer and texture
     */

    #ifndef NDEBUG
    fprintf(stderr, "In dev_free of %p - dev: 0x%p\n", buf, (void*)buf->dev);
    #endif

    assert(halide_validate_dev_pointer(buf));
    CHECK_CALL( clReleaseMemObject((cl_mem)buf->dev), "clReleaseMemObject" );
    buf->dev = 0;
}

WEAK void halide_init_kernels(const char* src) {
    /*
      do openGL initialization stuff here
      also initialize shaders, which will be our equivalent of kernels
     */
    int err;
    cl_device_id dev;
    // Initialize one shared context for all Halide compiled instances
    if (!cl_ctx) {
        // Make sure we have a device
        const cl_uint maxDevices = 4;
        cl_device_id devices[maxDevices];
        cl_uint deviceCount = 0;
        err = clGetDeviceIDs( NULL, CL_DEVICE_TYPE_ALL, maxDevices, devices, &deviceCount );
        CHECK_ERR( err, "clGetDeviceIDs" );
        if (deviceCount == 0) {
            fprintf(stderr, "Failed to get device\n");
            return;
        }
        
        dev = devices[deviceCount-1];

        #ifndef NDEBUG
        fprintf(stderr, "Got device %lld, about to create context (t=%d)\n", (long long)dev, halide_current_time());
        #endif


        // Create context
        cl_ctx = clCreateContext(0, 1, &dev, NULL, NULL, &err);
        CHECK_ERR( err, "clCreateContext" );
        // cuEventCreate(&__start, 0);
        // cuEventCreate(&__end, 0);
        
        assert(!cl_q);
        cl_q = clCreateCommandQueue(cl_ctx, dev, 0, &err);
        CHECK_ERR( err, "clCreateCommandQueue" );
    } else {
        //CHECK_CALL( cuCtxPushCurrent(cuda_ctx), "cuCtxPushCurrent" );
    }
    
    // Initialize a module for just this Halide module
    if (!__mod) {
        #ifndef NDEBUG
        fprintf(stderr, "-------\nCompiling kernel source:\n%s\n--------\n", src);
        #endif

        // Create module
        __mod = clCreateProgramWithSource(cl_ctx, 1, (const char**)&src, NULL, &err );
        CHECK_ERR( err, "clCreateProgramWithSource" );

        err = clBuildProgram( __mod, 0, NULL, NULL, NULL, NULL );
        if (err != CL_SUCCESS) {
            size_t len;
            char buffer[2048];

            fprintf(stderr, "Error: Failed to build program executable!\n");
            clGetProgramBuildInfo(__mod, dev, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
            fprintf(stderr, "%s\n", buffer);
            assert(err == CL_SUCCESS);
        }
    }
}

// Used to generate correct timings when tracing
WEAK void halide_dev_sync() {
    // Blocks until all previously queued OpenCL commands in command_queue are issued to the associated device and have completed.
    // clFinish(cl_q);
    
    // glFinish does not return until the effects of all previously called GL commands are complete.
    // Such effects include all changes to GL state, all changes to connection state, and all changes
    // to the frame buffer contents.
    glFinish();
}

WEAK void halide_release() {
    // cleanup

    // TODO: this is for timing; bad for release-mode performance
    #ifndef NDEBUG
    fprintf( stderr, "dev_sync on exit" );
    #endif
    halide_dev_sync();

    // TODO: destroy context if we own it

    // Unload the module
    if (__mod) {
        CHECK_CALL( clReleaseProgram(__mod), "clReleaseProgram" );
        __mod = 0;
    }
}

static cl_kernel __get_kernel(const char* entry_name) {
    cl_kernel f;
    #ifdef NDEBUG
    // char msg[1];
    #else
    char msg[256];
    snprintf(msg, 256, "get_kernel %s (t=%d)", entry_name, halide_current_time() );
    #endif
    // Get kernel function ptr
    TIME_START();
    int err;
    f = clCreateKernel(__mod, entry_name, &err);
    CHECK_ERR(err, "clCreateKernel");
    TIME_CHECK(msg);
    return f;
}

static cl_mem __dev_malloc(size_t bytes) {
    cl_mem p;
    #ifdef NDEBUG
    // char msg[1];
    #else
    char msg[256];
    snprintf(msg, 256, "dev_malloc (%zu bytes) (t=%d)", bytes, halide_current_time() );
    #endif
    TIME_START();
    int err;
    p = clCreateBuffer( cl_ctx, CL_MEM_READ_WRITE, bytes, NULL, &err );
    TIME_CHECK(msg);
    #ifndef NDEBUG
    fprintf(stderr, "    returned: %p (err: %d)\n", (void*)p, err);
    #endif
    assert(p);
    return p;
}

// for now should be same as from openCL
static inline size_t buf_size(buffer_t* buf) {
    size_t sz = buf->elem_size;
    if (buf->extent[0]) sz *= buf->extent[0];
    if (buf->extent[1]) sz *= buf->extent[1];
    if (buf->extent[2]) sz *= buf->extent[2];
    if (buf->extent[3]) sz *= buf->extent[3];
    assert(sz);
    return sz;
}

WEAK void halide_dev_malloc(buffer_t* buf) {

    // set up framebuffer on device

    #ifndef NDEBUG
    fprintf(stderr, "dev_malloc of %dx%dx%dx%d (%d bytes) (buf->dev = %p) buffer\n",
            buf->extent[0], buf->extent[1], buf->extent[2], buf->extent[3], buf->elem_size, (void*)buf->dev);
    #endif
    if (buf->dev) {
        assert(halide_validate_dev_pointer(buf));
        return;
    }
    size_t size = buf_size(buf);
    buf->dev = (uint64_t)__dev_malloc(size);
    assert(buf->dev);
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    if (buf->host_dirty) {
        assert(buf->host && buf->dev);
        size_t size = buf_size(buf);
        #ifdef NDEBUG
        // char msg[1];
        #else
        char msg[256];
        snprintf(msg, 256, "copy_to_dev (%zu bytes) %p -> %p (t=%d)", size, buf->host, (void*)buf->dev, halide_current_time() );
        #endif
        assert(halide_validate_dev_pointer(buf));
        TIME_START();
        int err = clEnqueueWriteBuffer( cl_q, (cl_mem)((void*)buf->dev), CL_TRUE, 0, size, buf->host, 0, NULL, NULL );
        CHECK_ERR( err, msg );
        TIME_CHECK(msg);
    }
    buf->host_dirty = false;
}

WEAK void halide_copy_to_host(buffer_t* buf) {

    // texImage2D

    if (buf->dev_dirty) {
        clFinish(cl_q); // block on completion before read back
        assert(buf->host && buf->dev);
        size_t size = buf_size(buf);
        #ifdef NDEBUG
        char msg[1];
        #else
        char msg[256];
        snprintf(msg, 256, "copy_to_host (%zu bytes) %p -> %p", size, (void*)buf->dev, buf->host );
        #endif
        assert(halide_validate_dev_pointer(buf, size));
        TIME_START();
        printf("%s\n", msg);
        int err = clEnqueueReadBuffer( cl_q, (cl_mem)((void*)buf->dev), CL_TRUE, 0, size, buf->host, 0, NULL, NULL );
        CHECK_ERR( err, msg );
        TIME_CHECK(msg);
    }
    buf->dev_dirty = false;
}
#define _COPY_TO_HOST

WEAK void halide_dev_run(
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[])
{
    cl_kernel f = __get_kernel(entry_name);
    #ifdef NDEBUG
    char msg[1];
    #else
    char msg[256];
    snprintf(
        msg, 256,
        "dev_run %s with (%dx%dx%d) blks, (%dx%dx%d) threads, %d shmem (t=%d)",
        entry_name, blocksX, blocksY, blocksZ, threadsX, threadsY, threadsZ, shared_mem_bytes,
        halide_current_time()
    );
    #endif
    // Pack dims
    size_t global_dim[3] = {blocksX*threadsX,  blocksY*threadsY,  blocksZ*threadsZ};
    size_t local_dim[3] = {threadsX, threadsY, threadsZ};

    // Set args
    int i = 0;
    while (arg_sizes[i] != 0) {
        CHECK_CALL( clSetKernelArg(f, i, arg_sizes[i], args[i]), "clSetKernelArg" );
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    CHECK_CALL( clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL), "clSetKernelArg" );

    // Launch kernel
    TIME_START();
    fprintf(stderr, "%s\n", msg);
    int err =
    clEnqueueNDRangeKernel(
        cl_q,
        f,
        3,
        NULL,
        global_dim,
        local_dim,
        0, NULL, NULL
    );
    CHECK_ERR(err, "clEnqueueNDRangeKernel");
    TIME_CHECK(msg);
    fprintf(stderr, "clEnqueueNDRangeKernel: %d\n", err);
}

#ifdef TEST_STUB
const char* src = "                                               \n"\
"__kernel void knl(                                                       \n" \
"   __global float* input,                                              \n" \
"   __global float* output,                                             \n" \
"   const unsigned int count,                                           \n" \
"   __local uchar* shared)                                            \n" \
"{                                                                      \n" \
"   int i = get_global_id(0);                                           \n" \
"   if(i < count)                                                       \n" \
"       output[i] = input[i] * input[i];                                \n" \
"}                                                                      \n";

int f( buffer_t *input, buffer_t *result, int N )
{
    const char* entry_name = "knl";

    int threadsX = 128;
    int threadsY =  1;
    int threadsZ =  1;
    int blocksX = N / threadsX;
    int blocksY = 1;
    int blocksZ = 1;


    threadsX = 8;
    threadsY =  1;
    threadsZ =  1;
    blocksX = 4;
    blocksY = 4;
    blocksZ = 1;

    // Invoke
    size_t argSizes[] = { sizeof(cl_mem), sizeof(cl_mem), sizeof(int), 0 };
    void* args[] = { &input->dev, &result->dev, &N, 0 };
    halide_dev_run(
        entry_name,
        blocksX,  blocksY,  blocksZ,
        threadsX, threadsY, threadsZ,
        1, // sharedMemBytes
        argSizes,
        args
    );

    return 0;
}

// this should probably also be the same as the openCL version
int main(int argc, char* argv[]) {
    halide_init_kernels(src);

    const int N = 2048;
    buffer_t in, out;

    in.dev = 0;
    in.host = (uint8_t*)malloc(N*sizeof(float));
    in.elem_size = sizeof(float);
    in.extent[0] = N; in.extent[1] = 1; in.extent[2] = 1; in.extent[3] = 1;

    out.dev = 0;
    out.host = (uint8_t*)malloc(N*sizeof(float));
    out.elem_size = sizeof(float);
    out.extent[0] = N; out.extent[1] = 1; out.extent[2] = 1; out.extent[3] = 1;

    for (int i = 0; i < N; i++) {
        ((float*)in.host)[i] = i / 2.0;
    }
    in.host_dirty = true;

    halide_dev_malloc(&in);
    halide_dev_malloc(&out);
    halide_copy_to_dev(&in);

    f( &in, &out, N );

    out.dev_dirty = true;
    halide_copy_to_host(&out);

    for (int i = 0; i < N; i++) {
        float a = ((float*)in.host)[i];
        float b = ((float*)out.host)[i];
        if (b != a*a) {
            printf("[%d] %f != %f^2\n", i, b, a);
        }
    }
}

#endif

} // extern "C" linkage

#endif
