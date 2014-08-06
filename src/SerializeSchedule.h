#ifndef SERIALIZESCHEDULE_H
#define SERIALIZESCHEDULE_H

#include "IR.h"
#include "Schedule.h"
#include "Reduction.h"
#include "Func.h"

namespace Halide {
namespace Internal {


std::string serialize(Expr value);

std::string pair(std::string name, std::string value, bool more=false);

std::string pair(std::string name, bool value, bool more=false);

std::string pair(std::string name, int value, bool more=false);

std::string pair(std::string name, Expr value, bool more=false);

std::string stringify(std::string value, bool more=false);

std::string serialize(const ReductionDomain rd);

std::string serialize(LoopLevel b);

std::string serialize(Specialization s);

std::string serialize(Bound b);

std::string serialize(Dim d);

std::string serialize(Split s);

std::string serialize(std::vector<Split> splits);

std::string serialize(std::string name, std::vector<std::string> v);

std::string serialize(std::vector<Bound> bounds);

std::string serialize(std::vector<Specialization> specs);

std::string serialize(std::vector<Dim> dims);

std::string serialize(Schedule s, std::string name);

inline std::string serialize(Schedule s) {
  return serialize(s, "func");
}

std::string serialize(IntrusivePtr<ScheduleContents> contents ); 

}
//This is purpsofully in the Halide namespace but not in the Halide::Internal one.
EXPORT void serialize_schedule(Func func, std::string filename);

}
#endif
