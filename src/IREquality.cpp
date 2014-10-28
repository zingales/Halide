#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

/** The class that does the work of comparing two IR nodes. */
class IRComparer : public IRVisitor {
public:

    /** Different possible results of a comparison. Unknown should
     * only occur internally due to a cache miss. */
    enum CmpResult {Unknown, Equal, LessThan, GreaterThan};

    /** The result of the comparison. Should be Equal, LessThan, or GreaterThan. */
    CmpResult result;

    /** Compare two expressions or statements and return the
     * result. Returns the result immediately if it is already
     * non-zero. */
    // @{
    CmpResult compare(const Expr &a, const Expr &b);
    CmpResult compare(const Stmt &a, const Stmt &b);
    // @}

    /** If the expressions you're comparing may contain many repeated
     * subexpressions, it's worth passing in a cache to use.
     * Currently this is only done in common-subexpression
     * elimination. */
    IRComparer(IRCompareCache *c = NULL) : result(Equal), cache(c) {}

private:
    Expr expr;
    Stmt stmt;
    IRCompareCache *cache;

    CmpResult compare(const std::string &a, const std::string &b);
    CmpResult compare(Type a, Type b);
    template<typename T>
    CmpResult compare(const std::vector<T> &a, const std::vector<T> &b);

    // Compare two things that already have a well-defined operator<
    template<typename T>
    CmpResult compare(T a, T b);

    void visit(const IntImm *);
    void visit(const FloatImm *);
    void visit(const StringImm *);
    void visit(const Cast *);
    void visit(const Variable *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Call *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const AssertStmt *);
    void visit(const Pipeline *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const Block *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);

};

template<typename T>
IRComparer::CmpResult IRComparer::compare(T a, T b) {
    if (result != Equal) return result;

    if (a < b) {
        result = LessThan;
    } else if (a > b) {
        result = GreaterThan;
    }

    return result;
}

IRComparer::CmpResult IRComparer::compare(const Expr &a, const Expr &b) {
    if (result != Equal) {
        return result;
    }

    if (a.same_as(b)) {
        result = Equal;
        return result;
    }

    if (!a.defined() && !b.defined()) {
        result = Equal;
        return result;
    }

    if (!a.defined()) {
        result = LessThan;
        return result;
    }

    if (!b.defined()) {
        result = GreaterThan;
        return result;
    }

    // If in the future we have hashes for Exprs, this is a good place
    // to compare the hashes:
    // if (compare(a.hash(), b.hash()) != Equal) {
    //   return result;
    // }

    if (compare(a.ptr->type_info(), b.ptr->type_info()) != Equal) {
        return result;
    }

    if (compare(a.type(), b.type()) != Equal) {
        return result;
    }

    // Check the cache - perhaps these exprs have already been compared and found equal.
    if (cache && cache->contains(a, b)) {
        result = Equal;
        return result;
    }

    expr = a;
    b.accept(this);

    if (cache && result == Equal) {
        cache->insert(a, b);
    }

    return result;
}

IRComparer::CmpResult IRComparer::compare(const Stmt &a, const Stmt &b) {
    if (result != Equal) {
        return result;
    }

    if (a.same_as(b)) {
        result = Equal;
        return result;
    }

    if (!a.defined() && !b.defined()) {
        result = Equal;
        return result;
    }

    if (!a.defined()) {
        result = LessThan;
        return result;
    }

    if (!b.defined()) {
        result = GreaterThan;
        return result;
    }

    if (compare(a.ptr->type_info(), b.ptr->type_info()) != Equal) {
        return result;
    }

    stmt = a;
    b.accept(this);

    return result;
}

IRComparer::CmpResult IRComparer::compare(Type a, Type b) {
    if (result != Equal) return result;

    compare(a.code, b.code);
    compare(a.bits, b.bits);
    compare(a.width, b.width);

    return result;
}

IRComparer::CmpResult IRComparer::compare(const string &a, const string &b) {
    if (result != Equal) return result;

    int string_cmp = a.compare(b);
    if (string_cmp < 0) {
        result = LessThan;
    } else if (string_cmp > 0) {
        result = GreaterThan;
    }

    return result;
}


template<typename T>
IRComparer::CmpResult IRComparer::compare(const vector<T> &a, const vector<T> &b) {
    if (result != Equal) return result;

    compare(a.size(), b.size());
    for (size_t i = 0; (i < a.size()) && result == Equal; i++) {
        compare(a[i], b[i]);
    }

    return result;
}

