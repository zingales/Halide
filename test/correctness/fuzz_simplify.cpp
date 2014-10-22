#include <stdio.h>
#include <Halide.h>
#include <time.h>
#include "../common/random_expr.h"
// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.

using namespace std;
using namespace Halide;
using namespace Halide::Internal;

// Generate expressions with 5 variables.
RandomExprGenerator fuzz(5);

Expr a(Variable::make(Int(0), "a"));
Expr b(Variable::make(Int(0), "b"));
Expr c(Variable::make(Int(0), "c"));
Expr d(Variable::make(Int(0), "d"));
Expr e(Variable::make(Int(0), "e"));

Expr ramp(Expr b, Expr s, int w) { return Ramp::make(b, s, w); }
Expr x1(Expr x) { return Broadcast::make(x, 2); }
Expr x2(Expr x) { return Broadcast::make(x, 2); }
Expr x4(Expr x) { return Broadcast::make(x, 2); }
Expr uint1(Expr x) { return Cast::make(UInt(1), x); }
Expr uint8(Expr x) { return Cast::make(UInt(8), x); }
Expr uint16(Expr x) { return Cast::make(UInt(16), x); }
Expr uint32(Expr x) { return Cast::make(UInt(32), x); }
Expr int8(Expr x) { return Cast::make(Int(8), x); }
Expr int16(Expr x) { return Cast::make(Int(16), x); }
Expr int32(Expr x) { return Cast::make(Int(32), x); }
Expr uint1x2(Expr x) { return Cast::make(UInt(1).vector_of(2), x); }
Expr uint8x2(Expr x) { return Cast::make(UInt(8).vector_of(2), x); }
Expr uint16x2(Expr x) { return Cast::make(UInt(16).vector_of(2), x); }
Expr uint32x2(Expr x) { return Cast::make(UInt(32).vector_of(2), x); }
Expr int8x2(Expr x) { return Cast::make(Int(8).vector_of(2), x); }
Expr int16x2(Expr x) { return Cast::make(Int(16).vector_of(2), x); }
Expr int32x2(Expr x) { return Cast::make(Int(32).vector_of(2), x); }



bool test_simplification(Expr a, Expr b, Type T, const map<string, Expr> &vars) {
    for (int j = 0; j < T.width; j++) {
        Expr a_j = a;
        Expr b_j = b;
        if (T.width != 1) {
            a_j = extract_lane(a, j);
            b_j = extract_lane(b, j);
        }

        Expr a_j_v = simplify(substitute(vars, a_j));
        Expr b_j_v = simplify(substitute(vars, b_j));
        // If the simplifier didn't produce constants, there must be
        // undefined behavior in this expression. Ignore it.
        if (!Internal::is_const(a_j_v) || !Internal::is_const(b_j_v)) {
            continue;
        }
        if (!equal(a_j_v, b_j_v)) {
            for(map<string, Expr>::const_iterator i = vars.begin(); i != vars.end(); i++) {
                std::cout << i->first << " = " << i->second << '\n';
            }

            std::cout << a << '\n';
            std::cout << b << '\n';
            std::cout << "In vector lane " << j << ":\n";
            std::cout << a_j << " -> " << a_j_v << '\n';
            std::cout << b_j << " -> " << b_j_v << '\n';
            return false;
        }
    }
    return true;
}

bool test_expression(Expr test, int samples) {
    Expr simplified = simplify(test);

    map<string, Expr> vars;
    for (size_t i = 0; i < fuzz.leafs.size(); i++) {
        if (const Variable *var = fuzz.leafs[i].as<Variable>()) {
            vars[var->name] = Expr();
        }
    }

    for (int i = 0; i < samples; i++) {
        for (std::map<string, Expr>::iterator v = vars.begin(); v != vars.end(); v++) {
            v->second = fuzz.random_leaf(test.type().element_of(), true);
        }

        if (!test_simplification(test, simplified, test.type(), vars)) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    // Number of random expressions to test.
    const int count = 1000;
    // Depth of the randomly generated expression trees.
    const int depth = 5;
    // Number of samples to test the generated expressions for.
    const int samples = 3;

    // We want different fuzz tests every time, to increase coverage.
    // We also report the seed to enable reproducing failures.
    int fuzz_seed = time(NULL);
    srand(fuzz_seed);
    std::cout << "Simplify fuzz test seed: " << fuzz_seed << '\n';

    int max_fuzz_vector_width = 4;

    for (size_t i = 0; i < fuzz.types.size(); i++) {
        Type T = fuzz.types[i];
        for (int w = 1; w < max_fuzz_vector_width; w *= 2) {
            Type VT = T.vector_of(w);
            for (int n = 0; n < count; n++) {
                // Generate a random expr...
                Expr test = fuzz.random_expr(VT, depth);
                if (!test_expression(test, samples)) {
                    return -1;
                }
            }
        }
    }
    std::cout << "Success!" << std::endl;
    return 0;
}

