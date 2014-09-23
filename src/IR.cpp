#include "IR.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

uint64_t hash_string(const std::string &s) {
    uint64_t result = 12345;
    for (size_t i = 0; i < s.size(); i++) {
        result = result * 33 + s[i];
    }
    return result * 17;
}

uint64_t hash_float(float f) {
    uint64_t val = reinterpret_bits<uint32_t>(f);
    return (val << 32) | 0xdeadbeef;
}

uint64_t hash_int(int x) {
    uint64_t val = x;
    val *= 0x12345679;
    return val;
}

namespace {

IntImm make_immortal_int(int x) {
    IntImm i;
    i.ref_count.increment();
    i.type = Int(32);
    i.value = x;
    i.hash = hash_int(x);
    return i;
}

// Some hashing helpers
struct HashCode {
    HashCode(Expr e);
    HashCode(Stmt e);
    HashCode(int x);
    HashCode(Type t);
    HashCode(const std::string &s);
    operator uint64_t() {return val;}
    uint64_t val;
    void combine(HashCode);
};


void HashCode::combine(HashCode h) {
    // Taken from boost::hash_combine
    val ^= (h.val + 0x9e3779b9 + (val << 6) + (val >> 2));
}

HashCode hash_combine(HashCode a, HashCode b) {
    a.combine(b);
    return a;
}

HashCode hash_combine(HashCode a, HashCode b, HashCode c) {
    a.combine(b);
    a.combine(c);
    return a;
}

HashCode hash_combine(HashCode a, HashCode b, HashCode c, HashCode d) {
    a.combine(b);
    a.combine(c);
    a.combine(d);
    return a;
}

HashCode hash_combine(HashCode a, HashCode b, HashCode c, HashCode d, HashCode e) {
    a.combine(b);
    a.combine(c);
    a.combine(d);
    a.combine(e);
    return a;
}


HashCode hash_combine(HashCode a, HashCode b, HashCode c, HashCode d, HashCode e, HashCode f) {
    a.combine(b);
    a.combine(c);
    a.combine(d);
    a.combine(e);
    a.combine(f);
    return a;
}

HashCode::HashCode(Type t) {
    val = hash_combine(t.code, t.bits, t.width);
}

HashCode::HashCode(int x) {
    val = x;
    val = (val << 32) | x;
}

HashCode::HashCode(Expr e) : val(e.hash()) {}

HashCode::HashCode(Stmt s) : val(s.hash()) {}

HashCode::HashCode(const std::string &s) : val(hash_string(s)) {}

}

IntImm IntImm::small_int_cache[] = {make_immortal_int(-8),
                                    make_immortal_int(-7),
                                    make_immortal_int(-6),
                                    make_immortal_int(-5),
                                    make_immortal_int(-4),
                                    make_immortal_int(-3),
                                    make_immortal_int(-2),
                                    make_immortal_int(-1),
                                    make_immortal_int(0),
                                    make_immortal_int(1),
                                    make_immortal_int(2),
                                    make_immortal_int(3),
                                    make_immortal_int(4),
                                    make_immortal_int(5),
                                    make_immortal_int(6),
                                    make_immortal_int(7),
                                    make_immortal_int(8)};


Expr Cast::make(Type t, Expr v) {
    internal_assert(v.defined()) << "Cast of undefined\n";

    Cast *node = new Cast;
    node->type = t;
    node->value = v;
    node->hash = hash_combine(Cast::_type_info, t, v);
    return node;
}

Expr Add::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Add of undefined\n";
    internal_assert(b.defined()) << "Add of undefined\n";
    internal_assert(a.type() == b.type()) << "Add of mismatched types\n";

    Add *node = new Add;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Add::_type_info, a, b);
    return node;
}

Expr Sub::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Sub of undefined\n";
    internal_assert(b.defined()) << "Sub of undefined\n";
    internal_assert(a.type() == b.type()) << "Sub of mismatched types\n";

    Sub *node = new Sub;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Sub::_type_info, a, b);
    return node;
}

