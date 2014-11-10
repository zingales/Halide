#include "IRMultiMutator.h"

using std::vector;
using std::string;

namespace Halide {
namespace Internal {

void IRMultiMutator::mutate(Expr e, vector<Expr> &result) {
    if (e.defined()) {
        result.swap(exprs);
        e.accept(this);
        result.swap(exprs);
    } else {
        result.clear();
        result.push_back(Expr());
    }
    stmts.clear();
}

void IRMultiMutator::mutate(Stmt s, vector<Stmt> &result) {
    if (s.defined()) {
        result.swap(stmts);
        s.accept(this);
        result.swap(stmts);
    } else {
        result.clear();
        result.push_back(Stmt());
    }
    exprs.clear();
}

void IRMultiMutator::mutate(Type t, vector<Type> &result) {
    result.clear();
    result.push_back(t);
}

namespace {
// Get either the first or the nth thing in a vector, depending of
// whether n is in-range. If n is not in-range, it asserts the vector
// has size 1.
template<typename T>
T get_first_or_nth(const vector<T> &vec, int n) {
    if ((size_t)n < vec.size()) return vec[n];
    internal_assert(vec.size() == 1);
    return vec[0];
}
}

void IRMultiMutator::visit(const IntImm *op) {
    exprs.push_back(op);
}

void IRMultiMutator::visit(const FloatImm *op) {
    exprs.push_back(op);
}

void IRMultiMutator::visit(const StringImm *op) {
    exprs.push_back(op);
}

void IRMultiMutator::visit(const Cast *op) {
    vector<Expr> values;
    vector<Type> types;
    mutate(op->value, values);
    mutate(op->type, types);

    if (values.empty() || types.empty()) return;

    size_t size = std::max(values.size(), types.size());

    for (size_t i = 0; i < size; i++) {
        Expr value = get_first_or_nth(values, i);
        Type type = get_first_or_nth(types, i);

        if (value.same_as(op->value) && type == op->type) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Cast::make(type, value));
        }
    }
}

void IRMultiMutator::visit(const Variable *op) {
    if (let_forks.contains(op->name)) {
        int num_forks = let_forks.get(op->name);
        for (int i = 0; i < num_forks; i++) {
            exprs.push_back(Variable::make(op->type, op->name + "." + int_to_string(i)));
        }
    } else {
        exprs.push_back(op);
    }
}

namespace {

template<typename Op>
void visit_binary_op(const Op *op, vector<Expr> &result, IRMultiMutator *m) {
    vector<Expr> as, bs;
    m->mutate(op->a, as);
    m->mutate(op->b, bs);

    if (as.empty() || bs.empty()) return;

    size_t size = std::max(as.size(), bs.size());

    for (size_t i = 0; i < size; i++) {
        Expr a = get_first_or_nth(as, i);
        Expr b = get_first_or_nth(bs, i);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            result.push_back(op);
        } else {
            result.push_back(Op::make(a, b));
        }
    }
}
}

void IRMultiMutator::visit(const Add *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Sub *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Mul *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Div *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Mod *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Min *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Max *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const EQ *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const NE *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const LT *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const LE *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const GT *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const GE *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const And *op) {visit_binary_op(op, exprs, this);}
void IRMultiMutator::visit(const Or *op) {visit_binary_op(op, exprs, this);}

void IRMultiMutator::visit(const Not *op) {
    vector<Expr> as;
    mutate(op->a, as);

    for (size_t i = 0; i < as.size(); i++) {
        if (as[i].same_as(op->a)) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Not::make(as[i]));
        }
    }
}

void IRMultiMutator::visit(const Select *op) {
    vector<Expr> conditions, trues, falses;
    mutate(op->condition, conditions);
    mutate(op->true_value, trues);
    mutate(op->false_value, falses);

    size_t size = std::max(std::max(conditions.size(), trues.size()), falses.size());

    if (conditions.empty() || trues.empty() || falses.empty()) return;

    for (size_t i = 0; i < size; i++) {
        Expr c = get_first_or_nth(conditions, i);
        Expr t = get_first_or_nth(trues, i);
        Expr f = get_first_or_nth(falses, i);
        if (c.same_as(op->condition) &&
            t.same_as(op->true_value) &&
            f.same_as(op->false_value)) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Select::make(c, t, f));
        }
    }
}

