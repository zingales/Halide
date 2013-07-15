#include "CodeGen_OpenGL_Dev.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

static ostringstream nil;

CodeGen_OpenGL_Dev::CodeGen_OpenGL_Dev() {
    clc = new CodeGen_OpenGL_C(src_stream);
}

string CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::print_type(Type type) {
    ostringstream oss;
    assert(type.width == 1 && "Can't codegen vector types to OpenGL C (yet)");
    if (type.is_float()) {
        // TODO figure out how many bits openGL floats have
        if (type.bits == 32) {
            oss << "float";
        } else {
            assert(false && "Can't represent a float with this many bits in OpenGL C");
        }
    } else {
        // TODO can't represent unsigned ints in glsl?
        switch (type.bits) {
        case 1:
            oss << "bool";
            break;
        case 32:
            oss << "int";
            break;
        default:
            assert(false && "Can't represent an integer with this many bits in OpenGL C");
        }
    }
    return oss.str();
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        debug(0) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        assert(loop->for_type == For::Parallel && "kernel loop must be parallel");

        loop->body.accept(this);
    } else {
    	assert(loop->for_type != For::Parallel && "Cannot emit parallel loops in OpenGL C");
    	CodeGen_C::visit(loop);
    }
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::visit(const Load *op) {
    CodeGen_C::visit(op);
    /*
    stream << "texture("
           << print_name(op->name)
           << ", "
           << print_expr(op->index)
           << ");";
    */
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::visit(const Store *op) {
    CodeGen_C::visit(op);
    /*
    stream << "gl_FragColor = "
           << "vec4("
           << print_expr(op->value)
           << ",0,0,1)"
           << ";\n";
    */
}

void CodeGen_OpenGL_Dev::compile(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi CodeGen_OpenGL_Dev::compile! " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    clc->compile(s, name, args);
}

namespace {
    const string preamble = "#version 410\n";
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::compile(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi! " << name << "\n";

    stream << preamble << "//" << name << "//\n";

    for (size_t i = 0; i < args.size(); i++) {
        stream << "uniform "
               << print_type(args[i].type)
               << " "
               << print_name(args[i].name)
               << ";\n";
    }

    stream << "void main() {\n";

    print(s);

    stream << "}\n";
}

void CodeGen_OpenGL_Dev::init_module() {
    debug(0) << "OpenGL device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    cur_kernel_name = "";
}

string CodeGen_OpenGL_Dev::compile_to_src() {
    return src_stream.str();
}

string CodeGen_OpenGL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenGL_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

}}
