#include "fo4/hkx/behavior_edit.hpp"
#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/behavior_validate.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/symbol_search.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_noop_check.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace fo4::test::hkx;
using fo4::hkx::HkxBehaviorEdit;
using fo4::hkx::HkxBehaviorEditKind;
using fo4::hkx::HkxGraphConditionKind;
using fo4::hkx::HkxGraphBinding;
using fo4::hkx::HkxGraphClip;
using fo4::hkx::HkxGraphState;
using fo4::hkx::HkxGraphEdge;
using fo4::hkx::HkxGraphBinding;
using fo4::hkx::HkxGraphClip;
using fo4::hkx::HkxGraphEdgeRole;
using fo4::hkx::HkxGraphIssue;
using fo4::hkx::HkxGraphIssueSeverity;
using fo4::hkx::HkxGraphModel;
using fo4::hkx::HkxGraphPointerFault;
using fo4::hkx::HkxGraphState;
using fo4::hkx::HkxGraphTransition;
using fo4::hkx::HkxGraphValidation;
using fo4::hkx::apply_behavior_edits;
using fo4::hkx::validate_behavior_model;
using fo4::hkx::HkxGraphNodeKind;
using fo4::hkx::HkxGraphOwnerKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;
using fo4::hkx::read_behavior_model;
using fo4::hkx::read_packfile;

namespace {

HkxStatus decode(const std::vector<std::byte>& bytes, HkxGraphModel& model) {
  HkxPackfile packfile;
  const HkxStatus status = read_packfile(bytes, packfile);
  if (status != HkxStatus::Ok) return status;
  return read_behavior_model(packfile, model);
}

HkxGraphModel decode_ok(const BehaviorFixtureOptions& options = {}) {
  HkxGraphModel model;
  const auto bytes = behavior_graph_packfile(options);
  EXPECT_EQ(decode(bytes, model), HkxStatus::Ok);
  return model;
}

}  // namespace

TEST(HkxBehaviorGraphModel, ReadsTheEventAndVariableTables) {
  const HkxGraphModel model = decode_ok();
  EXPECT_EQ(model.graph_name, "TestGraph");
  ASSERT_EQ(model.events.size(), 3U);
  EXPECT_EQ(model.events[0].name, "Go");
  EXPECT_EQ(model.events[2].name, "Wave");
  EXPECT_EQ(model.events[1].flags, 0x101U);

  ASSERT_EQ(model.variables.size(), 3U);
  EXPECT_EQ(model.variables[0].name, "bFlag");
  EXPECT_EQ(model.variables[0].type, static_cast<std::int8_t>(fo4::hkx::HkxVariableType::Bool));
  EXPECT_EQ(model.variables[0].int_value, 1);
  EXPECT_EQ(model.variables[0].role, 10);
  EXPECT_EQ(model.variables[1].name, "fSpeed");
  EXPECT_FLOAT_EQ(model.variables[1].real_value, 2.5f);
  EXPECT_FALSE(model.variables[1].has_vector);
  EXPECT_EQ(model.variables[2].name, "vLook");
  ASSERT_TRUE(model.variables[2].has_vector);
  EXPECT_FLOAT_EQ(model.variables[2].vector[0], 1.0f);
  EXPECT_FLOAT_EQ(model.variables[2].vector[3], 4.0f);
}

TEST(HkxBehaviorGraphModel, ReadsStateNamesIdsAndTheirGenerators) {
  const HkxGraphModel model = decode_ok();
  ASSERT_EQ(model.state_machines.size(), 1U);
  const auto& machine = model.state_machines[0];
  EXPECT_EQ(model.nodes[machine.node].name, "Root");
  EXPECT_EQ(model.nodes[machine.node].kind, HkxGraphNodeKind::StateMachine);
  EXPECT_EQ(model.root_node, static_cast<std::int32_t>(machine.node));
  EXPECT_EQ(machine.start_state_id, 7);
  EXPECT_EQ(machine.return_to_previous_event_id, 1);
  EXPECT_EQ(machine.change_event_id, 2);
  EXPECT_EQ(machine.sync_variable_index, -1);
  ASSERT_EQ(machine.state_count, 2U);
  ASSERT_EQ(model.states.size(), 2U);
  EXPECT_EQ(model.states[0].name, "Idle");
  EXPECT_EQ(model.states[0].state_id, 7);
  EXPECT_TRUE(model.states[0].enable);
  EXPECT_FLOAT_EQ(model.states[0].probability, 1.0f);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.states[0].generator_node)].name, "ClipA");
  EXPECT_EQ(model.states[1].name, "Mix");
  EXPECT_EQ(model.states[1].state_id, 9);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.states[1].generator_node)].class_name, "hkbBlenderGenerator");
}

TEST(HkxBehaviorGraphModel, ReadsTransitionsWithEventsTargetsEffectsAndConditions) {
  const HkxGraphModel model = decode_ok();
  ASSERT_EQ(model.transitions.size(), 2U);
  const auto& machine = model.state_machines[0];
  EXPECT_EQ(model.states[0].first_transition, 0U);
  EXPECT_EQ(model.states[0].transition_count, 1U);
  EXPECT_EQ(model.states[1].transition_count, 0U);
  EXPECT_EQ(machine.first_wildcard, 1U);
  EXPECT_EQ(machine.wildcard_count, 1U);

  const auto& state_transition = model.transitions[0];
  EXPECT_EQ(state_transition.state, 0);
  EXPECT_EQ(state_transition.event_id, 1);
  EXPECT_EQ(state_transition.to_state_id, 9);
  EXPECT_EQ(state_transition.priority, 3);
  EXPECT_EQ(state_transition.flags, 0x0A00U);
  EXPECT_EQ(state_transition.trigger_interval.enter_event_id, 1);
  EXPECT_EQ(state_transition.trigger_interval.exit_event_id, -1);
  EXPECT_FLOAT_EQ(state_transition.trigger_interval.enter_time, 0.25f);
  ASSERT_GE(state_transition.effect_node, 0);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(state_transition.effect_node)].name, "fade");
  ASSERT_EQ(model.effects.size(), 1U);
  EXPECT_EQ(static_cast<std::int32_t>(model.effects[0].node), state_transition.effect_node);
  EXPECT_FLOAT_EQ(model.effects[0].duration, 0.5f);
  EXPECT_FLOAT_EQ(model.effects[0].start_fraction, 0.25f);
  EXPECT_EQ(model.effects[0].flags, 6U);
  EXPECT_EQ(model.effects[0].end_mode, 2);
  EXPECT_EQ(model.effects[0].blend_curve, 3);
  EXPECT_EQ(state_transition.condition_kind, HkxGraphConditionKind::String);
  EXPECT_EQ(state_transition.condition, "x > 1");
  EXPECT_EQ(state_transition.condition_class, "hkbStringCondition");

  const auto& wildcard = model.transitions[1];
  EXPECT_EQ(wildcard.state, -1);
  EXPECT_EQ(wildcard.event_id, 0);
  EXPECT_EQ(wildcard.to_state_id, 7);
  EXPECT_EQ(wildcard.effect_node, -1);
  EXPECT_EQ(wildcard.condition_kind, HkxGraphConditionKind::Expression);
  EXPECT_EQ(wildcard.condition, "fSpeed > 0");
}

TEST(HkxBehaviorGraphModel, ReadsBlendersWithWeightsAndStaticContributions) {
  const HkxGraphModel model = decode_ok();
  ASSERT_EQ(model.blenders.size(), 3U);
  ASSERT_EQ(model.blend_children.size(), 7U);

  const auto& mix = model.blenders[0];
  EXPECT_EQ(model.nodes[mix.node].name, "Mix");
  EXPECT_FALSE(mix.parametric);
  EXPECT_TRUE(mix.resolved);
  ASSERT_EQ(mix.child_count, 2U);
  EXPECT_FLOAT_EQ(model.blend_children[mix.first_child].weight, 1.0f);
  EXPECT_FLOAT_EQ(model.blend_children[mix.first_child + 1].weight, 3.0f);
  EXPECT_FLOAT_EQ(model.blend_children[mix.first_child].contribution, 0.25f);
  EXPECT_FLOAT_EQ(model.blend_children[mix.first_child + 1].contribution, 0.75f);
  EXPECT_FLOAT_EQ(model.blend_children[mix.first_child].world_from_model_weight, 1.0f);
  const auto& first_target = model.nodes[static_cast<std::size_t>(model.blend_children[mix.first_child].generator_node)];
  EXPECT_EQ(first_target.name, "ClipA");
  EXPECT_EQ(first_target.kind, HkxGraphNodeKind::Clip);

  const auto& along = model.blenders[1];
  EXPECT_EQ(model.nodes[along.node].name, "Along");
  EXPECT_TRUE(along.parametric);
  EXPECT_FLOAT_EQ(along.blend_parameter, 25.0f);
  EXPECT_TRUE(along.resolved);
  ASSERT_EQ(along.child_count, 3U);
  EXPECT_FLOAT_EQ(model.blend_children[along.first_child].contribution, 0.5f);
  EXPECT_FLOAT_EQ(model.blend_children[along.first_child + 1].contribution, 0.0f);
  EXPECT_FLOAT_EQ(model.blend_children[along.first_child + 2].contribution, 0.5f);
}

TEST(HkxBehaviorGraphModel, ABlendParameterDrivenByAVariableIsNotResolved) {
  const HkxGraphModel model = decode_ok();
  const auto& driven = model.blenders[2];
  EXPECT_EQ(model.nodes[driven.node].name, "Driven");
  EXPECT_EQ(driven.blend_parameter_variable, 1);
  EXPECT_FALSE(driven.resolved);
  ASSERT_EQ(model.bindings.size(), 2U);
  EXPECT_EQ(model.bindings[0].owner_kind, HkxGraphOwnerKind::Blender);
  EXPECT_EQ(model.bindings[0].member_path, "other");
  EXPECT_EQ(model.bindings[1].owner_kind, HkxGraphOwnerKind::Blender);
  EXPECT_EQ(model.bindings[1].owner_index, 2U);
  EXPECT_EQ(model.bindings[1].member_path, "blendParameter");
  EXPECT_EQ(model.bindings[1].variable_index, 1);
}

TEST(HkxBehaviorGraphModel, AWeightDrivenByAVariableNamesTheVariableAndBlocksResolution) {
  BehaviorFixtureOptions options;
  options.driven_weight = true;
  const HkxGraphModel model = decode_ok(options);
  const auto& driven = model.blenders[2];
  EXPECT_EQ(model.blend_children[driven.first_child].weight_variable, -1);
  EXPECT_EQ(model.blend_children[driven.first_child + 1].weight_variable, 1);
  bool found = false;
  for (const auto& binding : model.bindings) {
    if (binding.owner_kind == HkxGraphOwnerKind::BlendChild && binding.member_path == "weight") found = true;
  }
  EXPECT_TRUE(found);
}

TEST(HkxBehaviorGraphModel, BoneWeightsStopAStaticBlendFromBeingResolved) {
  BehaviorFixtureOptions options;
  options.bone_weights = true;
  const HkxGraphModel model = decode_ok(options);
  EXPECT_TRUE(model.blend_children[model.blenders[0].first_child].has_bone_weights);
  EXPECT_FALSE(model.blenders[0].resolved);
  EXPECT_TRUE(model.blenders[1].resolved);
}

TEST(HkxBehaviorGraphModel, ReadsClipGeneratorsWithAnimationNamesAndTriggers) {
  const HkxGraphModel model = decode_ok();
  ASSERT_EQ(model.clips.size(), 3U);
  EXPECT_EQ(model.clips[0].animation, "Animations\\A.hkt");
  EXPECT_EQ(model.nodes[model.clips[0].node].name, "ClipA");
  EXPECT_EQ(model.nodes[model.clips[0].node].kind_index, 0U);
  EXPECT_EQ(model.clips[0].mode, 1);
  EXPECT_FLOAT_EQ(model.clips[0].playback_speed, 1.5f);
  ASSERT_EQ(model.clips[0].triggers.size(), 1U);
  EXPECT_FLOAT_EQ(model.clips[0].triggers[0].local_time, 0.5f);
  EXPECT_EQ(model.clips[0].triggers[0].event_id, 2);
  EXPECT_TRUE(model.clips[0].triggers[0].acyclic);
  EXPECT_FALSE(model.clips[0].triggers[0].relative_to_end_of_clip);
  EXPECT_EQ(model.clips[2].animation, "Animations\\C.hkt");
  EXPECT_TRUE(model.clips[2].triggers.empty());
}

TEST(HkxBehaviorGraphModel, ReportsAPointerThatDoesNotLandOnAnObjectInsteadOfDroppingItSilently) {
  BehaviorFixtureOptions options;
  options.stray_state_pointer = true;
  const HkxGraphModel model = decode_ok(options);
  EXPECT_EQ(model.states.size(), 1U);
  EXPECT_FALSE(model.diagnostics.empty());
}

TEST(HkxBehaviorGraphModel, RefusesAClassWhoseSignatureIsNotTheGeneratedOne) {
  BehaviorFixtureOptions options;
  options.machine_signature = 1234U;
  HkxGraphModel model;
  const auto bytes = behavior_graph_packfile(options);
  EXPECT_EQ(decode(bytes, model), HkxStatus::UnsupportedLayout);
  EXPECT_TRUE(model.nodes.empty());
}

TEST(HkxBehaviorGraphModel, SaysAPackfileWithoutAGraphHasNone) {
  const auto* layout = fo4::hkx::find_layout("hkbClipGenerator");
  const auto bytes = single_object_packfile("hkbClipGenerator", layout->signature,
                                            std::vector<std::byte>(layout->size, std::byte{0}), {});
  HkxGraphModel model;
  EXPECT_EQ(decode(bytes, model), HkxStatus::MissingObject);
}

TEST(HkxBlendContributions, NonParametricSharesAreTheClampedWeightsOverTheirSum) {
  const std::vector<float> weights{1.0f, 3.0f, -2.0f};
  const auto shares = fo4::hkx::blend_contributions(weights, false, 0.0f);
  ASSERT_EQ(shares.size(), 3U);
  EXPECT_FLOAT_EQ(shares[0], 0.25f);
  EXPECT_FLOAT_EQ(shares[1], 0.75f);
  EXPECT_FLOAT_EQ(shares[2], 0.0f);
}

TEST(HkxBlendContributions, AllZeroOrNegativeWeightsGiveNoShare) {
  const std::vector<float> weights{0.0f, -1.0f};
  for (const float share : fo4::hkx::blend_contributions(weights, false, 0.0f)) EXPECT_EQ(share, 0.0f);
  EXPECT_TRUE(fo4::hkx::blend_contributions({}, false, 0.0f).empty());
  EXPECT_TRUE(fo4::hkx::blend_contributions({}, true, 0.0f).empty());
}

TEST(HkxBlendContributions, ParametricSharesClampToTheEndsAndInterpolateBetweenNeighbours) {
  const std::vector<float> weights{100.0f, 0.0f, 50.0f};
  auto shares = fo4::hkx::blend_contributions(weights, true, -5.0f);
  EXPECT_FLOAT_EQ(shares[1], 1.0f);
  shares = fo4::hkx::blend_contributions(weights, true, 500.0f);
  EXPECT_FLOAT_EQ(shares[0], 1.0f);
  shares = fo4::hkx::blend_contributions(weights, true, 25.0f);
  EXPECT_FLOAT_EQ(shares[1], 0.5f);
  EXPECT_FLOAT_EQ(shares[2], 0.5f);
  EXPECT_FLOAT_EQ(shares[0], 0.0f);
  shares = fo4::hkx::blend_contributions(weights, true, 75.0f);
  EXPECT_FLOAT_EQ(shares[2], 0.5f);
  EXPECT_FLOAT_EQ(shares[0], 0.5f);
  shares = fo4::hkx::blend_contributions(weights, true, 50.0f);
  EXPECT_FLOAT_EQ(shares[2], 1.0f);
  EXPECT_FLOAT_EQ(shares[0], 0.0f);
}

TEST(HkxBlendContributions, ANonFiniteParameterGivesNoShareInsteadOfNaN) {
  const std::vector<float> weights{0.0f, 1.0f};
  for (const float parameter : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    for (const float share : fo4::hkx::blend_contributions(weights, true, parameter)) EXPECT_EQ(share, 0.0f);
  }
}

namespace {

bool mentions(const HkxGraphModel& model, const std::string& text) {
  for (const auto& line : model.diagnostics) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

struct Skeleton {
  std::size_t graph{0};
  std::size_t strings{0};
  std::size_t node_name{0};
  std::size_t event_names{0};
};

Skeleton skeleton(BehaviorFixture& f) {
  Skeleton out;
  out.graph = f.add("hkbBehaviorGraph");
  out.node_name = f.member("hkbNode", "name");
  const auto data = f.add("hkbBehaviorGraphData");
  f.point(out.graph + f.member("hkbBehaviorGraph", "data"), data);
  out.strings = f.add("hkbBehaviorGraphStringData");
  f.point(data + f.member("hkbBehaviorGraphData", "stringData"), out.strings);
  out.event_names = out.strings + f.member("hkbBehaviorGraphStringData", "eventNames");
  return out;
}

// Requires `references` slots of one machine to all point at the same state, whose transition array holds `rows` rows.
std::vector<std::byte> shared_state_packfile(std::size_t references, std::size_t rows) {
  BehaviorFixture f;
  const auto graph = f.add("hkbBehaviorGraph");
  const auto machine = f.add("hkbStateMachine");
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), machine);
  const auto list = f.array(machine + f.member("hkbStateMachine", "states"), references, 8);
  const auto state = f.add("hkbStateMachineStateInfo");
  if (rows != 0) {
    const auto array = f.add("hkbStateMachineTransitionInfoArray");
    f.point(state + f.member("hkbStateMachineStateInfo", "transitions"), array);
    f.array(array + f.member("hkbStateMachineTransitionInfoArray", "transitions"), rows,
            fo4::hkx::find_layout("hkbStateMachineTransitionInfo")->size);
  }
  for (std::size_t i = 0; i < references; ++i) f.point(list + i * 8, state);
  return f.bytes();
}

}  // namespace

TEST(HkxBehaviorGraphModel, ANegativeArrayCountIsABadSection) {
  BehaviorFixture f;
  const auto base = skeleton(f);
  f.array(base.event_names, 0, 8);
  f.set<std::int32_t>(base.event_names + 8, -1);
  HkxGraphModel model;
  EXPECT_EQ(decode(f.bytes(), model), HkxStatus::BadSection);
}

TEST(HkxBehaviorGraphModel, AnArrayThatRunsPastTheSectionIsAFixupOutOfRange) {
  BehaviorFixture f;
  const auto base = skeleton(f);
  f.array(base.event_names, 1, 8);
  f.set<std::int32_t>(base.event_names + 8, 1000000);
  HkxGraphModel model;
  EXPECT_EQ(decode(f.bytes(), model), HkxStatus::FixupOutOfRange);
}

TEST(HkxBehaviorGraphModel, AStringWithNoTerminatorIsAFixupOutOfRange) {
  BehaviorFixture f;
  const auto base = skeleton(f);
  f.unterminated_text(base.graph + base.node_name, "NoEnd");
  HkxGraphModel model;
  EXPECT_EQ(decode(f.bytes(), model), HkxStatus::FixupOutOfRange);
}

TEST(HkxBehaviorGraphModel, AStringLongerThanTheTextLimitIsAFixupOutOfRange) {
  BehaviorFixture f;
  const auto base = skeleton(f);
  f.text(base.graph + base.node_name, std::string(70000, 'a'));
  HkxGraphModel model;
  EXPECT_EQ(decode(f.bytes(), model), HkxStatus::FixupOutOfRange);
}

TEST(HkxBehaviorGraphModel, AnObjectCutShortByTheNextOneIsTruncated) {
  BehaviorFixture f;
  const auto base = skeleton(f);
  f.add_at("hkbClipGenerator", base.graph + 16);
  HkxGraphModel model;
  EXPECT_EQ(decode(f.bytes(), model), HkxStatus::Truncated);
}

TEST(HkxBehaviorGraphModel, AVectorWordPastTheQuadTableIsReportedAndLeavesNoVector) {
  BehaviorFixtureOptions options;
  options.quad_index = 5;
  const HkxGraphModel model = decode_ok(options);
  ASSERT_EQ(model.variables.size(), 3U);
  EXPECT_FALSE(model.variables[2].has_vector);
  EXPECT_TRUE(mentions(model, "quad"));
}

TEST(HkxBehaviorGraphModel, APointerToTheWrongClassIsSkippedAndReported) {
  BehaviorFixtureOptions options;
  options.wrong_class_state = true;
  const HkxGraphModel model = decode_ok(options);
  EXPECT_EQ(model.states.size(), 1U);
  EXPECT_TRUE(mentions(model, "expected hkbStateMachineStateInfo"));
}

