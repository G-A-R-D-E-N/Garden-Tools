#pragma once

#include "fo4/hkx/layout.hpp"
#include "hkx_test_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

// Requires every class added to have a generated layout: the object is that layout's size, zeroed, and every pointer is a fixup.
class BehaviorFixture {
 public:
  std::size_t add(const std::string& class_name) {
    const auto* layout = fo4::hkx::find_layout(class_name);
    pad();
    const std::size_t at = data_.size();
    data_.resize(at + layout->size, std::byte{0});
    virtuals_.push_back(static_cast<std::uint32_t>(at));
    virtuals_.push_back(0);
    virtuals_.push_back(name_of(class_name, *layout));
    return at;
  }

  // Requires the object to be placed over an earlier one, so that one's view is cut short at `at`.
  std::size_t add_at(const std::string& class_name, std::size_t at) {
    const auto* layout = fo4::hkx::find_layout(class_name);
    if (data_.size() < at + layout->size) data_.resize(at + layout->size, std::byte{0});
    virtuals_.push_back(static_cast<std::uint32_t>(at));
    virtuals_.push_back(0);
    virtuals_.push_back(name_of(class_name, *layout));
    return at;
  }

  // Requires this to be the last write: the string runs to the end of the section with no terminator.
  void unterminated_text(std::size_t field, const std::string& value) {
    pad();
    const std::size_t at = data_.size();
    for (const char ch : value) data_.push_back(static_cast<std::byte>(ch));
    link(local_, field, at);
  }

  void override_signature(const std::string& class_name, std::uint32_t signature) {
    signatures_[class_name] = signature;
  }

  template <typename T>
  void set(std::size_t at, T value) {
    std::memcpy(data_.data() + at, &value, sizeof value);
  }

  void point(std::size_t field, std::size_t object) {
    global_.push_back(static_cast<std::uint32_t>(field));
    global_.push_back(kDataSection);
    global_.push_back(static_cast<std::uint32_t>(object));
  }

  std::size_t text(std::size_t field, const std::string& value) {
    pad();
    const std::size_t at = data_.size();
    for (const char ch : value) data_.push_back(static_cast<std::byte>(ch));
    data_.push_back(std::byte{0});
    link(local_, field, at);
    return at;
  }

  // Requires `at` to be a string or array already written: one more pointer to it, the way a pooled file shares one.
  void share(std::size_t field, std::size_t at) { link(local_, field, at); }

  void share_array(std::size_t field, std::size_t at, std::size_t count) {
    set<std::uint32_t>(field + 8, static_cast<std::uint32_t>(count));
    link(local_, field, at);
  }

  // Returns the first element's offset; the count lands at field + 8 where an hkArray keeps it.
  std::size_t array(std::size_t field, std::size_t count, std::size_t stride) {
    pad();
    const std::size_t at = data_.size();
    data_.resize(at + count * stride, std::byte{0});
    set<std::uint32_t>(field + 8, static_cast<std::uint32_t>(count));
    if (count != 0) link(local_, field, at);
    return at;
  }

  // Requires the caller to point at the returned offset itself: `size` zeroed bytes after the last write, 16-aligned, with no fixup.
  std::size_t block(std::size_t size) {
    pad();
    const std::size_t at = data_.size();
    data_.resize(at + size, std::byte{0});
    return at;
  }

  std::size_t member(const std::string& class_name, const char* name) const { return member_at(class_name.c_str(), name); }

  std::vector<std::byte> bytes() const {
    return build_packfile({
        TestSection{"__classnames__", names_, {}, {}, {}},
        TestSection{"__types__", {}, {}, {}, {}},
        TestSection{"__data__", data_, local_, global_, virtuals_},
    });
  }

 private:
  static constexpr std::uint32_t kDataSection = 2;

  void pad() {
    while (data_.size() % 16 != 0) data_.push_back(std::byte{0});
  }

  std::uint32_t name_of(const std::string& class_name, const fo4::hkx::HkxClassLayout& layout) {
    const auto found = name_offsets_.find(class_name);
    if (found != name_offsets_.end()) return found->second;
    const auto signature = signatures_.count(class_name) ? signatures_[class_name] : layout.signature;
    const std::uint32_t at = add_class_name(names_, signature, class_name);
    name_offsets_.emplace(class_name, at);
    return at;
  }

