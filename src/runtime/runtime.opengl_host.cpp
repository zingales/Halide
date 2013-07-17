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
#endif //NDEBUG



// Device, Context, Module, and Function for this entrypoint are tracked locally
// and constructed lazily on first run.
// TODO: make __f, __mod into arrays?
// static vector<CUfunction> __f;
}
extern "C" {

//apparently this is important
WEAK void *__dso_handle;

// for parsing shader source
#define KNL_DELIMITER "#version"
#define KNL_NAME_DELIMITER "//"

// map from kernel name to OpenGL program object reference
static std::map<std::string, GLuint> __gl_programs;

// map for storing additional texture data
typedef struct {
    buffer_t *buf;
} tex_metadata;
static std::map<GLuint, tex_metadata*> __tex_info;

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
// GLSL_ES is based on GLSL version 1.2
const char* vertex_shader_src = \
"#version 120                                         \n"\
"attribute vec2 position;                             \n"\
"varying vec2 pixcoord;                               \n"\
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
        const GLuint *tex_ref = (GLuint *) buf->dev;
        glDeleteTextures(1, tex_ref);
        __tex_info.erase(*tex_ref);
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
    glBindTexture(GL_TEXTURE_2D, texture);
    CHECK_ERROR();

    // set parameters to use this texture as an image - use NN and clamp edges
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h,
                 0, format, type, NULL);
    buf->dev = texture;
    // save metadata where we can access it via the texture object name (which should be unique)
    tex_metadata *d = (tex_metadata*) malloc(sizeof(tex_metadata));
    d->buf = buf;
    assert(__tex_info.count(texture)==0 && "texture names should be unique");
    __tex_info[texture] = d;
    // clean up
    glBindTexture(GL_TEXTURE_2D, 0);
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
        
        glBindTexture(GL_TEXTURE_2D, buf->dev);
        CHECK_ERROR();
        
        GLenum format = GL_RGB;
        GLenum type = GL_FLOAT;
        GLint internal_format = GL_RGB32F;
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h,
                     0, format, type, (void *) buf->host);
        CHECK_ERROR();
        
        glBindTexture(GL_TEXTURE_2D, 0);
        CHECK_ERROR();
    }
    buf->host_dirty = false;
}