TEST(HkxBehaviorGraphModel, ManyReferencesToOneStateWithItsTransitionsAreCappedNotMultiplied) {
  HkxGraphModel model;
  EXPECT_EQ(decode(shared_state_packfile(1, 400), model), HkxStatus::Ok);
  EXPECT_EQ(model.transitions.size(), 400U);
  EXPECT_EQ(decode(shared_state_packfile(64, 400), model), HkxStatus::BadSection);
}

TEST(HkxBehaviorGraphModel, ManyReferencesToOneBareStateAreCapped) {
  HkxGraphModel model;
  EXPECT_EQ(decode(shared_state_packfile(4000, 0), model), HkxStatus::BadSection);
}

namespace {

// Requires `pointers` binding rows of one machine to share a single member path of `length` bytes.
std::vector<std::byte> pooled_text_packfile(std::size_t pointers, std::size_t length) {
  BehaviorFixture f;
  const auto graph = f.add("hkbBehaviorGraph");
  const auto machine = f.add("hkbStateMachine");
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), machine);
  const auto set = f.add("hkbVariableBindingSet");
  f.point(machine + f.member("hkbBindable", "variableBindingSet"), set);
  const auto row_size = fo4::hkx::find_layout("hkbVariableBindingSetBinding")->size;
  const auto rows = f.array(set + f.member("hkbVariableBindingSet", "bindings"), pointers, row_size);
  const auto path = f.member("hkbVariableBindingSetBinding", "memberPath");
  const auto text = f.text(rows + path, std::string(length, 'p'));
  for (std::size_t i = 1; i < pointers; ++i) f.share(rows + i * row_size + path, text);
  return f.bytes();
}

// Requires `owners` objects of one class to share one array of `slots` null pointers through `member`.
std::vector<std::byte> shared_null_array_packfile(const char* class_name, const char* member, std::size_t owners,
                                                  std::size_t slots) {
  BehaviorFixture f;
  const auto graph = f.add("hkbBehaviorGraph");
  const auto first = f.add(class_name);
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), first);
  const auto field = f.member(class_name, member);
  const auto array = f.array(first + field, slots, 8);
  for (std::size_t i = 1; i < owners; ++i) f.share_array(f.add(class_name) + field, array, slots);
  return f.bytes();
}

}  // namespace

TEST(HkxBehaviorGraphModel, OnePooledStringPointedAtManyTimesIsCappedByTotalBytes) {
  HkxGraphModel model;
  EXPECT_EQ(decode(pooled_text_packfile(4, 60000), model), HkxStatus::Ok);
  ASSERT_EQ(model.bindings.size(), 4U);
  EXPECT_EQ(model.bindings[3].member_path.size(), 60000U);
  EXPECT_EQ(decode(pooled_text_packfile(64, 60000), model), HkxStatus::BadSection);
}

TEST(HkxBehaviorGraphModel, ManyMachinesSharingANullFilledStateArrayAreCapped) {
  HkxGraphModel model;
  EXPECT_EQ(decode(shared_null_array_packfile("hkbStateMachine", "states", 1, 1000), model), HkxStatus::Ok);
  EXPECT_EQ(decode(shared_null_array_packfile("hkbStateMachine", "states", 64, 1000), model), HkxStatus::BadSection);
}

TEST(HkxBehaviorGraphModel, ManyBlendersSharingANullFilledChildArrayAreCapped) {
  HkxGraphModel model;
  EXPECT_EQ(decode(shared_null_array_packfile("hkbBlenderGenerator", "children", 1, 1000), model), HkxStatus::Ok);
  EXPECT_EQ(decode(shared_null_array_packfile("hkbBlenderGenerator", "children", 64, 1000), model), HkxStatus::BadSection);
}

namespace {

HkxGraphModel decode_generators(const GeneratorFixtureOptions& options = {}) {
  HkxGraphModel model;
  EXPECT_EQ(decode(generator_graph_packfile(options), model), HkxStatus::Ok);
  return model;
}

void expect_edge(const HkxGraphModel& model, std::size_t at, std::size_t parent, std::int32_t child,
                 HkxGraphEdgeRole role, std::int32_t slot, float weight, bool bones) {
  ASSERT_LT(at, model.edges.size());
  const HkxGraphEdge& edge = model.edges[at];
  EXPECT_EQ(edge.parent, parent) << "edge " << at;
  EXPECT_EQ(edge.child, child) << "edge " << at;
  EXPECT_EQ(edge.role, role) << "edge " << at;
  EXPECT_EQ(edge.slot, slot) << "edge " << at;
  EXPECT_FLOAT_EQ(edge.weight, weight) << "edge " << at;
  EXPECT_EQ(edge.has_bone_weights, bones) << "edge " << at;
}

}  // namespace

TEST(HkxBehaviorGraphModel, ReadsEveryGeneratorKindWithItsChildEdges) {
  const HkxGraphModel model = decode_generators();
  const HkxGraphNodeKind kinds[] = {
      HkxGraphNodeKind::Clip,
      HkxGraphNodeKind::Clip,
      HkxGraphNodeKind::Selector,
      HkxGraphNodeKind::ModifierGenerator,
      HkxGraphNodeKind::Layer,
      HkxGraphNodeKind::PoseMatching,
      HkxGraphNodeKind::BoneSwitch,
      HkxGraphNodeKind::OffsetAnimation,
      HkxGraphNodeKind::StateTagging,
      HkxGraphNodeKind::CyclicBlendTransition,
      HkxGraphNodeKind::BehaviorGraphSwap,
      HkxGraphNodeKind::DynamicAnimationTagging,
      HkxGraphNodeKind::BehaviorReference,
      HkxGraphNodeKind::GamebryoSequence,
      HkxGraphNodeKind::ReferencePose,
  };
  ASSERT_EQ(model.nodes.size(), std::size(kinds));
  for (std::size_t i = 0; i < std::size(kinds); ++i) {
    EXPECT_EQ(model.nodes[i].kind, kinds[i]) << model.nodes[i].class_name;
    if (kinds[i] != HkxGraphNodeKind::Clip) {
      EXPECT_EQ(model.nodes[i].kind_index, 0U) << model.nodes[i].class_name;
    }
  }
  EXPECT_EQ(model.nodes[4].name, "Layers");
  EXPECT_EQ(model.root_node, 3);

  using Role = HkxGraphEdgeRole;
  ASSERT_EQ(model.edges.size(), 15U);
  expect_edge(model, 0, 2, 0, Role::Selected, 0, 0.0f, false);
  expect_edge(model, 1, 2, 1, Role::Selected, 2, 0.0f, false);
  expect_edge(model, 2, 3, 2, Role::Wrapped, 0, 0.0f, false);
  expect_edge(model, 3, 4, 0, Role::Layer, 0, 0.25f, false);
  expect_edge(model, 4, 4, 1, Role::Layer, 1, 0.75f, true);
  expect_edge(model, 5, 5, 0, Role::BlendChild, 0, 1.0f, false);
  expect_edge(model, 6, 5, 1, Role::BlendChild, 1, 2.0f, false);
  expect_edge(model, 7, 6, 0, Role::Wrapped, 0, 0.0f, false);
  expect_edge(model, 8, 6, 1, Role::BoneSwitchChild, 0, 0.0f, true);
  expect_edge(model, 9, 7, 0, Role::Wrapped, 0, 0.0f, false);
  expect_edge(model, 10, 7, 1, Role::OffsetClip, 0, 0.0f, false);
  expect_edge(model, 11, 8, 0, Role::Wrapped, 0, 0.0f, false);
  expect_edge(model, 12, 9, 0, Role::Wrapped, 0, 0.0f, false);
  expect_edge(model, 13, 10, 1, Role::Wrapped, 0, 0.0f, false);
  expect_edge(model, 14, 11, 0, Role::Wrapped, 0, 0.0f, false);
}

TEST(HkxBehaviorGraphModel, APoseMatchingGeneratorIsNotABlenderRow) {
  const HkxGraphModel model = decode_generators();
  EXPECT_TRUE(model.blenders.empty());
  EXPECT_TRUE(model.blend_children.empty());
  EXPECT_EQ(model.clips.size(), 2U);
}

TEST(HkxBehaviorGraphModel, ASelectorEntryAfterANullOneKeepsItsListIndex) {
  const HkxGraphModel model = decode_generators();
  std::vector<std::int32_t> slots;
  for (const auto& edge : model.edges) {
    if (edge.parent == 2) slots.push_back(edge.slot);
  }
  EXPECT_EQ(slots, (std::vector<std::int32_t>{0, 2}));
}

TEST(HkxBehaviorGraphModel, AnEntryThatIsNotAGeneratorIsDroppedAndReported) {
  GeneratorFixtureOptions options;
  options.stray_selector_entry = true;
  const HkxGraphModel model = decode_generators(options);
  ASSERT_EQ(model.edges.size(), 15U);
  EXPECT_TRUE(mentions(model, "does not reach a node"));
  for (const auto& edge : model.edges) {
    EXPECT_GE(edge.child, 0);
    EXPECT_LT(static_cast<std::size_t>(edge.child), model.nodes.size());
  }
}

TEST(HkxBehaviorGraphModel, ANewKindWithAForeignSignatureKeepsItsKindAndReadsNoEdges) {
  GeneratorFixtureOptions options;
  options.tagging_signature = 1234U;
  const HkxGraphModel model = decode_generators(options);
  EXPECT_EQ(model.nodes[8].kind, HkxGraphNodeKind::StateTagging);
  EXPECT_EQ(model.edges.size(), 14U);
  for (const auto& edge : model.edges) EXPECT_NE(edge.parent, 8U);
  EXPECT_TRUE(mentions(model, "children are not read"));
}

TEST(HkxBehaviorGraphModel, ManyChildListsSharingANullFilledArrayAreCapped) {
  const char* const lists[][2] = {
      {"hkbManualSelectorGenerator", "generators"},
      {"hkbLayerGenerator", "layers"},
      {"BSBoneSwitchGenerator", "ChildrenA"},
  };
  for (const auto& list : lists) {
    SCOPED_TRACE(list[0]);
    HkxGraphModel model;
    EXPECT_EQ(decode(shared_null_array_packfile(list[0], list[1], 1, 1000), model), HkxStatus::Ok);
    EXPECT_EQ(decode(shared_null_array_packfile(list[0], list[1], 64, 1000), model), HkxStatus::BadSection);
  }
}

namespace {

const HkxGraphIssue* find_issue(const HkxGraphValidation& validation, std::string_view code) {
  for (const HkxGraphIssue& issue : validation.issues) {
    if (issue.code == code) return &issue;
  }
  return nullptr;
}

struct Defect {
  const char* code;
  void (*mutate)(HkxGraphModel&);
};

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

}  // namespace

// Constraint, regression caught: a validator that flags a healthy graph with an Error would block every save of a real behavior file; the fixture's Mix blender holds weights 1 and 3 without the parametric bit, so it also carries one blender.child_weight_range Warning.
TEST(HkxBehaviorValidation, AHealthyGraphHasNoErrorsAndOnlyWarnings) {
  const HkxGraphValidation validation = validate_behavior_model(decode_ok());
  EXPECT_EQ(validation.errors, 0U);
  EXPECT_EQ(validation.warnings, validation.issues.size());
  const HkxGraphIssue* unreachable = find_issue(validation, "node.unreachable");
  ASSERT_NE(unreachable, nullptr);
  EXPECT_EQ(unreachable->severity, HkxGraphIssueSeverity::Warning);
  EXPECT_EQ(unreachable->table, "nodes");
}

// Constraint, regression caught: a defect class that stops being detected lets a dangling reference reach a saved file.
TEST(HkxBehaviorValidation, EveryDefectClassIsReportedAsAnError) {
  const Defect defects[] = {
      {"root.missing", [](HkxGraphModel& m) { m.root_node = -1; }},
      {"state_machine.no_states", [](HkxGraphModel& m) { m.state_machines[0].state_count = 0; }},
      {"state_machine.start_state_missing", [](HkxGraphModel& m) { m.state_machines[0].start_state_id = 55; }},
      {"state_machine.event_range", [](HkxGraphModel& m) { m.state_machines[0].change_event_id = 50; }},
      {"state_machine.sync_variable_range", [](HkxGraphModel& m) { m.state_machines[0].sync_variable_index = 9; }},
      {"state_machine.state_range", [](HkxGraphModel& m) { m.state_machines[0].first_state = 5; }},
      {"state_machine.wildcard_range", [](HkxGraphModel& m) { m.state_machines[0].wildcard_count = 9; }},
      {"state.duplicate_id", [](HkxGraphModel& m) { m.states[1].state_id = 7; }},
      {"state.generator_range", [](HkxGraphModel& m) { m.states[0].generator_node = 99; }},
      {"state.transition_range", [](HkxGraphModel& m) { m.states[0].transition_count = 50; }},
      {"transition.target_missing", [](HkxGraphModel& m) { m.transitions[0].to_state_id = 42; }},
      {"transition.event_range", [](HkxGraphModel& m) { m.transitions[0].event_id = 99; }},
      {"transition.interval_event_range", [](HkxGraphModel& m) { m.transitions[0].trigger_interval.exit_event_id = 77; }},
      {"transition.effect_range", [](HkxGraphModel& m) { m.transitions[0].effect_node = 99; }},
      {"transition.state_range", [](HkxGraphModel& m) { m.transitions[0].state = 9; }},
      {"blender.sync_master_range", [](HkxGraphModel& m) { m.blenders[0].index_of_sync_master_child = 5; }},
      {"blender.variable_range", [](HkxGraphModel& m) { m.blenders[0].blend_parameter_variable = 9; }},
      {"blender.child_range", [](HkxGraphModel& m) { m.blenders[0].child_count = 99; }},
      {"blend_child.variable_range", [](HkxGraphModel& m) { m.blend_children[0].weight_variable = 9; }},
      {"blend_child.weight_invalid", [](HkxGraphModel& m) { m.blend_children[0].weight = kNaN; }},
      {"clip.trigger_event_range",
       [](HkxGraphModel& m) {
         for (auto& clip : m.clips) {
           for (auto& trigger : clip.triggers) trigger.event_id = 9;
         }
       }},
      {"clip.trigger_time_invalid",
       [](HkxGraphModel& m) {
         for (auto& clip : m.clips) {
           for (auto& trigger : clip.triggers) trigger.local_time = kNaN;
         }
       }},
      {"binding.owner_range", [](HkxGraphModel& m) { m.bindings[0].owner_index = 99; }},
      {"binding.variable_range", [](HkxGraphModel& m) { m.bindings[0].variable_index = 9; }},
      {"edge.range",
       [](HkxGraphModel& m) {
         HkxGraphEdge edge;
         edge.parent = 99;
         m.edges.push_back(edge);
       }},
      {"event.name_missing", [](HkxGraphModel& m) { m.events[1].name.clear(); }},
      {"event.duplicate_name", [](HkxGraphModel& m) { m.events[1].name = "Go"; }},
      {"variable.name_missing", [](HkxGraphModel& m) { m.variables[0].name.clear(); }},
      {"variable.duplicate_name", [](HkxGraphModel& m) { m.variables[1].name = "bFlag"; }},
      {"variable.vector_missing", [](HkxGraphModel& m) { m.variables[2].has_vector = false; }},
      {"event.table_length", [](HkxGraphModel& m) { m.event_info_count = m.event_name_count + 1; }},
      {"variable.table_length", [](HkxGraphModel& m) { m.variable_info_count = m.variable_name_count - 1; }},
      {"object.event_range", [](HkxGraphModel& m) { m.object_event_slots.push_back({3, "hkbEventDrivenModifier.activateEventId", 50}); }},
      {"object.variable_range",
       [](HkxGraphModel& m) { m.object_variable_slots.push_back({5, "hkbExpressionDataArray.expressionsData[0].assignmentVariableIndex", 9}); }},
      {"reference.dangling", [](HkxGraphModel& m) { m.pointer_faults.push_back({64, false, HkxGraphPointerFault::k_no_diagnostic, "stray"}); }},
      {"reference.wrong_class", [](HkxGraphModel& m) { m.pointer_faults.push_back({72, true, HkxGraphPointerFault::k_no_diagnostic, "wrong"}); }},
  };
  for (const Defect& defect : defects) {
    SCOPED_TRACE(defect.code);
    HkxGraphModel model = decode_ok();
    defect.mutate(model);
    const HkxGraphValidation validation = validate_behavior_model(model);
    const HkxGraphIssue* issue = find_issue(validation, defect.code);
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->severity, HkxGraphIssueSeverity::Error);
    EXPECT_GE(validation.errors, 1U);
  }
}

// Constraint, regression caught: a missing start state only matters in the default start mode; flagging a random or sync machine would refuse valid files.
TEST(HkxBehaviorValidation, AStartStateIsOnlyRequiredInTheDefaultMode) {
  HkxGraphModel model = decode_ok();
  model.state_machines[0].start_state_id = 55;
  model.state_machines[0].start_state_mode = 2;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "state_machine.start_state_missing"), nullptr);
}

// Constraint, regression caught: reachability that ignores wildcards or the order events would call states dead that the engine reaches.
TEST(HkxBehaviorValidation, UnreachableStatesAndTheirTransitionsAreWarnings) {
  HkxGraphModel model = decode_ok();
  auto& machine = model.state_machines[0];
  EXPECT_EQ(find_issue(validate_behavior_model(model), "state.unreachable"), nullptr);

  machine.random_transition_event_id = -1;
  machine.next_higher_event_id = -1;
  machine.next_lower_event_id = -1;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "state.unreachable"), nullptr);

  machine.start_state_id = 9;
  model.transitions[1].to_state_id = 9;
  const HkxGraphValidation validation = validate_behavior_model(model);
  EXPECT_EQ(validation.errors, 0U);
  const HkxGraphIssue* state = find_issue(validation, "state.unreachable");
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->severity, HkxGraphIssueSeverity::Warning);
  EXPECT_EQ(state->index, 0U);
  const HkxGraphIssue* transition = find_issue(validation, "transition.unreachable");
  ASSERT_NE(transition, nullptr);
  EXPECT_EQ(transition->index, 0U);

  machine.random_transition_event_id = 0;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "state.unreachable"), nullptr);
}

// Constraint, regression caught: the reader pads the shorter of two parallel tables, so without the raw counts a name table and an info table of different lengths would validate clean.
TEST(HkxBehaviorValidation, TheDecodedFixtureReportsEqualTableCounts) {
  const HkxGraphModel model = decode_ok();
  EXPECT_EQ(model.event_name_count, model.events.size());
  EXPECT_EQ(model.event_info_count, model.events.size());
  EXPECT_EQ(model.variable_name_count, model.variables.size());
  EXPECT_EQ(model.variable_info_count, model.variables.size());
  EXPECT_EQ(model.variable_value_count, model.variables.size());
}

// Constraint, regression caught: a variable with no stored initial value must be flagged, and a model with no value set at all must not (the fixture-less count is the sentinel).
TEST(HkxBehaviorValidation, ACountMismatchOnTheValueSetIsAWarningNotAnError) {
  HkxGraphModel model = decode_ok();
  model.variable_value_count = model.variables.size() - 1;
  const HkxGraphValidation validation = validate_behavior_model(model);
  const HkxGraphIssue* issue = find_issue(validation, "variable.value_missing");
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->severity, HkxGraphIssueSeverity::Warning);
  EXPECT_EQ(validation.errors, 0U);
  model.variable_value_count = HkxGraphModel::k_no_value_set;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "variable.value_missing"), nullptr);
}

// Constraint, regression caught: a user controlled clip with nothing on userControlledTimeFraction never advances in game; a bound or looping clip must not be flagged.
TEST(HkxBehaviorValidation, AUserControlledClipNeedsABindingOnItsTimeFraction) {
  HkxGraphModel model = decode_ok();
  model.clips[0].mode = 2;
  const HkxGraphValidation validation = validate_behavior_model(model);
  const HkxGraphIssue* issue = find_issue(validation, "clip.user_time_unbound");
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->severity, HkxGraphIssueSeverity::Warning);
  EXPECT_EQ(issue->index, 0U);
  HkxGraphBinding binding;
  binding.owner_kind = fo4::hkx::HkxGraphOwnerKind::Clip;
  binding.owner_index = 0;
  binding.member_path = "userControlledTimeFraction";
  binding.variable_index = 1;
  model.bindings.push_back(binding);
  EXPECT_EQ(find_issue(validate_behavior_model(model), "clip.user_time_unbound"), nullptr);
  model.bindings.pop_back();
  model.clips[0].mode = 1;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "clip.user_time_unbound"), nullptr);
}

