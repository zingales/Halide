#ifndef RANDOM_EXPR_GENERATOR_H
#define RANDOM_EXPR_GENERATOR_H

// Helper file for generating random expressions.
#include <Halide.h>

namespace Halide {

class RandomExprGenerator {
public:
    std::vector<Expr> leafs;
    std::vector<Type> types;

    RandomExprGenerator(int var_count = 5) {
        for (int i = 0; i < var_count; i++) {
            leafs.push_back(Internal::Variable::make(Int(0), std::string(1, 'a' + i)));
        }

        // Default behavior is to generate all the typical integer types
        // less than 32 bit.
        types.push_back(UInt(1));
        types.push_back(UInt(8));
        types.push_back(UInt(16));
        types.push_back(UInt(32));
        types.push_back(Int(8));
        types.push_back(Int(16));
        types.push_back(Int(32));
    }

    Type random_type(int width) {
        Type T = types[rand()%types.size()];
        if (width > 1) {
            T = T.vector_of(width);
        }
        return T;
    }

    Expr random_leaf(Type T, bool overflow_undef = false, bool imm_only = false) {
        if (T.is_int() && T.bits == 32) {
            overflow_undef = true;
        }
        if (T.is_scalar()) {
            int var = rand() % leafs.size() + 1;
            if (!imm_only && var < leafs.size()) {
                return cast(T, leafs[var]);
            } else {
                if (overflow_undef) {
                    // For Int(32), we don't care about correctness during
                    // overflow, so just use numbers that are unlikely to
                    // overflow.
                    return cast(T, rand()%256 - 128);
                } else {
                    return cast(T, rand() - RAND_MAX/2);
                }
            }
        } else {
            if (rand() % 2 == 0) {
                return Internal::Ramp::make(random_leaf(T.element_of(), overflow_undef),
                                            random_leaf(T.element_of(), overflow_undef),
                                            T.width);
            } else {
                return Internal::Broadcast::make(random_leaf(T.element_of(), overflow_undef), T.width);
            }
        }
    }

    Expr random_condition(Type T, int depth) {
        typedef Expr (*make_bin_op_fn)(Expr, Expr);
        static make_bin_op_fn make_bin_op[] = {
            Internal::EQ::make,
            Internal::NE::make,
            Internal::LT::make,
            Internal::LE::make,
            Internal::GT::make,
            Internal::GE::make,
        };
        const int op_count = sizeof(make_bin_op)/sizeof(make_bin_op[0]);

        Expr a = random_expr(T, depth);
        Expr b = random_expr(T, depth);
        int op = rand()%op_count;
        return make_bin_op[op](a, b);
    }

    Expr random_expr(Type T, int depth, bool overflow_undef = false) {
        typedef Expr (*make_bin_op_fn)(Expr, Expr);
        static make_bin_op_fn make_bin_op[] = {
            Internal::Add::make,
            Internal::Sub::make,
            Internal::Mul::make,
            Internal::Min::make,
            Internal::Max::make,
            Internal::Div::make,
            Internal::Mod::make,
        };

        static make_bin_op_fn make_bool_bin_op[] = {
            Internal::And::make,
            Internal::Or::make,
        };

        if (T.is_int() && T.bits == 32) {
            overflow_undef = true;
        }

        if (depth-- <= 0) {
            return random_leaf(T, overflow_undef);
        }

        const int bin_op_count = sizeof(make_bin_op) / sizeof(make_bin_op[0]);
        const int bool_bin_op_count = sizeof(make_bool_bin_op) / sizeof(make_bool_bin_op[0]);
        const int op_count = bin_op_count + bool_bin_op_count + 5;

        int op = rand() % op_count;
        switch(op) {
        case 0: return random_leaf(T);
        case 1: return Internal::Select::make(random_condition(T, depth),
                                              random_expr(T, depth, overflow_undef),
                                              random_expr(T, depth, overflow_undef));

        case 2:
            if (T.width != 1) {
                return Internal::Broadcast::make(random_expr(T.element_of(), depth, overflow_undef),
                                                 T.width);
            }
            break;
        case 3:
            if (T.width != 1) {
                return Internal::Ramp::make(random_expr(T.element_of(), depth, overflow_undef),
                                            random_expr(T.element_of(), depth, overflow_undef),
                                            T.width);
            }
            break;

        case 4:
            if (T.is_bool()) {
                return Internal::Not::make(random_expr(T, depth));
            }
            break;

        case 5:
            // When generating boolean expressions, maybe throw in a condition on non-bool types.
            if (T.is_bool()) {
                return random_condition(T, depth);
            }
            break;

        case 6:
            // Get a random type that isn't T or int32 (int32 can overflow and we don't care about that).
            Type subT;
            do {
                subT = random_type(T.width);
            } while (subT == T || (subT.is_int() && subT.bits == 32));
            return Internal::Cast::make(T, random_expr(subT, depth, overflow_undef));

        default:
            make_bin_op_fn maker;
            if (T.is_bool()) {
                maker = make_bool_bin_op[op%bool_bin_op_count];
            } else {
                maker = make_bin_op[op%bin_op_count];
            }
            Expr a = random_expr(T, depth, overflow_undef);
            Expr b = random_expr(T, depth, overflow_undef);
            return maker(a, b);
        }
        // If we got here, try again.
        return random_expr(T, depth, overflow_undef);
    }
};

}

#endif