void IRMultiMutator::visit(const Load *op) {
    vector<Expr> indexes;
    vector<Type> types;
    mutate(op->index, indexes);
    mutate(op->type, types);

    size_t size = std::max(indexes.size(), types.size());

    if (indexes.empty() || types.empty()) return;

    for (size_t i = 0; i < size; i++) {
        Expr index = get_first_or_nth(indexes, i);
        Type type = get_first_or_nth(types, i);
        if (index.same_as(op->index) && type == op->type) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Load::make(type, op->name, index, op->image, op->param));
        }
    }
}

void IRMultiMutator::visit(const Ramp *op) {
    vector<Expr> bases, strides;
    mutate(op->base, bases);
    mutate(op->stride, strides);

    if (bases.empty() || strides.empty()) return;

    size_t size = std::max(bases.size(), strides.size());

    for (size_t i = 0; i < size; i++) {
        Expr base = get_first_or_nth(bases, i);
        Expr stride = get_first_or_nth(strides, i);
        if (base.same_as(op->base) &&
            stride.same_as(op->stride)) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Ramp::make(base, stride, op->width));
        }
    }
}

void IRMultiMutator::visit(const Broadcast *op) {
    vector<Expr> values;
    mutate(op->value, values);

    for (size_t i = 0; i < values.size(); i++) {
        Expr value = values[i];
        if (value.same_as(op->value)) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Broadcast::make(value, op->width));
        }
    }
}

void IRMultiMutator::visit(const Call *op) {
    vector<vector<Expr> > args(op->args.size());
    vector<Type> types;
    mutate(op->type, types);

    size_t size = types.size();
    for (size_t i = 0; i < op->args.size(); i++) {
        mutate(op->args[i], args[i]);
        if (args[i].empty()) return;
        size = std::max(size, args[i].size());
    }

    for (size_t j = 0; j < size; j++) {
        vector<Expr> new_args(args.size());
        Type type = get_first_or_nth(types, j);
        bool unchanged = type == op->type;
        for (size_t i = 0; i < args.size(); i++) {
            new_args[i] = get_first_or_nth(args[i], j);
            unchanged &= new_args[i].same_as(op->args[i]);
        }
        if (unchanged) {
            exprs.push_back(op);
        } else {
            exprs.push_back(Call::make(type, op->name, new_args, op->call_type,
                                       op->func, op->value_index, op->image, op->param));
        }
    }

}

namespace {
template<typename StmtOrExpr, typename LetOrLetStmt>
void visit_let(LetOrLetStmt *op, vector<StmtOrExpr> &result, Scope<int> &let_forks, IRMultiMutator *m) {
    vector<Expr> values;
    vector<StmtOrExpr> bodies;
    m->mutate(op->value, values);
    m->mutate(op->body, bodies);

    size_t size = std::max(values.size(), bodies.size());

    if (values.empty() || bodies.empty()) return;

    if (size == 1) {
        if (bodies[0].same_as(op->body) && values[0].same_as(op->value)) {
            result.push_back(op);
        } else {
            result.push_back(LetOrLetStmt::make(op->name, values[0], bodies[0]));
        }
    } else if (bodies.size() == 1) {
        // Just wrap the lets around the body in series.
        StmtOrExpr body = bodies[0];
        for (size_t i = 0; i < values.size(); i++) {
            Expr value = values[i];
            string new_name = op->name + "." + int_to_string(i);
            body = LetOrLetStmt::make(new_name, value, body);
        }
    } else {
        // Keep the names unique by renaming the one in each fork.
        for (size_t i = 0; i < size; i++) {
            Expr value = get_first_or_nth(values, i);
            StmtOrExpr body = get_first_or_nth(bodies, i);
            string new_name = op->name + "." + int_to_string(i);
            let_forks.push(op->name, size);
            result.push_back(LetOrLetStmt::make(new_name, value, body));
        }
    }
}
}

void IRMultiMutator::visit(const Let *op) {
    visit_let(op, exprs, let_forks, this);
}

void IRMultiMutator::visit(const LetStmt *op) {
    visit_let(op, stmts, let_forks, this);
}

