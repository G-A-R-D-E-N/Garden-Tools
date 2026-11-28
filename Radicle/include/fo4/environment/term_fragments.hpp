#pragma once

#include "fo4/environment/world.hpp"
#include "fo4/schema/vmad.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::env {

enum class TermFragmentState : std::uint8_t { no_vmad = 0, no_section = 1, unreadable = 2, present = 3 };

// Invariant: index is the stored 32-bit Fragment Index, returned untouched; rows keep stored order, which is not sorted.
struct TermFragmentRow {
  std::uint32_t index{0};
  std::uint8_t unknown{0};
  std::string script, function;
};

struct TermFragments {
  TermFragmentState state{TermFragmentState::no_vmad};
  std::string failure;
  std::int32_t version{0};
  VmadScript script;
  std::vector<TermFragmentRow> rows;
};

TermFragments decode_term_fragments(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
