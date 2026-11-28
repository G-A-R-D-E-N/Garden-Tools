#pragma once

#include "fo4/hkx/packfile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: values follow the hkbVariableInfo type enum, so an unknown file value is kept as read and never remapped.
enum class HkxVariableType : std::int8_t {
  Bool = 0,
  Int8 = 1,
  Int16 = 2,
  Int32 = 3,
  Real = 4,
  Pointer = 5,
  Vector3 = 6,
  Vector4 = 7,
  Quaternion = 8,
};

// Invariant: values 4 and up name a generator class that has no table of its own, so kind_index is 0 for them; the edges table holds their children.
enum class HkxGraphNodeKind : std::uint8_t {
  Other = 0,
  StateMachine = 1,
  Blender = 2,
  Clip = 3,
  Selector = 4,
  Layer = 5,
  PoseMatching = 6,
  ModifierGenerator = 7,
  StateTagging = 8,
  CyclicBlendTransition = 9,
  OffsetAnimation = 10,
  BehaviorGraphSwap = 11,
  BoneSwitch = 12,
  BehaviorReference = 13,
  GamebryoSequence = 14,
  DynamicAnimationTagging = 15,
  ReferencePose = 16,
};

// Invariant: Wrapped is the one generator a wrapper plays by default; Selected, Layer, BoneSwitchChild and BlendChild are list entries and keep their list index in slot.
enum class HkxGraphEdgeRole : std::uint8_t {
  Wrapped = 1,
  OffsetClip = 2,
  Selected = 3,
  Layer = 4,
  BoneSwitchChild = 5,
  BlendChild = 6,
};

enum class HkxGraphConditionKind : std::uint8_t { None = 0, Expression = 1, String = 2, Other = 3 };

enum class HkxGraphOwnerKind : std::uint8_t { StateMachine = 1, State = 2, Blender = 3, BlendChild = 4, Clip = 5 };

struct HkxGraphEvent {
  std::string name;
  std::uint32_t flags{0};
};

// Invariant: word is the file's 32-bit initial value; int_value and real_value are two views of it, and vector holds the quad it indexes for the vector and quaternion types.
struct HkxGraphVariable {
  std::string name;
  std::int8_t type{0};
  std::int16_t role{0};
  std::uint16_t role_flags{0};
  std::uint32_t word{0};
  std::int32_t int_value{0};
  float real_value{0.0f};
  std::array<float, 4> vector{};
  bool has_vector{false};
};

// Invariant: object indexes the packfile's object list; kind_index indexes the matching table below for a state machine, blender or clip, and is 0 for every other kind.
struct HkxGraphNode {
  std::size_t object{0};
  std::string class_name;
  std::string name;
  HkxGraphNodeKind kind{HkxGraphNodeKind::Other};
  std::size_t kind_index{0};
};

struct HkxGraphTimeInterval {
  std::int32_t enter_event_id{-1};
  std::int32_t exit_event_id{-1};
  float enter_time{0.0f};
  float exit_time{0.0f};
};

// Invariant: state is the owning state's index in the states table, or -1 for a state machine wildcard; event_id and the state ids are raw file values.
struct HkxGraphTransition {
  std::size_t state_machine{0};
  std::int32_t state{-1};
  std::int32_t event_id{-1};
  std::int32_t to_state_id{0};
  std::int32_t from_nested_state_id{0};
  std::int32_t to_nested_state_id{0};
  std::int16_t priority{0};
  std::uint16_t flags{0};
  HkxGraphTimeInterval trigger_interval;
  HkxGraphTimeInterval initiate_interval;
  std::int32_t effect_node{-1};
  HkxGraphConditionKind condition_kind{HkxGraphConditionKind::None};
  std::string condition;
  std::string condition_class;
  // Invariant: offset is the row's byte offset in the data section and array_object the packfile object holding the row array; locators for an editor, not file values.
  std::size_t offset{0};
  std::size_t array_object{0};
};