void IRComparer::visit(const IntImm *op) {
    const IntImm *e = expr.as<IntImm>();
    compare(e->value, op->value);
}

void IRComparer::visit(const FloatImm *op) {
    const FloatImm *e = expr.as<FloatImm>();
    compare(e->value, op->value);
}

void IRComparer::visit(const StringImm *op) {
    const StringImm *e = expr.as<StringImm>();
    compare(e->value, op->value);
}

void IRComparer::visit(const Cast *op) {
    compare(expr.as<Cast>()->value, op->value);
}

void IRComparer::visit(const Variable *op) {
    const Variable *e = expr.as<Variable>();
    compare(e->name, op->name);
}

namespace {
template<typename T>
void visit_binary_operator(IRComparer *cmp, const T *op, Expr expr) {
    const T *e = expr.as<T>();
    cmp->compare(e->a, op->a);
    cmp->compare(e->b, op->b);
}
}

void IRComparer::visit(const Add *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Sub *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Mul *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Div *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Mod *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Min *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Max *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const EQ *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const NE *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const LT *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const LE *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const GT *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const GE *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const And *op) {visit_binary_operator(this, op, expr);}
void IRComparer::visit(const Or *op) {visit_binary_operator(this, op, expr);}

void IRComparer::visit(const Not *op) {
    const Not *e = expr.as<Not>();
    compare(e->a, op->a);
}

void IRComparer::visit(const Select *op) {
    const Select *e = expr.as<Select>();
    compare(e->condition, op->condition);
    compare(e->true_value, op->true_value);
    compare(e->false_value, op->false_value);

}

void IRComparer::visit(const Load *op) {
    const Load *e = expr.as<Load>();
    compare(op->name, e->name);
    compare(e->index, op->index);
}

void IRComparer::visit(const Ramp *op) {
    const Ramp *e = expr.as<Ramp>();
    // No need to compare width because we already compared types
    compare(e->base, op->base);
    compare(e->stride, op->stride);
}

void IRComparer::visit(const Broadcast *op) {
    const Broadcast *e = expr.as<Broadcast>();
    compare(e->value, op->value);
}

void IRComparer::visit(const Call *op) {
    const Call *e = expr.as<Call>();

    compare(e->name, op->name);
    compare(e->call_type, op->call_type);
    compare(e->value_index, op->value_index);
    compare(e->args, op->args);
}

void IRComparer::visit(const Let *op) {
    const Let *e = expr.as<Let>();

    compare(e->name, op->name);
    compare(e->value, op->value);
    compare(e->body, op->body);
}

void IRComparer::visit(const LetStmt *op) {
    const LetStmt *s = stmt.as<LetStmt>();

    compare(s->name, op->name);
    compare(s->value, op->value);
    compare(s->body, op->body);
}

void IRComparer::visit(const AssertStmt *op) {
    const AssertStmt *s = stmt.as<AssertStmt>();

    compare(s->condition, op->condition);
    compare(s->message, op->message);
}

void IRComparer::visit(const Pipeline *op) {
    const Pipeline *s = stmt.as<Pipeline>();

    compare(s->name, op->name);
    compare(s->produce, op->produce);
    compare(s->update, op->update);
    compare(s->consume, op->consume);
}

void IRComparer::visit(const For *op) {
    const For *s = stmt.as<For>();

    compare(s->name, op->name);
    compare(s->for_type, op->for_type);
    compare(s->min, op->min);
    compare(s->extent, op->extent);
    compare(s->body, op->body);
}

void IRComparer::visit(const Store *op) {
    const Store *s = stmt.as<Store>();

    compare(s->name, op->name);

    compare(s->value, op->value);
    compare(s->index, op->index);
}

void IRComparer::visit(const Provide *op) {
    const Provide *s = stmt.as<Provide>();

    compare(s->name, op->name);
    compare(s->args, op->args);
    compare(s->values, op->values);
}

void IRComparer::visit(const Allocate *op) {
    const Allocate *s = stmt.as<Allocate>();

    compare(s->name, op->name);
    compare(s->extents, op->extents);
    compare(s->body, op->body);
    compare(s->condition, op->condition);
}

