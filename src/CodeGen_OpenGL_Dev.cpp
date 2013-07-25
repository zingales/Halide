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
    if (type.is_scalar()) {
        if (type.is_float()) {
            if (type.bits == 32) {
                oss << "float";
            } else {
                assert(false && "Can't represent a float with this many bits in OpenGL ES SL");
            }
        } else {
            if (type.bits == 1) {
                oss << "bool";
            } else {
                assert(type.is_int() && "Can't represent an unsigned integer in OpenGL ES SL");
                if (type.bits == 32) {
                    oss << "int";
                } else {
                    assert("Can't represent an integer with this many bits in OpenGL ES SL");
                }
            }
        }
    } else if (type.width <= 4) {
        if (type.is_bool()) {
            oss << "b";
        } else if (type.is_int()) {
            oss << "i";
        } else if (!type.is_float()) {
            assert(type.is_int() && "Can't represent an unsigned integer in OpenGL ES SL, let alone a vector of them");
        }
        oss << "vec" << type.width;
        // TODO width
    } else {
        assert(false && "Can't codegen vector types wider than 4 in OpenGL ES SL");
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
    ostringstream rhs;
    //Type f1 = Float(32, 1);
    string idx_1d = print_expr(op->index);
    string x = "pixcoord.x"; //print_assignment(f1, "mod(" + idx_1d + ", dim_of_" + op->name + ".x);");
    string y = "pixcoord.y"; //print_assignment(f1, idx_1d + "/dim_of_" + op->name + ".y);");
    string idx_2d = "vec2(" + x + "," + y + ")";
    //rhs << "dim_of_" << print_name(op->name) << ".x";
    rhs << "texture2D("
        << print_name(op->name)
        << ", "
        << idx_2d
        << "/dim_of_"
        << print_name(op->name)
        << ".xy"
        << ").x";
    //Type f4 = Float(32, 4);
    print_assignment(op->type, rhs.str());
}

void CodeGen_OpenGL_Dev::CodeGen_OpenGL_C::visit(const Store *op) {
    do_indent();
    stream << OUTPUT_NAME_DELIMITER
           << op->name
           << OUTPUT_NAME_DELIMITER
           << "\n";
    string val = print_expr(op->value);
    do_indent();
    // TODO: handle higher dimensional outputs (this only works in 2D)
    stream << "gl_FragColor = "
           << "vec4("
           << val
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
                   << "; //"
                   << print_type(args[i].type)
                   << "\n"
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