// Invariant: a state's transitions are the contiguous run first_transition..first_transition + transition_count of the transitions table.
struct HkxGraphState {
  std::size_t state_machine{0};
  std::string name;
  std::int32_t state_id{0};
  float probability{1.0f};
  bool enable{true};
  std::int32_t generator_node{-1};
  std::size_t first_transition{0};
  std::size_t transition_count{0};
  // Invariant: object is the packfile object index of the state info, a locator for an editor.
  std::size_t object{0};
};

// Invariant: states and wildcards are contiguous runs of their tables; the *_event_id members and sync_variable_index are raw file values, so a consumer range-checks them against the events and variables tables.
struct HkxGraphStateMachine {
  std::size_t node{0};
  std::int32_t start_state_id{0};
  std::int32_t change_event_id{-1};
  std::int32_t return_to_previous_event_id{-1};
  std::int32_t random_transition_event_id{-1};
  std::int32_t next_higher_event_id{-1};
  std::int32_t next_lower_event_id{-1};
  std::int32_t sync_variable_index{-1};
  std::int8_t start_state_mode{0};
  std::int8_t self_transition_mode{0};
  std::size_t first_state{0};
  std::size_t state_count{0};
  std::size_t first_wildcard{0};
  std::size_t wildcard_count{0};
  // Invariant: set when startStateIdSelector points at an object, so the file does not choose the start state alone.
  bool has_start_state_selector{false};
};

// Invariant: weight_variable is the variable index bound to the child's weight, -1 when the stored weight is the value; contribution is the static share and is only meaningful when the owning blender is resolved.
struct HkxGraphBlendChild {
  std::size_t blender{0};
  std::int32_t generator_node{-1};
  float weight{0.0f};
  float world_from_model_weight{0.0f};
  bool has_bone_weights{false};
  std::int32_t weight_variable{-1};
  float contribution{0.0f};
  // Invariant: object is the packfile object index of the blend child info, a locator for an editor.
  std::size_t object{0};
};

// Invariant: resolved means every contribution is fixed by the file: no variable drives the blend parameter or a weight, no bone weights, no flag beyond parametric and no subtractive last child.
struct HkxGraphBlender {
  std::size_t node{0};
  float blend_parameter{0.0f};
  float min_cyclic_blend_parameter{0.0f};
  float max_cyclic_blend_parameter{0.0f};
  std::int16_t index_of_sync_master_child{-1};
  std::int16_t flags{0};
  bool parametric{false};
  bool subtract_last_child{false};
  std::int32_t blend_parameter_variable{-1};
  bool resolved{false};
  std::size_t first_child{0};
  std::size_t child_count{0};
};

// Invariant: node is the hkbBlendingTransitionEffect's index in the nodes table, so a transition's effect_node matches it; flags, end_mode and blend_curve are the file's raw values.
struct HkxGraphEffect {
  std::size_t node{0};
  float duration{0.0f};
  float start_fraction{0.0f};
  std::uint16_t flags{0};
  std::int8_t end_mode{0};
  std::int8_t blend_curve{0};
};

struct HkxGraphClipTrigger {
  float local_time{0.0f};
  std::int32_t event_id{-1};
  bool relative_to_end_of_clip{false};
  bool acyclic{false};
  bool is_annotation{false};
};

struct HkxGraphClip {
  std::size_t node{0};
  std::string animation;
  std::string animation_bundle;
  std::int8_t mode{0};
  std::uint8_t flags{0};
  float start_time{0.0f};
  float playback_speed{1.0f};
  float crop_start{0.0f};
  float crop_end{0.0f};
  float enforced_duration{0.0f};
  std::int16_t animation_binding_index{-1};
  std::vector<HkxGraphClipTrigger> triggers;
};

// Invariant: generator_count is the raw length of the generators array, null slots included, and selected_generator_index is the file's raw value.
struct HkxGraphSelector {
  std::size_t node{0};
  std::int16_t selected_generator_index{0};
  std::size_t generator_count{0};
  std::size_t null_generator_count{0};
};