Expr Mul::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Mul of undefined\n";
    internal_assert(b.defined()) << "Mul of undefined\n";
    internal_assert(a.type() == b.type()) << "Mul of mismatched types\n";

    Mul *node = new Mul;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Mul::_type_info, a, b);
    return node;
}

Expr Div::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Div of undefined\n";
    internal_assert(b.defined()) << "Div of undefined\n";
    internal_assert(a.type() == b.type()) << "Div of mismatched types\n";

    Div *node = new Div;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Div::_type_info, a, b);
    return node;
}

Expr Mod::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Mod of undefined\n";
    internal_assert(b.defined()) << "Mod of undefined\n";
    internal_assert(a.type() == b.type()) << "Mod of mismatched types\n";

    Mod *node = new Mod;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Mod::_type_info, a, b);
    return node;
}

Expr Min::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Min of undefined\n";
    internal_assert(b.defined()) << "Min of undefined\n";
    internal_assert(a.type() == b.type()) << "Min of mismatched types\n";

    Min *node = new Min;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Min::_type_info, a, b);
    return node;
}

Expr Max::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Max of undefined\n";
    internal_assert(b.defined()) << "Max of undefined\n";
    internal_assert(a.type() == b.type()) << "Max of mismatched types\n";

    Max *node = new Max;
    node->type = a.type();
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Max::_type_info, a, b);
    return node;
}

Expr EQ::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "EQ of undefined\n";
    internal_assert(b.defined()) << "EQ of undefined\n";
    internal_assert(a.type() == b.type()) << "EQ of mismatched types\n";

    EQ *node = new EQ;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(EQ::_type_info, a, b);
    return node;
}

Expr NE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "NE of undefined\n";
    internal_assert(b.defined()) << "NE of undefined\n";
    internal_assert(a.type() == b.type()) << "NE of mismatched types\n";

    NE *node = new NE;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(NE::_type_info, a, b);
    return node;
}

Expr LT::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "LT of undefined\n";
    internal_assert(b.defined()) << "LT of undefined\n";
    internal_assert(a.type() == b.type()) << "LT of mismatched types\n";

    LT *node = new LT;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(LT::_type_info, a, b);
    return node;
}


Expr LE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "LE of undefined\n";
    internal_assert(b.defined()) << "LE of undefined\n";
    internal_assert(a.type() == b.type()) << "LE of mismatched types\n";

    LE *node = new LE;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(LE::_type_info, a, b);
    return node;
}

Expr GT::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "GT of undefined\n";
    internal_assert(b.defined()) << "GT of undefined\n";
    internal_assert(a.type() == b.type()) << "GT of mismatched types\n";

    GT *node = new GT;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(GT::_type_info, a, b);
    return node;
}


Expr GE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "GE of undefined\n";
    internal_assert(b.defined()) << "GE of undefined\n";
    internal_assert(a.type() == b.type()) << "GE of mismatched types\n";

    GE *node = new GE;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(GE::_type_info, a, b);
    return node;
}

Expr And::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "And of undefined\n";
    internal_assert(b.defined()) << "And of undefined\n";
    internal_assert(a.type().is_bool()) << "lhs of And is not a bool\n";
    internal_assert(b.type().is_bool()) << "rhs of And is not a bool\n";

    And *node = new And;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(And::_type_info, a, b);
    return node;
}

Expr Or::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Or of undefined\n";
    internal_assert(b.defined()) << "Or of undefined\n";
    internal_assert(a.type().is_bool()) << "lhs of Or is not a bool\n";
    internal_assert(b.type().is_bool()) << "rhs of Or is not a bool\n";

    Or *node = new Or;
    node->type = Bool(a.type().width);
    node->a = a;
    node->b = b;
    node->hash = hash_combine(Or::_type_info, a, b);
    return node;
}

Expr Not::make(Expr a) {
    internal_assert(a.defined()) << "Not of undefined\n";
    internal_assert(a.type().is_bool()) << "argument of Not is not a bool\n";

    Not *node = new Not;
    node->type = Bool(a.type().width);
    node->a = a;
    node->hash = hash_combine(Not::_type_info, a);
    return node;
}

