#include "fo4/hkx/behavior_graph.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/symbol_search.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using detail::elements_at;
using detail::Elements;
using detail::in_section;
using detail::local_target;
using detail::value_at;

constexpr std::size_t k_pointer_size = 8;
constexpr std::size_t k_text_limit = 65536;
constexpr std::size_t k_diagnostic_limit = 64;
constexpr std::int16_t k_parametric_flag = 0x10;

// Constraint: a zero weight offset means the list entries carry no weight, and an empty child_class means the list slots are generator pointers themselves.
struct ChildList {
  std::size_t array;
  std::string_view child_class;
  std::size_t generator;
  std::size_t weight;
  std::size_t bones;
  HkxGraphEdgeRole role;
};

struct Layouts {
  bool ok{true};

  std::size_t member(std::string_view class_name, std::string_view name) {
    const HkxClassLayout* layout = find_layout(class_name);
    const auto offset = layout ? find_member_offset(*layout, name) : std::nullopt;
    if (!offset) {
      ok = false;
      return 0;
    }
    return *offset;
  }

  std::size_t size(std::string_view class_name) {
    const HkxClassLayout* layout = find_layout(class_name);
    if (layout == nullptr) {
      ok = false;
      return 0;
    }
    return layout->size;
  }

  std::size_t node_name{member("hkbNode", "name")};
  std::size_t bindable_set{member("hkbBindable", "variableBindingSet")};

  std::size_t graph_root{member("hkbBehaviorGraph", "rootGenerator")};
  std::size_t graph_data{member("hkbBehaviorGraph", "data")};
  std::size_t data_variable_infos{member("hkbBehaviorGraphData", "variableInfos")};
  std::size_t data_event_infos{member("hkbBehaviorGraphData", "eventInfos")};
  std::size_t data_initial_values{member("hkbBehaviorGraphData", "variableInitialValues")};
  std::size_t data_strings{member("hkbBehaviorGraphData", "stringData")};
  std::size_t data_variable_bounds{member("hkbBehaviorGraphData", "variableBounds")};
  std::size_t variable_bounds_size{size("hkbVariableBounds")};
  std::size_t strings_events{member("hkbBehaviorGraphStringData", "eventNames")};
  std::size_t strings_variables{member("hkbBehaviorGraphStringData", "variableNames")};
  std::size_t variable_info_size{size("hkbVariableInfo")};
  std::size_t variable_role{member("hkbVariableInfo", "role")};
  std::size_t variable_type{member("hkbVariableInfo", "type")};
  std::size_t role_role{member("hkbRoleAttribute", "role")};
  std::size_t role_flags{member("hkbRoleAttribute", "flags")};
  std::size_t event_info_size{size("hkbEventInfo")};
  std::size_t event_flags{member("hkbEventInfo", "flags")};
  std::size_t values_words{member("hkbVariableValueSet", "wordVariableValues")};
  std::size_t values_quads{member("hkbVariableValueSet", "quadVariableValues")};

  std::size_t machine_change_event{member("hkbStateMachine", "eventToSendWhenStateOrTransitionChanges")};
  std::size_t machine_start_state{member("hkbStateMachine", "startStateId")};
  std::size_t machine_return_event{member("hkbStateMachine", "returnToPreviousStateEventId")};
  std::size_t machine_random_event{member("hkbStateMachine", "randomTransitionEventId")};
  std::size_t machine_higher_event{member("hkbStateMachine", "transitionToNextHigherStateEventId")};
  std::size_t machine_lower_event{member("hkbStateMachine", "transitionToNextLowerStateEventId")};
  std::size_t machine_sync_variable{member("hkbStateMachine", "syncVariableIndex")};
  std::size_t machine_start_mode{member("hkbStateMachine", "startStateMode")};
  std::size_t machine_self_mode{member("hkbStateMachine", "selfTransitionMode")};
  std::size_t machine_states{member("hkbStateMachine", "states")};
  std::size_t machine_wildcards{member("hkbStateMachine", "wildcardTransitions")};
  std::size_t machine_start_selector{member("hkbStateMachine", "startStateIdSelector")};

  std::size_t state_size{size("hkbStateMachineStateInfo")};
  std::size_t state_transitions{member("hkbStateMachineStateInfo", "transitions")};
  std::size_t state_generator{member("hkbStateMachineStateInfo", "generator")};
  std::size_t state_name{member("hkbStateMachineStateInfo", "name")};
  std::size_t state_id{member("hkbStateMachineStateInfo", "stateId")};
  std::size_t state_probability{member("hkbStateMachineStateInfo", "probability")};
  std::size_t state_enable{member("hkbStateMachineStateInfo", "enable")};

  std::size_t transition_array{member("hkbStateMachineTransitionInfoArray", "transitions")};
  std::size_t transition_size{size("hkbStateMachineTransitionInfo")};
  std::size_t transition_trigger{member("hkbStateMachineTransitionInfo", "triggerInterval")};
  std::size_t transition_initiate{member("hkbStateMachineTransitionInfo", "initiateInterval")};
  std::size_t transition_effect{member("hkbStateMachineTransitionInfo", "transition")};
  std::size_t transition_condition{member("hkbStateMachineTransitionInfo", "condition")};
  std::size_t transition_event{member("hkbStateMachineTransitionInfo", "eventId")};
  std::size_t transition_to{member("hkbStateMachineTransitionInfo", "toStateId")};
  std::size_t transition_from_nested{member("hkbStateMachineTransitionInfo", "fromNestedStateId")};
  std::size_t transition_to_nested{member("hkbStateMachineTransitionInfo", "toNestedStateId")};
  std::size_t transition_priority{member("hkbStateMachineTransitionInfo", "priority")};
  std::size_t transition_flags{member("hkbStateMachineTransitionInfo", "flags")};
  std::size_t interval_enter_event{member("hkbStateMachineTimeInterval", "enterEventId")};
  std::size_t interval_exit_event{member("hkbStateMachineTimeInterval", "exitEventId")};
  std::size_t interval_enter_time{member("hkbStateMachineTimeInterval", "enterTime")};
  std::size_t interval_exit_time{member("hkbStateMachineTimeInterval", "exitTime")};
  std::size_t expression_text{member("hkbExpressionCondition", "expression")};
  std::size_t string_condition_text{member("hkbStringCondition", "conditionString")};

  std::size_t blender_parameter{member("hkbBlenderGenerator", "blendParameter")};
  std::size_t blender_min_cyclic{member("hkbBlenderGenerator", "minCyclicBlendParameter")};
  std::size_t blender_max_cyclic{member("hkbBlenderGenerator", "maxCyclicBlendParameter")};
  std::size_t blender_sync_master{member("hkbBlenderGenerator", "indexOfSyncMasterChild")};
  std::size_t blender_flags{member("hkbBlenderGenerator", "flags")};
  std::size_t blender_subtract_last{member("hkbBlenderGenerator", "subtractLastChild")};
  std::size_t blender_children{member("hkbBlenderGenerator", "children")};
  std::size_t child_size{size("hkbBlenderGeneratorChild")};
  std::size_t child_generator{member("hkbBlenderGeneratorChild", "generator")};
  std::size_t child_bone_weights{member("hkbBlenderGeneratorChild", "boneWeights")};
  std::size_t child_weight{member("hkbBlenderGeneratorChild", "weight")};
  std::size_t child_world_weight{member("hkbBlenderGeneratorChild", "worldFromModelWeight")};

