#include "SerializeSchedule.h"

#include "CallMap.h"

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>



namespace Halide {
namespace Internal {

std::string serialize(Expr value) {
  std::stringstream ss;
  ss << value;
  return "\""+ss.str()+"\"";
}

std::string pair(std::string name, std::string value, bool more) {
  return "\""+name+"\" : \"" + value +"\"" + (more ? ", " : "");
}

std::string pair(std::string name, bool value, bool more) {
  return "\""+name+"\" : " + (value ? "true": "false") + (more ? ", " : "");
}

std::string pair(std::string name, int value, bool more) {
  //this is more portable than to_string
  std::stringstream ss;
  ss<< value;
  return "\""+name+"\" : " + ss.str() + (more ? ", " : "");
}

std::string pair(std::string name, Expr value, bool more) {
  return "\""+name+"\" : " + serialize(value) + (more ? ", " : "");
}

std::string stringify(std::string value, bool more) {
  return "\"" + value + "\"" + (more ? ", " : "");
}

std::string serialize(const ReductionDomain rd) {
  if (rd.defined()) {
    return serialize(rd.domain());
  } else {
    return stringify("ReductionVariables") + " : []";
  }
}

std::string serialize(LoopLevel b) {
  std::string json = "{";
  json += pair("type", std::string("LoopLevel"), true);
  json += pair("func", b.func, true);
  json += pair("var", b.var, false);
  json += "}";
  return json;
}

std::string serialize(Specialization s) {
  std::string json = "{";
  json += pair("type", std::string("Specialization"), true);
  json += pair("condition", s.condition, true);
  json += stringify("ScheduleContents") +" : ";
  json += serialize(s.schedule);
  json += "}";
  return json;
}

std::string serialize(Bound b) {
  std::string json = "{";
  json += pair("type", std::string("Bound"), true);
  json += pair("var", b.var, true);
  json += pair("min", b.min, true);
  json += pair("extent", b.extent, false);
  json += "}";
  return json;
}

std::string serialize(Dim d) {
  std::string json = "{";
  json += pair("type", std::string("Dim"), true);
  json += pair("var", d.var, true);
  json += pair("ForType", d.for_type, true);
  json += pair("pure", d.pure, false);
  json += "}";
  return json;
}

std::string serialize(Split s) {
  std::string json = "{";
  json += pair("type", std::string("Split"), true);
  json += pair("old_var", s.old_var, true);
  json += pair("outer", s.outer, true);
  json += pair("inner", s.inner, true);
  json += pair("exact", s.exact, true);
  json += pair("split_type", s.split_type, true);
  json += pair("factor", s.factor, false);
  json += "}";
  return json;
}

std::string serialize(ReductionVariable rvar) {
  std::string json = "{";
  json += pair("var", rvar.var, true);
  json += pair("min", rvar.min, true);
  json += pair("extent", rvar.extent, false);
  json += "}";
  return json;
}

std::string serialize(std::vector<ReductionVariable> rvars) {
  std::string json = stringify("ReductionVariables") + " : [";
  if( rvars.size() >0) {
    json += "\n";
    std::vector<ReductionVariable>::iterator it_split, last_split;
    for (it_split = rvars.begin(), last_split = --rvars.end() ; it_split != last_split; ++it_split) {
      json += "\t";
      json += serialize(*it_split);
      json += ",\n";
    }
    json += "\t";
    json += serialize(*last_split);
    json += "\n";
  }
  json += "]";
  return json;
}

std::string serialize(std::vector<Split> splits) {
  std::string json = "";
  json += stringify("splits") + " : [";
  if( splits.size() >0) {
    json += "\n";
    std::vector<Split>::iterator it_split, last_split;
    for (it_split = splits.begin(), last_split = --splits.end() ; it_split != last_split; ++it_split) {
      json += "\t";
      json += serialize(*it_split);
      json += ",\n";
    }
    json += "\t";
    json += serialize(*last_split);
    json += "\n";
  }
  json += "]";
  return json;
}

std::string serialize(std::string name, std::vector<std::string> v) {
  std::string json = "";
  json += stringify(name) + " : [";
  if( v.size() >0) {
    //json += "\n";
    std::vector<std::string>::iterator it_s_dims, last_s_dims;
    for (it_s_dims = v.begin(), last_s_dims =--v.end() ; it_s_dims != last_s_dims; ++it_s_dims) {
      //json += "\t";
      json+= stringify(*it_s_dims, true);  
      //json += "\n";
    }
    //json += "\t";
    json+= stringify(*(last_s_dims), false);
    //json += "\n";
  }
  json += "]";
  return json;
}

std::string serialize(std::vector<Bound> bounds) {
  std::string json = "";
  json += stringify("bounds") + " : [";
  if( bounds.size() >0) {
    json += "\n";
    std::vector<Bound>::iterator it_bounds, last_bounds;
    for (it_bounds = bounds.begin(), last_bounds =--bounds.end() ; it_bounds!= last_bounds; ++it_bounds) {
      json += "\t";
      json += serialize(*it_bounds);
      json += ",\n";
    }
    json += "\t";
    json+= serialize(*last_bounds);
    json += "\n";
  }
  json += "]";
  return json;
}

std::string serialize(std::vector<Specialization> specs) {
  std::string json;
  json += stringify("specializations") + " : [";
  if( specs.size() >0) {
    json += "\n";
    std::vector<Specialization>::iterator it_spec, last_spec;
    for (it_spec= specs.begin(), last_spec=--specs.end() ; it_spec!= last_spec; ++it_spec) {
      json += "\t";
      json += serialize(*it_spec);
      json += ",\n";
    }
    json += "\t";
    json+= serialize(*last_spec);
    json += "\n";
  }
  json += "]";
  return json;
}

std::string serialize(std::vector<Dim> dims) {
  std::string json;
  json += stringify("dims") + " : [";
  if( dims.size() >0) {
    json += "\n";
    std::vector<Dim>::iterator it_dims, last_dims;
    for (it_dims = dims.begin(), last_dims =--dims.end() ; it_dims != last_dims; ++it_dims) {
      json += "\t";
      json += serialize(*it_dims);
      json += ",\n";
    }
    json += "\t";
    json += serialize(*last_dims);
    json += "\n";
  }
  json += "]";
  return json;
}

std::string serialize(Schedule s, std::string name) {

  std::string json = "{ ";

  //no comments allowed must add function name
  json += pair("name", name, true) +"\n";
  //race condition
  json += pair(std::string("allow_race_conditions"), s.allow_race_conditions(), true) + "\n";
 
  //splits
  json += serialize(s.splits()) + ",\n";
 
  //storage dims
  json += serialize("storage_dims", s.storage_dims()) + ",\n";
 
  //reduction domain
  json += serialize(s.reduction_domain());
  json += ",\n";

  //bound
  json += serialize(s.bounds()) + ",\n";

  ////specialization
  json += serialize(s.specializations()) + ",\n"; 

  ////loop level
  //store level
  json += stringify("store_level") + " : ";
  json += serialize(s.store_level());
  json += ",\n";
  ////compute level
  json += stringify("compute_level") + " : ";
  json += serialize(s.compute_level());
  json += ",\n";
  //
  ////dims
  json += serialize(s.dims()) +",\n";

  //touched
  json += pair("touched", s.touched(), false);

  json += "\n}";
  return json;
}

std::string serialize(IntrusivePtr<ScheduleContents> contents ) {
  // It would make more sense for the schedule to use call the ScheduleContents serialize method
  // however, you can't get the ScheduleContents from a schedule so we just propmote 
  // ScheduleContents to schedule. 
  return serialize(Schedule(contents));
}

}

void serialize_schedule(Func func, std::string filename, bool recurse) {
  //"/afs/csail.mit.edu/u/z/zingales/saman/opentuner/examples/halide/trial.json",
  std::ofstream myfile; 
  myfile.open(filename.c_str(), std::ios::app);

  // Comments are not legal json, we will eventually want to add it to the schedule file.
  // myfile << "// func name: " << func.name()<< "\n";
  myfile << "[\n";
  myfile<< Internal::serialize(func.function().schedule(), func.name());
  if(recurse) {
    std::map<std::string, Internal::Function> calls = Internal::find_all_calls(func);
    typedef std::map<std::string, Internal::Function>::iterator it_type;
    for(it_type it = calls.begin(); it != calls.end(); it++) {
      myfile << ",\n";
      myfile << Internal::serialize(it->second.schedule(), it->first);
    }
  }
  myfile << "]\n\n";
  myfile.close();

}

}
