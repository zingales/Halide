/*
 * Build standalone test (on Mac) with:
 *
 *   clang -framework OpenCL -DTEST_STUB runtime.opencl_host.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../buffer_t.h"
#include <map>
#include <string>
#include <math.h>

// The PTX host extends the x86 target
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

#include <GL/glew.h>
// Apple has GLUT framework headers in weird place
#ifdef __APPLE__
#  include <GLUT/glut.h> 
#else
#  include <GL/glut.h>
#endif

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#define WEAK __attribute__((weak))

extern "C" {

// #define NDEBUG // disable logging/asserts for performance

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

// for parsing shader source
#define KNL_DELIMITER "#version"
#define KNL_NAME_DELIMITER "//"

// map from kernel name to OpenGL program object reference
static std::map<std::string, GLuint> __gl_programs;


// shared framebuffer
static GLuint __framebuffer;

// have we initialized everything yet?
static bool __initialized;

// these two arrays are going to be used by every program
static GLuint vertex_buffer;
static GLuint element_buffer;

// info for filling said arrays
static const GLfloat g_vertex_buffer_data[] = { 
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f
};

static const GLushort g_element_buffer_data[] = { 0, 1, 2, 3 };

// vertex shader source
const char* vertex_shader_src = \
"#version 410                                         \n"\
"in vec2 position;                                    \n"\
"out vec2 pixcoord;                                   \n"\
"uniform ivec2 output_dim;                            \n"\
"void main()                                          \n"\
"{                                                    \n"\
"    gl_Position = vec4(position, 0.0, 1.0);          \n"\
"    vec2 texcoord = position*vec2(0.5) + vec2(0.5);  \n"\
"    pixcoord = output_dim*texcoord;                  \n"\
"}                                                    \n\0";

static GLuint vertex_shader;

//------------------------------------ open GL helper functions --------------------------//

/**
 * handle framebuffer errors
 */
