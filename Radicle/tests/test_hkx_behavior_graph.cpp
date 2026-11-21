#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "hkx_behavior_graph_fixture.hpp"
#include "hkx_test_fixture.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace fo4::test::hkx;
using fo4::hkx::HkxGraphConditionKind;
using fo4::hkx::HkxGraphEdge;
using fo4::hkx::HkxGraphEdgeRole;
using fo4::hkx::HkxGraphModel;
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
