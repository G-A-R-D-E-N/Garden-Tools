#pragma once

#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fo4::pex_analysis {

struct PexGoToState {
  PexFunctionRef function;
  std::string targetState;
  bool literal{false};
  bool knownState{false};
};

struct PexEventSummary {
  PexFunctionRef function;
  std::vector<std::string> calls;
};

struct PexObjectSummary {
  std::string name;
  std::string parentClassName;
  std::string autoStateName;
  std::vector<PexEventSummary> events;
  std::vector<PexGoToState> transitions;
  std::vector<std::string> referencedProperties;
  std::vector<std::string> writtenVariables;
};

struct PexScriptSummary {
  std::string sourceFileName;
  std::vector<PexObjectSummary> objects;
};

PexScriptSummary summarize_script(const PexCallGraph& graph, std::size_t fileIndex,
                                  const fo4::pex::PexFile& file);

}