void check_framebuffer_status(GLuint framebuffer) {
    SAY_HI();
    GLenum status = glCheckFramebufferStatus(framebuffer);
    if (status==GL_FRAMEBUFFER_COMPLETE) {
        printf("framebuffer =)\n");
    } else if (status==GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT) {
        printf("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
        //} else if (status==GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS) {
        //printf("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS\n");
    } else if (status==GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
        printf("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n");
    } else if (status==GL_FRAMEBUFFER_UNDEFINED) {
        printf("GL_FRAMEBUFFER_UNDEFINED\n");
    } else {
        printf("%d\n", status);
    }
}

/**
 * handle shader errors
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

/**
 * make shader from string
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

/**
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

/**
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

// -------------------------- END OPENGL CODE ---------------------------------//




cl_context WEAK cl_ctx = 0;
cl_command_queue WEAK cl_q = 0;

static cl_program __mod;
// static CUevent __start, __end;

// Used to create buffer_ts to track internal allocations caused by our runtime
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

// functions expected by CodeGen_GPU_Host

WEAK void halide_dev_free(buffer_t* buf) {
    SAY_HI();
    if (buf->dev) {
        glDeleteTextures(1, (const GLuint *) buf->dev);
    }
    buf->dev = 0;
    CHECK_ERROR();
}

WEAK void halide_dev_malloc(buffer_t* buf) {
    // we don't actually allocate memory here - we just get a name for the texture
    SAY_HI();
    if (buf->dev) {
        return;
    }

    // generate texture object name
    GLuint texture;
    glGenTextures(1, &texture);
    CHECK_ERROR();

    // create texture object
    glBindTexture(GL_TEXTURE_RECTANGLE, texture);
    CHECK_ERROR();

    // set parameters to use this texture as an image - use NN and clamp edges
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK_ERROR();

    int w = buf->extent[0];
    int h = buf->extent[1];
    // for now constrain buffer to have 3 color channels
    assert(buf->extent[2] == 3);
    assert(buf->extent[3] == 1);
    // TODO: vary format depending on 3rd dimension
    GLenum format = GL_RGB;
    GLenum type = GL_FLOAT;
    GLint internal_format = GL_RGB32F;
    // this actually allocates the space
    // the space will be used as long as subsequent calls
    // to glTexImage2D have the same fmt and dimensions
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, internal_format, w, h,
                 0, format, type, NULL);
    buf->dev = texture;
    // clean up
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    CHECK_ERROR();
}

// Used to generate correct timings when tracing
WEAK void halide_dev_sync() {  
    SAY_HI();
    // glFinish does not return until the effects of all previously called GL commands are complete.
    // Such effects include all changes to GL state, all changes to connection state, and all changes
    // to the frame buffer contents.
    glFinish();
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    SAY_HI();
    if(buf->host_dirty) {
        printf("copying texture %d to device\n", (int) buf->dev);
        int w = buf->extent[0];
        int h = buf->extent[1];
        // for now constrain buffer to have 3 color channels
        assert(buf->extent[2] == 3);
        assert(buf->extent[3] == 1);
        
        glBindTexture(GL_TEXTURE_RECTANGLE, buf->dev);
        CHECK_ERROR();
        
        GLenum format = GL_RGB;
        GLenum type = GL_FLOAT;
        GLint internal_format = GL_RGB32F;
        glTexImage2D(GL_TEXTURE_RECTANGLE, 0, internal_format, w, h,
                     0, format, type, (void *) buf->host);
        CHECK_ERROR();
        
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        CHECK_ERROR();
    }
    buf->host_dirty = false;
}

WEAK void halide_copy_to_host(buffer_t* buf) {
    // should copy pixels from texture to cpu memory
    SAY_HI();
    if (buf->dev_dirty) {
        glFinish(); // is this necessary?
        glBindTexture(GL_TEXTURE_RECTANGLE, (GLuint) buf->dev);
        glGetTexImage(GL_TEXTURE_RECTANGLE, 0, GL_RGB, GL_FLOAT, (void *) buf->host);
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        CHECK_ERROR();
    }
    buf->dev_dirty = false;
}

WEAK void halide_init_kernels(const char* src) {
    // need to split src (use #version as delimiter?)
    // and get kernel names (include as comments?)
    SAY_HI();
    if (!__initialized) {
        // initialize openGL stuff
        int argc = 0;
        glutInit(&argc, NULL);
        glutCreateWindow("Hello World");
        glewInit();
        // check flag for new enough version of OpenGL
        if (!GLEW_VERSION_4_0) {
            fprintf(stderr, "OpenGL 4.0 not available\n");
        }
        // make our framebuffer
        glGenFramebuffers(1, &__framebuffer);
        CHECK_ERROR();
        glBindFramebuffer(GL_FRAMEBUFFER, __framebuffer);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        CHECK_ERROR();
        // make our vertex and element buffers
        vertex_buffer = make_buffer(GL_ARRAY_BUFFER,
                                    g_vertex_buffer_data,
                                    sizeof(g_vertex_buffer_data));
        element_buffer = make_buffer(GL_ELEMENT_ARRAY_BUFFER,
                                     g_element_buffer_data,
                                     sizeof(g_element_buffer_data));;
        CHECK_ERROR();
        // make our shaders - vertex shader is the same for all
        vertex_shader = make_shader(GL_VERTEX_SHADER, vertex_shader_src, NULL);
        CHECK_ERROR();
        // now make fragment shader(s) from src
        std::string src_str = src;
        size_t start_pos = src_str.find(KNL_DELIMITER); // start at first instance
        size_t end_pos = std::string::npos;
        std::string knl;
        std::string knl_name;
        std::string knl_name_delimiter = KNL_NAME_DELIMITER;
        while(true) {
            end_pos = src_str.find(KNL_DELIMITER, start_pos+1);
            printf("start %lu end %lu\n", start_pos, end_pos);
            knl = src_str.substr(start_pos, end_pos - start_pos);

            size_t knl_name_start = knl.find(knl_name_delimiter) 
                + knl_name_delimiter.length();
            size_t knl_name_end = knl.find(knl_name_delimiter, knl_name_start);
            knl_name = knl.substr(knl_name_start, knl_name_end - knl_name_start);
            printf("making fragment shader named %s with src:\n---------\n%s\n--------\n",
                   knl_name.c_str(), knl.c_str());
            GLuint fragment_shader = make_shader(GL_FRAGMENT_SHADER, knl.c_str(), NULL);
            // now make program
            GLuint program = make_program(vertex_shader, fragment_shader);
            __gl_programs[knl_name] = program;
            CHECK_ERROR();

            if (end_pos == std::string::npos) {
                break;
            } else { // moar kernelz
                start_pos = end_pos;
            }
        }
        printf("kernel initialization success!\n");
    }
    // mark as initialized
    __initialized = true;
}

WEAK void halide_dev_run(
                         const char* entry_name,
                         int blocksX, int blocksY, int blocksZ,
                         int threadsX, int threadsY, int threadsZ,
                         int shared_mem_bytes,
                         char* arg_names[],
                         size_t arg_sizes[],
                         void* args[])
{
    SAY_HI();
    // attach output texture to framebuffer
    // TODO: we can't assume that this is our output
    GLuint input_texture = * (GLuint *) args[0];
    GLuint output_texture = * (GLuint *) args[1];
    glBindTexture(GL_TEXTURE_RECTANGLE, output_texture);
    glBindFramebuffer(GL_FRAMEBUFFER, __framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE, output_texture, 0);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    CHECK_ERROR();
    check_framebuffer_status(GL_FRAMEBUFFER);
    // The fragment shader output value is written into the nth color attachment
    // of the current framebuffer. n may range from 0 to the value of GL_MAX_COLOR_ATTACHMENTS.
    const GLenum bufs[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, bufs);
    CHECK_ERROR();
    // set the viewport to the size of the output
    glViewport(0, 0, threadsX, threadsY);
    CHECK_ERROR();
    // fetch the program
    GLuint program =  __gl_programs[entry_name];
    glUseProgram(program);
    // set args
    // first, put the input arguments into a map
    std::map <std::string, void*> arg_map;
    int i = 0;
    while(arg_sizes[i]!=0) {
        printf("arg[%d]: %s\n", i, arg_names[i]);
        std::string str(arg_names[i]);
        arg_map[str] = args[i];
        ++i;
    }
    // explicitly add output dimensions
    GLint output_dim = glGetUniformLocation(program, "output_dim");
    GLint output_dim_val[] = {threadsX, threadsY};
    arg_map["output_dim"] = (void *) output_dim_val;
    // now add passed in arguments
    GLint n_active_uniforms;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &n_active_uniforms);
    GLint max_uniform_length;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_uniform_length);
    printf("found %d active uniforms with max name length %d\n",
           n_active_uniforms, max_uniform_length);
    // keep track of active texture in case we have to bind multiple input textures
    int num_active_textures = 0;
    // allocate variables to store result of glGetActiveUniform
    GLint size;
    GLenum type;
    GLchar *name = (char*) malloc(max_uniform_length*sizeof(char));
    for (i = 0; i < n_active_uniforms; ++i) {
        glGetActiveUniform(program, i, max_uniform_length, NULL, &size, &type, name);
        if (arg_map.count(name) > 0) {
            GLint loc = glGetUniformLocation(program, name);
            void * val = arg_map[name];
            if (type==GL_FLOAT) {
                printf("setting float arg %s to %f\n", name, * (float *) val);
                glUniform1fv(loc, 1, (GLfloat *) val);
            } else if (type==GL_INT) {
                printf("setting int arg %s to %d\n", name, * (int *) val);
                glUniform1iv(loc, 1, (GLint *) val);
            } else if (type==GL_UNSIGNED_INT) {
                printf("setting unsigned int arg %s to %d\n", name, * (unsigned int *) val);
                glUniform1uiv(loc, 1, (GLuint *) val);
            } else if (type==GL_SAMPLER_2D_RECT) {
                printf("setting Sampler2DRect arg %s to %d\n", name, num_active_textures);
                // set active texture
                glActiveTexture(GL_TEXTURE0 + num_active_textures);
                // now this binds to active texture
                glBindTexture(GL_TEXTURE_RECTANGLE, * (GLuint *) val);
                glUniform1i(loc, num_active_textures);
                // increment so that if we have more textures
                // we bind to different active textures
                num_active_textures++;
            } else if (type==GL_INT_VEC2) {
                // this is probably our output dimensions
                printf("setting int vec2 arg %s to {%d, %d}\n", name,
                       * (int *) val, * (((int *) val) + 1));
                glUniform2iv(output_dim, 1, output_dim_val);
            } else {
                printf("missing case for argument %s\n", name);
                assert(false && "unrecognized argument type :(");
            }
        } else {
            printf("missing argument %s\n", name);
            assert(false);
        }
    }
    free(name);
    
    GLint position = glGetAttribLocation(program, "position");
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
    while(num_active_textures > 0) {
        num_active_textures--;
        glActiveTexture(GL_TEXTURE0 + num_active_textures);
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#if 0

WEAK void halide_dev_free(buffer_t* buf) {
    SAY_HI();
    if (buf->dev) {
        glDeleteTextures(1, (const GLuint *) buf->dev);
    }
    buf->dev = 0;
    CHECK_ERROR();
}

WEAK void halide_dev_malloc(buffer_t* buf) {
    // we don't actually allocate memory here - we just get a name for the texture
    SAY_HI();
    if (buf->dev) {
        return;
    }

    // generate texture object name
    GLuint texture;
    glGenTextures(1, &texture);
    CHECK_ERROR();

    // create texture object
    glBindTexture(GL_TEXTURE_RECTANGLE, texture);
    CHECK_ERROR();

    // set parameters to use this texture as an image - use NN and clamp edges
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK_ERROR();

    int w = buf->extent[0];
    int h = buf->extent[1];
    // for now constrain buffer to have 3 color channels
    assert(buf->extent[2] == 3);
    assert(buf->extent[3] == 1);
    // TODO: vary format depending on 3rd dimension
    GLenum format = GL_RGB;
    GLenum type = GL_FLOAT;
    GLint internal_format = GL_RGB32F;
    // this actually allocates the space
    // the space will be used as long as subsequent calls
    // to glTexImage2D have the same fmt and dimensions
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, internal_format, w, h,
                 0, format, type, NULL);
    buf->dev = texture;
    // clean up
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    CHECK_ERROR();
}
#endif

#if 0

WEAK void __release_buffer(buffer_t* buf)
{
    free(buf);
}
WEAK buffer_t* __malloc_buffer(int32_t size)
{
    return __make_buffer((uint8_t*)malloc(size), sizeof(uint8_t), size, 1, 1, 1);
}

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

WEAK void halide_dev_free(buffer_t* buf) {

    #ifndef NDEBUG
    fprintf(stderr, "In dev_free of %p - dev: 0x%p\n", buf, (void*)buf->dev);
    #endif

    assert(halide_validate_dev_pointer(buf));
    CHECK_CALL( clReleaseMemObject((cl_mem)buf->dev), "clReleaseMemObject" );
    buf->dev = 0;
}

WEAK void halide_init_kernels(const char* src) {
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
    clFinish(cl_q);
}

WEAK void halide_release() {
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
#endif

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