  std::size_t clip_bundle{member("hkbClipGenerator", "animationBundleName")};
  std::size_t clip_animation{member("hkbClipGenerator", "animationName")};
  std::size_t clip_triggers{member("hkbClipGenerator", "triggers")};
  std::size_t clip_crop_start{member("hkbClipGenerator", "cropStartAmountLocalTime")};
  std::size_t clip_crop_end{member("hkbClipGenerator", "cropEndAmountLocalTime")};
  std::size_t clip_start_time{member("hkbClipGenerator", "startTime")};
  std::size_t clip_speed{member("hkbClipGenerator", "playbackSpeed")};
  std::size_t clip_duration{member("hkbClipGenerator", "enforcedDuration")};
  std::size_t clip_mode{member("hkbClipGenerator", "mode")};
  std::size_t clip_flags{member("hkbClipGenerator", "flags")};
  std::size_t clip_binding_index{member("hkbClipGenerator", "animationBindingIndex")};
  std::size_t selector_selected{member("hkbManualSelectorGenerator", "selectedGeneratorIndex")};
  std::size_t effect_duration{member("hkbBlendingTransitionEffect", "duration")};
  std::size_t effect_start_fraction{member("hkbBlendingTransitionEffect", "toGeneratorStartTimeFraction")};
  std::size_t effect_flags{member("hkbBlendingTransitionEffect", "flags")};
  std::size_t effect_end_mode{member("hkbBlendingTransitionEffect", "endMode")};
  std::size_t effect_blend_curve{member("hkbBlendingTransitionEffect", "blendCurve")};
  std::size_t trigger_array{member("hkbClipTriggerArray", "triggers")};
  std::size_t trigger_size{size("hkbClipTrigger")};
  std::size_t trigger_time{member("hkbClipTrigger", "localTime")};
  std::size_t trigger_event{member("hkbClipTrigger", "event")};
  std::size_t trigger_relative{member("hkbClipTrigger", "relativeToEndOfClip")};
  std::size_t trigger_acyclic{member("hkbClipTrigger", "acyclic")};
  std::size_t trigger_annotation{member("hkbClipTrigger", "isAnnotation")};

  std::size_t binding_array{member("hkbVariableBindingSet", "bindings")};
  std::size_t binding_size{size("hkbVariableBindingSetBinding")};
  std::size_t binding_path{member("hkbVariableBindingSetBinding", "memberPath")};
  std::size_t binding_variable{member("hkbVariableBindingSetBinding", "variableIndex")};
  std::size_t binding_bit{member("hkbVariableBindingSetBinding", "bitIndex")};
  std::size_t binding_type{member("hkbVariableBindingSetBinding", "bindingType")};

  std::size_t modifier_child{member("hkbModifierGenerator", "generator")};
  std::size_t tagging_child{member("BSiStateTaggingGenerator", "pDefaultGenerator")};
  std::size_t cyclic_child{member("BSCyclicBlendTransitionGenerator", "pBlenderGenerator")};
  std::size_t offset_child{member("BSOffsetAnimationGenerator", "pDefaultGenerator")};
  std::size_t offset_clip{member("BSOffsetAnimationGenerator", "pOffsetClipGenerator")};
  std::size_t swap_child{member("BSBehaviorGraphSwapGenerator", "pDefaultGenerator")};
  std::size_t switch_child{member("BSBoneSwitchGenerator", "pDefaultGenerator")};
  std::size_t dynamic_child{member("DynamicAnimationTaggingGenerator", "pDefaultGenerator")};
  ChildList selector{member("hkbManualSelectorGenerator", "generators"), {}, 0, 0, 0, HkxGraphEdgeRole::Selected};
  ChildList layers{member("hkbLayerGenerator", "layers"),
                   "hkbLayer",
                   member("hkbLayer", "generator"),
                   member("hkbLayer", "weight"),
                   member("hkbLayer", "boneWeights"),
                   HkxGraphEdgeRole::Layer};
  ChildList bone_switch{member("BSBoneSwitchGenerator", "ChildrenA"),
                        "BSBoneSwitchGeneratorBoneData",
                        member("BSBoneSwitchGeneratorBoneData", "pGenerator"),
                        0,
                        member("BSBoneSwitchGeneratorBoneData", "spBoneWeight"),
                        HkxGraphEdgeRole::BoneSwitchChild};
  ChildList pose{blender_children, "hkbBlenderGeneratorChild", child_generator, child_weight, child_bone_weights,
                 HkxGraphEdgeRole::BlendChild};
};

bool is_node_class(std::string_view name) {
  return name == "hkbStateMachine" || name.ends_with("Generator") || name.ends_with("Effect");
}

HkxGraphNodeKind generic_kind(std::string_view name) {
  struct Row {
    std::string_view name;
    HkxGraphNodeKind kind;
  };
  static constexpr Row rows[] = {
      {"hkbManualSelectorGenerator", HkxGraphNodeKind::Selector},
      {"hkbLayerGenerator", HkxGraphNodeKind::Layer},
      {"hkbPoseMatchingGenerator", HkxGraphNodeKind::PoseMatching},
      {"hkbModifierGenerator", HkxGraphNodeKind::ModifierGenerator},
      {"BSiStateTaggingGenerator", HkxGraphNodeKind::StateTagging},
      {"BSCyclicBlendTransitionGenerator", HkxGraphNodeKind::CyclicBlendTransition},
      {"BSOffsetAnimationGenerator", HkxGraphNodeKind::OffsetAnimation},
      {"BSBehaviorGraphSwapGenerator", HkxGraphNodeKind::BehaviorGraphSwap},
      {"BSBoneSwitchGenerator", HkxGraphNodeKind::BoneSwitch},
      {"hkbBehaviorReferenceGenerator", HkxGraphNodeKind::BehaviorReference},
      {"BGSGamebryoSequenceGenerator", HkxGraphNodeKind::GamebryoSequence},
      {"DynamicAnimationTaggingGenerator", HkxGraphNodeKind::DynamicAnimationTagging},
      {"hkbReferencePoseGenerator", HkxGraphNodeKind::ReferencePose},
  };
  for (const Row& row : rows) {
    if (row.name == name) return row.kind;
  }
  return HkxGraphNodeKind::Other;
}