Expr Select::make(Expr condition, Expr true_value, Expr false_value) {
    internal_assert(condition.defined()) << "Select of undefined\n";
    internal_assert(true_value.defined()) << "Select of undefined\n";
    internal_assert(false_value.defined()) << "Select of undefined\n";
    internal_assert(condition.type().is_bool()) << "First argument to Select is not a bool\n";
    internal_assert(false_value.type() == true_value.type()) << "Select of mismatched types\n";
    internal_assert(condition.type().is_scalar() ||
                    condition.type().width == true_value.type().width)
        << "In Select, vector width of condition must either be 1, or equal to vector width of arguments\n";

    Select *node = new Select;
    node->type = true_value.type();
    node->condition = condition;
    node->true_value = true_value;
    node->false_value = false_value;
    node->hash = hash_combine(Select::_type_info, condition, true_value, false_value);
    return node;
}

Expr Load::make(Type type, std::string name, Expr index, Buffer image, Parameter param) {
    internal_assert(index.defined()) << "Load of undefined\n";
    internal_assert(type.width == index.type().width) << "Vector width of Load must match vector width of index\n";

    Load *node = new Load;
    node->type = type;
    node->name = name;
    node->index = index;
    node->image = image;
    node->param = param;
    node->hash = hash_combine(Load::_type_info, type, name, index);
    return node;
}

Expr Ramp::make(Expr base, Expr stride, int width) {
    internal_assert(base.defined()) << "Ramp of undefined\n";
    internal_assert(stride.defined()) << "Ramp of undefined\n";
    internal_assert(base.type().is_scalar()) << "Ramp with vector base\n";
    internal_assert(stride.type().is_scalar()) << "Ramp with vector stride\n";
    internal_assert(width > 1) << "Ramp of width <= 1\n";
    internal_assert(stride.type() == base.type()) << "Ramp of mismatched types\n";

    Ramp *node = new Ramp;
    node->type = base.type().vector_of(width);
    node->base = base;
    node->stride = stride;
    node->width = width;
    node->hash = hash_combine(Ramp::_type_info, base, stride, width);
    return node;
}

Expr Broadcast::make(Expr value, int width) {
    internal_assert(value.defined()) << "Broadcast of undefined\n";
    internal_assert(value.type().is_scalar()) << "Broadcast of vector\n";
    internal_assert(width > 1) << "Broadcast of width <= 1\n";

    Broadcast *node = new Broadcast;
    node->type = value.type().vector_of(width);
    node->value = value;
    node->width = width;
    node->hash = hash_combine(Broadcast::_type_info, value, width);
    return node;
}

Expr Let::make(std::string name, Expr value, Expr body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    Let *node = new Let;
    node->type = body.type();
    node->name = name;
    node->value = value;
    node->body = body;
    node->hash = hash_combine(Let::_type_info, name, value, body);
    return node;
}

Stmt LetStmt::make(std::string name, Expr value, Stmt body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    LetStmt *node = new LetStmt;
    node->name = name;
    node->value = value;
    node->body = body;
    node->hash = hash_combine(LetStmt::_type_info, name, value, body);
    return node;
}

Stmt AssertStmt::make(Expr condition, Expr message) {
    internal_assert(condition.defined()) << "AssertStmt of undefined\n";

    AssertStmt *node = new AssertStmt;
    node->condition = condition;
    node->message = message;
    node->hash = hash_combine(AssertStmt::_type_info, condition, message);
    return node;
}

Stmt AssertStmt::make(Expr condition, const char * message) {
    return AssertStmt::make(condition, Expr(message));
}

Stmt AssertStmt::make(Expr condition, const std::vector<Expr> &message) {
    internal_assert(!message.empty()) << "Assert with empty message\n";
    Expr m = Call::make(Handle(), Call::stringify, message, Call::Intrinsic);
    return AssertStmt::make(condition, m);
}

