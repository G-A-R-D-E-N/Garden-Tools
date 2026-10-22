#pragma once

#include "fo4/papyrus_source/ast.hpp"

#include <string>
#include <vector>

namespace fo4::psc {

struct FunctionSummary {
  std::string name;
  std::string state;
  bool isEvent{false};
  bool isNative{false};
  std::vector<std::string> calledFunctions;
  std::vector<std::string> propertiesRead;
  std::vector<std::string> propertiesWritten;
  std::vector<std::string> stateTransitions;
};

struct ScriptSummary {
  std::string name;
  std::string extends;
  std::string autoState;
  std::vector<std::string> imports;
  std::vector<std::string> declaredProperties;
  std::vector<std::string> autoProperties;
  std::vector<std::string> states;
  std::vector<std::string> calledFunctions;
  std::vector<std::string> propertiesRead;
  std::vector<std::string> propertiesWritten;
  std::vector<std::string> stateTransitions;
  std::vector<FunctionSummary> functions;
};

ScriptSummary summarize(const Script& script);

const FunctionSummary* find_function(const ScriptSummary& summary, const std::string& name);

}  // namespace fo4::psc
