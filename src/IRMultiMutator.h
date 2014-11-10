#ifndef HALIDE_IR_MULTI_MUTATOR_H
#define HALIDE_IR_MULTI_MUTATOR_H

#include "IR.h"
#include "Scope.h"

/** \file
 *
 * Defines a base class for mutations of IR that can cause single
 * Exprs to branch into multiple ones.
 */

namespace Halide {
namespace Internal {

/** A mutator similar to an IRMutator, but an Expr can transform to a
 * list of Exprs, and a Stmt can transform into a list of Stmts. */
class IRMultiMutator : public IRVisitor {

public:
    /** Mutate an Expr into a list of Exprs. Returns the list in the second argument. */
    EXPORT virtual void mutate(Expr expr, std::vector<Expr> &result);

    /** Mutate a Stmt into a list of Stmts. Returns the list in the second argument. */
    EXPORT virtual void mutate(Stmt stmt, std::vector<Stmt> &result);

    /** Mutate a Type into a list of Types. The default mutators for
     * Cast, Call, and Load use this to determine the types of the
     * copies. By default it just sets the result to the vector
     * containing the first argument. */
    EXPORT virtual void mutate(Type t, std::vector<Type> &result);

protected:
    /** The list of Exprs at the top of the stack. Equivalent to the
     * expr member of IRMutator. */
    std::vector<Expr> exprs;

    /** The list of stmts. */
    std::vector<Stmt> stmts;

    /** LetStmts can fork into a block of LetStmts. The ones that
     * forked are recorded here, along with how many copies they
     * forked into. The new names are the old name + "."  + the
     * index. */
    Scope<int> let_forks;

    // These methods return by appending to exprs and stmts.
    EXPORT virtual void visit(const IntImm *);
    EXPORT virtual void visit(const FloatImm *);
    EXPORT virtual void visit(const StringImm *);
    EXPORT virtual void visit(const Cast *);
    EXPORT virtual void visit(const Variable *);
    EXPORT virtual void visit(const Add *);
    EXPORT virtual void visit(const Sub *);
    EXPORT virtual void visit(const Mul *);
    EXPORT virtual void visit(const Div *);
    EXPORT virtual void visit(const Mod *);
    EXPORT virtual void visit(const Min *);
    EXPORT virtual void visit(const Max *);
    EXPORT virtual void visit(const EQ *);
    EXPORT virtual void visit(const NE *);
    EXPORT virtual void visit(const LT *);
    EXPORT virtual void visit(const LE *);
    EXPORT virtual void visit(const GT *);
    EXPORT virtual void visit(const GE *);
    EXPORT virtual void visit(const And *);
    EXPORT virtual void visit(const Or *);
    EXPORT virtual void visit(const Not *);
    EXPORT virtual void visit(const Select *);
    EXPORT virtual void visit(const Load *);
    EXPORT virtual void visit(const Ramp *);
    EXPORT virtual void visit(const Broadcast *);
    EXPORT virtual void visit(const Call *);
    EXPORT virtual void visit(const Let *);
    EXPORT virtual void visit(const LetStmt *);
    EXPORT virtual void visit(const AssertStmt *);
    EXPORT virtual void visit(const Pipeline *);
    EXPORT virtual void visit(const For *);
    EXPORT virtual void visit(const Store *);
    EXPORT virtual void visit(const Provide *);
    EXPORT virtual void visit(const Allocate *);
    EXPORT virtual void visit(const Free *);
    EXPORT virtual void visit(const Realize *);
    EXPORT virtual void visit(const Block *);
    EXPORT virtual void visit(const IfThenElse *);
    EXPORT virtual void visit(const Evaluate *);
};

}
}

#endif