// Constraint, regression caught: a sync-variable machine is entered from outside, a disabled state is never entered and is not reported, and a machine with no transitions reaches every state; reporting a state whose enable is off would flag a state its author switched off on purpose, while an enabled state nothing reaches must still be reported.
TEST(HkxBehaviorValidation, ReachabilityFollowsSyncVariableDisabledStatesAndTransitionlessMachines) {
  HkxGraphModel model = decode_ok();
  auto& machine = model.state_machines[0];
  machine.random_transition_event_id = -1;
  machine.next_higher_event_id = -1;
  machine.next_lower_event_id = -1;
  machine.start_state_id = 9;
  model.transitions[1].to_state_id = 9;
  EXPECT_NE(find_issue(validate_behavior_model(model), "state.unreachable"), nullptr);

  machine.sync_variable_index = 1;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "state.unreachable"), nullptr);
  machine.sync_variable_index = -1;

  model.states[0].enable = false;
  model.transitions[1].to_state_id = 7;
  machine.start_state_id = 7;
  const HkxGraphValidation disabled = validate_behavior_model(model);
  EXPECT_NE(find_issue(disabled, "state.unreachable"), nullptr);
  for (const HkxGraphIssue& issue : disabled.issues) {
    if (issue.code == "state.unreachable") { EXPECT_NE(issue.index, 0U); }
    if (issue.code == "transition.unreachable") {
      EXPECT_FALSE(issue.index >= model.states[0].first_transition &&
                   issue.index < model.states[0].first_transition + model.states[0].transition_count);
    }
  }
  model.states[0].enable = true;

  for (auto& transition : model.transitions) transition.to_state_id = 7;
  model.state_machines[0].wildcard_count = 0;
  for (auto& state : model.states) state.transition_count = 0;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "state.unreachable"), nullptr);
}

// Constraint, regression caught: duplicate names and rows and decoder notes are warnings; making them errors would block files the engine loads.
TEST(HkxBehaviorValidation, DuplicatesAndDecoderNotesAreWarnings) {
  HkxGraphModel model = decode_ok();
  model.states[1].name = "Idle";
  model.nodes[1].name = model.nodes[0].name;
  model.transitions.push_back(model.transitions[1]);
  model.state_machines[0].wildcard_count = 2;
  model.states[0].generator_node = -1;
  ASSERT_GT(model.blenders[0].child_count, 0U);
  model.blend_children[model.blenders[0].first_child].generator_node = -1;
  model.diagnostics.push_back("a decoder note");
  const HkxGraphValidation validation = validate_behavior_model(model);
  EXPECT_EQ(validation.errors, 0U);
  for (const char* code : {"state.duplicate_name", "node.duplicate_name", "transition.duplicate", "state.generator_missing",
                           "blend_child.generator_missing", "decode.note"}) {
    SCOPED_TRACE(code);
    const HkxGraphIssue* issue = find_issue(validation, code);
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->severity, HkxGraphIssueSeverity::Warning);
  }
}

namespace {

std::size_t count_issues(const HkxGraphValidation& validation, std::string_view code) {
  std::size_t count = 0;
  for (const HkxGraphIssue& issue : validation.issues) count += issue.code == code ? 1U : 0U;
  return count;
}

// Invariant: two events and two variables; the modifier, its binding set (one variable row, one character property row) and the expression array are objects the model does not decode, and three variable bounds exceed the two variables.
std::vector<std::byte> slot_packfile(std::int32_t event, std::int32_t variable) {
  BehaviorFixture f;
  const auto graph = f.add("hkbBehaviorGraph");
  const auto data = f.add("hkbBehaviorGraphData");
  f.point(graph + f.member("hkbBehaviorGraph", "data"), data);
  const auto strings = f.add("hkbBehaviorGraphStringData");
  f.point(data + f.member("hkbBehaviorGraphData", "stringData"), strings);
  auto block = f.array(strings + f.member("hkbBehaviorGraphStringData", "eventNames"), 2, 8);
  f.text(block, "Go");
  f.text(block + 8, "Stop");
  block = f.array(strings + f.member("hkbBehaviorGraphStringData", "variableNames"), 2, 8);
  f.text(block, "bFlag");
  f.text(block + 8, "fSpeed");
  f.array(data + f.member("hkbBehaviorGraphData", "eventInfos"), 2, fo4::hkx::find_layout("hkbEventInfo")->size);
  f.array(data + f.member("hkbBehaviorGraphData", "variableInfos"), 2, fo4::hkx::find_layout("hkbVariableInfo")->size);
  f.array(data + f.member("hkbBehaviorGraphData", "variableBounds"), 3, fo4::hkx::find_layout("hkbVariableBounds")->size);

  const auto driven = f.add("hkbEventDrivenModifier");
  f.set<std::int32_t>(driven + f.member("hkbEventDrivenModifier", "activateEventId"), event);
  f.set<std::int32_t>(driven + f.member("hkbEventDrivenModifier", "deactivateEventId"), -1);
  const auto set = f.add("hkbVariableBindingSet");
  f.point(driven + f.member("hkbBindable", "variableBindingSet"), set);
  const auto row_size = fo4::hkx::find_layout("hkbVariableBindingSetBinding")->size;
  const auto rows = f.array(set + f.member("hkbVariableBindingSet", "bindings"), 2, row_size);
  f.set<std::int32_t>(rows + f.member("hkbVariableBindingSetBinding", "variableIndex"), variable);
  f.set<std::int32_t>(rows + row_size + f.member("hkbVariableBindingSetBinding", "variableIndex"), 99);
  f.set<std::int8_t>(rows + row_size + f.member("hkbVariableBindingSetBinding", "bindingType"), 1);

  const auto expressions = f.add("hkbExpressionDataArray");
  const auto expression_rows = f.array(expressions + f.member("hkbExpressionDataArray", "expressionsData"), 1,
                                       fo4::hkx::find_layout("hkbExpressionData")->size);
  f.set<std::int32_t>(expression_rows + f.member("hkbExpressionData", "assignmentVariableIndex"), variable);
  f.set<std::int32_t>(expression_rows + f.member("hkbExpressionData", "assignmentEventIndex"), event);
  return f.bytes();
}

// Invariant: an outer machine (nodes 0, states 0 and 1) whose transition into state id 1 carries the nested flag, a modifier node 1 wrapping node 2, and an inner machine (states ids 4 and 5) that is checked because its one transition loops on state 4.
HkxGraphModel nested_model() {
  HkxGraphModel m;
  const auto node = [&](HkxGraphNodeKind kind, std::size_t kind_index) {
    fo4::hkx::HkxGraphNode n;
    n.kind = kind;
    n.kind_index = kind_index;
    n.name = "n" + std::to_string(m.nodes.size());
    m.nodes.push_back(n);
  };
  node(HkxGraphNodeKind::StateMachine, 0);
  node(HkxGraphNodeKind::ModifierGenerator, 0);
  node(HkxGraphNodeKind::StateMachine, 1);
  m.root_node = 0;
  HkxGraphEdge wrap;
  wrap.parent = 1;
  wrap.child = 2;
  wrap.role = HkxGraphEdgeRole::Wrapped;
  m.edges.push_back(wrap);

  const auto state = [&](std::size_t machine, std::int32_t id, std::int32_t generator, std::size_t first, std::size_t count) {
    fo4::hkx::HkxGraphState s;
    s.state_machine = machine;
    s.state_id = id;
    s.name = "s" + std::to_string(id);
    s.generator_node = generator;
    s.first_transition = first;
    s.transition_count = count;
    m.states.push_back(s);
  };
  state(0, 0, 1, 0, 1);
  state(0, 1, 1, 1, 0);
  state(1, 4, 1, 1, 1);
  state(1, 5, 1, 2, 0);

  HkxGraphTransition into;
  into.state_machine = 0;
  into.state = 0;
  into.to_state_id = 1;
  into.flags = 0x2000;
  into.to_nested_state_id = 5;
  m.transitions.push_back(into);
  HkxGraphTransition loop;
  loop.state_machine = 1;
  loop.state = 2;
  loop.to_state_id = 4;
  m.transitions.push_back(loop);

  fo4::hkx::HkxGraphStateMachine outer;
  outer.node = 0;
  outer.start_state_id = 0;
  outer.first_state = 0;
  outer.state_count = 2;
  m.state_machines.push_back(outer);
  fo4::hkx::HkxGraphStateMachine inner;
  inner.node = 2;
  inner.start_state_id = 4;
  inner.first_state = 2;
  inner.state_count = 2;
  m.state_machines.push_back(inner);
  return m;
}

std::vector<std::size_t> unreachable_states(const HkxGraphModel& model) {
  std::vector<std::size_t> out;
  for (const HkxGraphIssue& issue : validate_behavior_model(model).issues) {
    if (issue.code == "state.unreachable") out.push_back(issue.index);
  }
  return out;
}

}  // namespace

// Constraint, regression caught: an event or variable index held by a modifier, an expression or an undecoded binding set that points past the table loads as a wrong symbol in the engine; a character property binding row indexes another table and must not be judged against the variables.
TEST(HkxBehaviorValidation, EventAndVariableIndexesOfUndecodedObjectsAreRangeCheckedAndInRangeOnesStayClean) {
  HkxGraphModel broken;
  ASSERT_EQ(decode(slot_packfile(5, 9), broken), HkxStatus::Ok);
  ASSERT_EQ(broken.object_event_slots.size(), 2U);
  EXPECT_EQ(broken.object_event_slots[0].member, "hkbEventDrivenModifier.activateEventId");
  EXPECT_EQ(broken.object_event_slots[1].member, "hkbExpressionDataArray.expressionsData[0].assignmentEventIndex");
  ASSERT_EQ(broken.object_variable_slots.size(), 2U);
  EXPECT_EQ(broken.object_variable_slots[0].member, "hkbVariableBindingSet.bindings[0].variableIndex");
  EXPECT_EQ(broken.object_variable_slots[1].member, "hkbExpressionDataArray.expressionsData[0].assignmentVariableIndex");
  const HkxGraphValidation validation = validate_behavior_model(broken);
  EXPECT_EQ(count_issues(validation, "object.event_range"), 2U);
  EXPECT_EQ(count_issues(validation, "object.variable_range"), 2U);
  for (const HkxGraphIssue& issue : validation.issues) {
    if (issue.code.rfind("object.", 0) != 0) continue;
    EXPECT_EQ(issue.severity, HkxGraphIssueSeverity::Error);
    EXPECT_EQ(issue.table, "objects");
  }

  HkxGraphModel clean;
  ASSERT_EQ(decode(slot_packfile(1, 1), clean), HkxStatus::Ok);
  const HkxGraphValidation healthy = validate_behavior_model(clean);
  EXPECT_EQ(count_issues(healthy, "object.event_range"), 0U);
  EXPECT_EQ(count_issues(healthy, "object.variable_range"), 0U);
}

// Constraint, regression caught: the binding sets the model already decodes are checked by binding.variable_range; reading them again as object slots would report every bad binding twice.
TEST(HkxBehaviorGraphModel, BindingSetsTheModelDecodesAreNotReadAgainAsObjectSlots) {
  const HkxGraphModel model = decode_ok();
  ASSERT_FALSE(model.bindings.empty());
  EXPECT_TRUE(model.object_variable_slots.empty());
  EXPECT_TRUE(model.object_event_slots.empty());
}

// Constraint, regression caught: a pointer that lands off an object or on another class is dropped by the reader; without an Error the file saves with a state or table silently missing, and its decoder note must not also appear as a second warning.
TEST(HkxBehaviorValidation, DroppedPointersAreErrorsAndTheirDecoderNoteIsNotRepeated) {
  BehaviorFixtureOptions stray;
  stray.stray_state_pointer = true;
  const HkxGraphModel off_object = decode_ok(stray);
  ASSERT_EQ(off_object.pointer_faults.size(), 1U);
  EXPECT_FALSE(off_object.pointer_faults[0].wrong_class);
  ASSERT_LT(off_object.pointer_faults[0].diagnostic, off_object.diagnostics.size());
  EXPECT_EQ(off_object.diagnostics[off_object.pointer_faults[0].diagnostic], off_object.pointer_faults[0].message);
  const HkxGraphValidation dangling = validate_behavior_model(off_object);
  const HkxGraphIssue* issue = find_issue(dangling, "reference.dangling");
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->severity, HkxGraphIssueSeverity::Error);
  EXPECT_EQ(issue->table, "pointers");
  EXPECT_EQ(issue->index, off_object.pointer_faults[0].offset);
  for (const HkxGraphIssue& other : dangling.issues) {
    if (other.code == "decode.note") { EXPECT_NE(other.message, off_object.pointer_faults[0].message); }
  }

  BehaviorFixtureOptions wrong;
  wrong.wrong_class_state = true;
  const HkxGraphModel other_class = decode_ok(wrong);
  ASSERT_EQ(other_class.pointer_faults.size(), 1U);
  EXPECT_TRUE(other_class.pointer_faults[0].wrong_class);
  const HkxGraphValidation wrong_class_validation = validate_behavior_model(other_class);
  const HkxGraphIssue* mistyped = find_issue(wrong_class_validation, "reference.wrong_class");
  ASSERT_NE(mistyped, nullptr);
  EXPECT_EQ(mistyped->severity, HkxGraphIssueSeverity::Error);

  const HkxGraphModel healthy = decode_ok();
  EXPECT_TRUE(healthy.pointer_faults.empty());
  EXPECT_EQ(find_issue(validate_behavior_model(healthy), "reference.dangling"), nullptr);
  EXPECT_EQ(find_issue(validate_behavior_model(healthy), "reference.wrong_class"), nullptr);

  HkxGraphModel unmirrored = decode_ok();
  const std::size_t notes = count_issues(validate_behavior_model(unmirrored), "decode.note");
  unmirrored.pointer_faults.push_back({64, false, HkxGraphPointerFault::k_no_diagnostic, "cut"});
  unmirrored.diagnostics.push_back("a note that only the diagnostics carry");
  const HkxGraphValidation kept = validate_behavior_model(unmirrored);
  EXPECT_EQ(count_issues(kept, "decode.note"), notes + 1U);
}

// Constraint, regression caught: values or bounds past the variable table are leftovers an editor may have orphaned; they are warnings, and a table of exactly the right length must stay clean.
TEST(HkxBehaviorValidation, SurplusValuesAndBoundsAreWarningsAndMatchingCountsAreClean) {
  HkxGraphModel decoded;
  ASSERT_EQ(decode(slot_packfile(1, 1), decoded), HkxStatus::Ok);
  EXPECT_EQ(decoded.variable_bounds_count, 3U);
  const HkxGraphValidation bounds_validation = validate_behavior_model(decoded);
  const HkxGraphIssue* bounds = find_issue(bounds_validation, "variable.bounds_surplus");
  ASSERT_NE(bounds, nullptr);
  EXPECT_EQ(bounds->severity, HkxGraphIssueSeverity::Warning);
  decoded.variable_bounds_count = decoded.variable_name_count;
  EXPECT_EQ(find_issue(validate_behavior_model(decoded), "variable.bounds_surplus"), nullptr);

  HkxGraphModel model = decode_ok();
  EXPECT_EQ(find_issue(validate_behavior_model(model), "variable.value_surplus"), nullptr);
  model.variable_value_count = model.variables.size() + 1;
  const HkxGraphValidation validation = validate_behavior_model(model);
  const HkxGraphIssue* values = find_issue(validation, "variable.value_surplus");
  ASSERT_NE(values, nullptr);
  EXPECT_EQ(values->severity, HkxGraphIssueSeverity::Warning);
  EXPECT_EQ(validation.errors, 0U);
  model.variable_value_count = HkxGraphModel::k_no_value_set;
  EXPECT_EQ(find_issue(validate_behavior_model(model), "variable.value_surplus"), nullptr);
}

// Constraint, regression caught: a state only a nested transition enters is live; reporting it would flag every nested machine, while a nested flag that is off, a disabled target or an unreached outer machine must still leave it reported.
TEST(HkxBehaviorValidation, ANestedEntryReachesTheInnerStateThroughWrappersAndFromOpenMachines) {
  HkxGraphModel model = nested_model();
  EXPECT_TRUE(unreachable_states(model).empty());

  model.states[1].generator_node = 2;
  EXPECT_TRUE(unreachable_states(model).empty());
  model.states[1].generator_node = 1;

  model.transitions[0].flags = 0;
  EXPECT_EQ(unreachable_states(model), std::vector<std::size_t>{3});
  model.transitions[0].flags = 0x2000;

  model.states[1].enable = false;
  EXPECT_EQ(unreachable_states(model), std::vector<std::size_t>{3});
  model.states[1].enable = true;

  model.edges.clear();
  EXPECT_EQ(unreachable_states(model), std::vector<std::size_t>{3});
  HkxGraphEdge wrap;
  wrap.parent = 1;
  wrap.child = 2;
  wrap.role = HkxGraphEdgeRole::Wrapped;
  model.edges.push_back(wrap);

  model.state_machines[0].has_start_state_selector = true;
  EXPECT_TRUE(unreachable_states(model).empty());
  EXPECT_EQ(validate_behavior_model(model).errors, 0U);
}

// Constraint, regression caught: a nested entry follows six wrapper hops to the inner machine, as the BGS route does; stopping one hop short would flag live inner states, and following more would hide dead ones.
TEST(HkxBehaviorValidation, ANestedEntrySeesTheInnerMachineSixWrapperHopsDownAndNoFurther) {
  const auto with_hops = [](std::size_t hops) {
    HkxGraphModel model = nested_model();
    model.edges.clear();
    std::int32_t from = 1;
    for (std::size_t i = 1; i < hops; ++i) {
      fo4::hkx::HkxGraphNode wrapper;
      wrapper.kind = HkxGraphNodeKind::ModifierGenerator;
      wrapper.name = "w" + std::to_string(i);
      model.nodes.push_back(wrapper);
      const auto at = static_cast<std::int32_t>(model.nodes.size() - 1);
      HkxGraphEdge edge;
      edge.parent = static_cast<std::size_t>(from);
      edge.child = at;
      edge.role = HkxGraphEdgeRole::Wrapped;
      model.edges.push_back(edge);
      from = at;
    }
    HkxGraphEdge last;
    last.parent = static_cast<std::size_t>(from);
    last.child = 2;
    last.role = HkxGraphEdgeRole::Wrapped;
    model.edges.push_back(last);
    return model;
  };
  EXPECT_TRUE(unreachable_states(with_hops(1)).empty());
  EXPECT_TRUE(unreachable_states(with_hops(6)).empty());
  EXPECT_EQ(unreachable_states(with_hops(7)), std::vector<std::size_t>{3});
}

// Constraint, regression caught: a machine whose start state a selector picks is entered from outside, and a wildcard cannot fire before any state is current; reporting the first or trusting the second would flag live states or hide dead ones.
TEST(HkxBehaviorValidation, ASelectedStartOpensTheMachineAndAWildcardNeedsACurrentState) {
  BehaviorFixtureOptions options;
  options.start_selector = true;
  HkxGraphModel model = decode_ok(options);
  EXPECT_TRUE(model.state_machines[0].has_start_state_selector);
  EXPECT_FALSE(decode_ok().state_machines[0].has_start_state_selector);
  auto& machine = model.state_machines[0];
  machine.random_transition_event_id = -1;
  machine.next_higher_event_id = -1;
  machine.next_lower_event_id = -1;
  machine.start_state_id = 9;
  model.transitions[1].to_state_id = 9;
  EXPECT_TRUE(unreachable_states(model).empty());
  machine.has_start_state_selector = false;
  EXPECT_EQ(unreachable_states(model), std::vector<std::size_t>{0});

  machine.start_state_id = 7;
  model.states[0].enable = false;
  model.transitions[1].to_state_id = 9;
  EXPECT_EQ(unreachable_states(model), std::vector<std::size_t>{1});
  model.states[0].enable = true;
  EXPECT_TRUE(unreachable_states(model).empty());
}

// Constraint, regression caught: a start state that names no state is already an Error; also marking every state unreachable buries that one finding under a warning per state.
TEST(HkxBehaviorValidation, AStartStateThatNamesNoStateDoesNotFloodUnreachableWarnings) {
  HkxGraphModel model = decode_ok();
  auto& machine = model.state_machines[0];
  machine.random_transition_event_id = -1;
  machine.next_higher_event_id = -1;
  machine.next_lower_event_id = -1;
  machine.start_state_id = 55;
  const HkxGraphValidation validation = validate_behavior_model(model);
  EXPECT_NE(find_issue(validation, "state_machine.start_state_missing"), nullptr);
  EXPECT_EQ(count_issues(validation, "state.unreachable"), 0U);
  EXPECT_EQ(count_issues(validation, "transition.unreachable"), 0U);
}

namespace {

void expect_warning(const HkxGraphModel& model, std::string_view code, std::string_view table) {
  const HkxGraphValidation validation = validate_behavior_model(model);
  const HkxGraphIssue* issue = find_issue(validation, code);
  ASSERT_NE(issue, nullptr) << code;
  EXPECT_EQ(issue->severity, HkxGraphIssueSeverity::Warning) << code;
  EXPECT_EQ(issue->table, table) << code;
}

void expect_absent(const HkxGraphModel& model, std::string_view code) {
  EXPECT_EQ(find_issue(validate_behavior_model(model), code), nullptr) << code;
}

}  // namespace