Stmt Pipeline::make(std::string name, Stmt produce, Stmt update, Stmt consume) {
    internal_assert(produce.defined()) << "Pipeline of undefined\n";
    // update is allowed to be null
    internal_assert(consume.defined()) << "Pipeline of undefined\n";

    Pipeline *node = new Pipeline;
    node->name = name;
    node->produce = produce;
    node->update = update;
    node->consume = consume;
    node->hash = hash_combine(Pipeline::_type_info, produce, update, consume);
    return node;
}

Stmt For::make(std::string name, Expr min, Expr extent, ForType for_type, Stmt body) {
    internal_assert(min.defined()) << "For of undefined\n";
    internal_assert(extent.defined()) << "For of undefined\n";
    internal_assert(min.type().is_scalar()) << "For with vector min\n";
    internal_assert(extent.type().is_scalar()) << "For with vector extent\n";
    internal_assert(body.defined()) << "For of undefined\n";

    For *node = new For;
    node->name = name;
    node->min = min;
    node->extent = extent;
    node->for_type = for_type;
    node->body = body;
    node->hash = hash_combine(For::_type_info, name, min, extent, for_type, body);
    return node;
}

Stmt Store::make(std::string name, Expr value, Expr index) {
    internal_assert(value.defined()) << "Store of undefined\n";
    internal_assert(index.defined()) << "Store of undefined\n";

    Store *node = new Store;
    node->name = name;
    node->value = value;
    node->index = index;
    node->hash = hash_combine(Store::_type_info, name, value, index);
    return node;
}

Stmt Provide::make(std::string name, const std::vector<Expr> &values, const std::vector<Expr> &args) {
    HashCode h = hash_combine(Provide::_type_info, name);
    internal_assert(!values.empty()) << "Provide of no values\n";
    for (size_t i = 0; i < values.size(); i++) {
        internal_assert(values[i].defined()) << "Provide of undefined value\n";
        h.combine(values[i]);
    }
    for (size_t i = 0; i < args.size(); i++) {
        internal_assert(args[i].defined()) << "Provide to undefined location\n";
        h.combine(args[i]);
    }

    Provide *node = new Provide;
    node->name = name;
    node->values = values;
    node->args = args;
    node->hash = h;
    return node;
}

Stmt Allocate::make(std::string name, Type type, const std::vector<Expr> &extents,
                 Expr condition, Stmt body) {
    HashCode h = hash_combine(Allocate::_type_info, name, type, condition, body);
    for (size_t i = 0; i < extents.size(); i++) {
        internal_assert(extents[i].defined()) << "Allocate of undefined extent\n";
        internal_assert(extents[i].type().is_scalar() == 1) << "Allocate of vector extent\n";
        h.combine(extents[i]);
    }
    internal_assert(body.defined()) << "Allocate of undefined\n";

    Allocate *node = new Allocate;
    node->name = name;
    node->type = type;
    node->extents = extents;
    node->condition = condition;
    node->body = body;
    node->hash = h;
    return node;
}

Stmt Free::make(std::string name) {
    Free *node = new Free;
    node->name = name;
    node->hash = hash_combine(Free::_type_info, name);
    return node;
}

Stmt Realize::make(const std::string &name, const std::vector<Type> &types, const Region &bounds, Expr condition, Stmt body) {
    HashCode h = hash_combine(Realize::_type_info, condition, body);
    for (size_t i = 0; i < bounds.size(); i++) {
        internal_assert(bounds[i].min.defined()) << "Realize of undefined\n";
        internal_assert(bounds[i].extent.defined()) << "Realize of undefined\n";
        internal_assert(bounds[i].min.type().is_scalar()) << "Realize of vector size\n";
        internal_assert(bounds[i].extent.type().is_scalar()) << "Realize of vector size\n";
        h.combine(bounds[i].min);
        h.combine(bounds[i].extent);
    }
    for (size_t i = 0; i < types.size(); i++) {
        h.combine(types[i]);
    }
    internal_assert(body.defined()) << "Realize of undefined\n";
    internal_assert(!types.empty()) << "Realize has empty type\n";

    Realize *node = new Realize;
    node->name = name;
    node->types = types;
    node->bounds = bounds;
    node->condition = condition;
    node->body = body;
    node->hash = h;
    return node;
}

