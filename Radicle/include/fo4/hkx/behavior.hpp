#pragma once

#include "fo4/hkx/packfile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: a node keeps the packfile object index, so a caller can jump from the graph to the typed object view.
struct HkxBehaviorNode {
  std::size_t object{0};
  std::string class_name;
  std::uint32_t offset{0};
  std::size_t size{0};
  bool layout_known{false};
  std::size_t references_out{0};
  std::size_t references_in{0};
};

// Constraint: a link exists only between two behavior objects, because a link to anything else is not a graph edge.
struct HkxBehaviorLink {
  std::size_t from{0};
  std::size_t to{0};
  std::string member;
};

struct HkxBehaviorGraph {
  std::size_t object_count{0};
  std::size_t matched{0};
  std::vector<std::string> classes;
  std::vector<HkxBehaviorNode> nodes;
  std::vector<HkxBehaviorLink> links;
  std::vector<std::string> diagnostics;
};

// Requires every object class name to start with hkb for a behavior object; other classes stay in the packfile view.
HkxStatus inspect_behavior(const HkxPackfile& packfile, std::size_t limit, HkxBehaviorGraph& out);

}  // namespace fo4::hkx
