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

std::string serialize(Schedule s, std::string name, 
      std::map<std::string, std::string> additional= std::map<std::string, std::string>() );

inline std::string serialize(Schedule s,
      std::map<std::string, std::string> additional= std::map<std::string, std::string>() ){
  return serialize(s, "func", additional);
}

std::string serialize(IntrusivePtr<ScheduleContents> contents, 
                      std::map<std::string, std::string> additional= std::map<std::string, std::string>() );

} //end Internal namespace
//This is purpsofully in the Halide namespace but not in the Halide::Internal one.
EXPORT void serialize_schedule_for_opentuner(Func func, std::string filename);

}
#endif