void IRComparer::visit(const Realize *op) {
    const Realize *s = stmt.as<Realize>();

    compare(s->name, op->name);
    compare(s->types.size(), op->types.size());
    compare(s->bounds.size(), op->bounds.size());
    for (size_t i = 0; (result == Equal) && (i < s->types.size()); i++) {
        compare(s->types[i], op->types[i]);
    }
    for (size_t i = 0; (result == Equal) && (i < s->bounds.size()); i++) {
        compare(s->bounds[i].min, op->bounds[i].min);
        compare(s->bounds[i].extent, op->bounds[i].extent);
    }
    compare(s->body, op->body);
    compare(s->condition, op->condition);
}

void IRComparer::visit(const Block *op) {
    const Block *s = stmt.as<Block>();

    compare(s->stmts, op->stmts);
}

void IRComparer::visit(const Free *op) {
    const Free *s = stmt.as<Free>();

    compare(s->name, op->name);
}

void IRComparer::visit(const IfThenElse *op) {
    const IfThenElse *s = stmt.as<IfThenElse>();

    compare(s->condition, op->condition);
    compare(s->then_case, op->then_case);
    compare(s->else_case, op->else_case);
}

void IRComparer::visit(const Evaluate *op) {
    const Evaluate *s = stmt.as<Evaluate>();

    compare(s->value, op->value);
}

} // namespace


// Now the methods exposed in the header.
bool equal(Expr a, Expr b) {
    return IRComparer().compare(a, b) == IRComparer::Equal;
}

bool equal(Stmt a, Stmt b) {
    return IRComparer().compare(a, b) == IRComparer::Equal;
}

bool IRDeepCompare::operator()(const Expr &a, const Expr &b) const {
    IRComparer cmp;
    cmp.compare(a, b);
    return cmp.result == IRComparer::LessThan;
}

bool IRDeepCompare::operator()(const Stmt &a, const Stmt &b) const {
    IRComparer cmp;
    cmp.compare(a, b);
    return cmp.result == IRComparer::LessThan;
}

bool ExprWithCompareCache::operator<(const ExprWithCompareCache &other) const {
    IRComparer cmp(cache);
    cmp.compare(expr, other.expr);
    return cmp.result == IRComparer::LessThan;
}

// Testing code
namespace {

IRComparer::CmpResult flip_result(IRComparer::CmpResult r) {
    switch(r) {
    case IRComparer::LessThan: return IRComparer::GreaterThan;
    case IRComparer::Equal: return IRComparer::Equal;
    case IRComparer::GreaterThan: return IRComparer::LessThan;
    case IRComparer::Unknown: return IRComparer::Unknown;
    }
    return IRComparer::Unknown;
}

void check_equal(Expr a, Expr b) {
    IRCompareCache cache(5);
    IRComparer::CmpResult r = IRComparer(&cache).compare(a, b);
    internal_assert(r == IRComparer::Equal)
        << "Error in ir_equality_test: " << r
        << " instead of " << IRComparer::Equal
        << " when comparing:\n" << a
        << "\nand\n" << b << "\n";
}

void check_not_equal(Expr a, Expr b) {
    IRCompareCache cache(5);
    IRComparer::CmpResult r1 = IRComparer(&cache).compare(a, b);
    IRComparer::CmpResult r2 = IRComparer(&cache).compare(b, a);
    internal_assert(r1 != IRComparer::Equal &&
                    r1 != IRComparer::Unknown &&
                    flip_result(r1) == r2)
        << "Error in ir_equality_test: " << r1
        << " is not the opposite of " << r2
        << " when comparing:\n" << a
        << "\nand\n" << b << "\n";
}

} // namespace

void ir_equality_test() {
    Expr x = Variable::make(Int(32), "x");
    check_equal(Ramp::make(x, 4, 3), Ramp::make(x, 4, 3));
    check_not_equal(Ramp::make(x, 2, 3), Ramp::make(x, 4, 3));

    check_equal(x, Variable::make(Int(32), "x"));
    check_not_equal(x, Variable::make(Int(32), "y"));

    // Something that will hang if IREquality has poor computational
    // complexity.
    Expr e1 = x, e2 = x;
    for (int i = 0; i < 100; i++) {
        e1 = e1*e1 + e1;
        e2 = e2*e2 + e2;
    }
    check_equal(e1, e2);
    // These are only discovered to be not equal way down the tree:
    e2 = e2*e2 + e2;
    check_not_equal(e1, e2);

    debug(0) << "ir_equality_test passed\n";
}

}}
