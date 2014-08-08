// TODO: refactor in terms of find_direct_calls/find_transitive_calls
//       Challenge: this dump requires distinguishing calls in each update from those in the pure definition (I think?)

// TODO: docs
// TODO: calls in reduction index terms/bounds

// NOTE: this should be built with -fno-rtti to be sure it links successfully
// with the corresponding types (IRVisitor, etc.) in libHalide.a as it is
// usually compiled.

#include "CallMap.h"

#include <map>
#include <cstdio>

#include "IR.h"
#include "IRVisitor.h"

using std::map;
using std::string;

namespace Halide {
namespace Internal {

/* Find all the internal halide calls in an expr */
class FindAllCalls : public IRVisitor {
private:
    bool recursive;
public:
    FindAllCalls(bool recurse = false) : recursive(recurse) {}

    map<string, Function> calls;

    typedef map<string, Function>::iterator iterator;

    using IRVisitor::visit;

    void include_function(Function f) {
        iterator iter = calls.find(f.name());
        if (iter == calls.end()) {
            calls[f.name()] = f;
            if (recursive) {
                // recursively add everything called in the definition of f
                for (size_t i = 0; i < f.values().size(); i++) {
                    f.values()[i].accept(this);
                }
                // recursively add everything called in the definition of f's update step
                for (size_t i = 0; i < f.reductions().size(); i++) {
                    for (size_t j = 0; j < f.reductions()[i].values.size(); j++) {
                        f.reductions()[i].values[j].accept(this);
                    }
                }
            }
        } else {
            assert(iter->second.same_as(f) &&
                   "Can't compile a pipeline using multiple functions with same name");
        }
    }

    void visit(const Call *call) {
        IRVisitor::visit(call);
        if (call->call_type == Call::Halide) {
            include_function(call->func);
        }
    }

    void dump_calls(FILE *of) {
        iterator it = calls.begin();
        while (it != calls.end()) {
            fprintf(of, "\"%s\"", it->first.c_str());
            ++it;
            if (it != calls.end()) {
                fprintf(of, ", ");
            }
        }
    }

};

// returns all calls recurivley
map<std::string, Function> find_calls(Func root, bool recurse) {
    const Function f = root.function();
    FindAllCalls all_calls(recurse);
    for (size_t i = 0; i < f.values().size(); i++) {
        f.values()[i].accept(&all_calls);
    }
    return all_calls.calls;
}

map<std::string, Function> find_update_calls(Func root) {
    FindAllCalls update_calls(false);
    const Function f = root.function();
    for (size_t i = 0; i < f.reductions().size(); i++) {
      for (size_t j = 0; j < f.reductions()[i].values.size(); j++) {
        f.reductions()[i].values[j].accept(&update_calls);
      }
    }
   return update_calls.calls;
}



} // end of Halide::Internal namespace
}
