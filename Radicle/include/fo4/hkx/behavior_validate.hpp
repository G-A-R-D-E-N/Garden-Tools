#pragma once

#include "fo4/hkx/behavior_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class HkxGraphIssueSeverity : std::uint8_t { Error = 0, Warning = 1 };

// Invariant: table names the model table index points into ("events", "states", "transitions", ...) and is empty for a graph-level issue; "objects" indexes the packfile's object list and "pointers" is a data-section offset, neither a model table; code is a stable dotted name a consumer may switch on.
struct HkxGraphIssue {
  HkxGraphIssueSeverity severity{HkxGraphIssueSeverity::Error};
  std::string code;
  std::string table;
  std::size_t index{0};
  std::string message;
};

struct HkxGraphValidation {
  std::vector<HkxGraphIssue> issues;
  std::size_t errors{0};
  std::size_t warnings{0};
};

// Requires a model from read_behavior_model; an Error is a reference or table fact the engine cannot resolve, a Warning one that may be intended.
HkxGraphValidation validate_behavior_model(const HkxGraphModel& model);

}  // namespace fo4::hkx