// Constraint, regression caught: the engine rejects an empty blender, a motion weight outside 0 to 1 and, unless the parametric bit is set, a child weight outside 0 to 1; a validator that stays silent lets such a blender save, and one that also flags parametric blenders or weights a variable drives would flag shipped graphs (UNVERIFIED engine evidence, see the code note).
TEST(HkxBehaviorValidation, BlenderChecksFromTheEngineAreWarningsAndParametricOrDrivenWeightsStayClean) {
  HkxGraphModel model = decode_ok();
  const HkxGraphValidation base = validate_behavior_model(model);
  EXPECT_EQ(count_issues(base, "blender.child_weight_range"), 1U);
  EXPECT_EQ(count_issues(base, "blender.motion_weight_range"), 0U);
  EXPECT_EQ(count_issues(base, "blender.no_children"), 0U);

  const std::size_t mix = model.blenders[0].first_child;
  const std::size_t along = model.blenders[1].first_child;
  EXPECT_TRUE(model.blenders[1].parametric);
  expect_warning(model, "blender.child_weight_range", "blend_children");
  model.blend_children[mix + 1].weight = 0.75f;
  expect_absent(model, "blender.child_weight_range");
  model.blend_children[mix + 1].weight = -0.5f;
  expect_warning(model, "blender.child_weight_range", "blend_children");
  model.blend_children[mix + 1].weight = 3.0f;
  model.blend_children[mix + 1].weight_variable = 0;
  expect_absent(model, "blender.child_weight_range");
  model.blend_children[mix + 1].weight_variable = -1;

  model.blenders[1].parametric = false;
  EXPECT_EQ(count_issues(validate_behavior_model(model), "blender.child_weight_range"), 3U);
  model.blenders[1].parametric = true;
  EXPECT_GT(model.blend_children[along + 1].weight, 1.0f);

  model.blend_children[mix + 1].weight = 0.75f;
  expect_absent(model, "blender.motion_weight_range");
  model.blend_children[mix].world_from_model_weight = 0.0f;
  model.blend_children[mix + 1].world_from_model_weight = 1.0f;
  expect_absent(model, "blender.motion_weight_range");
  model.blend_children[mix].world_from_model_weight = 1.5f;
  expect_warning(model, "blender.motion_weight_range", "blend_children");
  model.blend_children[mix].world_from_model_weight = -0.1f;
  expect_warning(model, "blender.motion_weight_range", "blend_children");
  model.blend_children[mix].world_from_model_weight = kNaN;
  expect_warning(model, "blender.motion_weight_range", "blend_children");
  model.blend_children[mix].world_from_model_weight = 1.0f;

  model.blenders[2].child_count = 0;
  expect_warning(model, "blender.no_children", "blenders");
}

// Constraint, regression caught: the engine rejects a machine over 0x8000 states, a chooser start mode with no chooser and an eventless transition with no condition; each must be reported by id as a Warning, while 0x8000 states, a chooser that exists and an eventless transition that has a condition stay clean.
TEST(HkxBehaviorValidation, StateMachineAndTransitionChecksFromTheEngineAreWarningsAndTheirLimitsStayClean) {
  HkxGraphModel model = decode_ok();
  expect_absent(model, "state_machine.chooser_missing");
  expect_absent(model, "transition.eventless_no_condition");
  expect_absent(model, "state_machine.too_many_states");

  model.state_machines[0].start_state_mode = 3;
  expect_warning(model, "state_machine.chooser_missing", "state_machines");
  model.state_machines[0].has_start_state_selector = true;
  expect_absent(model, "state_machine.chooser_missing");
  model.state_machines[0].has_start_state_selector = false;
  model.state_machines[0].start_state_mode = 0;
  expect_absent(model, "state_machine.chooser_missing");

  EXPECT_EQ(model.transitions[0].condition_kind, HkxGraphConditionKind::String);
  model.transitions[0].event_id = -1;
  expect_absent(model, "transition.eventless_no_condition");
  model.transitions[0].condition_kind = HkxGraphConditionKind::None;
  expect_warning(model, "transition.eventless_no_condition", "transitions");
  expect_absent(model, "transition.event_range");
  EXPECT_EQ(count_issues(validate_behavior_model(model), "transition.eventless_no_condition"), 1U);
  model.transitions[0].condition_kind = HkxGraphConditionKind::Expression;
  expect_absent(model, "transition.eventless_no_condition");
  model.transitions[0].event_id = 1;

  const HkxGraphState copy = model.states[1];
  model.states.resize(0x8000, copy);
  model.state_machines[0].state_count = 0x8000;
  expect_absent(model, "state_machine.too_many_states");
  model.states.push_back(copy);
  model.state_machines[0].state_count = 0x8001;
  expect_warning(model, "state_machine.too_many_states", "state_machines");
}

// Constraint, regression caught: the engine rejects a selector with no generator, a null generator or a selected index outside the generator list; the raw slot count and the null slots must reach the validator, and an index inside a list with no null slot must stay clean.
TEST(HkxBehaviorValidation, SelectorChecksFromTheEngineAreWarningsAndAnInRangeIndexStaysClean) {
  HkxGraphModel model = decode_generators();
  ASSERT_EQ(model.selectors.size(), 1U);
  EXPECT_EQ(model.selectors[0].node, 2U);
  EXPECT_EQ(model.selectors[0].generator_count, 3U);
  EXPECT_EQ(model.selectors[0].null_generator_count, 1U);
  EXPECT_EQ(model.selectors[0].selected_generator_index, 0);
  expect_warning(model, "selector.null_child", "selectors");
  expect_absent(model, "selector.index_range");
  expect_absent(model, "selector.no_children");

  model.selectors[0].null_generator_count = 0;
  model.selectors[0].selected_generator_index = 2;
  expect_absent(model, "selector.null_child");
  expect_absent(model, "selector.index_range");
  model.selectors[0].selected_generator_index = 3;
  expect_warning(model, "selector.index_range", "selectors");
  model.selectors[0].selected_generator_index = -1;
  expect_warning(model, "selector.index_range", "selectors");

  model.selectors[0].generator_count = 0;
  expect_warning(model, "selector.no_children", "selectors");
  expect_absent(model, "selector.index_range");
}

// Constraint, regression caught: a clip with a non-negative animation binding index skips the engine's name lookup and binds whatever sits in that slot, so the reader must carry the raw value; a clip at -1, as every shipped clip is, must stay clean.
TEST(HkxBehaviorValidation, AClipBindingIndexOtherThanMinusOneIsAWarning) {
  HkxGraphModel model = decode_ok();
  for (const HkxGraphClip& clip : model.clips) EXPECT_EQ(clip.animation_binding_index, -1);
  expect_absent(model, "clip.binding_index_set");
  EXPECT_EQ(decode_generators().clips[0].animation_binding_index, 0);
  model.clips[1].animation_binding_index = 4;
  expect_warning(model, "clip.binding_index_set", "clips");
  EXPECT_EQ(count_issues(validate_behavior_model(model), "clip.binding_index_set"), 1U);
  model.clips[1].animation_binding_index = -2;
  expect_warning(model, "clip.binding_index_set", "clips");
}

// Constraint, regression caught: the editor patches the row the reader decoded, so a wrong locator would write another row or another file position.
TEST(HkxBehaviorGraphModel, TransitionsAndStatesCarryTheirFileLocators) {
  const auto bytes = behavior_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxGraphModel model;
  ASSERT_EQ(read_behavior_model(packfile, model), HkxStatus::Ok);
  const auto* layout = fo4::hkx::find_layout("hkbStateMachineTransitionInfo");
  const std::size_t event_at = *fo4::hkx::find_member_offset(*layout, "eventId");
  for (const auto& transition : model.transitions) {
    EXPECT_EQ(packfile.objects[transition.array_object].class_name, "hkbStateMachineTransitionInfoArray");
    std::int32_t event = 0;
    std::memcpy(&event, packfile.sections[packfile.data_section].data.data() + transition.offset + event_at, sizeof event);
    EXPECT_EQ(event, transition.event_id);
  }
  for (const auto& state : model.states) {
    EXPECT_EQ(packfile.objects[state.object].class_name, "hkbStateMachineStateInfo");
  }
}

namespace {

struct EditRun {
  fo4::hkx::HkxBehaviorEditReport report;
  std::vector<std::byte> out;
  HkxGraphModel model;
};

EditRun run_edits(const std::vector<std::byte>& bytes, const std::vector<HkxBehaviorEdit>& edits) {
  EditRun run;
  run.report = apply_behavior_edits(bytes, edits, run.out);
  if (run.report.ok) { EXPECT_EQ(decode(run.out, run.model), HkxStatus::Ok); }
  return run;
}

HkxGraphTransition row(std::int32_t event, std::int32_t to) {
  HkxGraphTransition transition;
  transition.event_id = event;
  transition.to_state_id = to;
  transition.priority = 4;
  transition.flags = 0x0800;
  return transition;
}

HkxBehaviorEdit transition_edit(HkxBehaviorEditKind kind, std::int32_t state, std::size_t index,
                                const HkxGraphTransition& transition = {}) {
  HkxBehaviorEdit edit;
  edit.kind = kind;
  edit.state = state;
  edit.index = index;
  edit.transition = transition;
  return edit;
}

HkxBehaviorEdit event_edit(HkxBehaviorEditKind kind, std::size_t index, const std::string& name, std::uint32_t flags = 0) {
  HkxBehaviorEdit edit;
  edit.kind = kind;
  edit.index = index;
  edit.name = name;
  edit.flags = flags;
  return edit;
}

HkxBehaviorEdit value_edit(HkxBehaviorEditKind kind, std::int32_t value, std::size_t machine = 0) {
  HkxBehaviorEdit edit;
  edit.kind = kind;
  edit.state_machine = machine;
  edit.value = value;
  return edit;
}

HkxBehaviorEdit state_edit(HkxBehaviorEditKind kind, std::int32_t state, const std::string& name = "", std::int32_t id = 0,
                           std::int32_t node = -1, std::size_t machine = 0) {
  HkxBehaviorEdit edit;
  edit.kind = kind;
  edit.state_machine = machine;
  edit.state = state;
  edit.name = name;
  edit.value = id;
  edit.node = node;
  return edit;
}

HkxBehaviorEdit binding_row_edit(HkxBehaviorEditKind kind, std::size_t index, std::int32_t value = 0,
                                 std::uint32_t owner_kind = 0, const std::string& path = "") {
  HkxBehaviorEdit edit;
  edit.kind = kind;
  edit.index = index;
  edit.value = value;
  edit.flags = owner_kind;
  edit.name = path;
  return edit;
}

std::vector<std::byte> with_wildcard_event(const std::vector<std::byte>& bytes, std::int32_t event) {
  HkxPackfile packfile;
  HkxGraphModel model;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_EQ(read_behavior_model(packfile, model), HkxStatus::Ok);
  const auto* layout = fo4::hkx::find_layout("hkbStateMachineTransitionInfo");
  const std::size_t at = model.transitions[1].offset + *fo4::hkx::find_member_offset(*layout, "eventId");
  std::memcpy(packfile.sections[packfile.data_section].data.data() + at, &event, sizeof event);
  std::vector<std::byte> out;
  EXPECT_EQ(fo4::hkx::rewrite_packfile(bytes, packfile, out), HkxStatus::Ok);
  return out;
}

std::vector<std::byte> with_shared_array(const std::vector<std::byte>& bytes) {
  HkxPackfile packfile;
  HkxGraphModel model;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_EQ(read_behavior_model(packfile, model), HkxStatus::Ok);
  const auto* layout = fo4::hkx::find_layout("hkbStateMachineStateInfo");
  const std::size_t field = packfile.objects[model.states[1].object].offset + *fo4::hkx::find_member_offset(*layout, "transitions");
  const std::size_t array = packfile.objects[model.transitions[0].array_object].offset;
  packfile.sections[packfile.data_section].global_fixups.push_back(
      {static_cast<std::uint32_t>(field), static_cast<std::uint32_t>(packfile.data_section), static_cast<std::uint32_t>(array)});
  std::vector<std::byte> out;
  EXPECT_EQ(fo4::hkx::rewrite_packfile(bytes, packfile, out), HkxStatus::Ok);
  return out;
}

}  // namespace

// Constraint, regression caught: patching a row in place must not disturb its effect, its condition or any other row.
TEST(HkxBehaviorEdit, SetTransitionChangesOnlyThatRowAndSurvivesReopen) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  HkxGraphTransition changed = before.transitions[0];
  changed.event_id = 2;
  changed.to_state_id = 7;
  changed.priority = 9;
  changed.trigger_interval.enter_time = 1.5f;
  const EditRun run = run_edits(bytes, {transition_edit(HkxBehaviorEditKind::SetTransition, 0, 0, changed)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.transitions.size(), 2U);
  const auto& row0 = run.model.transitions[0];
  EXPECT_EQ(row0.event_id, 2);
  EXPECT_EQ(row0.to_state_id, 7);
  EXPECT_EQ(row0.priority, 9);
  EXPECT_FLOAT_EQ(row0.trigger_interval.enter_time, 1.5f);
  EXPECT_EQ(row0.flags, before.transitions[0].flags);
  EXPECT_EQ(row0.effect_node, before.transitions[0].effect_node);
  EXPECT_EQ(row0.condition, "x > 1");
  EXPECT_EQ(run.model.transitions[1].condition, "fSpeed > 0");
  EXPECT_EQ(run.model.transitions[1].to_state_id, 7);
}