  std::vector<std::byte> names_;
  std::vector<std::byte> data_;
  std::vector<std::uint32_t> local_;
  std::vector<std::uint32_t> global_;
  std::vector<std::uint32_t> virtuals_;
  std::map<std::string, std::uint32_t> name_offsets_;
  std::map<std::string, std::uint32_t> signatures_;
};

struct BehaviorFixtureOptions {
  bool driven_weight{false};
  bool bone_weights{false};
  bool stray_state_pointer{false};
  bool wrong_class_state{false};
  std::uint32_t quad_index{0};
  std::uint32_t machine_signature{0};
  std::int32_t trigger_event{2};
  bool start_selector{false};
  std::string clip_a{"Animations\\A.hkt"};
  std::string clip_b{"Animations\\B.hkt"};
  std::string clip_c{"Animations\\C.hkt"};
};

// Invariant: the graph holds three events, three variables (bool, real, vector3), one state machine with two states, one state transition, one wildcard, three blenders and three clips; the numbers below are asserted by the tests.
inline std::vector<std::byte> behavior_graph_packfile(const BehaviorFixtureOptions& options = {}) {
  BehaviorFixture f;
  if (options.machine_signature != 0) f.override_signature("hkbStateMachine", options.machine_signature);
  const auto graph = f.add("hkbBehaviorGraph");
  const auto node_name = f.member("hkbNode", "name");
  f.text(graph + node_name, "TestGraph");

  const auto data = f.add("hkbBehaviorGraphData");
  f.point(graph + f.member("hkbBehaviorGraph", "data"), data);
  const auto strings = f.add("hkbBehaviorGraphStringData");
  f.point(data + f.member("hkbBehaviorGraphData", "stringData"), strings);
  const auto values = f.add("hkbVariableValueSet");
  f.point(data + f.member("hkbBehaviorGraphData", "variableInitialValues"), values);

  const char* events[] = {"Go", "Stop", "Wave"};
  auto block = f.array(strings + f.member("hkbBehaviorGraphStringData", "eventNames"), 3, 8);
  for (std::size_t i = 0; i < 3; ++i) f.text(block + i * 8, events[i]);
  const char* variables[] = {"bFlag", "fSpeed", "vLook"};
  block = f.array(strings + f.member("hkbBehaviorGraphStringData", "variableNames"), 3, 8);
  for (std::size_t i = 0; i < 3; ++i) f.text(block + i * 8, variables[i]);

  const auto event_info = fo4::hkx::find_layout("hkbEventInfo")->size;
  block = f.array(data + f.member("hkbBehaviorGraphData", "eventInfos"), 3, event_info);
  for (std::uint32_t i = 0; i < 3; ++i) f.set<std::uint32_t>(block + i * event_info, 0x100 + i);

  const auto info_size = fo4::hkx::find_layout("hkbVariableInfo")->size;
  const auto info_type = f.member("hkbVariableInfo", "type");
  const auto info_role = f.member("hkbVariableInfo", "role");
  block = f.array(data + f.member("hkbBehaviorGraphData", "variableInfos"), 3, info_size);
  const std::int8_t types[] = {0, 4, 6};
  for (std::size_t i = 0; i < 3; ++i) {
    f.set<std::int8_t>(block + i * info_size + info_type, types[i]);
    f.set<std::int16_t>(block + i * info_size + info_role, static_cast<std::int16_t>(10 + i));
  }

  block = f.array(values + f.member("hkbVariableValueSet", "wordVariableValues"), 3, 4);
  f.set<std::uint32_t>(block, 1);
  f.set<float>(block + 4, 2.5f);
  f.set<std::uint32_t>(block + 8, options.quad_index);
  block = f.array(values + f.member("hkbVariableValueSet", "quadVariableValues"), 1, 16);
  const float quad[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  for (std::size_t i = 0; i < 4; ++i) f.set<float>(block + i * 4, quad[i]);

  const auto clip = [&](const char* name, const char* animation, float speed) {
    const auto at = f.add("hkbClipGenerator");
    f.text(at + node_name, name);
    f.text(at + f.member("hkbClipGenerator", "animationName"), animation);
    f.set<std::int8_t>(at + f.member("hkbClipGenerator", "mode"), 1);
    f.set<std::int16_t>(at + f.member("hkbClipGenerator", "animationBindingIndex"), -1);
    f.set<float>(at + f.member("hkbClipGenerator", "playbackSpeed"), speed);
    return at;
  };
  const auto clip_a = clip("ClipA", options.clip_a.c_str(), 1.5f);
  const auto clip_b = clip("ClipB", options.clip_b.c_str(), 1.0f);
  const auto clip_c = clip("ClipC", options.clip_c.c_str(), 1.0f);

  const auto triggers = f.add("hkbClipTriggerArray");
  f.point(clip_a + f.member("hkbClipGenerator", "triggers"), triggers);
  const auto trigger_size = fo4::hkx::find_layout("hkbClipTrigger")->size;
  block = f.array(triggers + f.member("hkbClipTriggerArray", "triggers"), 1, trigger_size);
  f.set<float>(block + f.member("hkbClipTrigger", "localTime"), 0.5f);
  f.set<std::int32_t>(block + f.member("hkbClipTrigger", "event"), options.trigger_event);
  f.set<std::uint8_t>(block + f.member("hkbClipTrigger", "acyclic"), 1);

  const auto blender = [&](const char* name, float parameter, std::int16_t flags,
                           const std::vector<float>& weights, const std::vector<std::size_t>& targets) {
    const auto at = f.add("hkbBlenderGenerator");
    f.text(at + node_name, name);
    f.set<float>(at + f.member("hkbBlenderGenerator", "blendParameter"), parameter);
    f.set<std::int16_t>(at + f.member("hkbBlenderGenerator", "flags"), flags);
    const auto list = f.array(at + f.member("hkbBlenderGenerator", "children"), weights.size(), 8);
    std::vector<std::size_t> children;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      const auto child = f.add("hkbBlenderGeneratorChild");
      f.set<float>(child + f.member("hkbBlenderGeneratorChild", "weight"), weights[i]);
      f.set<float>(child + f.member("hkbBlenderGeneratorChild", "worldFromModelWeight"), 1.0f);
      f.point(child + f.member("hkbBlenderGeneratorChild", "generator"), targets[i]);
      f.point(list + i * 8, child);
      children.push_back(child);
    }
    return std::pair{at, children};
  };
  const auto mix = blender("Mix", 0.0f, 0, {1.0f, 3.0f}, {clip_a, clip_b});
  const auto along = blender("Along", 25.0f, 0x10, {0.0f, 100.0f, 50.0f}, {clip_a, clip_b, clip_c});
  const auto driven = blender("Driven", 0.0f, 0, {1.0f, 1.0f}, {clip_b, clip_c});
  if (options.bone_weights) f.point(mix.second[0] + f.member("hkbBlenderGeneratorChild", "boneWeights"), clip_c);

  const auto binding = [&](std::size_t owner, const char* path, std::int32_t variable) {
    const auto set = f.add("hkbVariableBindingSet");
    f.point(owner + f.member("hkbBindable", "variableBindingSet"), set);
    const auto row_size = fo4::hkx::find_layout("hkbVariableBindingSetBinding")->size;
    const auto rows = f.array(set + f.member("hkbVariableBindingSet", "bindings"), 1, row_size);
    f.text(rows + f.member("hkbVariableBindingSetBinding", "memberPath"), path);
    f.set<std::int32_t>(rows + f.member("hkbVariableBindingSetBinding", "variableIndex"), variable);
  };
  binding(driven.first, "blendParameter", 1);
  if (options.driven_weight) binding(driven.second[1], "weight", 1);
  binding(along.first, "other", 2);

  const auto effect = f.add("hkbBlendingTransitionEffect");
  f.text(effect + node_name, "fade");
  f.set<float>(effect + f.member("hkbBlendingTransitionEffect", "duration"), 0.5f);
  f.set<float>(effect + f.member("hkbBlendingTransitionEffect", "toGeneratorStartTimeFraction"), 0.25f);
  f.set<std::uint16_t>(effect + f.member("hkbBlendingTransitionEffect", "flags"), 6);
  f.set<std::int8_t>(effect + f.member("hkbBlendingTransitionEffect", "endMode"), 2);
  f.set<std::int8_t>(effect + f.member("hkbBlendingTransitionEffect", "blendCurve"), 3);
  const auto string_condition = f.add("hkbStringCondition");
  f.text(string_condition + f.member("hkbStringCondition", "conditionString"), "x > 1");
  const auto expression = f.add("hkbExpressionCondition");
  f.text(expression + f.member("hkbExpressionCondition", "expression"), "fSpeed > 0");

  const auto machine = f.add("hkbStateMachine");
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), machine);
  f.text(machine + node_name, "Root");
  f.set<std::int32_t>(machine + f.member("hkbStateMachine", "startStateId"), 7);
  f.set<std::int32_t>(machine + f.member("hkbStateMachine", "returnToPreviousStateEventId"), 1);
  f.set<std::int32_t>(machine + f.member("hkbStateMachine", "eventToSendWhenStateOrTransitionChanges"), 2);
  f.set<std::int32_t>(machine + f.member("hkbStateMachine", "syncVariableIndex"), -1);
  if (options.start_selector) f.point(machine + f.member("hkbStateMachine", "startStateIdSelector"), clip_c);
  const auto state_list = f.array(machine + f.member("hkbStateMachine", "states"), 2, 8);

  const auto state = [&](std::size_t slot, const char* name, std::int32_t id, std::size_t generator) {
    const auto at = f.add("hkbStateMachineStateInfo");
    f.text(at + f.member("hkbStateMachineStateInfo", "name"), name);
    f.set<std::int32_t>(at + f.member("hkbStateMachineStateInfo", "stateId"), id);
    f.set<float>(at + f.member("hkbStateMachineStateInfo", "probability"), 1.0f);
    f.set<std::uint8_t>(at + f.member("hkbStateMachineStateInfo", "enable"), 1);
    f.point(at + f.member("hkbStateMachineStateInfo", "generator"), generator);
    f.point(state_list + slot * 8, options.stray_state_pointer && slot == 1 ? machine + 4
                                       : options.wrong_class_state && slot == 1 ? clip_a : at);
    return at;
  };
  const auto state_a = state(0, "Idle", 7, clip_a);
  state(1, "Mix", 9, mix.first);

  const auto row_size = fo4::hkx::find_layout("hkbStateMachineTransitionInfo")->size;
  const auto transition = [&](std::size_t holder, std::size_t field, std::int32_t event, std::int32_t to,
                              std::size_t condition, std::size_t with_effect) {
    const auto array = f.add("hkbStateMachineTransitionInfoArray");
    f.point(holder + field, array);
    const auto row = f.array(array + f.member("hkbStateMachineTransitionInfoArray", "transitions"), 1, row_size);
    f.set<std::int32_t>(row + f.member("hkbStateMachineTransitionInfo", "eventId"), event);
    f.set<std::int32_t>(row + f.member("hkbStateMachineTransitionInfo", "toStateId"), to);
    f.set<std::int16_t>(row + f.member("hkbStateMachineTransitionInfo", "priority"), 3);
    f.set<std::uint16_t>(row + f.member("hkbStateMachineTransitionInfo", "flags"), 0x0A00);
    const auto trigger_at = row + f.member("hkbStateMachineTransitionInfo", "triggerInterval");
    f.set<std::int32_t>(trigger_at + f.member("hkbStateMachineTimeInterval", "enterEventId"), 1);
    f.set<std::int32_t>(trigger_at + f.member("hkbStateMachineTimeInterval", "exitEventId"), -1);
    f.set<float>(trigger_at + f.member("hkbStateMachineTimeInterval", "enterTime"), 0.25f);
    if (condition != 0) f.point(row + f.member("hkbStateMachineTransitionInfo", "condition"), condition);
    if (with_effect != 0) f.point(row + f.member("hkbStateMachineTransitionInfo", "transition"), with_effect);
  };
  transition(state_a, f.member("hkbStateMachineStateInfo", "transitions"), 1, 9, string_condition, effect);
  transition(machine, f.member("hkbStateMachine", "wildcardTransitions"), 0, 7, expression, 0);

  return f.bytes();
}