void IRMultiMutator::visit(const AssertStmt *op) {
    vector<Expr> conditions, messages;
    mutate(op->condition, conditions);
    mutate(op->message, messages);

    size_t size = std::max(conditions.size(), messages.size());

    for (size_t i = 0; i < size; i++) {
        Expr condition = get_first_or_nth(conditions, i);
        Expr message = get_first_or_nth(messages, i);
        if (condition.same_as(op->condition) &&
            message.same_as(op->message)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(AssertStmt::make(condition, message));
        }
    }
}

void IRMultiMutator::visit(const Pipeline *op) {
    vector<Stmt> produces, updates, consumes;

    mutate(op->produce, produces);
    mutate(op->update, updates);
    mutate(op->consume, consumes);

    size_t size = std::max(std::max(produces.size(), updates.size()), consumes.size());

    if (produces.empty() || updates.empty() || consumes.empty()) return;

    for (size_t i = 0; i < size; i++) {
        Stmt p = get_first_or_nth(produces, i);
        Stmt u = get_first_or_nth(updates, i);
        Stmt c = get_first_or_nth(consumes, i);
        if (p.same_as(op->produce) &&
            u.same_as(op->update) &&
            c.same_as(op->consume)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Pipeline::make(op->name, p, u, c));
        }
    }
}

void IRMultiMutator::visit(const For *op) {
    vector<Expr> mins, extents;
    vector<Stmt> bodies;

    mutate(op->min, mins);
    mutate(op->extent, extents);
    mutate(op->body, bodies);

    size_t size = std::max(std::max(mins.size(), extents.size()), bodies.size());

    if (mins.empty() || extents.empty() || bodies.empty()) return;

    if (size == 1) {
        Expr m = mins[0];
        Expr e = extents[0];
        Stmt b = bodies[0];
        if (m.same_as(op->min) && e.same_as(op->extent) && b.same_as(op->body)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(For::make(op->name, m, e, op->for_type, b));
        }
    } else {
        for (size_t i = 0; i < size; i++) {
            Expr m = get_first_or_nth(mins, i);
            Expr e = get_first_or_nth(extents, i);
            Stmt b = get_first_or_nth(bodies, i);
            string name = op->name + "." + int_to_string(i);
            let_forks.push(op->name, (int)size);
            stmts.push_back(For::make(name, m, e, op->for_type, b));
        }
    }
}

void IRMultiMutator::visit(const Store *op) {
    vector<Expr> values, indexes;
    mutate(op->value, values);
    mutate(op->index, indexes);

    size_t size = std::max(values.size(), indexes.size());

    for (size_t i = 0; i < size; i++) {
        Expr value = get_first_or_nth(values, i);
        Expr index = get_first_or_nth(indexes, i);
        if (value.same_as(op->value) &&
            index.same_as(op->index)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Store::make(op->name, value, index));
        }
    }
}

void IRMultiMutator::visit(const Provide *op) {
    vector<vector<Expr> > args(op->args.size());
    vector<vector<Expr> > values(op->values.size());
    size_t size = 0;

    for (size_t i = 0; i < op->args.size(); i++) {
        mutate(op->args[i], args[i]);
        if (args[i].empty()) return;
        size = std::max(size, args[i].size());
    }

    for (size_t i = 0; i < op->values.size(); i++) {
        mutate(op->values[i], values[i]);
        if (values[i].empty()) return;
        size = std::max(size, values[i].size());
    }

    for (size_t j = 0; j < size; j++) {
        vector<Expr> new_args(args.size());
        vector<Expr> new_values(values.size());
        bool unchanged = true;

        for (size_t i = 0; i < args.size(); i++) {
            new_args[i] = get_first_or_nth(args[i], j);
            unchanged &= new_args[i].same_as(op->args[i]);
        }

        for (size_t i = 0; i < values.size(); i++) {
            new_values[i] = get_first_or_nth(values[i], j);
            unchanged &= new_values[i].same_as(op->values[i]);
        }

        if (unchanged) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Provide::make(op->name, new_values, new_args));
        }
    }

}