// Constraint, regression caught: an add that grows an array in place would overrun into the next object, and the relocation must carry the effect and condition pointers of the rows it moves.
TEST(HkxBehaviorEdit, AddTransitionInsertsBeforeAnExistingRowAndKeepsItsPointers) {
  const auto bytes = behavior_graph_packfile();
  const EditRun run = run_edits(bytes, {transition_edit(HkxBehaviorEditKind::AddTransition, 0, 0, row(2, 7))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.transitions.size(), 3U);
  EXPECT_EQ(run.model.states[0].transition_count, 2U);
  const auto& added = run.model.transitions[0];
  EXPECT_EQ(added.event_id, 2);
  EXPECT_EQ(added.to_state_id, 7);
  EXPECT_EQ(added.priority, 4);
  EXPECT_EQ(added.flags, 0x0800U);
  EXPECT_EQ(added.effect_node, -1);
  EXPECT_EQ(added.condition_kind, HkxGraphConditionKind::None);
  const auto& moved = run.model.transitions[1];
  EXPECT_EQ(moved.event_id, 1);
  EXPECT_EQ(moved.to_state_id, 9);
  ASSERT_GE(moved.effect_node, 0);
  EXPECT_EQ(run.model.nodes[static_cast<std::size_t>(moved.effect_node)].name, "fade");
  EXPECT_EQ(moved.condition, "x > 1");
  EXPECT_EQ(run.model.transitions[2].condition, "fSpeed > 0");
}

// Constraint, regression caught: a state with no transition array must get a fresh one; refusing it would make an empty state uneditable.
TEST(HkxBehaviorEdit, AddTransitionToAStateWithoutAnArrayCreatesOneAndKeepsEveryOtherObject) {
  const auto bytes = behavior_graph_packfile();
  const EditRun run = run_edits(bytes, {transition_edit(HkxBehaviorEditKind::AddTransition, 1, 0, row(2, 7))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  ASSERT_EQ(run.model.transitions.size(), 3U);
  EXPECT_EQ(run.model.states[1].transition_count, 1U);
  const auto& added = run.model.transitions[run.model.states[1].first_transition];
  EXPECT_EQ(added.state, 1);
  EXPECT_EQ(added.event_id, 2);
  EXPECT_EQ(added.to_state_id, 7);
  EXPECT_EQ(run.model.transitions[0].condition, "x > 1");
  EXPECT_EQ(run.model.transitions[2].condition, "fSpeed > 0");

  HkxPackfile original;
  HkxPackfile edited;
  ASSERT_EQ(read_packfile(bytes, original), HkxStatus::Ok);
  ASSERT_EQ(read_packfile(run.out, edited), HkxStatus::Ok);
  ASSERT_EQ(edited.objects.size(), original.objects.size() + 1);
  for (std::size_t i = 0; i < original.objects.size(); ++i) {
    const std::string_view name = original.objects[i].class_name;
    if (name == "hkbStateMachineTransitionInfoArray" || name == "hkbBehaviorGraphData" || name == "hkbBehaviorGraphStringData") {
      continue;
    }
    ASSERT_EQ(edited.objects[i].class_name, name);
    ASSERT_EQ(edited.objects[i].offset, original.objects[i].offset);
    const auto size = fo4::hkx::find_layout(name)->size;
    EXPECT_EQ(std::memcmp(edited.objects[i].data.data(), original.objects[i].data.data(), size), 0) << name;
  }
}

// Constraint, regression caught: a wildcard run is a separate array owned by the machine; appending to it must not touch the state runs.
TEST(HkxBehaviorEdit, AddTransitionAppendsToTheWildcardRun) {
  const EditRun run = run_edits(behavior_graph_packfile(), {transition_edit(HkxBehaviorEditKind::AddTransition, -1, 1, row(2, 9))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  const auto& machine = run.model.state_machines[0];
  ASSERT_EQ(machine.wildcard_count, 2U);
  EXPECT_EQ(run.model.transitions[machine.first_wildcard].condition, "fSpeed > 0");
  EXPECT_EQ(run.model.transitions[machine.first_wildcard + 1].to_state_id, 9);
  EXPECT_EQ(run.model.transitions[machine.first_wildcard + 1].state, -1);
  EXPECT_EQ(run.model.states[0].transition_count, 1U);
}

// Constraint, regression caught: a row added into a run that a remove just emptied must carry only its own payload, not the effect or condition of the row that was removed.
TEST(HkxBehaviorEdit, RemoveThenAddInOneBatchReusesTheRunWithoutStaleData) {
  const EditRun run = run_edits(behavior_graph_packfile(),
                                {transition_edit(HkxBehaviorEditKind::RemoveTransition, 0, 0),
                                 transition_edit(HkxBehaviorEditKind::AddTransition, 0, 0, row(2, 9))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  ASSERT_EQ(run.model.transitions.size(), 2U);
  const auto& added = run.model.transitions[0];
  EXPECT_EQ(added.event_id, 2);
  EXPECT_EQ(added.effect_node, -1);
  EXPECT_EQ(added.condition_kind, HkxGraphConditionKind::None);
  EXPECT_EQ(run.model.transitions[1].condition, "fSpeed > 0");

  const EditRun removed = run_edits(behavior_graph_packfile(), {transition_edit(HkxBehaviorEditKind::RemoveTransition, 0, 0)});
  ASSERT_TRUE(removed.report.ok) << removed.report.refusal;
  EXPECT_EQ(removed.model.states[0].transition_count, 0U);
  ASSERT_EQ(removed.model.transitions.size(), 1U);
  EXPECT_EQ(removed.model.transitions[0].condition, "fSpeed > 0");
}

// Constraint, regression caught: both event arrays must grow together, or the new event has a name and no flags and every later event shifts.
TEST(HkxBehaviorEdit, AddEventGrowsNamesAndInfosAndALaterEditCanUseIt) {
  const auto bytes = behavior_graph_packfile();
  const EditRun run = run_edits(bytes, {event_edit(HkxBehaviorEditKind::AddEvent, 0, "Jump", 7),
                                        transition_edit(HkxBehaviorEditKind::AddTransition, 1, 0, row(3, 7))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.events.size(), 4U);
  EXPECT_EQ(run.model.events[0].name, "Go");
  EXPECT_EQ(run.model.events[2].name, "Wave");
  EXPECT_EQ(run.model.events[0].flags, 0x100U);
  EXPECT_EQ(run.model.events[2].flags, 0x102U);
  EXPECT_EQ(run.model.events[3].name, "Jump");
  EXPECT_EQ(run.model.events[3].flags, 7U);
  EXPECT_EQ(run.model.variables.size(), 3U);

  const EditRun without = run_edits(bytes, {transition_edit(HkxBehaviorEditKind::AddTransition, 1, 0, row(3, 7))});
  EXPECT_FALSE(without.report.ok);
}

// Constraint, regression caught: a rename must repoint only that name and keep the info row, so flags do not move to another event.
TEST(HkxBehaviorEdit, RenameEventAndSetFlagsKeepTheOtherEvents) {
  const EditRun run = run_edits(behavior_graph_packfile(), {event_edit(HkxBehaviorEditKind::RenameEvent, 1, "Halt"),
                                                            event_edit(HkxBehaviorEditKind::SetEventFlags, 2, "", 0x55)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  ASSERT_EQ(run.model.events.size(), 3U);
  EXPECT_EQ(run.model.events[0].name, "Go");
  EXPECT_EQ(run.model.events[1].name, "Halt");
  EXPECT_EQ(run.model.events[1].flags, 0x101U);
  EXPECT_EQ(run.model.events[2].name, "Wave");
  EXPECT_EQ(run.model.events[2].flags, 0x55U);
  EXPECT_EQ(run.model.transitions.size(), 2U);
}

// Constraint, regression caught: a writer that restates the values a file already holds must hand back the same packfile model, so a save that drops a section, reorders a fixup table or loses an object cannot hide behind an edit that changed nothing.
TEST(HkxBehaviorEdit, RestatingAnEventFlagAndTheStartStateWritesTheSamePackfileModel) {
  const BehaviorNoOp noop = behavior_noop(behavior_graph_packfile());
  EXPECT_EQ(noop.difference, "");
  EXPECT_GE(noop.edits, 1U);
}

// Constraint, regression caught: the three variable arrays must grow together, or the new variable has a name and no type and every binding index after it points at the wrong row.
TEST(HkxBehaviorEdit, AddVariableGrowsNamesInfosAndValuesAndKeepsEveryOtherVariable) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  const EditRun run = run_edits(bytes, {event_edit(HkxBehaviorEditKind::AddVariable, 0, "iCount", 3)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.variables.size(), 4U);
  EXPECT_EQ(run.model.variable_name_count, 4U);
  EXPECT_EQ(run.model.variable_info_count, 4U);
  EXPECT_EQ(run.model.variable_value_count, 4U);
  EXPECT_EQ(run.model.variables[3].name, "iCount");
  EXPECT_EQ(run.model.variables[3].type, 3);
  EXPECT_EQ(run.model.variables[3].word, 0U);
  for (std::size_t i = 0; i < 3; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(run.model.variables[i].name, before.variables[i].name);
    EXPECT_EQ(run.model.variables[i].type, before.variables[i].type);
    EXPECT_EQ(run.model.variables[i].word, before.variables[i].word);
    EXPECT_EQ(run.model.variables[i].has_vector, before.variables[i].has_vector);
    EXPECT_EQ(run.model.variables[i].vector, before.variables[i].vector);
  }
  ASSERT_EQ(run.model.bindings.size(), before.bindings.size());
  for (std::size_t i = 0; i < before.bindings.size(); ++i) {
    EXPECT_EQ(run.model.bindings[i].variable_index, before.bindings[i].variable_index);
  }
  EXPECT_EQ(run.model.events.size(), before.events.size());
}

// Constraint, regression caught: a rename must repoint one name only and must not run when an expression string would keep the old name and silently stop matching.
TEST(HkxBehaviorEdit, RenameVariableRepointsOneNameAndSeesTheExpressionsThatMentionIt) {
  const auto bytes = behavior_graph_packfile();
  const EditRun run = run_edits(bytes, {event_edit(HkxBehaviorEditKind::RenameVariable, 0, "bActive")});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.model.variables[0].name, "bActive");
  EXPECT_EQ(run.model.variables[1].name, "fSpeed");
  EXPECT_EQ(run.model.variables[2].name, "vLook");
  EXPECT_EQ(run.model.variables[0].type, 0);

  const EditRun back = run_edits(bytes, {event_edit(HkxBehaviorEditKind::RenameVariable, 0, "bActive"),
                                         event_edit(HkxBehaviorEditKind::RenameVariable, 0, "bFlag")});
  ASSERT_TRUE(back.report.ok) << back.report.refusal;
  EXPECT_EQ(back.model.variables[0].name, "bFlag");

  const EditRun named = run_edits(bytes, {event_edit(HkxBehaviorEditKind::RenameVariable, 1, "fRun")});
  EXPECT_FALSE(named.report.ok);
  EXPECT_NE(named.report.refusal.find("fSpeed"), std::string::npos);
}

// Constraint, regression caught: the clip name must land on the clip it was asked for and leave the other clips' animation strings alone.
TEST(HkxBehaviorEdit, SetClipAnimationRepointsOneClip) {
  const HkxGraphModel before = decode_ok();
  ASSERT_GE(before.clips.size(), 2U);
  const EditRun run = run_edits(behavior_graph_packfile(), {event_edit(HkxBehaviorEditKind::SetClipAnimation, 1, "Animations\\Other.hkt")});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.clips.size(), before.clips.size());
  for (std::size_t i = 0; i < before.clips.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(run.model.clips[i].animation, i == 1 ? "Animations\\Other.hkt" : before.clips[i].animation);
    EXPECT_EQ(run.model.clips[i].mode, before.clips[i].mode);
  }
}

// Constraint, regression caught: the start state must be written to the machine it was asked for and only an id the machine holds may be written.
TEST(HkxBehaviorEdit, SetStartStateWritesTheIdOfAStateOfThatMachine) {
  const EditRun run = run_edits(behavior_graph_packfile(), {value_edit(HkxBehaviorEditKind::SetStartState, 9)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  EXPECT_EQ(run.model.state_machines[0].start_state_id, 9);
  EXPECT_EQ(run.model.states[0].state_id, 7);
}

// Constraint, regression caught: an added state must land at the end of its machine's run with its own id, name and generator, leave every existing state alone, and accept a transition added to it in the same batch.
TEST(HkxBehaviorEdit, AddStateAppendsToTheMachineAndALaterEditCanAddItsTransition) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  const auto generator = static_cast<std::int32_t>(before.clips[2].node);
  const EditRun run = run_edits(bytes, {state_edit(HkxBehaviorEditKind::AddState, -1, "Walk", 11, generator),
                                        transition_edit(HkxBehaviorEditKind::AddTransition, 2, 0, row(2, 7))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.states.size(), 3U);
  EXPECT_EQ(run.model.state_machines[0].state_count, 3U);
  EXPECT_EQ(run.model.states[0].state_id, 7);
  EXPECT_EQ(run.model.states[0].name, before.states[0].name);
  EXPECT_EQ(run.model.states[0].generator_node, before.states[0].generator_node);
  EXPECT_EQ(run.model.states[1].state_id, 9);
  EXPECT_EQ(run.model.states[1].generator_node, before.states[1].generator_node);
  const auto& added = run.model.states[2];
  EXPECT_EQ(added.name, "Walk");
  EXPECT_EQ(added.state_id, 11);
  EXPECT_EQ(added.generator_node, generator);
  EXPECT_TRUE(added.enable);
  EXPECT_FLOAT_EQ(added.probability, 1.0f);
  EXPECT_EQ(added.transition_count, 1U);
  ASSERT_EQ(run.model.transitions.size(), 3U);
  EXPECT_EQ(run.model.transitions[0].condition, "x > 1");
  EXPECT_EQ(run.model.transitions[2].condition, "fSpeed > 0");
  EXPECT_EQ(run.model.transitions[run.model.states[2].first_transition].to_state_id, 7);

  const EditRun alone = run_edits(bytes, {state_edit(HkxBehaviorEditKind::AddState, -1, "Walk", 11, generator)});
  ASSERT_TRUE(alone.report.ok) << alone.report.refusal;
  ASSERT_EQ(alone.model.states.size(), 3U);
  EXPECT_EQ(alone.model.states[2].transition_count, 0U);
}

// Constraint, regression caught: a state that duplicates an id, names no generator, wraps its own machine or has a bad name must be refused whole, because each would load as a crash or a silently ignored state.
TEST(HkxBehaviorEdit, AddStateRefusesWhatTheEngineCannotLoad) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  const auto clip = static_cast<std::int32_t>(before.clips[0].node);
  const auto machine = static_cast<std::int32_t>(before.state_machines[0].node);
  const auto effect = static_cast<std::int32_t>(before.effects[0].node);
  const auto blender = static_cast<std::int32_t>(before.blenders[0].node);
  struct Case {
    const char* what;
    HkxBehaviorEdit edit;
    const char* reason;
  };
  const Case cases[] = {
      {"duplicate id", state_edit(HkxBehaviorEditKind::AddState, -1, "S", 7, clip), "already has a state with that id"},
      {"negative id", state_edit(HkxBehaviorEditKind::AddState, -1, "S", -2, clip), "must not be negative"},
      {"empty name", state_edit(HkxBehaviorEditKind::AddState, -1, "", 11, clip), "state name must be"},
      {"no generator", state_edit(HkxBehaviorEditKind::AddState, -1, "S", 11, -1), "generator node is outside"},
      {"generator past the table", state_edit(HkxBehaviorEditKind::AddState, -1, "S", 11, 99), "generator node is outside"},
      {"generator is an effect", state_edit(HkxBehaviorEditKind::AddState, -1, "S", 11, effect), "is not a generator"},
      {"generator is the machine", state_edit(HkxBehaviorEditKind::AddState, -1, "S", 11, machine), "would contain its own state machine"},
      {"machine past the table", state_edit(HkxBehaviorEditKind::AddState, -1, "S", 11, clip, 5), "outside the graph"},
  };
  const std::vector<std::byte> sentinel{std::byte{9}};
  for (const Case& entry : cases) {
    SCOPED_TRACE(entry.what);
    std::vector<std::byte> out = sentinel;
    const auto report = apply_behavior_edits(bytes, std::vector{entry.edit}, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find(entry.reason), std::string::npos) << report.refusal;
    EXPECT_EQ(out, sentinel);
  }
  const EditRun ok = run_edits(bytes, {state_edit(HkxBehaviorEditKind::AddState, -1, "S", 11, blender)});
  EXPECT_TRUE(ok.report.ok) << ok.report.refusal;
}

// Constraint, regression caught: removing a state must drop exactly that pointer and keep the other states' transitions and generators, and a state that other transitions still target must not be removable.
TEST(HkxBehaviorEdit, RemoveStateDropsOneStateAndIsRefusedWhileATransitionTargetsIt) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();

  const EditRun targeted = run_edits(bytes, {state_edit(HkxBehaviorEditKind::RemoveState, 1)});
  EXPECT_FALSE(targeted.report.ok);
  EXPECT_NE(targeted.report.refusal.find("transition.target_missing"), std::string::npos);

  const EditRun run = run_edits(bytes, {transition_edit(HkxBehaviorEditKind::RemoveTransition, 0, 0),
                                        state_edit(HkxBehaviorEditKind::RemoveState, 1)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.states.size(), 1U);
  EXPECT_EQ(run.model.state_machines[0].state_count, 1U);
  EXPECT_EQ(run.model.states[0].state_id, 7);
  EXPECT_EQ(run.model.states[0].generator_node, before.states[0].generator_node);
  ASSERT_EQ(run.model.transitions.size(), 1U);
  EXPECT_EQ(run.model.transitions[0].condition, "fSpeed > 0");
}

// Constraint, regression caught: an index that is not a state of the named machine, a last remaining state, or the state the machine starts in must be refused and not written.
TEST(HkxBehaviorEdit, RemoveStateRefusesAnUnknownStateAndTheLastState) {
  const auto bytes = behavior_graph_packfile();
  std::vector<std::byte> out{std::byte{9}};
  const auto start = apply_behavior_edits(bytes, std::vector{state_edit(HkxBehaviorEditKind::RemoveState, 0)}, out);
  EXPECT_FALSE(start.ok);
  EXPECT_NE(start.refusal.find("start state id"), std::string::npos);
  EXPECT_FALSE(apply_behavior_edits(bytes, std::vector{state_edit(HkxBehaviorEditKind::RemoveState, 9)}, out).ok);
  EXPECT_FALSE(apply_behavior_edits(bytes, std::vector{state_edit(HkxBehaviorEditKind::RemoveState, -1)}, out).ok);
  const EditRun one = run_edits(bytes, {transition_edit(HkxBehaviorEditKind::RemoveTransition, 0, 0),
                                        transition_edit(HkxBehaviorEditKind::RemoveTransition, -1, 0),
                                        state_edit(HkxBehaviorEditKind::RemoveState, 1)});
  ASSERT_TRUE(one.report.ok) << one.report.refusal;
  const auto last = apply_behavior_edits(one.out, std::vector{state_edit(HkxBehaviorEditKind::RemoveState, 0)}, out);
  EXPECT_FALSE(last.ok);
  EXPECT_EQ(out, std::vector<std::byte>{std::byte{9}});
}

constexpr std::uint32_t kBlenderOwner = static_cast<std::uint32_t>(HkxGraphOwnerKind::Blender);

// Constraint, regression caught: retargeting a binding must change only that row's variable index and keep its path and its neighbours, or a blend parameter ends up driven by the wrong variable.
TEST(HkxBehaviorEdit, SetBindingRetargetsOneRowAndKeepsTheOthers) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  ASSERT_EQ(before.bindings.size(), 2U);
  const EditRun run = run_edits(bytes, {binding_row_edit(HkxBehaviorEditKind::SetBinding, 1, 0)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  ASSERT_EQ(run.model.bindings.size(), 2U);
  EXPECT_EQ(run.model.bindings[1].variable_index, 0);
  EXPECT_EQ(run.model.bindings[1].member_path, "blendParameter");
  EXPECT_EQ(run.model.bindings[1].owner_index, before.bindings[1].owner_index);
  EXPECT_EQ(run.model.bindings[0].variable_index, before.bindings[0].variable_index);
  EXPECT_EQ(run.model.bindings[0].member_path, "other");
}

// Constraint, regression caught: a new row must land at the end of its owner's set with the path and variable asked, a later edit in the same batch must address it, and a removed row must take only its own path and index with it.
TEST(HkxBehaviorEdit, AddAndRemoveBindingGrowAndShrinkOnlyTheOwnersSet) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  const EditRun added = run_edits(bytes, {binding_row_edit(HkxBehaviorEditKind::AddBinding, 2, 1, kBlenderOwner, "minCyclicBlendParameter"),
                                          binding_row_edit(HkxBehaviorEditKind::SetBinding, 2, 0)});
  ASSERT_TRUE(added.report.ok) << added.report.refusal;
  ASSERT_EQ(added.model.bindings.size(), 3U);
  EXPECT_EQ(added.model.bindings[2].owner_kind, HkxGraphOwnerKind::Blender);
  EXPECT_EQ(added.model.bindings[2].owner_index, 2U);
  EXPECT_EQ(added.model.bindings[2].member_path, "minCyclicBlendParameter");
  EXPECT_EQ(added.model.bindings[2].variable_index, 0);
  EXPECT_EQ(added.model.bindings[2].bit_index, -1);
  EXPECT_EQ(added.model.bindings[1].member_path, "blendParameter");
  EXPECT_EQ(added.model.bindings[1].variable_index, before.bindings[1].variable_index);
  EXPECT_EQ(added.model.bindings[0].member_path, "other");

  const EditRun back = run_edits(added.out, {binding_row_edit(HkxBehaviorEditKind::RemoveBinding, 2)});
  ASSERT_TRUE(back.report.ok) << back.report.refusal;
  ASSERT_EQ(back.model.bindings.size(), 2U);
  EXPECT_EQ(back.model.bindings[1].member_path, "blendParameter");
  EXPECT_EQ(back.model.bindings[0].member_path, "other");
}

// Constraint, regression caught: emptying a set and adding to it again must work, or a binding removed by mistake can never be restored.
TEST(HkxBehaviorEdit, ASetEmptiedByRemoveBindingTakesANewRowAgain) {
  const auto bytes = behavior_graph_packfile();
  const EditRun run = run_edits(bytes, {binding_row_edit(HkxBehaviorEditKind::RemoveBinding, 1),
                                        binding_row_edit(HkxBehaviorEditKind::AddBinding, 2, 1, kBlenderOwner, "blendParameter")});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  ASSERT_EQ(run.model.bindings.size(), 2U);
  EXPECT_EQ(run.model.bindings[1].owner_index, 2U);
  EXPECT_EQ(run.model.bindings[1].member_path, "blendParameter");
  EXPECT_EQ(run.model.bindings[1].variable_index, 1);
  const EditRun only = run_edits(bytes, {binding_row_edit(HkxBehaviorEditKind::RemoveBinding, 1)});
  ASSERT_TRUE(only.report.ok) << only.report.refusal;
  ASSERT_EQ(only.model.bindings.size(), 1U);
  EXPECT_EQ(only.model.bindings[0].member_path, "other");
}

// Constraint, regression caught: a binding row that belongs to a blend child must be refused by name and not edited through some other owner's set.
TEST(HkxBehaviorEdit, ABlendChildBindingCannotBeAddressed) {
  BehaviorFixtureOptions options;
  options.driven_weight = true;
  const auto bytes = behavior_graph_packfile(options);
  const HkxGraphModel before = decode_ok(options);
  std::size_t child_row = before.bindings.size();
  for (std::size_t i = 0; i < before.bindings.size(); ++i) {
    if (before.bindings[i].owner_kind == HkxGraphOwnerKind::BlendChild) child_row = i;
  }
  ASSERT_LT(child_row, before.bindings.size());
  std::vector<std::byte> out;
  const auto report = apply_behavior_edits(bytes, std::vector{binding_row_edit(HkxBehaviorEditKind::SetBinding, child_row, 0)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("blend child"), std::string::npos);
}

namespace {

HkxBehaviorEdit field_edit(HkxGraphOwnerKind owner, std::size_t index, const std::string& member,
                           float real = std::numeric_limits<float>::quiet_NaN(), std::int32_t value = 0) {
  HkxBehaviorEdit edit;
  edit.kind = HkxBehaviorEditKind::SetGeneratorField;
  edit.flags = static_cast<std::uint32_t>(owner);
  edit.index = index;
  edit.name = member;
  edit.real = real;
  edit.value = value;
  return edit;
}

std::vector<std::byte> with_shared_blend_child(const std::vector<std::byte>& bytes) {
  HkxPackfile packfile;
  HkxGraphModel model;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_EQ(read_behavior_model(packfile, model), HkxStatus::Ok);
  auto& data = packfile.sections[packfile.data_section];
  const std::size_t first = packfile.objects[model.blend_children[0].object].offset;
  const std::size_t second = packfile.objects[model.blend_children[1].object].offset;
  for (auto& fixup : data.local_fixups) {
    if (fixup.destination == second) fixup.destination = static_cast<std::uint32_t>(first);
  }
  for (auto& fixup : data.global_fixups) {
    if (fixup.destination == second) fixup.destination = static_cast<std::uint32_t>(first);
  }
  std::vector<std::byte> out;
  EXPECT_EQ(fo4::hkx::rewrite_packfile(bytes, packfile, out), HkxStatus::Ok);
  return out;
}

}  // namespace

// Constraint, regression caught: a scalar write must land on the addressed clip, blender or blend child only, as the width the decoder reads, and a batch must address each row as the file left it; a write that hit a neighbour or the wrong width would change another clip's speed or corrupt a flag byte.
TEST(HkxBehaviorEdit, SetGeneratorFieldWritesOneMemberAndKeepsEveryOtherRow) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  ASSERT_GE(before.clips.size(), 3U);
  ASSERT_GE(before.blend_children.size(), 2U);
  const EditRun run = run_edits(
      bytes, {field_edit(HkxGraphOwnerKind::Clip, 0, "playbackSpeed", 2.5f), field_edit(HkxGraphOwnerKind::Clip, 2, "mode", kNaN, 3),
              field_edit(HkxGraphOwnerKind::Clip, 1, "flags", kNaN, 0x21),
              field_edit(HkxGraphOwnerKind::Clip, 1, "cropEndAmountLocalTime", 0.75f),
              field_edit(HkxGraphOwnerKind::Blender, 0, "blendParameter", 0.5f),
              field_edit(HkxGraphOwnerKind::Blender, 1, "minCyclicBlendParameter", 0.25f),
              field_edit(HkxGraphOwnerKind::BlendChild, 1, "weight", 2.0f),
              field_edit(HkxGraphOwnerKind::BlendChild, 1, "worldFromModelWeight", 0.5f)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.clips.size(), before.clips.size());
  EXPECT_FLOAT_EQ(run.model.clips[0].playback_speed, 2.5f);
  EXPECT_FLOAT_EQ(run.model.clips[1].playback_speed, before.clips[1].playback_speed);
  EXPECT_EQ(run.model.clips[2].mode, 3);
  EXPECT_EQ(run.model.clips[0].mode, before.clips[0].mode);
  EXPECT_EQ(run.model.clips[1].flags, 0x21U);
  EXPECT_EQ(run.model.clips[0].flags, before.clips[0].flags);
  EXPECT_FLOAT_EQ(run.model.clips[1].crop_end, 0.75f);
  EXPECT_EQ(run.model.clips[0].animation, before.clips[0].animation);
  EXPECT_FLOAT_EQ(run.model.blenders[0].blend_parameter, 0.5f);
  EXPECT_FLOAT_EQ(run.model.blenders[1].blend_parameter, before.blenders[1].blend_parameter);
  EXPECT_FLOAT_EQ(run.model.blenders[1].min_cyclic_blend_parameter, 0.25f);
  EXPECT_FLOAT_EQ(run.model.blend_children[1].weight, 2.0f);
  EXPECT_FLOAT_EQ(run.model.blend_children[1].world_from_model_weight, 0.5f);
  EXPECT_FLOAT_EQ(run.model.blend_children[0].weight, before.blend_children[0].weight);
  EXPECT_FLOAT_EQ(run.model.blend_children[2].weight, before.blend_children[2].weight);
  EXPECT_EQ(run.model.bindings.size(), before.bindings.size());
}

// Constraint, regression caught: every refusal must name its reason and leave the output untouched, so a wrong owner, a missing real, a value the enum does not have or a member a binding drives is never written.
TEST(HkxBehaviorEdit, SetGeneratorFieldRefusalsNameTheirReasonAndWriteNothing) {
  const auto bytes = behavior_graph_packfile();
  const float inf = std::numeric_limits<float>::infinity();
  struct Case {
    const char* what;
    HkxBehaviorEdit edit;
    const char* reason;
  };
  HkxBehaviorEdit wrong_kind = field_edit(HkxGraphOwnerKind::State, 0, "weight", 1.0f);
  const Case cases[] = {
      {"owner kind that is not a generator", wrong_kind, "owner kind is not"},
      {"member of another owner", field_edit(HkxGraphOwnerKind::Clip, 0, "blendParameter", 1.0f), "does not set"},
      {"member outside the table", field_edit(HkxGraphOwnerKind::Blender, 0, "flags", kNaN, 1), "does not set"},
      {"clip past the table", field_edit(HkxGraphOwnerKind::Clip, 9, "playbackSpeed", 1.0f), "clip index is outside"},
      {"blender past the table", field_edit(HkxGraphOwnerKind::Blender, 9, "blendParameter", 1.0f), "blender index is outside"},
      {"blend child past the table", field_edit(HkxGraphOwnerKind::BlendChild, 99, "weight", 1.0f), "blend child index is outside"},
      {"real member given no real", field_edit(HkxGraphOwnerKind::Clip, 0, "playbackSpeed", kNaN, 2), "finite real number"},
      {"infinite real", field_edit(HkxGraphOwnerKind::BlendChild, 0, "weight", inf), "finite real number"},
      {"mode past the enum", field_edit(HkxGraphOwnerKind::Clip, 0, "mode", kNaN, 4), "within 0 to 3"},
      {"negative mode", field_edit(HkxGraphOwnerKind::Clip, 0, "mode", kNaN, -1), "within 0 to 3"},
      {"flag bit the class does not have", field_edit(HkxGraphOwnerKind::Clip, 0, "flags", kNaN, 0x40), "within 0 to 63"},
      {"blend parameter a binding drives", field_edit(HkxGraphOwnerKind::Blender, 2, "blendParameter", 1.0f), "a binding drives"},
  };
  const std::vector<std::byte> sentinel{std::byte{1}, std::byte{2}, std::byte{3}};
  for (const Case& entry : cases) {
    SCOPED_TRACE(entry.what);
    std::vector<std::byte> out = sentinel;
    const auto report = apply_behavior_edits(bytes, std::vector{entry.edit}, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find(entry.reason), std::string::npos) << report.refusal;
    EXPECT_EQ(report.refused_edit, 0U);
    EXPECT_EQ(out, sentinel);
  }
}

// Constraint, regression caught: a NaN already stored in an untouched blend child must not make the read-back compare unequal to itself, which would refuse every valid edit of any other row in that file.
TEST(HkxBehaviorEdit, SetGeneratorFieldSucceedsWhileAnotherRowHoldsANaN) {
  const auto bytes = behavior_graph_packfile();
  HkxPackfile packfile;
  HkxGraphModel model;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  ASSERT_EQ(read_behavior_model(packfile, model), HkxStatus::Ok);
  ASSERT_GE(model.blend_children.size(), 2U);
  const auto* layout = fo4::hkx::find_layout("hkbBlenderGeneratorChild");
  ASSERT_NE(layout, nullptr);
  const std::size_t at = packfile.objects[model.blend_children[1].object].offset + *fo4::hkx::find_member_offset(*layout, "weight");
  std::memcpy(packfile.sections[packfile.data_section].data.data() + at, &kNaN, sizeof kNaN);
  std::vector<std::byte> poisoned;
  ASSERT_EQ(fo4::hkx::rewrite_packfile(bytes, packfile, poisoned), HkxStatus::Ok);
  const EditRun run = run_edits(poisoned, {field_edit(HkxGraphOwnerKind::Clip, 0, "playbackSpeed", 2.5f)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_FLOAT_EQ(run.model.clips[0].playback_speed, 2.5f);
  EXPECT_TRUE(std::isnan(run.model.blend_children[1].weight));
}

// Constraint, regression caught: a weight a variable drives is not played from the stored value, and a blend child info that two slots share would change two blends; both must be refused and a free neighbour still edits.
TEST(HkxBehaviorEdit, SetGeneratorFieldRefusesADrivenWeightAndASharedBlendChild) {
  BehaviorFixtureOptions options;
  options.driven_weight = true;
  const auto bytes = behavior_graph_packfile(options);
  const HkxGraphModel before = decode_ok(options);
  std::size_t driven = before.blend_children.size();
  for (const auto& binding : before.bindings) {
    if (binding.owner_kind == HkxGraphOwnerKind::BlendChild) driven = binding.owner_index;
  }
  ASSERT_LT(driven, before.blend_children.size());
  std::vector<std::byte> out;
  auto report = apply_behavior_edits(bytes, std::vector{field_edit(HkxGraphOwnerKind::BlendChild, driven, "weight", 2.0f)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("a binding drives"), std::string::npos) << report.refusal;
  EXPECT_TRUE(out.empty());
  const std::size_t free_child = driven == 0 ? 1 : 0;
  EXPECT_TRUE(run_edits(bytes, {field_edit(HkxGraphOwnerKind::BlendChild, free_child, "weight", 2.0f)}).report.ok);

  report = apply_behavior_edits(with_shared_blend_child(behavior_graph_packfile()),
                                std::vector{field_edit(HkxGraphOwnerKind::BlendChild, 0, "weight", 2.0f)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("more than one owner"), std::string::npos) << report.refusal;
  EXPECT_TRUE(out.empty());
}

// Constraint, regression caught: a refused edit, alone or inside a batch, must leave the output untouched and say which edit failed.
TEST(HkxBehaviorEdit, RefusedEditsReportAReasonAndWriteNothing) {
  const auto bytes = behavior_graph_packfile();
  struct Case {
    const char* what;
    std::vector<HkxBehaviorEdit> edits;
    std::size_t refused_edit;
    const char* reason;
  };
  HkxGraphTransition nan_time = row(1, 7);
  nan_time.trigger_interval.enter_time = kNaN;
  HkxGraphTransition bad_effect = row(1, 7);
  bad_effect.effect_node = 99;
  HkxBehaviorEdit bad_machine = transition_edit(HkxBehaviorEditKind::AddTransition, -1, 0, row(1, 7));
  bad_machine.state_machine = 5;
  const Case cases[] = {
      {"remove event", {event_edit(HkxBehaviorEditKind::RemoveEvent, 0, "")}, 0, "removing an event is not supported"},
      {"add vector variable", {event_edit(HkxBehaviorEditKind::AddVariable, 0, "x", 6)}, 0, "only bool, int8"},
      {"add pointer variable", {event_edit(HkxBehaviorEditKind::AddVariable, 0, "x", 5)}, 0, "only bool, int8"},
      {"add empty variable", {event_edit(HkxBehaviorEditKind::AddVariable, 0, "", 0)}, 0, "variable name must be non-empty"},
      {"add duplicate variable", {event_edit(HkxBehaviorEditKind::AddVariable, 0, "fSpeed", 4)}, 0, "already exists"},
      {"remove variable", {event_edit(HkxBehaviorEditKind::RemoveVariable, 0, "")}, 0, "removing a variable is not supported"},
      {"rename variable past the table", {event_edit(HkxBehaviorEditKind::RenameVariable, 9, "y")}, 0, "variable index is outside"},
      {"rename variable to a taken name", {event_edit(HkxBehaviorEditKind::RenameVariable, 0, "fSpeed")}, 0, "already exists"},
      {"rename a variable an expression names", {event_edit(HkxBehaviorEditKind::RenameVariable, 1, "fRun")}, 0, "another string in the file mentions"},
      {"clip animation past the table", {event_edit(HkxBehaviorEditKind::SetClipAnimation, 9, "A.hkt")}, 0, "clip index is outside"},
      {"empty clip animation", {event_edit(HkxBehaviorEditKind::SetClipAnimation, 0, "")}, 0, "animation name must be non-empty"},
      {"start state that is not a state", {value_edit(HkxBehaviorEditKind::SetStartState, 55)}, 0, "names no state of that state machine"},
      {"retarget past the table", {binding_row_edit(HkxBehaviorEditKind::SetBinding, 9, 0)}, 0, "binding index is outside"},
      {"retarget to a variable past the table", {binding_row_edit(HkxBehaviorEditKind::SetBinding, 0, 9)}, 0, "variable index is outside"},
      {"unbind with a negative variable", {binding_row_edit(HkxBehaviorEditKind::SetBinding, 0, -1)}, 0, "variable index is outside"},
      {"remove binding past the table", {binding_row_edit(HkxBehaviorEditKind::RemoveBinding, 9)}, 0, "binding index is outside"},
      {"add binding to an owner without a set", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 0, 0, 5, "x")}, 0, "has no binding set"},
      {"add binding to a blend child", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 0, 0, 4, "x")}, 0, "blend child"},
      {"add binding with an unknown owner kind", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 0, 0, 9, "x")}, 0, "owner kind is not"},
      {"add binding to an owner past the table", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 9, 0, kBlenderOwner, "x")}, 0, "outside the blenders table"},
      {"add a path the set already binds", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 2, 1, kBlenderOwner, "blendParameter")}, 0, "already binds"},
      {"add binding with an empty path", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 2, 1, kBlenderOwner, "")}, 0, "member path must be"},
      {"add binding to a variable past the table", {binding_row_edit(HkxBehaviorEditKind::AddBinding, 2, 9, kBlenderOwner, "x")}, 0, "variable index is outside"},
      {"set past the run", {transition_edit(HkxBehaviorEditKind::SetTransition, 0, 5, row(1, 7))}, 0, "outside the run"},
      {"add past the run", {transition_edit(HkxBehaviorEditKind::AddTransition, 0, 3, row(1, 7))}, 0, "past the end of the run"},
      {"remove from an empty run", {transition_edit(HkxBehaviorEditKind::RemoveTransition, 1, 0)}, 0, "outside the run"},
      {"state out of range", {transition_edit(HkxBehaviorEditKind::AddTransition, 7, 0, row(1, 7))}, 0, "not a state of that state machine"},
      {"machine out of range", {bad_machine}, 0, "outside the graph"},
      {"event out of range", {transition_edit(HkxBehaviorEditKind::AddTransition, 0, 0, row(99, 7))}, 0, "transition.event_range"},
      {"target missing", {transition_edit(HkxBehaviorEditKind::AddTransition, 0, 0, row(1, 42))}, 0, "transition.target_missing"},
      {"non-finite time", {transition_edit(HkxBehaviorEditKind::SetTransition, 0, 0, nan_time)}, 0, "not finite"},
      {"effect out of range", {transition_edit(HkxBehaviorEditKind::SetTransition, 0, 0, bad_effect)}, 0, "effect node is outside"},
      {"duplicate event name", {event_edit(HkxBehaviorEditKind::RenameEvent, 1, "Go")}, 0, "event.duplicate_name"},
      {"empty event name", {event_edit(HkxBehaviorEditKind::RenameEvent, 1, "")}, 0, "event name must be non-empty"},
      {"add empty event", {event_edit(HkxBehaviorEditKind::AddEvent, 0, "")}, 0, "event name must be non-empty"},
      {"add event with NUL", {event_edit(HkxBehaviorEditKind::AddEvent, 0, std::string("a\0b", 3))}, 0, "event name must be non-empty"},
      {"flags past the table", {event_edit(HkxBehaviorEditKind::SetEventFlags, 9, "", 1)}, 0, "event index is outside"},
      {"later edit refused", {event_edit(HkxBehaviorEditKind::AddEvent, 0, "Jump"), event_edit(HkxBehaviorEditKind::RemoveEvent, 0, "")}, 1, "removing an event is not supported"},
  };
  const std::vector<std::byte> sentinel{std::byte{1}, std::byte{2}, std::byte{3}};
  for (const Case& entry : cases) {
    SCOPED_TRACE(entry.what);
    std::vector<std::byte> out = sentinel;
    const auto report = apply_behavior_edits(bytes, entry.edits, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find(entry.reason), std::string::npos) << report.refusal;
    EXPECT_EQ(report.refused_edit, entry.refused_edit);
    EXPECT_EQ(out, sentinel);
  }
  std::vector<std::byte> out = sentinel;
  EXPECT_FALSE(apply_behavior_edits(bytes, {}, out).ok);
  EXPECT_EQ(out, sentinel);
  EXPECT_FALSE(apply_behavior_edits(std::vector<std::byte>(16), std::vector{event_edit(HkxBehaviorEditKind::AddEvent, 0, "x")}, out).ok);
  EXPECT_EQ(out, sentinel);
}

// Constraint, regression caught: removing row 0 of a two-row run must shift the surviving row's effect and condition pointers down with it, or the survivor loses or swaps them.
TEST(HkxBehaviorEdit, RemovingTheFirstRowOfATwoRowRunKeepsTheSurvivorsEffectAndCondition) {
  const EditRun run = run_edits(behavior_graph_packfile(), {transition_edit(HkxBehaviorEditKind::AddTransition, 0, 0, row(2, 9)),
                                                            transition_edit(HkxBehaviorEditKind::RemoveTransition, 0, 0)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  ASSERT_EQ(run.model.transitions.size(), 2U);
  EXPECT_EQ(run.model.states[0].transition_count, 1U);
  const auto& survivor = run.model.transitions[0];
  EXPECT_EQ(survivor.state, 0);
  EXPECT_EQ(survivor.event_id, 1);
  EXPECT_EQ(survivor.to_state_id, 9);
  EXPECT_EQ(survivor.priority, 3);
  EXPECT_EQ(survivor.flags, 0x0A00U);
  ASSERT_GE(survivor.effect_node, 0);
  EXPECT_EQ(run.model.nodes[static_cast<std::size_t>(survivor.effect_node)].name, "fade");
  EXPECT_EQ(survivor.condition_kind, HkxGraphConditionKind::String);
  EXPECT_EQ(survivor.condition, "x > 1");
  EXPECT_EQ(run.model.transitions[1].condition, "fSpeed > 0");
}

// Constraint, regression caught: a transition effect must be a decoded transition effect, or a row can point at any node and the engine reads a wrong class as an effect.
TEST(HkxBehaviorEdit, AnEffectNodeThatIsNotATransitionEffectIsRefused) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel model = decode_ok();
  HkxGraphTransition bad = row(1, 7);
  bad.effect_node = static_cast<std::int32_t>(model.state_machines[0].node);
  ASSERT_NE(bad.effect_node, model.transitions[0].effect_node);
  for (const auto kind : {HkxBehaviorEditKind::SetTransition, HkxBehaviorEditKind::AddTransition}) {
    const EditRun run = run_edits(bytes, {transition_edit(kind, 0, 0, bad)});
    EXPECT_FALSE(run.report.ok);
    EXPECT_NE(run.report.refusal.find("not a transition effect"), std::string::npos) << run.report.refusal;
  }
  HkxGraphTransition good = row(1, 7);
  good.effect_node = model.transitions[0].effect_node;
  EXPECT_TRUE(run_edits(bytes, {transition_edit(HkxBehaviorEditKind::SetTransition, 0, 0, good)}).report.ok);
}

// Constraint, regression caught: an event name is stored with a NUL terminator inside a 65536 byte limit, so a 65536 byte name overruns it and must be refused while 65535 fits.
TEST(HkxBehaviorEdit, AnEventNameAtTheLimitIsAcceptedAndOneByteMoreIsRefused) {
  const auto bytes = behavior_graph_packfile();
  const EditRun fits = run_edits(bytes, {event_edit(HkxBehaviorEditKind::AddEvent, 0, std::string(65535, 'a'))});
  ASSERT_TRUE(fits.report.ok) << fits.report.refusal;
  ASSERT_EQ(fits.model.events.size(), 4U);
  EXPECT_EQ(fits.model.events[3].name.size(), 65535U);
  const EditRun over = run_edits(bytes, {event_edit(HkxBehaviorEditKind::AddEvent, 0, std::string(65536, 'a'))});
  EXPECT_FALSE(over.report.ok);
  const EditRun rename = run_edits(bytes, {event_edit(HkxBehaviorEditKind::RenameEvent, 1, std::string(65536, 'a'))});
  EXPECT_FALSE(rename.report.ok);
}

// Constraint, regression caught: an array that two states point at must be refused, or editing one state silently rewrites the other.
TEST(HkxBehaviorEdit, ASharedTransitionArrayIsRefused) {
  const auto bytes = with_shared_array(behavior_graph_packfile());
  for (const auto kind : {HkxBehaviorEditKind::RemoveTransition, HkxBehaviorEditKind::SetTransition}) {
    std::vector<std::byte> out;
    const auto report = apply_behavior_edits(bytes, std::vector{transition_edit(kind, 0, 0, row(1, 7))}, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find("shared"), std::string::npos) << report.refusal;
    EXPECT_TRUE(out.empty());
  }
}

// Constraint, regression caught: only errors an edit adds are refused, so a file that already had a defect can still be repaired or extended.
TEST(HkxBehaviorEdit, AnEditIsJudgedAgainstTheDefectsTheSourceAlreadyHad) {
  const auto broken = with_wildcard_event(behavior_graph_packfile(), 99);
  HkxGraphModel model;
  ASSERT_EQ(decode(broken, model), HkxStatus::Ok);
  EXPECT_NE(find_issue(validate_behavior_model(model), "transition.event_range"), nullptr);

  const EditRun extended = run_edits(broken, {event_edit(HkxBehaviorEditKind::AddEvent, 0, "Jump")});
  ASSERT_TRUE(extended.report.ok) << extended.report.refusal;
  EXPECT_EQ(extended.report.before.errors, 1U);
  EXPECT_EQ(extended.report.after.errors, 1U);

  HkxGraphTransition fixed = model.transitions[1];
  fixed.event_id = 1;
  const EditRun repaired = run_edits(broken, {transition_edit(HkxBehaviorEditKind::SetTransition, -1, 0, fixed)});
  ASSERT_TRUE(repaired.report.ok) << repaired.report.refusal;
  EXPECT_EQ(repaired.report.after.errors, 0U);

  const EditRun worse = run_edits(broken, {transition_edit(HkxBehaviorEditKind::AddTransition, 0, 0, row(98, 7))});
  EXPECT_FALSE(worse.report.ok);
}

namespace {

const fo4::hkx::HkxEventUsage* usage_of_event(const fo4::hkx::HkxSymbolUsage& usage, std::int32_t id) {
  for (const auto& row : usage.events) {
    if (row.event_id == id) return &row;
  }
  return nullptr;
}

bool has_site(const fo4::hkx::HkxEventUsage& row, fo4::hkx::HkxEventSite site, std::int32_t state, const std::string& owner) {
  for (const auto& entry : row.sites) {
    if (entry.site == site && entry.state == state && entry.owner == owner) return true;
  }
  return false;
}

fo4::hkx::HkxSearchResult search_graph(const std::string& query, std::size_t limit = 100) {
  const auto bytes = behavior_graph_packfile();
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxSearchResult result;
  EXPECT_EQ(fo4::hkx::search_symbols(packfile, query, limit, result), HkxStatus::Ok);
  return result;
}

bool has_hit(const fo4::hkx::HkxSearchResult& result, fo4::hkx::HkxHitKind kind, const std::string& text) {
  for (const auto& hit : result.hits) {
    if (hit.kind == kind && hit.text == text) return true;
  }
  return false;
}

}  // namespace

// Regression: an event raised by a clip trigger and a machine must list both sites, so renaming or removing it shows every user.
TEST(HkxSymbolUsage, ListsEveryDecodedSiteOfAnEventWithItsOwner) {
  const HkxGraphModel model = decode_ok();
  fo4::hkx::HkxSymbolUsage usage;
  fo4::hkx::collect_symbol_usage(model, usage);
  const auto* wave = usage_of_event(usage, 2);
  ASSERT_NE(wave, nullptr);
  EXPECT_EQ(wave->name, "Wave");
  EXPECT_TRUE(wave->in_range);
  EXPECT_TRUE(has_site(*wave, fo4::hkx::HkxEventSite::ClipTrigger, -1, "ClipA"));
  EXPECT_TRUE(has_site(*wave, fo4::hkx::HkxEventSite::StateMachineChange, -1, "Root"));

  const auto* stop = usage_of_event(usage, 1);
  ASSERT_NE(stop, nullptr);
  EXPECT_TRUE(has_site(*stop, fo4::hkx::HkxEventSite::StateMachineReturnToPrevious, -1, "Root"));
  EXPECT_TRUE(has_site(*stop, fo4::hkx::HkxEventSite::Transition, 0, "Idle"));
  EXPECT_TRUE(has_site(*stop, fo4::hkx::HkxEventSite::TriggerEnter, 0, "Idle"));

  const auto* go = usage_of_event(usage, 0);
  ASSERT_NE(go, nullptr);
  EXPECT_TRUE(has_site(*go, fo4::hkx::HkxEventSite::Transition, -1, "Root"));
}

// Regression: an id outside the events table must come back as its own unnamed row, never be dropped or given a name.
TEST(HkxSymbolUsage, AnOutOfRangeEventIdIsReportedUnnamed) {
  HkxGraphModel model = decode_ok();
  model.transitions[0].event_id = 99;
  fo4::hkx::HkxSymbolUsage usage;
  fo4::hkx::collect_symbol_usage(model, usage);
  const auto* stray = usage_of_event(usage, 99);
  ASSERT_NE(stray, nullptr);
  EXPECT_FALSE(stray->in_range);
  EXPECT_TRUE(stray->name.empty());
  EXPECT_TRUE(has_site(*stray, fo4::hkx::HkxEventSite::Transition, 0, "Idle"));
}

// Regression: variable and state usage must come from the bindings and transition targets, with in-degree counted per machine.
TEST(HkxSymbolUsage, VariablesListTheirBindingsAndStatesCountTransitions) {
  const HkxGraphModel model = decode_ok();
  fo4::hkx::HkxSymbolUsage usage;
  fo4::hkx::collect_symbol_usage(model, usage);
  ASSERT_GE(usage.variables.size(), 3U);
  EXPECT_TRUE(usage.variables[0].sites.empty());
  ASSERT_EQ(usage.variables[1].sites.size(), 1U);
  EXPECT_EQ(usage.variables[1].sites[0].owner_kind, HkxGraphOwnerKind::Blender);
  EXPECT_EQ(usage.variables[1].sites[0].member_path, "blendParameter");
  ASSERT_EQ(usage.variables[2].sites.size(), 1U);
  EXPECT_EQ(usage.variables[2].sites[0].member_path, "other");

  ASSERT_EQ(usage.states.size(), 2U);
  EXPECT_EQ(usage.states[0].name, "Idle");
  EXPECT_EQ(usage.states[0].incoming, 1U);
  EXPECT_EQ(usage.states[0].outgoing, 1U);
  EXPECT_TRUE(usage.states[0].is_start);
  EXPECT_EQ(usage.states[1].incoming, 1U);
  EXPECT_EQ(usage.states[1].outgoing, 0U);
  EXPECT_FALSE(usage.states[1].is_start);
}

// Regression: one query must reach symbols, assets and class fields, ignoring case, so the project search finds more than file names.
TEST(HkxSymbolSearch, MatchesEventsNamesAssetsAndFieldsIgnoringCase) {
  EXPECT_TRUE(has_hit(search_graph("WAVE"), fo4::hkx::HkxHitKind::Event, "Wave"));
  EXPECT_TRUE(has_hit(search_graph("fspeed"), fo4::hkx::HkxHitKind::Variable, "fSpeed"));
  EXPECT_TRUE(has_hit(search_graph("idle"), fo4::hkx::HkxHitKind::Name, "Idle"));
  EXPECT_TRUE(has_hit(search_graph("b.hkt"), fo4::hkx::HkxHitKind::Asset, "Animations\\B.hkt"));
  EXPECT_TRUE(has_hit(search_graph("hkbClipGen"), fo4::hkx::HkxHitKind::Class, "hkbClipGenerator"));
  EXPECT_TRUE(has_hit(search_graph("animationName"), fo4::hkx::HkxHitKind::Field, "hkbClipGenerator.animationName"));
}

// Regression: an empty query must return nothing (the index view lists everything, a search must not), and a limit must report truncation.
TEST(HkxSymbolSearch, EmptyQueryFindsNothingAndTheLimitReportsTruncation) {
  const auto none = search_graph("");
  EXPECT_TRUE(none.hits.empty());
  EXPECT_FALSE(none.truncated);

  const auto capped = search_graph("a", 2);
  EXPECT_EQ(capped.hits.size(), 2U);
  EXPECT_TRUE(capped.truncated);
}

// Regression: a "#N" query must find object N by index, and an index past the list must find nothing; surrounding spaces must not change the answer.
TEST(HkxSymbolSearch, HashQueryMatchesAnObjectIndex) {
  EXPECT_EQ(search_graph("  #1 ").hits.size(), search_graph("#1").hits.size());
  const auto found = search_graph("#1");
  ASSERT_FALSE(found.hits.empty());
  EXPECT_EQ(found.hits[0].kind, fo4::hkx::HkxHitKind::Object);
  EXPECT_EQ(found.hits[0].object, 1);
  EXPECT_TRUE(search_graph("#99999").hits.empty());
}

// Regression: a field value must be found by its text, through inherited members, struct arrays and every written scalar kind, or the project search stays name-only.
TEST(HkxSymbolSearch, FindsFieldValuesThroughInheritedMembersAndStructArrays) {
  using fo4::hkx::HkxHitKind;
  EXPECT_TRUE(has_hit(search_graph("ClipA"), HkxHitKind::Name, "hkbClipGenerator.name = ClipA"));
  EXPECT_TRUE(has_hit(search_graph("Idle"), HkxHitKind::Name, "hkbStateMachineStateInfo.name = Idle"));
  EXPECT_TRUE(has_hit(search_graph("animations\\a.hkt"), HkxHitKind::Asset, "hkbClipGenerator.animationName = Animations\\A.hkt"));
  EXPECT_TRUE(has_hit(search_graph("x > 1"), HkxHitKind::Field, "hkbStringCondition.conditionString = x > 1"));
  EXPECT_TRUE(has_hit(search_graph("1.5"), HkxHitKind::Field, "hkbClipGenerator.playbackSpeed = 1.5"));
  EXPECT_TRUE(has_hit(search_graph("true", 1000), HkxHitKind::Field, "hkbStateMachineStateInfo.enable = true"));
  EXPECT_TRUE(has_hit(search_graph("9", 1000), HkxHitKind::Field, "hkbStateMachineStateInfo.stateId = 9"));
  EXPECT_TRUE(has_hit(search_graph("2", 1000), HkxHitKind::Event, "hkbClipTriggerArray.triggers[0].event.id = 2"));
  EXPECT_TRUE(search_graph("no such value anywhere").hits.empty());
}

// Regression: a value hit must name the object that holds it, so a project search can open it.
TEST(HkxSymbolSearch, AValueHitCarriesTheObjectIndexOfItsHolder) {
  const auto result = search_graph("ClipB");
  for (const auto& hit : result.hits) {
    if (hit.text != "hkbClipGenerator.name = ClipB") continue;
    EXPECT_GE(hit.object, 0);
    return;
  }
  ADD_FAILURE() << "no value hit for ClipB";
}

// Regression: the typed layout table must keep runtime-only members and pointers out of the value walk and must chain a derived class to its bases.
TEST(HkxLayoutTable, TypesWrittenMembersAndChainsDerivedClassesToTheirBases) {
  using fo4::hkx::HkxFieldKind;
  const auto* clip = fo4::hkx::find_layout("hkbClipGenerator");
  ASSERT_NE(clip, nullptr);
  const auto kind_of = [](const fo4::hkx::HkxClassLayout& layout, std::string_view member) {
    for (const auto& row : layout.members) {
      if (row.name == member) return row.kind;
    }
    return HkxFieldKind::Other;
  };
  EXPECT_EQ(kind_of(*clip, "playbackSpeed"), HkxFieldKind::Real);
  EXPECT_EQ(kind_of(*clip, "animationName"), HkxFieldKind::String);
  EXPECT_EQ(kind_of(*clip, "triggers"), HkxFieldKind::Other);
  const auto* triggers = fo4::hkx::find_layout("hkbClipTriggerArray");
  ASSERT_NE(triggers, nullptr);
  ASSERT_EQ(triggers->members.size(), 1U);
  EXPECT_EQ(triggers->members[0].kind, HkxFieldKind::Array);
  EXPECT_EQ(triggers->members[0].element, HkxFieldKind::Struct);
  EXPECT_EQ(triggers->members[0].type_class, "hkbClipTrigger");

  EXPECT_EQ(clip->parent, "hkbGenerator");
  const auto* generator = fo4::hkx::find_layout(clip->parent);
  ASSERT_NE(generator, nullptr);
  EXPECT_EQ(generator->parent, "hkbNode");
  const auto* node = fo4::hkx::find_layout("hkbNode");
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(kind_of(*node, "name"), HkxFieldKind::String);
  EXPECT_EQ(kind_of(*node, "id"), HkxFieldKind::Other);
}

// Regression: vector, matrix, half and relative-array members must be typed (the inspector and the value search read them), while a ulong with no verified width stays untyped and an ambiguous enum name stays unresolved.
TEST(HkxLayoutTable, TypesVectorsMatricesHalfAndRelativeArraysAndResolvesEnumNames) {
  using fo4::hkx::HkxFieldKind;
  const auto member_of = [](const char* klass, std::string_view name) {
    for (const auto& row : fo4::hkx::all_members(*fo4::hkx::find_layout(klass))) {
      if (row.name == name) return row;
    }
    ADD_FAILURE() << klass << " has no member " << name;
    return fo4::hkx::HkxMemberLayout{};
  };
  EXPECT_EQ(member_of("hkMassProperties", "centerOfMass").kind, HkxFieldKind::Vector4);
  EXPECT_EQ(member_of("hkMassProperties", "inertiaTensor").kind, HkxFieldKind::Matrix3);
  EXPECT_EQ(member_of("hkMotionState", "transform").kind, HkxFieldKind::Transform);
  EXPECT_EQ(member_of("hkMotionState", "linearDamping").kind, HkxFieldKind::Half);
  EXPECT_EQ(member_of("BSAlignBoneModifier", "debugTargetRotation").kind, HkxFieldKind::Quaternion);
  const auto planes = member_of("hknpConvexPolytopeShape", "planes");
  EXPECT_EQ(planes.kind, HkxFieldKind::RelArray);
  EXPECT_EQ(planes.element, HkxFieldKind::Vector4);
  const auto faces = member_of("hknpConvexPolytopeShape", "faces");
  EXPECT_EQ(faces.element, HkxFieldKind::Struct);
  EXPECT_EQ(faces.type_class, "hknpConvexPolytopeShapeFace");
  const auto user_data = member_of("hkbNode", "userData");
  EXPECT_EQ(user_data.kind, HkxFieldKind::Other);
  EXPECT_EQ(user_data.type, "ulong");

  const auto mode = member_of("hkbClipGenerator", "mode");
  ASSERT_NE(mode.enumeration, nullptr);
  EXPECT_EQ(fo4::hkx::enum_value_name(*mode.enumeration, false, 1), "MODE_LOOPING");
  EXPECT_EQ(fo4::hkx::enum_value_name(*mode.enumeration, false, 9), "");
  EXPECT_EQ(fo4::hkx::enum_value_name(*mode.enumeration, false, 6), "");
  EXPECT_EQ(fo4::hkx::enum_value_name(*mode.enumeration, true, 6), "MODE_USER_CONTROLLED|MODE_COUNT");
  EXPECT_EQ(fo4::hkx::enum_value_name(*mode.enumeration, true, 8), "");
  EXPECT_EQ(member_of("hknpShape", "dispatchType").enumeration, nullptr);
}

namespace {

fo4::hkx::HkxSearchResult search_bytes(const std::vector<std::byte>& bytes, const std::string& query) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  fo4::hkx::HkxSearchResult result;
  EXPECT_EQ(fo4::hkx::search_symbols(packfile, query, 100, result), HkxStatus::Ok);
  return result;
}

}  // namespace

// Regression: a vector or quaternion value must be searchable, so a target position or rotation is found by one of its components.
TEST(HkxSymbolSearch, FindsAVectorOrQuaternionByOneOfItsComponents) {
  using fo4::hkx::HkxHitKind;
  BehaviorFixture f;
  const auto at = f.add("BSAlignBoneModifier");
  const auto location = at + f.member("BSAlignBoneModifier", "debugTargetLocation");
  const auto rotation = at + f.member("BSAlignBoneModifier", "debugTargetRotation");
  f.set<float>(location, 1.25f);
  f.set<float>(location + 4, 2.0f);
  f.set<float>(location + 8, 3.0f);
  f.set<float>(rotation + 8, 0.75f);
  f.set<float>(rotation + 12, 1.0f);
  const auto bytes = f.bytes();
  EXPECT_TRUE(has_hit(search_bytes(bytes, "1.25"), HkxHitKind::Field, "BSAlignBoneModifier.debugTargetLocation = [1.25, 2, 3, 0]"));
  EXPECT_TRUE(has_hit(search_bytes(bytes, "0.75"), HkxHitKind::Field, "BSAlignBoneModifier.debugTargetRotation = [0, 0, 0.75, 1]"));
  EXPECT_TRUE(search_bytes(bytes, "9.5").hits.empty());
}

// Regression: an enum value showed only as a number, so "MODE_LOOPING" found nothing; a value with no name must still be found by its number.
TEST(HkxSymbolSearch, FindsAnEnumValueByNameAndByNumberAndKeepsAnUnnamedOneNumeric) {
  using fo4::hkx::HkxHitKind;
  BehaviorFixture f;
  const auto clip = f.add("hkbClipGenerator");
  f.set<std::int8_t>(clip + f.member("hkbClipGenerator", "mode"), 1);
  const auto modifier = f.add("BSAlignBoneModifier");
  f.set<std::int8_t>(modifier + f.member("BSAlignBoneModifier", "context"), 9);
  const auto bytes = f.bytes();
  EXPECT_TRUE(has_hit(search_bytes(bytes, "mode_looping"), HkxHitKind::Field, "hkbClipGenerator.mode = MODE_LOOPING (1)"));
  EXPECT_TRUE(has_hit(search_bytes(bytes, "looping"), HkxHitKind::Field, "hkbClipGenerator.mode = MODE_LOOPING (1)"));
  EXPECT_TRUE(has_hit(search_bytes(bytes, "9"), HkxHitKind::Field, "BSAlignBoneModifier.context = 9"));
  EXPECT_TRUE(search_bytes(bytes, "weaponGrip").hits.empty());
}

// Regression: a string with bytes that are not UTF-8 made a hit text the JSON reply could not carry; the bad bytes must be escaped and the rest kept.
TEST(HkxSymbolSearch, AStringWithBytesThatAreNotUtf8IsFoundWithThemEscaped) {
  BehaviorFixture f;
  const auto at = f.add("BSAlignBoneModifier");
  f.text(at + f.member("BSAlignBoneModifier", "targetName"), "caf\xC3\xA9 \xFF" "b");
  const auto result = search_bytes(f.bytes(), "caf");
  EXPECT_TRUE(has_hit(result, fo4::hkx::HkxHitKind::Field, "BSAlignBoneModifier.targetName = caf\xC3\xA9 \\xffb"));
}

namespace {

std::vector<std::byte> modifier_event_packfile() {
  BehaviorFixture f;
  const auto id = f.member("hkbEventBase", "id");
  const auto timer = f.add("hkbTimerModifier");
  f.text(timer + f.member("hkbNode", "name"), "Tick");
  f.set<std::int32_t>(timer + f.member("hkbTimerModifier", "alarmEvent") + id, 1);
  const auto driven = f.add("hkbEventDrivenModifier");
  f.set<std::int32_t>(driven + f.member("hkbEventDrivenModifier", "activateEventId"), 2);
  f.set<std::int32_t>(driven + f.member("hkbEventDrivenModifier", "deactivateEventId"), -1);
  const auto notify = f.add("hkbStateMachineEventPropertyArray");
  const auto stride = fo4::hkx::find_layout("hkbEventProperty")->size;
  const auto rows = f.array(notify + f.member("hkbStateMachineEventPropertyArray", "events"), 2, stride);
  f.set<std::int32_t>(rows + id, 1);
  f.set<std::int32_t>(rows + stride + id, 99);
  const auto layer = f.add("hkbLayer");
  f.set<std::int32_t>(layer + f.member("hkbLayer", "onEventId"), 0);
  f.set<std::int32_t>(layer + f.member("hkbLayer", "offEventId"), -1);
  return f.bytes();
}

bool has_object_site(const fo4::hkx::HkxEventUsage& row, const std::string& member, const std::string& owner) {
  for (const auto& entry : row.sites) {
    if (entry.site == fo4::hkx::HkxEventSite::ObjectMember && entry.member == member && entry.owner == owner) return true;
  }
  return false;
}

}  // namespace

// Regression: an event raised only by a modifier, a notify array or a layer must list that site, or removing the event looks safe.
TEST(HkxSymbolUsage, ListsEventSlotsOfModifiersAndNotifyArraysFromTheLayoutTable) {
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(modifier_event_packfile(), packfile), HkxStatus::Ok);
  fo4::hkx::HkxSymbolUsage usage;
  usage.events = {{0, "Go", true, {}}, {1, "Stop", true, {}}, {2, "Wave", true, {}}};
  fo4::hkx::collect_object_event_sites(packfile, usage);

  ASSERT_EQ(usage.events.size(), 4U);
  EXPECT_TRUE(has_object_site(usage.events[0], "hkbLayer.onEventId", "hkbLayer#3"));
  EXPECT_EQ(usage.events[0].sites.size(), 1U);
  EXPECT_TRUE(has_object_site(usage.events[1], "hkbTimerModifier.alarmEvent", "Tick"));
  EXPECT_TRUE(has_object_site(usage.events[1], "hkbStateMachineEventPropertyArray.events[0]",
                              "hkbStateMachineEventPropertyArray#2"));
  EXPECT_EQ(usage.events[1].sites.size(), 2U);
  EXPECT_TRUE(has_object_site(usage.events[2], "hkbEventDrivenModifier.activateEventId", "hkbEventDrivenModifier#1"));
  EXPECT_EQ(usage.events[2].sites.size(), 1U);
  EXPECT_EQ(usage.events[2].sites[0].object, 1);

  const auto& stray = usage.events[3];
  EXPECT_EQ(stray.event_id, 99);
  EXPECT_FALSE(stray.in_range);
  EXPECT_TRUE(stray.name.empty());
  EXPECT_TRUE(has_object_site(stray, "hkbStateMachineEventPropertyArray.events[1]", "hkbStateMachineEventPropertyArray#2"));
}

// Regression: sites the typed model already lists (clip triggers, machine events) must not come back a second time from the object scan.
TEST(HkxSymbolUsage, TheObjectScanNeverRepeatsASiteTheModelAlreadyDecoded) {
  const auto bytes = behavior_graph_packfile();
  HkxPackfile packfile;
  ASSERT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  HkxGraphModel model;
  ASSERT_EQ(read_behavior_model(packfile, model), HkxStatus::Ok);
  fo4::hkx::HkxSymbolUsage usage;
  fo4::hkx::collect_symbol_usage(model, usage);
  std::vector<std::size_t> before;
  for (const auto& row : usage.events) before.push_back(row.sites.size());

  fo4::hkx::collect_object_event_sites(packfile, usage);
  ASSERT_EQ(usage.events.size(), before.size());
  for (std::size_t at = 0; at < before.size(); ++at) EXPECT_EQ(usage.events[at].sites.size(), before[at]) << "event " << at;
}

namespace {

std::size_t object_member(const HkxPackfile& packfile, const char* class_name, std::size_t nth, const char* member) {
  const auto* layout = fo4::hkx::find_layout(class_name);
  EXPECT_NE(layout, nullptr);
  std::size_t seen = 0;
  for (const auto& object : packfile.objects) {
    if (object.class_name != class_name) continue;
    if (seen++ == nth) return object.offset + *fo4::hkx::find_member_offset(*layout, member);
  }
  ADD_FAILURE() << "no " << class_name << " number " << nth;
  return 0;
}

template <typename T>
std::vector<std::byte> with_member(const std::vector<std::byte>& bytes, const char* class_name, std::size_t nth, const char* member, T value) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  const std::size_t at = object_member(packfile, class_name, nth, member);
  std::memcpy(packfile.sections[packfile.data_section].data.data() + at, &value, sizeof value);
  std::vector<std::byte> out;
  EXPECT_EQ(fo4::hkx::rewrite_packfile(bytes, packfile, out), HkxStatus::Ok);
  return out;
}

template <typename T>
T read_member(const std::vector<std::byte>& bytes, const char* class_name, std::size_t nth, const char* member) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  T value{};
  std::memcpy(&value, packfile.sections[packfile.data_section].data.data() + object_member(packfile, class_name, nth, member), sizeof value);
  return value;
}