class Decoder {
 public:
  Decoder(const HkxPackfile& packfile, HkxGraphModel& model, const Layouts& layouts)
      : packfile_(packfile),
        section_(packfile.sections[packfile.data_section]),
        model_(model),
        layouts_(layouts),
        node_of_object_(packfile.objects.size(), -1) {
    for (const HkxRemoteFixup& fixup : section_.global_fixups) {
      if (fixup.section == packfile.data_section && in_section(section_, fixup.destination, 1)) {
        globals_.emplace_back(fixup.source, fixup.destination);
      }
    }
    std::sort(globals_.begin(), globals_.end());
  }

  HkxStatus run(std::size_t graph);

  const std::vector<std::size_t>& decoded_binding_sets() const { return binding_sets_; }

 private:
  std::vector<std::size_t> binding_sets_;
  const HkxPackfile& packfile_;
  const HkxSection& section_;
  HkxGraphModel& model_;
  const Layouts& layouts_;
  std::vector<std::int32_t> node_of_object_;
  std::vector<std::pair<std::size_t, std::size_t>> globals_;
  std::size_t triggers_read_{0};
  std::size_t slots_read_{0};
  std::size_t text_bytes_{0};

  void note(std::string text) {
    if (model_.diagnostics.size() < k_diagnostic_limit) model_.diagnostics.push_back(std::move(text));
  }

  // Constraint: the fault list is cut at the same limit as the diagnostics, so a crafted file cannot grow it without bound.
  void fault(std::size_t field, bool wrong_class, std::string text) {
    if (model_.pointer_faults.size() < k_diagnostic_limit) {
      const std::size_t line = model_.diagnostics.size() < k_diagnostic_limit ? model_.diagnostics.size()
                                                                              : HkxGraphPointerFault::k_no_diagnostic;
      model_.pointer_faults.push_back({field, wrong_class, line, text});
    }
    note(std::move(text));
  }

  template <typename T>
  T at(std::size_t offset) const {
    return value_at<T>(section_.data, offset);
  }

  // Requires the class layout to match the file's signature, so a foreign Havok build is refused instead of misread.
  HkxStatus checked(const HkxObjectView& object, const HkxClassLayout*& layout) const {
    layout = find_layout(object.class_name, object.signature);
    if (layout == nullptr) return HkxStatus::UnsupportedLayout;
    return detail::holds_layout(object, *layout) ? HkxStatus::Ok : HkxStatus::Truncated;
  }

  HkxStatus text(std::size_t field, std::string& out) {
    out.clear();
    if (!in_section(section_, field, k_pointer_size)) return HkxStatus::FixupOutOfRange;
    const auto target = local_target(section_, field);
    if (!target) return HkxStatus::Ok;
    if (*target >= section_.data.size()) return HkxStatus::FixupOutOfRange;
    const std::size_t available = std::min(section_.data.size() - *target, k_text_limit);
    const auto* begin = reinterpret_cast<const char*>(section_.data.data() + *target);
    const void* end = std::memchr(begin, 0, available);
    if (end == nullptr) return HkxStatus::FixupOutOfRange;
    out.assign(begin, static_cast<const char*>(end));
    // Constraint: one pooled string may be pointed at many times, so the decoded bytes in total are capped at 16 times the section.
    text_bytes_ += out.size() + 1;
    return text_bytes_ <= 16 * section_.data.size() ? HkxStatus::Ok : HkxStatus::BadSection;
  }

  // Requires the sorted global index: one probe per pointer, so a crafted file cannot turn each reference into a linear scan.
  std::optional<std::size_t> pointer_to(std::size_t field) const {
    if (const auto local = local_target(section_, field)) return local;
    const auto found = std::lower_bound(globals_.begin(), globals_.end(), field,
                                        [](const auto& entry, std::size_t value) { return entry.first < value; });
    if (found == globals_.end() || found->first != field) return std::nullopt;
    return found->second;
  }

  // Constraint: more rows than the data section could hold as distinct elements means repeated references to one array, so the read stops instead of multiplying them.
  HkxStatus within_budget(std::size_t rows, std::size_t element_size) const {
    return rows < section_.data.size() / std::max<std::size_t>(element_size, 1) ? HkxStatus::Ok : HkxStatus::BadSection;
  }

  std::int32_t object_at(std::size_t offset) const {
    const auto& objects = packfile_.objects;
    const auto after = std::upper_bound(objects.begin(), objects.end(), offset,
                                        [](std::size_t value, const HkxObjectView& object) { return value < object.offset; });
    if (after == objects.begin()) return -1;
    const auto found = after - 1;
    return found->offset == offset ? static_cast<std::int32_t>(found - objects.begin()) : -1;
  }

  // Constraint: a pointer that is null, or that lands off an object start, is -1; the second case is reported.
  HkxStatus object_ref(std::size_t field, std::int32_t& out) {
    out = -1;
    if (!in_section(section_, field, k_pointer_size)) return HkxStatus::FixupOutOfRange;
    const auto target = pointer_to(field);
    if (!target) return HkxStatus::Ok;
    out = object_at(*target);
    if (out < 0) fault(field, false, "a pointer at offset " + std::to_string(field) + " does not land on an object");
    return HkxStatus::Ok;
  }

  HkxStatus node_ref(std::size_t field, std::int32_t& out) {
    std::int32_t object = -1;
    if (const auto status = object_ref(field, object); status != HkxStatus::Ok) return status;
    out = object >= 0 ? node_of_object_[static_cast<std::size_t>(object)] : -1;
    return HkxStatus::Ok;
  }

  // Requires the target's class to be the one named: another class behind the pointer is skipped and reported.
  HkxStatus typed_ref(std::size_t field, std::string_view class_name, std::int32_t& out) {
    if (const auto status = object_ref(field, out); status != HkxStatus::Ok) return status;
    if (out >= 0 && packfile_.objects[static_cast<std::size_t>(out)].class_name != class_name) {
      fault(field, true,
            "a pointer at offset " + std::to_string(field) + " expected " + std::string(class_name) + " and found " +
                std::string(packfile_.objects[static_cast<std::size_t>(out)].class_name));
      out = -1;
    }
    return HkxStatus::Ok;
  }

  HkxStatus interval(std::size_t base, HkxGraphTimeInterval& out) const {
    if (!in_section(section_, base, 16)) return HkxStatus::FixupOutOfRange;
    out.enter_event_id = at<std::int32_t>(base + layouts_.interval_enter_event);
    out.exit_event_id = at<std::int32_t>(base + layouts_.interval_exit_event);
    out.enter_time = at<float>(base + layouts_.interval_enter_time);
    out.exit_time = at<float>(base + layouts_.interval_exit_time);
    return HkxStatus::Ok;
  }