void IRMultiMutator::visit(const Allocate *op) {
    vector<vector<Expr> > extents(op->extents.size());

    size_t size = 0;
    for (size_t i = 0; i < op->extents.size(); i++) {
        mutate(op->extents[i], extents[i]);
        if (extents[i].empty()) return;
        size = std::max(size, extents[i].size());
    }

    vector<Stmt> bodies;
    mutate(op->body, bodies);
    if (bodies.empty()) return;

    vector<Expr> conditions;
    mutate(op->condition, conditions);
    if (conditions.empty()) return;

    for (size_t j = 0; j < size; j++) {
        bool unchanged = true;

        Stmt body = get_first_or_nth(bodies, j);
        unchanged &= body.same_as(op->body);

        Expr condition = get_first_or_nth(conditions, j);
        unchanged &= condition.same_as(op->condition);

        vector<Expr> new_extents(extents.size());
        for (size_t i = 0; i < extents.size(); i++) {
            new_extents[i] = get_first_or_nth(extents[i], j);
            unchanged &= new_extents[i].same_as(op->extents[i]);
        }

        if (unchanged) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Allocate::make(op->name, op->type, new_extents, condition, body));
        }
    }
}

void IRMultiMutator::visit(const Free *op) {
    stmts.push_back(op);
}

void IRMultiMutator::visit(const Realize *op) {
    vector<vector<Expr> > new_mins(op->bounds.size());
    vector<vector<Expr> > new_extents(op->bounds.size());

    // Mutate the bounds
    size_t size = 0;
    for (size_t i = 0; i < op->bounds.size(); i++) {
        mutate(op->bounds[i].min, new_mins[i]);
        mutate(op->bounds[i].extent, new_extents[i]);
        if (new_mins.empty() || new_extents.empty()) return;
        size = std::max(size, new_mins.size());
        size = std::max(size, new_extents.size());
    }

    vector<Stmt> bodies;
    mutate(op->body, bodies);
    if (bodies.empty()) return;

    vector<Expr> conditions;
    mutate(op->condition, conditions);
    if (conditions.empty()) return;

    for (size_t j = 0; j < size; j++) {
        bool unchanged = true;

        Expr condition = get_first_or_nth(conditions, j);
        unchanged &= condition.same_as(op->condition);

        Stmt body = get_first_or_nth(bodies, j);
        unchanged &= body.same_as(op->body);

        Region new_bounds(op->bounds.size());
        for (size_t i = 0; i < new_bounds.size(); i++) {
            new_bounds[i].min = get_first_or_nth(new_mins[i], j);
            unchanged &= new_bounds[i].min.same_as(op->bounds[i].min);

            new_bounds[i].extent = get_first_or_nth(new_extents[i], j);
            unchanged &= new_bounds[i].extent.same_as(op->bounds[i].extent);
        }

        if (unchanged) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Realize::make(op->name, op->types, new_bounds, condition, body));
        }
    }
}

void IRMultiMutator::visit(const Block *op) {
    vector<Stmt> firsts, rests;
    mutate(op->first, firsts);
    mutate(op->rest, rests);

    if (firsts.empty() || rests.empty()) return;

    size_t size = std::max(firsts.size(), rests.size());

    for (size_t i = 0; i < size; i++) {
        Stmt first = get_first_or_nth(firsts, i);
        Stmt rest = get_first_or_nth(rests, i);
        if (first.same_as(op->first) && rest.same_as(op->rest)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Block::make(first, rest));
        }
    }
}

void IRMultiMutator::visit(const IfThenElse *op) {
    vector<Expr> conditions;
    vector<Stmt> thens, elses;
    mutate(op->condition, conditions);
    mutate(op->then_case, thens);
    mutate(op->else_case, elses);

    size_t size = std::max(std::max(conditions.size(), thens.size()), elses.size());

    if (conditions.empty() || thens.empty() || elses.empty()) return;

    for (size_t i = 0; i < size; i++) {
        Expr c = get_first_or_nth(conditions, i);
        Stmt t = get_first_or_nth(thens, i);
        Stmt e = get_first_or_nth(elses, i);
        if (c.same_as(op->condition) &&
            t.same_as(op->then_case) &&
            e.same_as(op->else_case)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(IfThenElse::make(c, t, e));
        }
    }
}

void IRMultiMutator::visit(const Evaluate *op) {
    vector<Expr> values;
    mutate(op->value, values);

    for (size_t i = 0; i < values.size(); i++) {
        Expr value = values[i];
        if (value.same_as(op->value)) {
            stmts.push_back(op);
        } else {
            stmts.push_back(Evaluate::make(value));
        }
    }
}

}
}