std::size_t object_count(const std::vector<std::byte>& bytes, const char* class_name) {
  HkxPackfile packfile;
  EXPECT_EQ(read_packfile(bytes, packfile), HkxStatus::Ok);
  std::size_t count = 0;
  for (const auto& object : packfile.objects) count += object.class_name == class_name ? 1 : 0;
  return count;
}

HkxBehaviorEdit child_edit(HkxBehaviorEditKind kind, std::size_t index, std::int32_t node = -1, float real = kNaN, std::int32_t value = 0) {
  HkxBehaviorEdit edit;
  edit.kind = kind;
  edit.index = index;
  edit.node = node;
  edit.real = real;
  edit.value = value;
  return edit;
}

std::vector<std::byte> selector_packfile(std::int16_t selected, std::int16_t current) {
  BehaviorFixture f;
  const auto graph = f.add("hkbBehaviorGraph");
  const auto clip_a = f.add("hkbClipGenerator");
  const auto clip_b = f.add("hkbClipGenerator");
  const auto clip_c = f.add("hkbClipGenerator");
  const auto selector = f.add("hkbManualSelectorGenerator");
  const auto entries = f.array(selector + f.member("hkbManualSelectorGenerator", "generators"), 3, 8);
  f.point(entries, clip_a);
  f.point(entries + 8, clip_b);
  f.point(entries + 16, clip_c);
  f.set<std::int16_t>(selector + f.member("hkbManualSelectorGenerator", "selectedGeneratorIndex"), selected);
  f.set<std::int16_t>(selector + f.member("hkbManualSelectorGenerator", "currentGeneratorIndex"), current);
  f.set<std::int16_t>(selector + f.member("hkbManualSelectorGenerator", "generatorIndexAtActivate"), current);
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), selector);
  return f.bytes();
}