// Invariant: owner_index indexes the table named by owner_kind; variable_index is the file's variableIndex, -1 for none.
struct HkxGraphBinding {
  HkxGraphOwnerKind owner_kind{HkxGraphOwnerKind::StateMachine};
  std::size_t owner_index{0};
  std::string member_path;
  std::int32_t variable_index{-1};
  std::int8_t bit_index{-1};
  std::int8_t binding_type{0};
};

// Invariant: parent and child are valid indexes in the nodes table; the edges of one parent are contiguous, ordered by slot, and parents follow node order; weight is the stored weight for Layer and BlendChild and 0 for the other roles; has_bone_weights is set when the list entry points at a bone weight array.
struct HkxGraphEdge {
  std::size_t parent{0};
  std::int32_t child{-1};
  HkxGraphEdgeRole role{HkxGraphEdgeRole::Wrapped};
  std::int32_t slot{0};
  float weight{0.0f};
  bool has_bone_weights{false};
};

// Invariant: offset is the data-section offset of an object pointer that did not resolve; wrong_class is false for a target that is not an object start and true for an object of another class; diagnostic indexes the diagnostics line that carries the text, or k_no_diagnostic when the line was cut by the limit.
struct HkxGraphPointerFault {
  static constexpr std::size_t k_no_diagnostic = static_cast<std::size_t>(-1);
  std::size_t offset{0};
  bool wrong_class{false};
  std::size_t diagnostic{k_no_diagnostic};
  std::string message;
};

// Invariant: an event or variable index read from an object the tables above do not decode; object indexes the packfile's object list and member is the "Class.path" of the slot; negative values are never kept.
struct HkxGraphObjectSlot {
  std::size_t object{0};
  std::string member;
  std::int32_t value{0};
};

// Invariant: node, generator and effect indexes and the table runs are validated (valid or -1); event ids, variable indexes and state ids are raw file values that may point outside their tables.
struct HkxGraphModel {
  std::string graph_name;
  std::int32_t root_node{-1};
  std::vector<HkxGraphEvent> events;
  std::vector<HkxGraphVariable> variables;
  std::vector<HkxGraphNode> nodes;
  std::vector<HkxGraphStateMachine> state_machines;
  std::vector<HkxGraphState> states;
  std::vector<HkxGraphTransition> transitions;
  std::vector<HkxGraphBlender> blenders;
  std::vector<HkxGraphBlendChild> blend_children;
  std::vector<HkxGraphClip> clips;
  std::vector<HkxGraphSelector> selectors;
  std::vector<HkxGraphEffect> effects;
  std::vector<HkxGraphBinding> bindings;
  std::vector<HkxGraphEdge> edges;
  std::vector<std::string> diagnostics;
  // Invariant: raw lengths of the parallel tables as stored, because events and variables are padded to the longer of names and infos; variable_value_count is k_no_value_set when the file has no initial value set.
  static constexpr std::size_t k_no_value_set = static_cast<std::size_t>(-1);
  std::size_t event_name_count{0};
  std::size_t event_info_count{0};
  std::size_t variable_name_count{0};
  std::size_t variable_info_count{0};
  std::size_t variable_value_count{k_no_value_set};
  std::size_t variable_bounds_count{0};
  std::vector<HkxGraphPointerFault> pointer_faults;
  // Invariant: variable slots exclude the binding sets already decoded into the bindings table.
  std::vector<HkxGraphObjectSlot> object_event_slots;
  std::vector<HkxGraphObjectSlot> object_variable_slots;
};

// Requires an x64 packfile whose behaviour classes match the generated layout signatures; a file with no hkbBehaviorGraph comes back as MissingObject.
HkxStatus read_behavior_model(const HkxPackfile& packfile, HkxGraphModel& out);

// Constraint: non-parametric shares are max(0, weight) over their sum; parametric shares interpolate the two weights around the parameter, clamping to the ends; a non-finite parameter or an empty sum gives zeros.
std::vector<float> blend_contributions(std::span<const float> weights, bool parametric, float parameter);

}  // namespace fo4::hkx
