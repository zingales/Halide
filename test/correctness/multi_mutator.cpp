#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

using std::vector;

// Define a multi-mutator that scalarizes vector code.
class Scalarize : public IRMultiMutator {
    void visit(const Variable *op) {
        IRMultiMutator::visit(op);
        // Change the type of each expr to scalar.
        for (size_t i = 0; i < exprs.size(); i++) {
            const Variable *v = exprs[i].as<Variable>();
            exprs[i] = Variable::make(v->type.element_of(), v->name);
        }
    }

    void visit(const Broadcast *op) {
        exprs.push_back(op->value);
    }

    void visit(const Ramp *op) {
        for (int i = 0; i < op->width; i++) {
            exprs.push_back(op->base + cast(op->base.type(), i) * op->stride);
        }
    }

    void visit(const For *op) {
        IRMultiMutator::visit(op);
        // Make sure for loops don't get forked into multiple copies.
        assert(stmts.size() == 1);
    }

public:
    using IRMultiMutator::mutate;

    void mutate(Stmt s, vector<Stmt> &result) {
        // All stmt mutations should merge the results into a block of
        // stmts - we don't want multiple copies for For loops and the
        // like.
        IRMultiMutator::mutate(s, result);
        Stmt stmt = result.back();
        while (result.size() > 1) {
            result.pop_back();
            stmt = Block::make(result.back(), stmt);
        }
        result[0] = stmt;
    }

    void mutate(Type t, vector<Type> &result) {
        for (int i = 0; i < t.width; i++) {
            result.push_back(t.element_of());
        }
    }

};

Stmt scalarize(Stmt s) {
    vector<Stmt> stmts;
    Scalarize().mutate(s, stmts);
    if (stmts.size() != 1) {
        printf("Wrong number of stmts returned from scalarize: %d\n", (int)stmts.size());
        exit(-1);
    }
    return simplify(stmts[0]);
}

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    f(x, y) = x + y * 2.5f;

    g(x, y) = cast<uint8_t>(x) + 3;
    g(x, y) += cast<uint8_t>(2);

    h(x, y) = g(x-1, y) + g(x+1, y) + f(x, y);

    f.compute_at(h, y).vectorize(x, 8).unroll(x, 2);
    h.vectorize(x, 4);

    h.add_custom_lowering_pass(&scalarize);

    Image<float> output = h.realize(32, 32);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float correct = 10.0f + x * 3 + y * 2.5;
            if (output(x, y) != correct) {
                printf("output(%d, %d) = %f instead of %f\n",
                       x, y, output(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
