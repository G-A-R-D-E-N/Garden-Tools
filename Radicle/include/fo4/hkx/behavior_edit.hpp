#pragma once

#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/behavior_validate.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: RemoveEvent and RemoveVariable are always refused; they are listed so a caller gets a named refusal and not an unknown kind. Values are never renumbered, new kinds are appended.
enum class HkxBehaviorEditKind : std::uint8_t {
  SetTransition = 1,
  AddTransition = 2,
  RemoveTransition = 3,
  AddEvent = 4,
  RenameEvent = 5,
  SetEventFlags = 6,
  RemoveEvent = 7,
  AddVariable = 8,
  RemoveVariable = 9,
  SetClipAnimation = 10,
  RenameVariable = 11,
  SetStartState = 12,
  AddState = 13,
  RemoveState = 14,
  SetBinding = 15,
  AddBinding = 16,
  RemoveBinding = 17,
  SetGeneratorField = 18,
  AddBlendChild = 19,
  RemoveBlendChild = 20,
  AddSelectorChild = 21,
  RemoveSelectorChild = 22,
  RemoveSubtree = 23,
};

// Invariant: state is an index in the states table, or -1 for the wildcard run of state_machine; index is the row inside that run (AddTransition inserts before it, so index == run size appends), the event, variable or clip index; transition supplies every field of Set and Add except its condition, which is kept on Set and empty on Add. AddVariable takes the variable type in flags (0 to 4) and the name in name; value is a state id (SetStartState, AddState) and node the generator node index of AddState; RemoveState names the state in state, an index in the states table. SetBinding and RemoveBinding name a row by index in the bindings table and SetBinding takes the variable in value; AddBinding names the owner by kind in flags (an HkxGraphOwnerKind value) and index, the member path in name and the variable in value.
// Invariant: SetGeneratorField names the owner by kind in flags (Blender, BlendChild or Clip of HkxGraphOwnerKind) and the table index in index, the member in name; a real member takes real (NaN means not given) and an integer member takes value.
// Invariant: AddBlendChild names the blender by index in the blenders table in index, the generator node in node and the weight in real, and appends the child last; RemoveBlendChild names the child by index in the blend children table; AddSelectorChild names the selector node in node and the generator node in value, and appends; RemoveSelectorChild names the child by the index of its Selected edge in the edges table; RemoveSubtree names the generator node in node. The sync master and selected indexes are renumbered only by a removal before them and a removal of the sync master or selected child is refused.
struct HkxBehaviorEdit {
  HkxBehaviorEditKind kind{HkxBehaviorEditKind::SetTransition};
  std::size_t state_machine{0};
  std::int32_t state{-1};
  std::size_t index{0};
  HkxGraphTransition transition;
  std::string name;
  std::uint32_t flags{0};
  std::int32_t value{0};
  std::int32_t node{-1};
  float real{std::numeric_limits<float>::quiet_NaN()};
};

// Invariant: ok is false whenever refusal is set, and refused_edit is the position in the edit list of the edit that was refused; before and after hold the validation of the source and of the result.
struct HkxBehaviorEditReport {
  bool ok{false};
  std::string refusal;
  std::size_t refused_edit{0};
  std::size_t applied{0};
  HkxGraphValidation before;
  HkxGraphValidation after;
};

// Requires a packfile read_behavior_model accepts. Constraint: source is never changed and out is assigned only when ok; edits apply in order to a private copy, each addressing the graph as the previous edit left it, and the result is refused when any validation code has more errors than in the source.
HkxBehaviorEditReport apply_behavior_edits(std::span<const std::byte> source, std::span<const HkxBehaviorEdit> edits,
                                           std::vector<std::byte>& out);

}  // namespace fo4::hkx