std::size_t selector_edge(const HkxGraphModel& model, std::int32_t slot) {
  for (std::size_t i = 0; i < model.edges.size(); ++i) {
    if (model.edges[i].role == HkxGraphEdgeRole::Selected && model.edges[i].slot == slot) return i;
  }
  ADD_FAILURE() << "no selected edge in slot " << slot;
  return 0;
}

}  // namespace

// Constraint, regression caught: an appended blend child must land last with its weight and generator, leave every other blender's rows and the sync master index alone, and read back through the decoder; an append that renumbered the sync master would change which child the engine syncs to.
TEST(HkxBehaviorEdit, AddBlendChildAppendsLastAndKeepsTheSyncMaster) {
  const auto bytes = with_member<std::int16_t>(behavior_graph_packfile(), "hkbBlenderGenerator", 1, "indexOfSyncMasterChild", 2);
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const auto clip_c = static_cast<std::int32_t>(before.clips[2].node);
  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::AddBlendChild, 1, clip_c, 0.5f)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  ASSERT_EQ(run.model.blend_children.size(), before.blend_children.size() + 1);
  EXPECT_EQ(run.model.blenders[1].child_count, before.blenders[1].child_count + 1);
  EXPECT_EQ(run.model.blenders[1].index_of_sync_master_child, 2);
  const auto& added = run.model.blend_children[run.model.blenders[1].first_child + before.blenders[1].child_count];
  EXPECT_FLOAT_EQ(added.weight, 0.5f);
  EXPECT_EQ(added.generator_node, clip_c);
  EXPECT_EQ(run.model.blenders[0].child_count, before.blenders[0].child_count);
  EXPECT_EQ(run.model.blenders[2].child_count, before.blenders[2].child_count);
  EXPECT_EQ(read_member<std::int16_t>(run.out, "hkbBlenderGenerator", 1, "indexOfSyncMasterChild"), 2);
}