  HkxStatus string_table(std::size_t field, std::vector<std::string>& out);
  HkxStatus read_tables(std::size_t graph_field_data);
  HkxStatus read_nodes();
  HkxStatus read_bindings(const HkxObjectView& owner, HkxGraphOwnerKind kind, std::size_t index, std::size_t& first);
  HkxStatus read_machine(std::size_t object_index);
  HkxStatus read_transitions(std::size_t array_object, std::size_t machine, std::int32_t state);
  HkxStatus read_transition(std::size_t base, std::size_t array_object, std::size_t machine, std::int32_t state);
  HkxStatus read_blender(std::size_t object_index);
  HkxStatus read_clip(std::size_t object_index);
  void read_effect(std::size_t object_index);
  bool usable(const HkxObjectView& object);
  HkxStatus edge(std::size_t parent, std::size_t field, HkxGraphEdgeRole role, std::int32_t slot, float weight,
                 bool bones);
  HkxStatus read_children(std::size_t parent, std::size_t base, const ChildList& list);
  HkxStatus read_edges(std::size_t object_index);
  bool drives(std::size_t first, HkxGraphOwnerKind kind, std::size_t index, std::string_view member) const;
};

HkxStatus Decoder::string_table(std::size_t field, std::vector<std::string>& out) {
  out.clear();
  Elements elements;
  if (const auto status = elements_at(section_, field, k_pointer_size, k_pointer_size, elements);
      status != HkxStatus::Ok) {
    return status;
  }
  out.resize(elements.count);
  for (std::size_t i = 0; i < elements.count; ++i) {
    if (const auto status = text(elements.first + i * k_pointer_size, out[i]); status != HkxStatus::Ok) return status;
  }
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_nodes() {
  for (std::size_t i = 0; i < packfile_.objects.size(); ++i) {
    const HkxObjectView& object = packfile_.objects[i];
    if (!is_node_class(object.class_name)) continue;
    HkxGraphNode node;
    node.object = i;
    node.class_name = std::string(object.class_name);
    node.kind = generic_kind(object.class_name);
    const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
    if (layout != nullptr && object.data.size() >= layouts_.node_name + k_pointer_size) {
      if (const auto status = text(object.offset + layouts_.node_name, node.name); status != HkxStatus::Ok) return status;
    } else {
      note("no layout matches " + node.class_name + ", so its name is not read");
    }
    node_of_object_[i] = static_cast<std::int32_t>(model_.nodes.size());
    model_.nodes.push_back(std::move(node));
  }
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_tables(std::size_t graph_offset) {
  std::int32_t data_object = -1;
  if (const auto status = typed_ref(graph_offset + layouts_.graph_data, "hkbBehaviorGraphData", data_object);
      status != HkxStatus::Ok) {
    return status;
  }
  if (data_object < 0) {
    note("the graph has no hkbBehaviorGraphData, so the variable and event tables are empty");
    return HkxStatus::Ok;
  }
  const HkxObjectView& data = packfile_.objects[static_cast<std::size_t>(data_object)];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(data, layout); status != HkxStatus::Ok) return status;

  std::vector<std::string> event_names;
  std::vector<std::string> variable_names;
  std::int32_t strings_object = -1;
  if (const auto status = typed_ref(data.offset + layouts_.data_strings, "hkbBehaviorGraphStringData", strings_object);
      status != HkxStatus::Ok) {
    return status;
  }
  if (strings_object >= 0) {
    const HkxObjectView& strings = packfile_.objects[static_cast<std::size_t>(strings_object)];
    if (const auto status = checked(strings, layout); status != HkxStatus::Ok) return status;
    if (const auto status = string_table(strings.offset + layouts_.strings_events, event_names); status != HkxStatus::Ok) {
      return status;
    }
    if (const auto status = string_table(strings.offset + layouts_.strings_variables, variable_names);
        status != HkxStatus::Ok) {
      return status;
    }
  }

  Elements event_infos;
  if (const auto status = elements_at(section_, data.offset + layouts_.data_event_infos, k_pointer_size,
                                      layouts_.event_info_size, event_infos);
      status != HkxStatus::Ok) {
    return status;
  }
  model_.event_name_count = event_names.size();
  model_.event_info_count = event_infos.count;
  model_.events.resize(std::max(event_names.size(), event_infos.count));
  for (std::size_t i = 0; i < model_.events.size(); ++i) {
    if (i < event_names.size()) model_.events[i].name = std::move(event_names[i]);
    if (i < event_infos.count) {
      model_.events[i].flags = at<std::uint32_t>(event_infos.first + i * layouts_.event_info_size + layouts_.event_flags);
    }
  }

  Elements variable_infos;
  if (const auto status = elements_at(section_, data.offset + layouts_.data_variable_infos, k_pointer_size,
                                      layouts_.variable_info_size, variable_infos);
      status != HkxStatus::Ok) {
    return status;
  }
  model_.variable_name_count = variable_names.size();
  model_.variable_info_count = variable_infos.count;
  model_.variables.resize(std::max(variable_names.size(), variable_infos.count));
  for (std::size_t i = 0; i < model_.variables.size(); ++i) {
    HkxGraphVariable& variable = model_.variables[i];
    if (i < variable_names.size()) variable.name = std::move(variable_names[i]);
    if (i >= variable_infos.count) continue;
    const std::size_t info = variable_infos.first + i * layouts_.variable_info_size;
    variable.role = at<std::int16_t>(info + layouts_.variable_role + layouts_.role_role);
    variable.role_flags = at<std::uint16_t>(info + layouts_.variable_role + layouts_.role_flags);
    variable.type = at<std::int8_t>(info + layouts_.variable_type);
  }

  Elements bounds;
  if (elements_at(section_, data.offset + layouts_.data_variable_bounds, k_pointer_size, layouts_.variable_bounds_size,
                  bounds) == HkxStatus::Ok) {
    model_.variable_bounds_count = bounds.count;
  } else {
    note("the variable bounds array does not fit the data section, so its length is not checked");
  }

  std::int32_t values_object = -1;
  if (const auto status = typed_ref(data.offset + layouts_.data_initial_values, "hkbVariableValueSet", values_object);
      status != HkxStatus::Ok) {
    return status;
  }
  if (values_object < 0) return HkxStatus::Ok;
  const HkxObjectView& values = packfile_.objects[static_cast<std::size_t>(values_object)];
  if (const auto status = checked(values, layout); status != HkxStatus::Ok) return status;
  Elements words;
  Elements quads;
  if (const auto status = elements_at(section_, values.offset + layouts_.values_words, k_pointer_size, 4, words);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = elements_at(section_, values.offset + layouts_.values_quads, k_pointer_size, 16, quads);
      status != HkxStatus::Ok) {
    return status;
  }
  model_.variable_value_count = words.count;
  for (std::size_t i = 0; i < model_.variables.size() && i < words.count; ++i) {
    HkxGraphVariable& variable = model_.variables[i];
    variable.word = at<std::uint32_t>(words.first + i * 4);
    variable.int_value = static_cast<std::int32_t>(variable.word);
    std::memcpy(&variable.real_value, &variable.word, sizeof variable.real_value);
    const bool vector_type = variable.type == static_cast<std::int8_t>(HkxVariableType::Vector3) ||
                             variable.type == static_cast<std::int8_t>(HkxVariableType::Vector4) ||
                             variable.type == static_cast<std::int8_t>(HkxVariableType::Quaternion);
    if (!vector_type) continue;
    if (variable.word >= quads.count) {
      note("variable " + std::to_string(i) + " indexes a quad value that the file does not hold");
      continue;
    }
    for (std::size_t lane = 0; lane < 4; ++lane) {
      variable.vector[lane] = at<float>(quads.first + variable.word * 16 + lane * 4);
    }
    variable.has_vector = true;
  }
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_bindings(const HkxObjectView& owner, HkxGraphOwnerKind kind, std::size_t index,
                                 std::size_t& first) {
  first = model_.bindings.size();
  std::int32_t set_object = -1;
  if (const auto status = typed_ref(owner.offset + layouts_.bindable_set, "hkbVariableBindingSet", set_object);
      status != HkxStatus::Ok) {
    return status;
  }
  if (set_object < 0) return HkxStatus::Ok;
  const HkxObjectView& set = packfile_.objects[static_cast<std::size_t>(set_object)];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(set, layout); status != HkxStatus::Ok) return status;
  binding_sets_.push_back(static_cast<std::size_t>(set_object));
  Elements rows;
  if (const auto status = elements_at(section_, set.offset + layouts_.binding_array, k_pointer_size,
                                      layouts_.binding_size, rows);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < rows.count; ++i) {
    const std::size_t row = rows.first + i * layouts_.binding_size;
    if (const auto status = within_budget(model_.bindings.size(), layouts_.binding_size); status != HkxStatus::Ok) {
      return status;
    }
    HkxGraphBinding binding;
    binding.owner_kind = kind;
    binding.owner_index = index;
    if (const auto status = text(row + layouts_.binding_path, binding.member_path); status != HkxStatus::Ok) return status;
    binding.variable_index = at<std::int32_t>(row + layouts_.binding_variable);
    binding.bit_index = at<std::int8_t>(row + layouts_.binding_bit);
    binding.binding_type = at<std::int8_t>(row + layouts_.binding_type);
    model_.bindings.push_back(std::move(binding));
  }
  return HkxStatus::Ok;
}

bool Decoder::drives(std::size_t first, HkxGraphOwnerKind kind, std::size_t index, std::string_view member) const {
  for (std::size_t i = first; i < model_.bindings.size(); ++i) {
    const HkxGraphBinding& binding = model_.bindings[i];
    if (binding.owner_kind == kind && binding.owner_index == index && binding.member_path.ends_with(member)) return true;
  }
  return false;
}

HkxStatus Decoder::read_transition(std::size_t base, std::size_t array_object, std::size_t machine,
                                   std::int32_t state) {
  if (const auto status = within_budget(model_.transitions.size(), layouts_.transition_size); status != HkxStatus::Ok) {
    return status;
  }
  HkxGraphTransition transition;
  transition.offset = base;
  transition.array_object = array_object;
  transition.state_machine = machine;
  transition.state = state;
  transition.event_id = at<std::int32_t>(base + layouts_.transition_event);
  transition.to_state_id = at<std::int32_t>(base + layouts_.transition_to);
  transition.from_nested_state_id = at<std::int32_t>(base + layouts_.transition_from_nested);
  transition.to_nested_state_id = at<std::int32_t>(base + layouts_.transition_to_nested);
  transition.priority = at<std::int16_t>(base + layouts_.transition_priority);
  transition.flags = at<std::uint16_t>(base + layouts_.transition_flags);
  if (const auto status = interval(base + layouts_.transition_trigger, transition.trigger_interval);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = interval(base + layouts_.transition_initiate, transition.initiate_interval);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = node_ref(base + layouts_.transition_effect, transition.effect_node); status != HkxStatus::Ok) {
    return status;
  }

  std::int32_t condition = -1;
  if (const auto status = object_ref(base + layouts_.transition_condition, condition); status != HkxStatus::Ok) {
    return status;
  }
  if (condition >= 0) {
    const HkxObjectView& object = packfile_.objects[static_cast<std::size_t>(condition)];
    transition.condition_class = std::string(object.class_name);
    transition.condition_kind = HkxGraphConditionKind::Other;
    std::size_t field = 0;
    if (object.class_name == "hkbExpressionCondition") {
      transition.condition_kind = HkxGraphConditionKind::Expression;
      field = layouts_.expression_text;
    } else if (object.class_name == "hkbStringCondition") {
      transition.condition_kind = HkxGraphConditionKind::String;
      field = layouts_.string_condition_text;
    }
    if (transition.condition_kind != HkxGraphConditionKind::Other) {
      const HkxClassLayout* layout = nullptr;
      if (const auto status = checked(object, layout); status != HkxStatus::Ok) return status;
      if (const auto status = text(object.offset + field, transition.condition); status != HkxStatus::Ok) return status;
    }
  }
  model_.transitions.push_back(std::move(transition));
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_transitions(std::size_t array_object, std::size_t machine, std::int32_t state) {
  const HkxObjectView& object = packfile_.objects[array_object];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(object, layout); status != HkxStatus::Ok) return status;
  Elements rows;
  if (const auto status = elements_at(section_, object.offset + layouts_.transition_array, k_pointer_size,
                                      layouts_.transition_size, rows);
      status != HkxStatus::Ok) {
    return status;
  }
  for (std::size_t i = 0; i < rows.count; ++i) {
    if (const auto status = read_transition(rows.first + i * layouts_.transition_size, array_object, machine, state);
        status != HkxStatus::Ok) {
      return status;
    }
  }
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_machine(std::size_t object_index) {
  const HkxObjectView& object = packfile_.objects[object_index];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(object, layout); status != HkxStatus::Ok) return status;
  const std::size_t base = object.offset;
  const std::size_t machine_index = model_.state_machines.size();
  HkxGraphStateMachine machine;
  machine.node = static_cast<std::size_t>(node_of_object_[object_index]);
  machine.start_state_id = at<std::int32_t>(base + layouts_.machine_start_state);
  machine.change_event_id = at<std::int32_t>(base + layouts_.machine_change_event);
  machine.return_to_previous_event_id = at<std::int32_t>(base + layouts_.machine_return_event);
  machine.random_transition_event_id = at<std::int32_t>(base + layouts_.machine_random_event);
  machine.next_higher_event_id = at<std::int32_t>(base + layouts_.machine_higher_event);
  machine.next_lower_event_id = at<std::int32_t>(base + layouts_.machine_lower_event);
  machine.sync_variable_index = at<std::int32_t>(base + layouts_.machine_sync_variable);
  machine.start_state_mode = at<std::int8_t>(base + layouts_.machine_start_mode);
  machine.self_transition_mode = at<std::int8_t>(base + layouts_.machine_self_mode);
  machine.has_start_state_selector = pointer_to(base + layouts_.machine_start_selector).has_value();
  machine.first_state = model_.states.size();
  model_.nodes[machine.node].kind = HkxGraphNodeKind::StateMachine;
  model_.nodes[machine.node].kind_index = machine_index;
  std::size_t first_binding = 0;
  if (const auto status = read_bindings(object, HkxGraphOwnerKind::StateMachine, machine_index, first_binding);
      status != HkxStatus::Ok) {
    return status;
  }

  Elements states;
  if (const auto status = elements_at(section_, base + layouts_.machine_states, k_pointer_size, k_pointer_size, states);
      status != HkxStatus::Ok) {
    return status;
  }
  slots_read_ += states.count;
  if (const auto status = within_budget(slots_read_, k_pointer_size); status != HkxStatus::Ok) return status;
  for (std::size_t i = 0; i < states.count; ++i) {
    std::int32_t state_object = -1;
    if (const auto status = typed_ref(states.first + i * k_pointer_size, "hkbStateMachineStateInfo", state_object);
        status != HkxStatus::Ok) {
      return status;
    }
    if (state_object < 0) continue;
    if (const auto status = within_budget(model_.states.size(), layouts_.state_size); status != HkxStatus::Ok) return status;
    const HkxObjectView& info = packfile_.objects[static_cast<std::size_t>(state_object)];
    const HkxClassLayout* state_layout = nullptr;
    if (const auto status = checked(info, state_layout); status != HkxStatus::Ok) return status;
    HkxGraphState state;
    state.object = static_cast<std::size_t>(state_object);
    state.state_machine = machine_index;
    state.state_id = at<std::int32_t>(info.offset + layouts_.state_id);
    state.probability = at<float>(info.offset + layouts_.state_probability);
    state.enable = at<std::uint8_t>(info.offset + layouts_.state_enable) != 0;
    if (const auto status = text(info.offset + layouts_.state_name, state.name); status != HkxStatus::Ok) return status;
    if (const auto status = node_ref(info.offset + layouts_.state_generator, state.generator_node);
        status != HkxStatus::Ok) {
      return status;
    }
    const std::size_t state_index = model_.states.size();
    state.first_transition = model_.transitions.size();
    model_.states.push_back(std::move(state));
    std::size_t state_binding = 0;
    if (const auto status = read_bindings(info, HkxGraphOwnerKind::State, state_index, state_binding);
        status != HkxStatus::Ok) {
      return status;
    }
    std::int32_t transitions = -1;
    if (const auto status = typed_ref(info.offset + layouts_.state_transitions, "hkbStateMachineTransitionInfoArray",
                                      transitions);
        status != HkxStatus::Ok) {
      return status;
    }
    if (transitions >= 0) {
      if (const auto status = read_transitions(static_cast<std::size_t>(transitions), machine_index,
                                                static_cast<std::int32_t>(state_index));
          status != HkxStatus::Ok) {
        return status;
      }
    }
    model_.states[state_index].transition_count = model_.transitions.size() - model_.states[state_index].first_transition;
  }
  machine.state_count = model_.states.size() - machine.first_state;

  machine.first_wildcard = model_.transitions.size();
  std::int32_t wildcards = -1;
  if (const auto status = typed_ref(base + layouts_.machine_wildcards, "hkbStateMachineTransitionInfoArray", wildcards);
      status != HkxStatus::Ok) {
    return status;
  }
  if (wildcards >= 0) {
    if (const auto status = read_transitions(static_cast<std::size_t>(wildcards), machine_index, -1);
        status != HkxStatus::Ok) {
      return status;
    }
  }
  machine.wildcard_count = model_.transitions.size() - machine.first_wildcard;
  model_.state_machines.push_back(machine);
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_blender(std::size_t object_index) {
  const HkxObjectView& object = packfile_.objects[object_index];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(object, layout); status != HkxStatus::Ok) return status;
  const std::size_t base = object.offset;
  const std::size_t blender_index = model_.blenders.size();
  HkxGraphBlender blender;
  blender.node = static_cast<std::size_t>(node_of_object_[object_index]);
  blender.blend_parameter = at<float>(base + layouts_.blender_parameter);
  blender.min_cyclic_blend_parameter = at<float>(base + layouts_.blender_min_cyclic);
  blender.max_cyclic_blend_parameter = at<float>(base + layouts_.blender_max_cyclic);
  blender.index_of_sync_master_child = at<std::int16_t>(base + layouts_.blender_sync_master);
  blender.flags = at<std::int16_t>(base + layouts_.blender_flags);
  blender.parametric = (blender.flags & k_parametric_flag) != 0;
  blender.subtract_last_child = at<std::uint8_t>(base + layouts_.blender_subtract_last) != 0;
  blender.first_child = model_.blend_children.size();
  model_.nodes[blender.node].kind = HkxGraphNodeKind::Blender;
  model_.nodes[blender.node].kind_index = blender_index;

  std::size_t first_binding = 0;
  if (const auto status = read_bindings(object, HkxGraphOwnerKind::Blender, blender_index, first_binding);
      status != HkxStatus::Ok) {
    return status;
  }
  const bool parameter_driven = drives(first_binding, HkxGraphOwnerKind::Blender, blender_index, "blendParameter");
  for (std::size_t i = first_binding; i < model_.bindings.size(); ++i) {
    const HkxGraphBinding& binding = model_.bindings[i];
    if (binding.binding_type == 0 && binding.member_path.ends_with("blendParameter")) {
      blender.blend_parameter_variable = binding.variable_index;
    }
  }

  Elements children;
  if (const auto status = elements_at(section_, base + layouts_.blender_children, k_pointer_size, k_pointer_size, children);
      status != HkxStatus::Ok) {
    return status;
  }
  bool supported = (blender.flags & ~k_parametric_flag) == 0 && !blender.subtract_last_child;
  bool child_driven = false;
  std::vector<float> weights;
  slots_read_ += children.count;
  if (const auto status = within_budget(slots_read_, k_pointer_size); status != HkxStatus::Ok) return status;
  for (std::size_t i = 0; i < children.count; ++i) {
    std::int32_t child_object = -1;
    if (const auto status = typed_ref(children.first + i * k_pointer_size, "hkbBlenderGeneratorChild", child_object);
        status != HkxStatus::Ok) {
      return status;
    }
    if (child_object < 0) continue;
    if (const auto status = within_budget(model_.blend_children.size(), layouts_.child_size); status != HkxStatus::Ok) {
      return status;
    }
    const HkxObjectView& info = packfile_.objects[static_cast<std::size_t>(child_object)];
    const HkxClassLayout* child_layout = nullptr;
    if (const auto status = checked(info, child_layout); status != HkxStatus::Ok) return status;
    HkxGraphBlendChild child;
    child.blender = blender_index;
    child.object = static_cast<std::size_t>(child_object);
    child.weight = at<float>(info.offset + layouts_.child_weight);
    child.world_from_model_weight = at<float>(info.offset + layouts_.child_world_weight);
    if (const auto status = node_ref(info.offset + layouts_.child_generator, child.generator_node);
        status != HkxStatus::Ok) {
      return status;
    }
    child.has_bone_weights = pointer_to(info.offset + layouts_.child_bone_weights).has_value();
    supported = supported && !child.has_bone_weights;
    const std::size_t child_index = model_.blend_children.size();
    std::size_t child_binding = 0;
    if (const auto status = read_bindings(info, HkxGraphOwnerKind::BlendChild, child_index, child_binding);
        status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t b = child_binding; b < model_.bindings.size(); ++b) {
      const HkxGraphBinding& binding = model_.bindings[b];
      if (!binding.member_path.ends_with("weight")) continue;
      child_driven = true;
      if (binding.binding_type == 0) child.weight_variable = binding.variable_index;
    }
    weights.push_back(child.weight);
    model_.blend_children.push_back(std::move(child));
  }
  blender.child_count = model_.blend_children.size() - blender.first_child;
  const std::vector<float> shares = blend_contributions(weights, blender.parametric, blender.blend_parameter);
  for (std::size_t i = 0; i < shares.size(); ++i) model_.blend_children[blender.first_child + i].contribution = shares[i];
  blender.resolved = supported && !parameter_driven && !child_driven;
  model_.blenders.push_back(blender);
  return HkxStatus::Ok;
}

void Decoder::read_effect(std::size_t object_index) {
  const HkxObjectView& object = packfile_.objects[object_index];
  const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
  if (layout == nullptr || !detail::holds_layout(object, *layout)) {
    note("no layout matches or holds hkbBlendingTransitionEffect, so its duration is not read");
    return;
  }
  const std::size_t base = object.offset;
  HkxGraphEffect effect;
  effect.node = static_cast<std::size_t>(node_of_object_[object_index]);
  effect.duration = at<float>(base + layouts_.effect_duration);
  effect.start_fraction = at<float>(base + layouts_.effect_start_fraction);
  effect.flags = at<std::uint16_t>(base + layouts_.effect_flags);
  effect.end_mode = at<std::int8_t>(base + layouts_.effect_end_mode);
  effect.blend_curve = at<std::int8_t>(base + layouts_.effect_blend_curve);
  model_.effects.push_back(effect);
}

HkxStatus Decoder::read_clip(std::size_t object_index) {
  const HkxObjectView& object = packfile_.objects[object_index];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(object, layout); status != HkxStatus::Ok) return status;
  const std::size_t base = object.offset;
  const std::size_t clip_index = model_.clips.size();
  HkxGraphClip clip;
  clip.node = static_cast<std::size_t>(node_of_object_[object_index]);
  clip.crop_start = at<float>(base + layouts_.clip_crop_start);
  clip.crop_end = at<float>(base + layouts_.clip_crop_end);
  clip.start_time = at<float>(base + layouts_.clip_start_time);
  clip.playback_speed = at<float>(base + layouts_.clip_speed);
  clip.enforced_duration = at<float>(base + layouts_.clip_duration);
  clip.mode = at<std::int8_t>(base + layouts_.clip_mode);
  clip.flags = at<std::uint8_t>(base + layouts_.clip_flags);
  clip.animation_binding_index = at<std::int16_t>(base + layouts_.clip_binding_index);
  if (const auto status = text(base + layouts_.clip_animation, clip.animation); status != HkxStatus::Ok) return status;
  if (const auto status = text(base + layouts_.clip_bundle, clip.animation_bundle); status != HkxStatus::Ok) return status;
  model_.nodes[clip.node].kind = HkxGraphNodeKind::Clip;
  model_.nodes[clip.node].kind_index = clip_index;

  std::int32_t triggers = -1;
  if (const auto status = typed_ref(base + layouts_.clip_triggers, "hkbClipTriggerArray", triggers);
      status != HkxStatus::Ok) {
    return status;
  }
  if (triggers >= 0) {
    const HkxObjectView& array = packfile_.objects[static_cast<std::size_t>(triggers)];
    const HkxClassLayout* array_layout = nullptr;
    if (const auto status = checked(array, array_layout); status != HkxStatus::Ok) return status;
    Elements rows;
    if (const auto status = elements_at(section_, array.offset + layouts_.trigger_array, k_pointer_size,
                                        layouts_.trigger_size, rows);
        status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t i = 0; i < rows.count; ++i) {
      if (const auto status = within_budget(triggers_read_++, layouts_.trigger_size); status != HkxStatus::Ok) return status;
      const std::size_t row = rows.first + i * layouts_.trigger_size;
      HkxGraphClipTrigger trigger;
      trigger.local_time = at<float>(row + layouts_.trigger_time);
      trigger.event_id = at<std::int32_t>(row + layouts_.trigger_event);
      trigger.relative_to_end_of_clip = at<std::uint8_t>(row + layouts_.trigger_relative) != 0;
      trigger.acyclic = at<std::uint8_t>(row + layouts_.trigger_acyclic) != 0;
      trigger.is_annotation = at<std::uint8_t>(row + layouts_.trigger_annotation) != 0;
      clip.triggers.push_back(trigger);
    }
  }
  std::size_t first_binding = 0;
  if (const auto status = read_bindings(object, HkxGraphOwnerKind::Clip, clip_index, first_binding);
      status != HkxStatus::Ok) {
    return status;
  }
  model_.clips.push_back(std::move(clip));
  return HkxStatus::Ok;
}

