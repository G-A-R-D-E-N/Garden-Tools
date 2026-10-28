#pragma once

#include "state.hpp"

#include <fo4/avm1/interpreter.hpp>

#include <map>
#include <string>
#include <vector>

namespace fo4::avm1 {

struct Avm1Object {
  bool is_array{false};
  std::vector<std::string> order;
  std::map<std::string, Avm1Data> properties;

  ~Avm1Object();
};

namespace object_ops {

Avm1Data plain_object();
Avm1Data arguments_object(const std::vector<Avm1Data>& args);
Avm1RunStatus new_object(Avm1State& state, std::string& message);
Avm1RunStatus init_array(Avm1State& state, std::string& message);
Avm1RunStatus init_object(Avm1State& state, std::string& message);
Avm1RunStatus get_member(Avm1State& state, std::string& message);
Avm1RunStatus set_member(Avm1State& state, std::string& message);
Avm1RunStatus delete_member(Avm1State& state, std::string& message);
Avm1RunStatus delete_variable(Avm1State& state, std::string& message);

}  // namespace object_ops

}  // namespace fo4::avm1
