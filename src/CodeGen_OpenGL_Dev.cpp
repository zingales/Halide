#include "CodeGen_OpenGL_Dev.h"
#include "Debug.h"

#define KNL_NAME_DELIMITER "//*KNL*//"
#define OUTPUT_NAME_DELIMITER "//*OUT*//"

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

        string idx;
        if (ends_with(loop->name, ".blockidx") || 
            ends_with(loop->name, ".blockidy")) {
            idx = "0";
        } else if (ends_with(loop->name, ".threadidx")) {
            idx = "int(pixcoord.x)";
        } else if (ends_with(loop->name, ".threadidy")) {
            idx = "int(pixcoord.y)";
        }
        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name) << " = " << idx << ";\n";
        loop->body.accept(this);
    } else {
    	assert(loop->for_type != For::Parallel && "Cannot emit parallel loops in OpenGL C");
    	CodeGen_C::visit(loop);
    }
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::visit(const Load *op) {
    //stream << "#if 0\n";
    //CodeGen_C::visit(op);
    //stream << "#endif\n";
    do_indent();
    stream << "texture("
           << print_name(op->name)
           << ", "
           << print_expr(op->index)
           << ");";
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::visit(const Store *op) {
    //stream << "#if 0\n";
    //CodeGen_C::visit(op);
    //stream << "#endif\n";
    do_indent();
    stream << OUTPUT_NAME_DELIMITER
           << op->name
           << OUTPUT_NAME_DELIMITER
           << "\n";
    do_indent();
    stream << "gl_FragColor = "
           << "vec4("
           << print_expr(op->value)
           << ",0,0,1)"
           << ";\n";
}

void CodeGen_OpenGL_Dev::compile(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi CodeGen_OpenGL_Dev::compile! " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    clc->compile(s, name, args);
}

namespace {
    const string preamble = "#version 120\n";
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::compile(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi! " << name << "\n";

    stream << preamble << KNL_NAME_DELIMITER << name << KNL_NAME_DELIMITER << "\n";

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            // add dimensions argument
            stream << "uniform sampler2D "
                   << print_name(args[i].name)
                   << ";\n"
                   << "uniform ivec4 dim_of_"
                   << print_name(args[i].name)
                   << ";\n";
        } else {
            stream << "uniform "
               << print_type(args[i].type)
               << " "
               << print_name(args[i].name)
               << ";\n";
        }
    }
    // add location argument
    stream << "varying vec2 pixcoord;\n";
    
    stream << "void main() {\n";
    indent++;
    print(s);
    indent--;
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