struct GeneratorFixtureOptions {
  bool stray_selector_entry{false};
  std::uint32_t tagging_signature{0};
};

// Invariant: nodes 0 to 14 are ClipA, ClipB, Sel, Mod, Layers, Pose, Switch, Offset, Tag, Cyclic, Swap, Dyn, Ref, Seq and RefPose; the 15 edges are listed in test_hkx_behavior_graph.cpp:ReadsEveryGeneratorKindWithItsChildEdges.
inline std::vector<std::byte> generator_graph_packfile(const GeneratorFixtureOptions& options = {}) {
  BehaviorFixture f;
  if (options.tagging_signature != 0) f.override_signature("BSiStateTaggingGenerator", options.tagging_signature);
  const auto graph = f.add("hkbBehaviorGraph");
  const auto node_name = f.member("hkbNode", "name");
  f.text(graph + node_name, "GeneratorGraph");

  const auto node = [&](const char* class_name, const char* label) {
    const auto at = f.add(class_name);
    f.text(at + node_name, label);
    return at;
  };
  const auto wrap = [&](const char* class_name, const char* label, const char* member, std::size_t child) {
    const auto at = node(class_name, label);
    f.point(at + f.member(class_name, member), child);
    return at;
  };

  const auto clip_a = node("hkbClipGenerator", "ClipA");
  const auto clip_b = node("hkbClipGenerator", "ClipB");

  const auto selector = node("hkbManualSelectorGenerator", "Sel");
  const auto entries = f.array(selector + f.member("hkbManualSelectorGenerator", "generators"), 3, 8);
  f.point(entries, clip_a);
  if (options.stray_selector_entry) f.point(entries + 8, graph);
  f.point(entries + 16, clip_b);

  const auto modifier = wrap("hkbModifierGenerator", "Mod", "generator", selector);
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), modifier);

  const auto layers = node("hkbLayerGenerator", "Layers");
  const auto layer_list = f.array(layers + f.member("hkbLayerGenerator", "layers"), 2, 8);
  const float layer_weights[] = {0.25f, 0.75f};
  const std::size_t layer_targets[] = {clip_a, clip_b};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto layer = f.add("hkbLayer");
    f.set<float>(layer + f.member("hkbLayer", "weight"), layer_weights[i]);
    f.point(layer + f.member("hkbLayer", "generator"), layer_targets[i]);
    if (i == 1) f.point(layer + f.member("hkbLayer", "boneWeights"), clip_a);
    f.point(layer_list + i * 8, layer);
  }

  const auto pose = node("hkbPoseMatchingGenerator", "Pose");
  const auto pose_list = f.array(pose + f.member("hkbBlenderGenerator", "children"), 2, 8);
  const float pose_weights[] = {1.0f, 2.0f};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto child = f.add("hkbBlenderGeneratorChild");
    f.set<float>(child + f.member("hkbBlenderGeneratorChild", "weight"), pose_weights[i]);
    f.point(child + f.member("hkbBlenderGeneratorChild", "generator"), layer_targets[i]);
    f.point(pose_list + i * 8, child);
  }

  const auto bone_switch = wrap("BSBoneSwitchGenerator", "Switch", "pDefaultGenerator", clip_a);
  const auto switch_list = f.array(bone_switch + f.member("BSBoneSwitchGenerator", "ChildrenA"), 1, 8);
  const auto bone_data = f.add("BSBoneSwitchGeneratorBoneData");
  f.point(bone_data + f.member("BSBoneSwitchGeneratorBoneData", "pGenerator"), clip_b);
  f.point(bone_data + f.member("BSBoneSwitchGeneratorBoneData", "spBoneWeight"), clip_a);
  f.point(switch_list, bone_data);

  const auto offset = wrap("BSOffsetAnimationGenerator", "Offset", "pDefaultGenerator", clip_a);
  f.point(offset + f.member("BSOffsetAnimationGenerator", "pOffsetClipGenerator"), clip_b);
  wrap("BSiStateTaggingGenerator", "Tag", "pDefaultGenerator", clip_a);
  wrap("BSCyclicBlendTransitionGenerator", "Cyclic", "pBlenderGenerator", clip_a);
  wrap("BSBehaviorGraphSwapGenerator", "Swap", "pDefaultGenerator", clip_b);
  wrap("DynamicAnimationTaggingGenerator", "Dyn", "pDefaultGenerator", clip_a);
  node("hkbBehaviorReferenceGenerator", "Ref");
  node("BGSGamebryoSequenceGenerator", "Seq");
  node("hkbReferencePoseGenerator", "RefPose");
  return f.bytes();
}

}  // namespace fo4::test::hkx