Stmt Block::make(Stmt first, Stmt rest) {
    internal_assert(first.defined()) << "Block of undefined\n";
    // rest is allowed to be null

    Block *node = new Block;
    node->first = first;
    node->rest = rest;
    node->hash = hash_combine(Block::_type_info, first, rest);
    return node;
}

Stmt IfThenElse::make(Expr condition, Stmt then_case, Stmt else_case) {
    internal_assert(condition.defined() && then_case.defined()) << "IfThenElse of undefined\n";
    // else_case may be null.

    IfThenElse *node = new IfThenElse;
    node->condition = condition;
    node->then_case = then_case;
    node->else_case = else_case;
    node->hash = hash_combine(IfThenElse::_type_info, condition, then_case, else_case);
    return node;
}

Stmt Evaluate::make(Expr v) {
    internal_assert(v.defined()) << "Evaluate of undefined\n";

    Evaluate *node = new Evaluate;
    node->value = v;
    node->hash = hash_combine(Evaluate::_type_info, v);
    return node;
}

Expr Call::make(Type type, std::string name, const std::vector<Expr> &args, CallType call_type,
                Function func, int value_index,
                Buffer image, Parameter param) {
    HashCode h = hash_combine(type, name, call_type, value_index);
    for (size_t i = 0; i < args.size(); i++) {
        internal_assert(args[i].defined()) << "Call of undefined\n";
        h.combine(args[i]);
    }
    if (call_type == Halide) {
        internal_assert(value_index >= 0 &&
                        value_index < func.outputs())
            << "Value index out of range in call to halide function\n";
        internal_assert((func.has_pure_definition() || func.has_extern_definition())) << "Call to undefined halide function\n";
        internal_assert((int)args.size() <= func.dimensions()) << "Call node with too many arguments.\n";
        for (size_t i = 0; i < args.size(); i++) {
            internal_assert(args[i].type() == Int(32)) << "Args to call to halide function must be type Int(32)\n";
        }
    } else if (call_type == Image) {
        internal_assert((param.defined() || image.defined())) << "Call node to undefined image\n";
        for (size_t i = 0; i < args.size(); i++) {
            internal_assert(args[i].type() == Int(32)) << "Args to load from image must be type Int(32)\n";
        }
    }

    Call *node = new Call;
    node->type = type;
    node->name = name;
    node->args = args;
    node->call_type = call_type;
    node->func = func;
    node->value_index = value_index;
    node->image = image;
    node->param = param;
    node->hash = h;
    return node;
}

Expr Variable::make(Type type, std::string name, Buffer image, Parameter param, ReductionDomain reduction_domain) {
    internal_assert(!name.empty());
    Variable *node = new Variable;
    node->type = type;
    node->name = name;
    node->image = image;
    node->param = param;
    node->reduction_domain = reduction_domain;
    node->hash = hash_combine(type, name);
    return node;
}