// Requires the object's signature to match the generated layout and its view to hold it: otherwise the node keeps its kind, reads no edges and is reported.
bool Decoder::usable(const HkxObjectView& object) {
  const HkxClassLayout* layout = find_layout(object.class_name, object.signature);
  if (layout != nullptr && detail::holds_layout(object, *layout)) return true;
  note("no layout matches or holds " + std::string(object.class_name) + ", so its children are not read");
  return false;
}

HkxStatus Decoder::edge(std::size_t parent, std::size_t field, HkxGraphEdgeRole role, std::int32_t slot, float weight,
                        bool bones) {
  if (!in_section(section_, field, k_pointer_size)) return HkxStatus::FixupOutOfRange;
  if (!pointer_to(field)) return HkxStatus::Ok;
  if (const auto status = within_budget(model_.edges.size(), k_pointer_size); status != HkxStatus::Ok) return status;
  std::int32_t child = -1;
  if (const auto status = node_ref(field, child); status != HkxStatus::Ok) return status;
  if (child < 0) {
    note("a generator pointer at offset " + std::to_string(field) + " does not reach a node, so the edge is dropped");
    return HkxStatus::Ok;
  }
  model_.edges.push_back({parent, child, role, slot, weight, bones});
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_children(std::size_t parent, std::size_t base, const ChildList& list) {
  Elements slots;
  if (const auto status = elements_at(section_, base + list.array, k_pointer_size, k_pointer_size, slots);
      status != HkxStatus::Ok) {
    return status;
  }
  slots_read_ += slots.count;
  if (const auto status = within_budget(slots_read_, k_pointer_size); status != HkxStatus::Ok) return status;
  for (std::size_t i = 0; i < slots.count; ++i) {
    const std::size_t slot = slots.first + i * k_pointer_size;
    const auto index = static_cast<std::int32_t>(i);
    if (list.child_class.empty()) {
      if (const auto status = edge(parent, slot, list.role, index, 0.0f, false); status != HkxStatus::Ok) return status;
      continue;
    }
    std::int32_t holder = -1;
    if (const auto status = typed_ref(slot, list.child_class, holder); status != HkxStatus::Ok) return status;
    if (holder < 0) continue;
    const HkxObjectView& entry = packfile_.objects[static_cast<std::size_t>(holder)];
    if (!usable(entry)) continue;
    const float weight = list.weight != 0 ? at<float>(entry.offset + list.weight) : 0.0f;
    const bool bones = list.bones != 0 && pointer_to(entry.offset + list.bones).has_value();
    if (const auto status = edge(parent, entry.offset + list.generator, list.role, index, weight, bones);
        status != HkxStatus::Ok) {
      return status;
    }
  }
  return HkxStatus::Ok;
}

HkxStatus Decoder::read_edges(std::size_t object_index) {
  const HkxObjectView& object = packfile_.objects[object_index];
  const auto parent = static_cast<std::size_t>(node_of_object_[object_index]);
  if (!usable(object)) return HkxStatus::Ok;
  const std::size_t base = object.offset;
  const auto wrapped = [&](std::size_t member) {
    return edge(parent, base + member, HkxGraphEdgeRole::Wrapped, 0, 0.0f, false);
  };
  switch (model_.nodes[parent].kind) {
    case HkxGraphNodeKind::Selector: {
      Elements slots;
      if (const auto status = elements_at(section_, base + layouts_.selector.array, k_pointer_size, k_pointer_size, slots);
          status != HkxStatus::Ok) {
        return status;
      }
      HkxGraphSelector row;
      row.node = parent;
      row.selected_generator_index = at<std::int16_t>(base + layouts_.selector_selected);
      row.generator_count = slots.count;
      for (std::size_t i = 0; i < slots.count; ++i) {
        if (!pointer_to(slots.first + i * k_pointer_size)) ++row.null_generator_count;
      }
      model_.selectors.push_back(row);
      return read_children(parent, base, layouts_.selector);
    }
    case HkxGraphNodeKind::Layer:
      return read_children(parent, base, layouts_.layers);
    case HkxGraphNodeKind::PoseMatching:
      return read_children(parent, base, layouts_.pose);
    case HkxGraphNodeKind::ModifierGenerator:
      return wrapped(layouts_.modifier_child);
    case HkxGraphNodeKind::StateTagging:
      return wrapped(layouts_.tagging_child);
    case HkxGraphNodeKind::CyclicBlendTransition:
      return wrapped(layouts_.cyclic_child);
    case HkxGraphNodeKind::BehaviorGraphSwap:
      return wrapped(layouts_.swap_child);
    case HkxGraphNodeKind::DynamicAnimationTagging:
      return wrapped(layouts_.dynamic_child);
    case HkxGraphNodeKind::OffsetAnimation:
      if (const auto status = wrapped(layouts_.offset_child); status != HkxStatus::Ok) return status;
      return edge(parent, base + layouts_.offset_clip, HkxGraphEdgeRole::OffsetClip, 0, 0.0f, false);
    case HkxGraphNodeKind::BoneSwitch:
      if (const auto status = wrapped(layouts_.switch_child); status != HkxStatus::Ok) return status;
      return read_children(parent, base, layouts_.bone_switch);
    default:
      return HkxStatus::Ok;
  }
}

HkxStatus Decoder::run(std::size_t graph) {
  const HkxObjectView& root = packfile_.objects[graph];
  const HkxClassLayout* layout = nullptr;
  if (const auto status = checked(root, layout); status != HkxStatus::Ok) return status;
  if (const auto status = read_nodes(); status != HkxStatus::Ok) return status;
  if (const auto status = text(root.offset + layouts_.node_name, model_.graph_name); status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = node_ref(root.offset + layouts_.graph_root, model_.root_node); status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_tables(root.offset); status != HkxStatus::Ok) return status;
  for (std::size_t i = 0; i < packfile_.objects.size(); ++i) {
    const std::string_view name = packfile_.objects[i].class_name;
    HkxStatus status = HkxStatus::Ok;
    if (name == "hkbStateMachine") {
      status = read_machine(i);
    } else if (name == "hkbBlenderGenerator") {
      status = read_blender(i);
    } else if (name == "hkbClipGenerator") {
      status = read_clip(i);
    } else if (name == "hkbBlendingTransitionEffect") {
      read_effect(i);
    } else if (node_of_object_[i] >= 0 &&
               model_.nodes[static_cast<std::size_t>(node_of_object_[i])].kind >= HkxGraphNodeKind::Selector) {
      status = read_edges(i);
    }
    if (status != HkxStatus::Ok) return status;
  }
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus read_behavior_model(const HkxPackfile& packfile, HkxGraphModel& out) {
  out = HkxGraphModel{};
  std::optional<std::size_t> graph;
  for (std::size_t i = 0; i < packfile.objects.size(); ++i) {
    if (packfile.objects[i].class_name == "hkbBehaviorGraph") {
      graph = i;
      break;
    }
  }
  if (!graph) return HkxStatus::MissingObject;
  if (packfile.pointer_size != k_pointer_size || packfile.data_section >= packfile.sections.size()) {
    return HkxStatus::UnsupportedLayout;
  }
  const Layouts layouts;
  if (!layouts.ok) return HkxStatus::UnsupportedLayout;
  HkxGraphModel model;
  Decoder decoder(packfile, model, layouts);
  if (const auto status = decoder.run(*graph); status != HkxStatus::Ok) return status;
  collect_object_slots(packfile, decoder.decoded_binding_sets(), model.object_event_slots, &model.object_variable_slots);
  out = std::move(model);
  return HkxStatus::Ok;
}

std::vector<float> blend_contributions(std::span<const float> weights, bool parametric, float parameter) {
  std::vector<float> shares(weights.size(), 0.0f);
  if (weights.empty()) return shares;
  if (!parametric) {
    float total = 0.0f;
    for (const float weight : weights) total += std::max(0.0f, weight);
    if (!(total > 0.0f) || !std::isfinite(total)) return shares;
    for (std::size_t i = 0; i < weights.size(); ++i) shares[i] = std::max(0.0f, weights[i]) / total;
    return shares;
  }
  if (!std::isfinite(parameter)) return shares;
  std::vector<std::size_t> order(weights.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) { return weights[left] < weights[right]; });
  if (parameter <= weights[order.front()]) {
    shares[order.front()] = 1.0f;
    return shares;
  }
  if (parameter >= weights[order.back()]) {
    shares[order.back()] = 1.0f;
    return shares;
  }
  for (std::size_t i = 0; i + 1 < order.size(); ++i) {
    const float low = weights[order[i]];
    const float high = weights[order[i + 1]];
    if (parameter < low || parameter > high) continue;
    const float span = high - low;
    const float t = span > 1e-9f ? (parameter - low) / span : 0.0f;
    shares[order[i]] = 1.0f - t;
    shares[order[i + 1]] = t;
    break;
  }
  return shares;
}

}  // namespace fo4::hkx