// Constraint, regression caught: an add that would put a generator inside itself, a non-generator, a weight outside 0 to 1 on a non-parametric blender, or no weight must be refused with its reason and write nothing; the blender validator rejects those children at load.
TEST(HkxBehaviorEdit, AddBlendChildRefusesWhatTheBlenderCannotLoad) {
  const auto bytes = behavior_graph_packfile();
  const HkxGraphModel before = decode_ok();
  const auto clip_c = static_cast<std::int32_t>(before.clips[2].node);
  const auto mix = static_cast<std::int32_t>(before.blenders[0].node);
  const auto effect = static_cast<std::int32_t>(before.effects[0].node);
  struct Case {
    const char* what;
    HkxBehaviorEdit edit;
    const char* reason;
  };
  const Case cases[] = {
      {"blender past the table", child_edit(HkxBehaviorEditKind::AddBlendChild, 9, clip_c, 0.5f), "blender index is outside"},
      {"no weight", child_edit(HkxBehaviorEditKind::AddBlendChild, 0, clip_c), "finite real number"},
      {"weight above one", child_edit(HkxBehaviorEditKind::AddBlendChild, 0, clip_c, 1.5f), "child weight from 0 to 1"},
      {"negative weight", child_edit(HkxBehaviorEditKind::AddBlendChild, 0, clip_c, -0.5f), "child weight from 0 to 1"},
      {"generator past the table", child_edit(HkxBehaviorEditKind::AddBlendChild, 0, 99, 0.5f), "generator node is outside"},
      {"generator is an effect", child_edit(HkxBehaviorEditKind::AddBlendChild, 0, effect, 0.5f), "is not a generator"},
      {"generator is the blender", child_edit(HkxBehaviorEditKind::AddBlendChild, 0, mix, 0.5f), "would contain its own blender"},
  };
  const std::vector<std::byte> sentinel{std::byte{9}};
  for (const Case& entry : cases) {
    SCOPED_TRACE(entry.what);
    std::vector<std::byte> out = sentinel;
    const auto report = apply_behavior_edits(bytes, std::vector{entry.edit}, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find(entry.reason), std::string::npos) << report.refusal;
    EXPECT_EQ(out, sentinel);
  }
  EXPECT_TRUE(run_edits(bytes, {child_edit(HkxBehaviorEditKind::AddBlendChild, 1, clip_c, 7.0f)}).report.ok);
}

// Constraint, regression caught: removing a child before the sync master must move the sync index down with it, or the engine would sync to the neighbour; removing the sync master itself, the last child, or a child of a subtractLastChild blender is refused, and the removed wrapper object must be gone from the file.
TEST(HkxBehaviorEdit, RemoveBlendChildRenumbersTheSyncMasterAndRefusesTheUnsafe) {
  const auto bytes = with_member<std::int16_t>(behavior_graph_packfile(), "hkbBlenderGenerator", 1, "indexOfSyncMasterChild", 2);
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const std::size_t first = before.blenders[1].first_child;
  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::RemoveBlendChild, first)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.report.after.errors, 0U);
  EXPECT_EQ(run.model.blenders[1].child_count, 2U);
  EXPECT_EQ(run.model.blenders[1].index_of_sync_master_child, 1);
  EXPECT_EQ(run.model.nodes.size(), before.nodes.size());
  EXPECT_FLOAT_EQ(run.model.blend_children[first].weight, before.blend_children[first + 1].weight);
  EXPECT_EQ(run.model.blend_children[first + 1].generator_node, before.blend_children[first + 2].generator_node);
  EXPECT_EQ(object_count(run.out, "hkbBlenderGeneratorChild"), object_count(bytes, "hkbBlenderGeneratorChild") - 1);

  struct Case {
    const char* what;
    std::vector<std::byte> source;
    std::size_t index;
    const char* reason;
  };
  const auto plain = behavior_graph_packfile();
  const Case cases[] = {
      {"sync master", bytes, first + 2, "is the sync master"},
      {"child past the table", bytes, 99, "blend child index is outside"},
      {"sync master at zero", plain, 0, "is the sync master"},
      {"subtractLastChild", with_member<std::uint8_t>(plain, "hkbBlenderGenerator", 1, "subtractLastChild", 1), first, "subtractLastChild is set"},
      {"numActiveChildren", with_member<std::int32_t>(plain, "hkbBlenderGenerator", 1, "numActiveChildren", 2), first, "numActiveChildren is not 0"},
      {"beginIntervalIndex", with_member<std::int16_t>(plain, "hkbBlenderGenerator", 1, "beginIntervalIndex", 1), first, "beginIntervalIndex is not 0"},
      {"endIntervalIndex", with_member<std::int16_t>(plain, "hkbBlenderGenerator", 1, "endIntervalIndex", 1), first, "endIntervalIndex is not 0"},
  };
  const std::vector<std::byte> sentinel{std::byte{9}};
  for (const Case& entry : cases) {
    SCOPED_TRACE(entry.what);
    std::vector<std::byte> out = sentinel;
    const auto report = apply_behavior_edits(entry.source, std::vector{child_edit(HkxBehaviorEditKind::RemoveBlendChild, entry.index)}, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find(entry.reason), std::string::npos) << report.refusal;
    EXPECT_EQ(out, sentinel);
  }

  const EditRun last = run_edits(plain, {child_edit(HkxBehaviorEditKind::RemoveBlendChild, 1), child_edit(HkxBehaviorEditKind::RemoveBlendChild, 0)});
  EXPECT_FALSE(last.report.ok);
  EXPECT_EQ(last.report.refused_edit, 1U);
  EXPECT_NE(last.report.refusal.find("keeps at least one child"), std::string::npos) << last.report.refusal;

  const auto active = with_member<std::int32_t>(plain, "hkbBlenderGenerator", 1, "numActiveChildren", 2);
  const HkxGraphModel plain_model = decode_ok();
  std::vector<std::byte> out = sentinel;
  const auto add = apply_behavior_edits(
      active, std::vector{child_edit(HkxBehaviorEditKind::AddBlendChild, 1, static_cast<std::int32_t>(plain_model.clips[2].node), 0.5f)}, out);
  EXPECT_FALSE(add.ok);
  EXPECT_NE(add.refusal.find("numActiveChildren is not 0"), std::string::npos) << add.refusal;
  EXPECT_EQ(out, sentinel);
}

// Constraint, regression caught: a selector append keeps the selected, current and at-activate indexes and adds a Selected edge in the last slot; a selector with a null slot must be refused, because the decoded edges skip it and every slot index after it would be off by one.
TEST(HkxBehaviorEdit, AddSelectorChildAppendsLastAndRefusesANullSlot) {
  const auto bytes = selector_packfile(1, 1);
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const auto selector = static_cast<std::int32_t>(before.nodes.size()) - 1;
  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::AddSelectorChild, 0, selector, kNaN, 0)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.model.edges.size(), before.edges.size() + 1);
  EXPECT_EQ(run.model.edges[selector_edge(run.model, 3)].child, 0);
  EXPECT_EQ(read_member<std::int16_t>(run.out, "hkbManualSelectorGenerator", 0, "selectedGeneratorIndex"), 1);
  EXPECT_EQ(read_member<std::int16_t>(run.out, "hkbManualSelectorGenerator", 0, "currentGeneratorIndex"), 1);

  const std::vector<std::byte> sentinel{std::byte{9}};
  std::vector<std::byte> out = sentinel;
  const auto report = apply_behavior_edits(generator_graph_packfile(), std::vector{child_edit(HkxBehaviorEditKind::AddSelectorChild, 0, 2, kNaN, 0)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("null or foreign slots"), std::string::npos) << report.refusal;
  EXPECT_EQ(out, sentinel);
  out = sentinel;
  const auto not_selector = apply_behavior_edits(bytes, std::vector{child_edit(HkxBehaviorEditKind::AddSelectorChild, 0, 0, kNaN, 1)}, out);
  EXPECT_FALSE(not_selector.ok);
  EXPECT_NE(not_selector.refusal.find("not a hkbManualSelectorGenerator"), std::string::npos) << not_selector.refusal;
  EXPECT_EQ(out, sentinel);
}

// Constraint, regression caught: removing a selector child before the selected one must move selected, current and at-activate down together, or the selector would switch to the neighbour; removing the selected child, or one while current differs from selected, is refused.
TEST(HkxBehaviorEdit, RemoveSelectorChildRenumbersTheSelectedIndexAndRefusesTheUnsafe) {
  const auto bytes = selector_packfile(1, 1);
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::RemoveSelectorChild, selector_edge(before, 0))});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.model.edges.size(), before.edges.size() - 1);
  EXPECT_EQ(run.model.nodes.size(), before.nodes.size());
  EXPECT_EQ(read_member<std::int16_t>(run.out, "hkbManualSelectorGenerator", 0, "selectedGeneratorIndex"), 0);
  EXPECT_EQ(read_member<std::int16_t>(run.out, "hkbManualSelectorGenerator", 0, "currentGeneratorIndex"), 0);
  EXPECT_EQ(read_member<std::int16_t>(run.out, "hkbManualSelectorGenerator", 0, "generatorIndexAtActivate"), 0);

  const EditRun after = run_edits(bytes, {child_edit(HkxBehaviorEditKind::RemoveSelectorChild, selector_edge(before, 2))});
  ASSERT_TRUE(after.report.ok) << after.report.refusal;
  EXPECT_EQ(read_member<std::int16_t>(after.out, "hkbManualSelectorGenerator", 0, "selectedGeneratorIndex"), 1);

  const auto split = with_member<std::int16_t>(bytes, "hkbManualSelectorGenerator", 0, "currentGeneratorIndex", 2);
  struct Case {
    const char* what;
    std::vector<std::byte> source;
    std::size_t edge;
    const char* reason;
  };
  const Case cases[] = {
      {"selected child", bytes, selector_edge(before, 1), "is the selected generator"},
      {"current differs", split, selector_edge(before, 0), "differs from the selected index"},
      {"edge past the table", bytes, 99, "edge index is outside"},
  };
  const std::vector<std::byte> sentinel{std::byte{9}};
  for (const Case& entry : cases) {
    SCOPED_TRACE(entry.what);
    std::vector<std::byte> out = sentinel;
    const auto report = apply_behavior_edits(entry.source, std::vector{child_edit(HkxBehaviorEditKind::RemoveSelectorChild, entry.edge)}, out);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.refusal.find(entry.reason), std::string::npos) << report.refusal;
    EXPECT_EQ(out, sentinel);
  }
}

// Constraint, regression caught: a subtree removal must take a generator nothing points at and no other node, must refuse the root and a generator a parent still references, and the node table must shrink by exactly that node with every other edge kept.
TEST(HkxBehaviorEdit, RemoveSubtreeTakesAnUnreferencedGeneratorAndRefusesTheRootAndTheReferenced) {
  const auto bytes = selector_packfile(1, 1);
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const auto clip_c = static_cast<std::int32_t>(before.edges[selector_edge(before, 2)].child);
  const std::vector<std::byte> sentinel{std::byte{9}};
  std::vector<std::byte> out = sentinel;
  auto report = apply_behavior_edits(bytes, std::vector{child_edit(HkxBehaviorEditKind::RemoveSubtree, 0, clip_c)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("is still pointed at"), std::string::npos) << report.refusal;
  EXPECT_EQ(out, sentinel);
  report = apply_behavior_edits(bytes, std::vector{child_edit(HkxBehaviorEditKind::RemoveSubtree, 0, before.root_node)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("root generator"), std::string::npos) << report.refusal;
  EXPECT_EQ(out, sentinel);
  report = apply_behavior_edits(bytes, std::vector{child_edit(HkxBehaviorEditKind::RemoveSubtree, 0, 99)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("outside the nodes table"), std::string::npos) << report.refusal;
  EXPECT_EQ(out, sentinel);

  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::RemoveSelectorChild, selector_edge(before, 2)),
                                        child_edit(HkxBehaviorEditKind::RemoveSubtree, 0, clip_c)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.model.nodes.size(), before.nodes.size() - 1);
  EXPECT_EQ(run.model.edges.size(), before.edges.size() - 1);
  EXPECT_EQ(object_count(run.out, "hkbClipGenerator"), object_count(bytes, "hkbClipGenerator") - 1);
}

namespace {

// Invariant: blender Inner holds Shared and Other, blender Top (the root) holds Shared, Only and Inner, every sync master is -1; Only is reached by Top's second child alone.
std::vector<std::byte> shared_generator_packfile() {
  BehaviorFixture f;
  const auto node_name = f.member("hkbNode", "name");
  const auto graph = f.add("hkbBehaviorGraph");
  const auto clip = [&](const char* name) {
    const auto at = f.add("hkbClipGenerator");
    f.text(at + node_name, name);
    f.set<std::int16_t>(at + f.member("hkbClipGenerator", "animationBindingIndex"), -1);
    return at;
  };
  const auto shared = clip("Shared");
  const auto only = clip("Only");
  const auto other = clip("Other");
  const auto blender = [&](const char* name, const std::vector<std::size_t>& targets) {
    const auto at = f.add("hkbBlenderGenerator");
    f.text(at + node_name, name);
    f.set<std::int16_t>(at + f.member("hkbBlenderGenerator", "indexOfSyncMasterChild"), -1);
    const auto list = f.array(at + f.member("hkbBlenderGenerator", "children"), targets.size(), 8);
    for (std::size_t i = 0; i < targets.size(); ++i) {
      const auto child = f.add("hkbBlenderGeneratorChild");
      f.set<float>(child + f.member("hkbBlenderGeneratorChild", "weight"), 1.0f);
      f.set<float>(child + f.member("hkbBlenderGeneratorChild", "worldFromModelWeight"), 1.0f);
      f.point(child + f.member("hkbBlenderGeneratorChild", "generator"), targets[i]);
      f.point(list + i * 8, child);
    }
    return at;
  };
  const auto inner = blender("Inner", {shared, other});
  const auto top = blender("Top", {shared, only, inner});
  f.point(graph + f.member("hkbBehaviorGraph", "rootGenerator"), top);
  return f.bytes();
}

std::int32_t node_named(const HkxGraphModel& model, const std::string& name) {
  for (std::size_t i = 0; i < model.nodes.size(); ++i) {
    if (model.nodes[i].name == name) return static_cast<std::int32_t>(i);
  }
  return -1;
}

std::size_t blender_named(const HkxGraphModel& model, const std::string& name) {
  for (std::size_t i = 0; i < model.blenders.size(); ++i) {
    if (model.nodes[model.blenders[i].node].name == name) return i;
  }
  ADD_FAILURE() << "no blender " << name;
  return 0;
}

}  // namespace

// Constraint, regression caught: a generator whose only referrer is the removed blend child must stay in the file as an unreferenced node (the cascade must not take it, or a later RemoveSubtree could never decide), and RemoveSubtree must then take it; clearing the wrapper's generator pointer first is what keeps the cascade away from it.
TEST(HkxBehaviorEdit, RemovingABlendChildLeavesItsOnlyGeneratorAsAnUnreferencedNode) {
  const auto bytes = shared_generator_packfile();
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const std::int32_t only = node_named(before, "Only");
  ASSERT_GE(only, 0);
  const std::size_t top = blender_named(before, "Top");
  ASSERT_EQ(before.blenders[top].index_of_sync_master_child, -1);
  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::RemoveBlendChild, before.blenders[top].first_child + 1)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.model.blenders[blender_named(run.model, "Top")].index_of_sync_master_child, -1);
  EXPECT_EQ(run.model.nodes.size(), before.nodes.size());
  EXPECT_EQ(object_count(run.out, "hkbBlenderGeneratorChild"), object_count(bytes, "hkbBlenderGeneratorChild") - 1);
  EXPECT_EQ(node_named(run.model, "Only"), only);
  EXPECT_EQ(run.model.blenders[blender_named(run.model, "Top")].child_count, 2U);

  const EditRun taken = run_edits(run.out, {child_edit(HkxBehaviorEditKind::RemoveSubtree, 0, only)});
  ASSERT_TRUE(taken.report.ok) << taken.report.refusal;
  EXPECT_EQ(taken.model.nodes.size(), before.nodes.size() - 1);
  EXPECT_EQ(node_named(taken.model, "Only"), -1);
}

// Constraint, regression caught: a generator another blender still reaches must survive the removal of one blender's child and stay reachable through that blender, and RemoveSubtree must refuse it while that child points at it.
TEST(HkxBehaviorEdit, RemovingABlendChildKeepsAGeneratorAnotherBlenderStillReaches) {
  const auto bytes = shared_generator_packfile();
  HkxGraphModel before;
  ASSERT_EQ(decode(bytes, before), HkxStatus::Ok);
  const std::int32_t shared = node_named(before, "Shared");
  ASSERT_GE(shared, 0);
  const EditRun run = run_edits(bytes, {child_edit(HkxBehaviorEditKind::RemoveBlendChild, before.blenders[blender_named(before, "Top")].first_child)});
  ASSERT_TRUE(run.report.ok) << run.report.refusal;
  EXPECT_EQ(run.model.nodes.size(), before.nodes.size());
  const auto& inner = run.model.blenders[blender_named(run.model, "Inner")];
  ASSERT_EQ(inner.child_count, 2U);
  EXPECT_EQ(run.model.blend_children[inner.first_child].generator_node, shared);

  std::vector<std::byte> out;
  const auto report = apply_behavior_edits(run.out, std::vector{child_edit(HkxBehaviorEditKind::RemoveSubtree, 0, shared)}, out);
  EXPECT_FALSE(report.ok);
  EXPECT_NE(report.refusal.find("is still pointed at"), std::string::npos) << report.refusal;
}
