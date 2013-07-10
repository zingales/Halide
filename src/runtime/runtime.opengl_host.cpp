/* build test

g++ -c -o runtime.opengl_host.o runtime.opengl_host.cpp -I/usr/include
gcc -o test-gl runtime.opengl_host.o -L/usr/lib -lGL -lglut -lGLEW -lm

*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <map>
#include <string>
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

//extern "C" {

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

// ----------------------------------- global state -------------------------------------//

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
"#version 410                                    \n"\
"in vec2 position;                               \n"\
"out vec2 texcoord;                              \n"\
"void main()                                     \n"\
"{                                               \n"\
"    gl_Position = vec4(position, 0.0, 1.0);     \n"\
"    texcoord = position*vec2(0.5) + vec2(0.5);  \n"\
"}                                               \n\0";

static GLuint vertex_shader;

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
    // this is what binds our output buffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE, texture, 0);
    CHECK_ERROR();
    check_framebuffer_status(GL_FRAMEBUFFER);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE) {
        printf("framebuffer =)\n");
    } else {
        printf("framebuffer =(\n");
    }
    CHECK_ERROR();
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



const char* fragment_shader_src = \
"#version 410                                    \n"\
"uniform sampler2DRect input;                    \n"\
"uniform ivec2 input_dim;                        \n"\
"uniform float fade_factor;                      \n"\
"uniform float useless;                      \n"\
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
    glutInit(&argc, NULL);
    // specify what buffers default framebuffer should have
    // this specifies a colour buffer with double buffering
    //glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    // set window size to 400x300
    GLint w = 768;
    GLint h = 1024;
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
    // GLuint input_framebuffer = make_framebuffer(input_texture);
    CHECK_ERROR();

    // generate texture object name
    GLuint output_texture = make_texture(w, h, NULL);
    CHECK_ERROR();
    
    // generate buffer object name
    GLuint output_framebuffer = make_framebuffer(output_texture);
    CHECK_ERROR();

    // somehow we know what the current framebufffer is
    printf("output_framebuffer: %d\n", output_framebuffer);
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

    // let's print out stuff just for fun
    GLint params;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &params);
    printf("number of active uniforms is %d\n", (int) params);
    GLsizei length;
    GLint size;
    GLenum type;
    GLchar name[100];
    for (int i = 0; i < params; i++) {
        glGetActiveUniform(program, i, 100, &length, &size, &type, name);
        GLint loc = glGetUniformLocation(program, name);
        printf("%s at %d\n", name, loc);
    }
    GLint fade_factor = glGetUniformLocation(program, "fade_factor");
    GLint input = glGetUniformLocation(program, "input");
    GLint input_dim = glGetUniformLocation(program, "input_dim");
    GLint useless = glGetUniformLocation(program, "useless");
    GLint position = glGetAttribLocation(program, "position");
    CHECK_ERROR();

    float fade_factor_val = 1.0;

    // render
    glUseProgram(program);
    // location, value 
   // things we need to know for each argument
    // argument name
    // count
    // value (can be pointer)
    // since args is an array of void*s we could pass in an array of structs
    // that have type and name information
    glUniform1fv(fade_factor, 1, &fade_factor_val);
    CHECK_ERROR();
    GLint dims[] = {w, h};
    glUniform2iv(input_dim, 1, dims); //(GLint) w, (GLint) h);
    CHECK_ERROR();
    float useless_val = 100000.0;
    glUniform1fv(useless, 1, &useless_val);
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
    // glDeleteFramebuffers(1, &input_framebuffer);
    glDeleteFramebuffers(1, &output_framebuffer);
    glDeleteTextures(1, &input_texture);
    glDeleteTextures(1, &output_texture);

    // suppresses compiler warnings
    return 0;
}


// -------------------------- END OPENGL CODE ---------------------------------//

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
        printf("uhoh\n");
        return;
    }

    // generate texture object name
    GLuint texture;
    // void glGenTextures(GLsizei  n, GLuint *  textures);
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

    printf("made new texture: %d\n", (int) buf->dev);
    int w = buf->extent[0];
    int h = buf->extent[1];
    // for now constrain buffer to have 3 color channels
    assert(buf->extent[2] == 3);
    assert(buf->extent[3] == 1);
    GLenum format = GL_RGB;
    GLenum type = GL_FLOAT;
    GLint internal_format = GL_RGB32F;
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, internal_format, w, h,
                 0, format, type, NULL);

    buf->dev = texture;

    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    CHECK_ERROR();
}

// Used to generate correct timings when tracing
// If all went well with OpenGl, this won't die
WEAK void halide_dev_sync() {  
    SAY_HI();
    // glFinish does not return until the effects of all previously called GL commands are complete.
    // Such effects include all changes to GL state, all changes to connection state, and all changes
    // to the frame buffer contents.
    glFinish();
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    // should copy pixels from cpu memory to texture
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
        printf("framebuffer %d\n", __framebuffer);
        glGenFramebuffers(1, &__framebuffer);
        check_framebuffer_status(GL_FRAMEBUFFER);
        printf("framebuffer %d\n", __framebuffer);
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
        GLuint fragment_shader = make_shader(GL_FRAGMENT_SHADER, src, NULL);
        const std::string knl_name = "knl";
        printf("making fragment shader named %s with src:\n%s\n--------\n",
               knl_name.c_str(), src);
        // now make program
        GLuint program = make_program(vertex_shader, fragment_shader);
        __gl_programs[knl_name] = program;
        CHECK_ERROR();
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
    size_t arg_sizes[],
    void* args[])
{
    SAY_HI();

    // attach output texture to framebuffer
    GLuint input_texture = * (GLuint *) args[0];
    GLuint output_texture = * (GLuint *) args[1];
    printf("output_texture is %d\n", output_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE, output_texture);
    CHECK_ERROR();
    glBindFramebuffer(GL_FRAMEBUFFER, __framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE, output_texture, 0);
    CHECK_ERROR();
    check_framebuffer_status(GL_FRAMEBUFFER);

    // set the viewport to the size of the output
    const GLenum bufs[] = {GL_COLOR_ATTACHMENT0};
    CHECK_ERROR();
    // The fragment shader output value is written into the nth color attachment
    // of the current framebuffer. n may range from 0 to the value of GL_MAX_COLOR_ATTACHMENTS.
    glDrawBuffers(1, bufs);
    CHECK_ERROR();
    
    // render into entire framebuffer
    glViewport(0, 0, threadsX, threadsY);
    CHECK_ERROR();
    // fetch the program
    GLuint program =  __gl_programs[entry_name];
    glUseProgram(program);    

    // set args
    GLint fade_factor = glGetUniformLocation(program, "fade_factor");
    GLint input = glGetUniformLocation(program, "input");
    GLint input_dim = glGetUniformLocation(program, "input_dim");
    GLint useless = glGetUniformLocation(program, "useless");
    
    float fade_factor_val = 1.0;
    glUniform1fv(fade_factor, 1, &fade_factor_val);
    CHECK_ERROR();
    GLint dims[] = {threadsX, threadsY};
    glUniform2iv(input_dim, 1, dims); //(GLint) w, (GLint) h);
    CHECK_ERROR();
    float useless_val = 100000.0;
    glUniform1fv(useless, 1, &useless_val);
    glActiveTexture(GL_TEXTURE0);
    CHECK_ERROR();
    glBindTexture(GL_TEXTURE_RECTANGLE, input_texture);
    CHECK_ERROR();
    // i think the 0 matches texture 0
    glUniform1i(input, 0);
    

    // TODO also need to set attributes
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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int f(buffer_t *input, buffer_t *result ) {
    SAY_HI();
    const char* entry_name = "knl";

    int threadsX = result->extent[0];
    int threadsY = result->extent[1];
    int threadsZ = result->extent[2];
    int blocksX = 1; // don't care
    int blocksY = 1; // don't care
    int blocksZ = 1; // don't care
    int shared_mem_bytes = 0; // don't care

    // Invoke
    // also doesn't matter?
    size_t argSizes[] = { 1, 1, 1, 0};
    // matters - maybe make this an array of structs?
    void* args[] = { &input->dev, &result->dev, NULL, 0 };

    halide_dev_run(
                   entry_name,
                   blocksX,  blocksY,  blocksZ,
                   threadsX, threadsY, threadsZ,
                   shared_mem_bytes,
                   argSizes,
                   args
                   );
    
    return 0;
}

// this should probably also be the same as the openCL version
int main(int argc, char* argv[]) {
    SAY_HI();
    printf("hello world!\n");
#if 1
    halide_init_kernels(fragment_shader_src);

    int W = 100, H = 200, C = 3;

    buffer_t in, out;

    in.dev = 0;
    in.host = (uint8_t*) random_image(W, H, C, 1);
    in.elem_size = sizeof(float);
    in.extent[0] = W; in.extent[1] = H; in.extent[2] = C; in.extent[3] = 1;

    out.dev = 0;
    out.host = (uint8_t*) empty_image(W, H, C, 1);
    out.elem_size = sizeof(float);
    out.extent[0] = W; out.extent[1] = H; out.extent[2] = C; out.extent[3] = 1;

    in.host_dirty = true;

    halide_dev_malloc(&in);
    halide_dev_malloc(&out);
    halide_copy_to_dev(&in);

    f(&in, &out);

    out.dev_dirty = true;
    halide_copy_to_host(&out);
    float fade_factor_val = 1.0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            for (int c = 0; c < 3; c++) {
                int idx = y*W*3 + x*3 + c;
                float tex_val = ((float *) in.host)[idx];
                float ref = fade_factor_val*tex_val*tex_val;
                float result = ((float *) out.host)[idx];
                if (ref != result) {
                    printf("mismatch at (%d, %d, %d), ref: %f  result: %f\n",
                           x, y, c, ref, result);
                }
            }
        }
    }
    printf("all good!\n");
#else
    main_old(0, NULL);
#endif
}

// } // extern C linkage