WEAK void halide_copy_to_host(buffer_t* buf) {
    // should copy pixels from texture to cpu memory
    SAY_HI();
    if (buf->dev_dirty) {
        glFinish(); // is this necessary?
        glBindTexture(GL_TEXTURE_2D, (GLuint) buf->dev);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, (void *) buf->host);
        glBindTexture(GL_TEXTURE_2D, 0);
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
        assert(vertex_shader && "failed to make vertex shader");
        CHECK_ERROR();
        // now make fragment shader(s) from src
        std::string src_str = src;
        if (src_str.length() > 0) {
            size_t start_pos = src_str.find(KNL_DELIMITER); // start at first instance
            size_t end_pos = std::string::npos;
            std::string knl;
            std::string knl_name;
            std::string knl_name_delimiter = KNL_NAME_DELIMITER;
            while(true) {
                end_pos = src_str.find(KNL_DELIMITER, start_pos+1);
                printf("start %lu end %lu\n", start_pos, end_pos);
                if (start_pos >= end_pos) {
                    printf("bailing out and dumping source:\n%s\n", src);
                    assert(false && "source is missing #version delimiter");
                }
                knl = src_str.substr(start_pos, end_pos - start_pos);
                
                size_t knl_name_start = knl.find(knl_name_delimiter) 
                    + knl_name_delimiter.length();
                size_t knl_name_end = knl.find(knl_name_delimiter, knl_name_start);
                knl_name = knl.substr(knl_name_start, knl_name_end - knl_name_start);
                printf("making fragment shader named %s with src:\n---------\n%s\n--------\n",
                       knl_name.c_str(), knl.c_str());
                GLuint fragment_shader = make_shader(GL_FRAGMENT_SHADER, knl.c_str(), NULL);
                assert(fragment_shader && "failed to make fragment shader");
                // now make program
                GLuint program = make_program(vertex_shader, fragment_shader);
                assert(program && "failed to make program");
                __gl_programs[knl_name] = program;
                CHECK_ERROR();
                
                if (end_pos == std::string::npos) {
                    break;
                } else { // moar kernelz
                    start_pos = end_pos;
                }
            }
        }
        printf("kernel initialization success! src string lengeth was %ld\n", src_str.length());
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
    GLuint output_texture = * (GLuint *) args[1];
    glBindTexture(GL_TEXTURE_2D, output_texture);
    glBindFramebuffer(GL_FRAMEBUFFER, __framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, output_texture, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    std::string name_str;
    // add dimension arguments
    for (i = 0; i < n_active_uniforms; ++i) {
        glGetActiveUniform(program, i, max_uniform_length, NULL, &size, &type, name);
        if (arg_map.count(name) > 0 && type==GL_SAMPLER_2D) {
            // for each input texture, we look up the dimension value and add it as argument
            void * val = arg_map[name];
            GLuint texture = * (GLuint *) val;
            tex_metadata* d = __tex_info[texture];
            GLint dim[4];
            dim[0] = d->buf->extent[0];
            dim[1] = d->buf->extent[1];
            dim[2] = d->buf->extent[2];
            dim[3] = d->buf->extent[3];
            // it would be nicer maybe to put this in the arg_map
            name_str = "dim_of_" + std::string(name);
            GLint loc = glGetUniformLocation(program, name_str.c_str());
            glUniform4iv(loc, 1, dim);
        }
    }
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
            } else if (type==GL_SAMPLER_2D) {
                printf("setting Sampler2D arg %s to active texture %d with texture %d\n",
                       name, num_active_textures, * (GLuint *) val);
                // set active texture
                glActiveTexture(GL_TEXTURE0 + num_active_textures);
                // now this binds to active texture
                glBindTexture(GL_TEXTURE_2D, * (GLuint *) val);
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
                //assert(false && "unrecognized argument type :(");
            }
        } else if (true) {
            printf("missing argument %s\n", name);
            //assert(false);
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
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#ifdef TEST_STUB

//------------------------------------ helper functions ----------------------------//

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

const char* fragment_shader_src = \
"#version 120                                    \n"\
"//knl//                                         \n"\
"uniform sampler2D texture;                      \n"\
"uniform ivec4 dim_of_texture;                   \n"\
"uniform float fade_factor;                      \n"\
"uniform float useless;                          \n"\
"varying vec2 pixcoord;                          \n"\
"void main()                                     \n"\
"{                                               \n"\
"    vec4 tex_val = texture2D(texture, pixcoord/dim_of_texture.xy);\n"\
"    gl_FragColor = fade_factor*tex_val*tex_val; \n"\
"}                                               \n"\
"#version 120                                    \n"\
"//knl2//                                        \n"\
"uniform sampler2D texture;                      \n"\
"uniform ivec4 dim_of_texture;                   \n"\
"uniform float fade_factor;                      \n"\
"uniform float useless;                          \n"\
"varying vec2 pixcoord;                          \n"\
"void main()                                     \n"\
"{                                               \n"\
"    vec4 tex_val = texture2D(texture, pixcoord);\n"\
"    gl_FragColor = fade_factor*tex_val*tex_val; \n"\
"}                                               \n";

int f(buffer_t *input, buffer_t *result, float fade_factor ) {
    SAY_HI();
    const char* entry_name = "knl";
    
    int threadsX = result->extent[0];
    int threadsY = result->extent[1];
    int threadsZ = result->extent[2];
    int blocksX = 1; // don't care
    int blocksY = 1; // don't care
    int blocksZ = 1; // don't care
    int shared_mem_bytes = 0; // don't care
    
    char* arg_names[4];
    arg_names[0] = "texture";
    arg_names[1] = "output";
    arg_names[2] = "fade_factor";
    size_t argSizes[] = { 1, 1, 1, 0};
    void* args[] = { &input->dev, &result->dev, &fade_factor, 0 };
    
    halide_dev_run(
                   entry_name,
                   blocksX,  blocksY,  blocksZ,
                   threadsX, threadsY, threadsZ,
                   shared_mem_bytes,
                   arg_names,
                   argSizes,
                   args
                   );
    
    return 0;
}

int main(int argc, char* argv[]) {
    SAY_HI();
    printf("hello world!\n");
    halide_init_kernels(fragment_shader_src);
    
    int W = 4, H = 5, C = 3;
    
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
    
    float fade_factor = 2.0;
    f(&in, &out, fade_factor);
    
    out.dev_dirty = true;
    halide_copy_to_host(&out);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            //printf("(");
            for (int c = 0; c < 3; c++) {
                int idx = y*W*3 + x*3 + c;
                float tex_val = ((float *) in.host)[idx];
                float ref = fade_factor*tex_val*tex_val;
                float result = ((float *) out.host)[idx];
                //printf("%f ", result);
                if (ref != result) {
                    printf("mismatch at (%d, %d, %d), ref: %f  result: %f\n",
                           x, y, c, ref, result);
                    return -1;
                }
            }
            //printf(") ");
        }
        //printf("\n");
    }
    printf("success!\n");
    return 0;
}

#endif // TEST_STUB

} // extern "C" linkage