template<> EXPORT IRNodeType ExprNode<IntImm>::_type_info = 0;
template<> EXPORT IRNodeType ExprNode<FloatImm>::_type_info = 1;
template<> EXPORT IRNodeType ExprNode<StringImm>::_type_info = 2;
template<> EXPORT IRNodeType ExprNode<Cast>::_type_info = 3;
template<> EXPORT IRNodeType ExprNode<Variable>::_type_info = 4;
template<> EXPORT IRNodeType ExprNode<Add>::_type_info = 5;
template<> EXPORT IRNodeType ExprNode<Sub>::_type_info = 6;
template<> EXPORT IRNodeType ExprNode<Mul>::_type_info = 7;
template<> EXPORT IRNodeType ExprNode<Div>::_type_info = 8;
template<> EXPORT IRNodeType ExprNode<Mod>::_type_info = 9;
template<> EXPORT IRNodeType ExprNode<Min>::_type_info = 10;
template<> EXPORT IRNodeType ExprNode<Max>::_type_info = 11;
template<> EXPORT IRNodeType ExprNode<EQ>::_type_info = 12;
template<> EXPORT IRNodeType ExprNode<NE>::_type_info = 13;
template<> EXPORT IRNodeType ExprNode<LT>::_type_info = 14;
template<> EXPORT IRNodeType ExprNode<LE>::_type_info = 15;
template<> EXPORT IRNodeType ExprNode<GT>::_type_info = 16;
template<> EXPORT IRNodeType ExprNode<GE>::_type_info = 17;
template<> EXPORT IRNodeType ExprNode<And>::_type_info = 18;
template<> EXPORT IRNodeType ExprNode<Or>::_type_info = 19;
template<> EXPORT IRNodeType ExprNode<Not>::_type_info = 20;
template<> EXPORT IRNodeType ExprNode<Select>::_type_info = 21;
template<> EXPORT IRNodeType ExprNode<Load>::_type_info = 22;
template<> EXPORT IRNodeType ExprNode<Ramp>::_type_info = 23;
template<> EXPORT IRNodeType ExprNode<Broadcast>::_type_info = 24;
template<> EXPORT IRNodeType ExprNode<Call>::_type_info = 25;
template<> EXPORT IRNodeType ExprNode<Let>::_type_info = 26;
template<> EXPORT IRNodeType StmtNode<LetStmt>::_type_info = 27;
template<> EXPORT IRNodeType StmtNode<AssertStmt>::_type_info = 28;
template<> EXPORT IRNodeType StmtNode<Pipeline>::_type_info = 29;
template<> EXPORT IRNodeType StmtNode<For>::_type_info = 30;
template<> EXPORT IRNodeType StmtNode<Store>::_type_info = 31;
template<> EXPORT IRNodeType StmtNode<Provide>::_type_info = 32;
template<> EXPORT IRNodeType StmtNode<Allocate>::_type_info = 33;
template<> EXPORT IRNodeType StmtNode<Free>::_type_info = 34;
template<> EXPORT IRNodeType StmtNode<Realize>::_type_info = 35;
template<> EXPORT IRNodeType StmtNode<Block>::_type_info = 36;
template<> EXPORT IRNodeType StmtNode<IfThenElse>::_type_info = 37;
template<> EXPORT IRNodeType StmtNode<Evaluate>::_type_info = 38;

using std::string;
const string Call::debug_to_file = "debug_to_file";
const string Call::shuffle_vector = "shuffle_vector";
const string Call::interleave_vectors = "interleave_vectors";
const string Call::reinterpret = "reinterpret";
const string Call::bitwise_and = "bitwise_and";
const string Call::bitwise_not = "bitwise_not";
const string Call::bitwise_xor = "bitwise_xor";
const string Call::bitwise_or = "bitwise_or";
const string Call::shift_left = "shift_left";
const string Call::shift_right = "shift_right";
const string Call::abs = "abs";
const string Call::lerp = "lerp";
const string Call::random = "random";
const string Call::rewrite_buffer = "rewrite_buffer";
const string Call::profiling_timer = "profiling_timer";
const string Call::create_buffer_t = "create_buffer_t";
const string Call::extract_buffer_min = "extract_buffer_min";
const string Call::extract_buffer_max = "extract_buffer_max";
const string Call::set_host_dirty = "set_host_dirty";
const string Call::set_dev_dirty = "set_dev_dirty";
const string Call::popcount = "popcount";
const string Call::count_leading_zeros = "count_leading_zeros";
const string Call::count_trailing_zeros = "count_trailing_zeros";
const string Call::undef = "undef";
const string Call::address_of = "address_of";
const string Call::null_handle = "null_handle";
const string Call::trace = "trace";
const string Call::trace_expr = "trace_expr";
const string Call::return_second = "return_second";
const string Call::if_then_else = "if_then_else";
const string Call::glsl_texture_load = "glsl_texture_load";
const string Call::glsl_texture_store = "glsl_texture_store";
const string Call::make_struct = "make_struct";
const string Call::stringify = "stringify";
const string Call::memoize_expr = "memoize_expr";
const string Call::copy_memory = "copy_memory";

}
}
