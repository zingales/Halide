#ifndef CALLMAP_H
#define CALLMAP_H

//This class based on https://github.com/halide/dump-call-graph

#include "Func.h"
#include "Function.h"

namespace Halide{
namespace Internal{

EXPORT std::map<std::string, Function> find_all_calls(Func root);

}
}

#endif /* end of include guard: DUMPCALLGRAPH_H */
