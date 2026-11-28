#include "fo4/c_api_hkx.h"
#include "fo4/c_api_hkx_behavior.h"
#include "hkx_behavior_graph_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace fo4::test::hkx;

namespace {

struct HkxDeleter {
  void operator()(radicle_hkx* hkx) const { radicle_hkx_destroy(hkx); }
};
using HkxHandle = std::unique_ptr<radicle_hkx, HkxDeleter>;

HkxHandle open(const std::vector<std::byte>& bytes) {
  radicle_hkx* raw = nullptr;
  EXPECT_EQ(radicle_hkx_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &raw), RADICLE_STATUS_OK);
  return HkxHandle(raw);
}

std::string take_text(uint8_t* bytes, size_t size) {
  if (!bytes) return {};
  std::string text(reinterpret_cast<const char*>(bytes), size);
  radicle_free_bytes(bytes);
  return text;
}

using TextGetter = radicle_status (*)(const radicle_hkx*, size_t, uint8_t**, size_t*);

std::string text_of(const radicle_hkx* hkx, TextGetter getter, size_t index) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(getter(hkx, index, &bytes, &size), RADICLE_STATUS_OK);
  return take_text(bytes, size);
}

}  // namespace

TEST(CApiHkxBehavior, SummaryCountsEveryTable) {
  const auto hkx = open(behavior_graph_packfile());
  radicle_hkx_behavior_summary summary{};
  ASSERT_EQ(radicle_hkx_behavior_read_summary(hkx.get(), &summary), RADICLE_STATUS_OK);
  EXPECT_EQ(summary.event_count, 3U);
  EXPECT_EQ(summary.variable_count, 3U);
  EXPECT_EQ(summary.state_machine_count, 1U);
  EXPECT_EQ(summary.state_count, 2U);
  EXPECT_EQ(summary.transition_count, 2U);
  EXPECT_EQ(summary.blender_count, 3U);
  EXPECT_EQ(summary.blend_child_count, 7U);
  EXPECT_EQ(summary.clip_count, 3U);
  EXPECT_EQ(summary.binding_count, 2U);
  EXPECT_GE(summary.root_node, 0);

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_hkx_behavior_graph_name(hkx.get(), &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(bytes, size), "TestGraph");
}

TEST(CApiHkxBehavior, EventsAndVariablesCarryNamesAndValues) {
  const auto hkx = open(behavior_graph_packfile());
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_event_name, 1), "Stop");
  radicle_hkx_behavior_event_info event{};
  ASSERT_EQ(radicle_hkx_behavior_event_read_info(hkx.get(), 2, &event), RADICLE_STATUS_OK);
  EXPECT_EQ(event.flags, 0x102U);

  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_variable_name, 2), "vLook");
  radicle_hkx_behavior_variable_info variable{};
  ASSERT_EQ(radicle_hkx_behavior_variable_read_info(hkx.get(), 2, &variable), RADICLE_STATUS_OK);
  EXPECT_EQ(variable.type, 6);
  EXPECT_EQ(variable.has_vector, 1);
  EXPECT_FLOAT_EQ(variable.vector[1], 2.0f);
  ASSERT_EQ(radicle_hkx_behavior_variable_read_info(hkx.get(), 1, &variable), RADICLE_STATUS_OK);
  EXPECT_EQ(variable.type, 4);
  EXPECT_FLOAT_EQ(variable.real_value, 2.5f);
  EXPECT_EQ(variable.has_vector, 0);
}

TEST(CApiHkxBehavior, StatesTransitionsAndConditionsAreReadable) {
  const auto hkx = open(behavior_graph_packfile());
  radicle_hkx_behavior_state_machine_info machine{};
  ASSERT_EQ(radicle_hkx_behavior_state_machine_read_info(hkx.get(), 0, &machine), RADICLE_STATUS_OK);
  EXPECT_EQ(machine.start_state_id, 7);
  EXPECT_EQ(machine.state_count, 2U);
  EXPECT_EQ(machine.wildcard_count, 1U);
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_node_name, machine.node), "Root");
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_node_class, machine.node), "hkbStateMachine");

  radicle_hkx_behavior_state_info state{};
  ASSERT_EQ(radicle_hkx_behavior_state_read_info(hkx.get(), 1, &state), RADICLE_STATUS_OK);
  EXPECT_EQ(state.state_id, 9);
  EXPECT_EQ(state.enable, 1);
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_state_name, 1), "Mix");

  radicle_hkx_behavior_transition_info transition{};
  ASSERT_EQ(radicle_hkx_behavior_transition_read_info(hkx.get(), 0, &transition), RADICLE_STATUS_OK);
  EXPECT_EQ(transition.event_id, 1);
  EXPECT_EQ(transition.to_state_id, 9);
  EXPECT_EQ(transition.condition_kind, 2);
  EXPECT_EQ(transition.trigger_enter_event_id, 1);
  EXPECT_FLOAT_EQ(transition.trigger_enter_time, 0.25f);
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_transition_condition, 0), "x > 1");
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_transition_condition_class, 0), "hkbStringCondition");
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_node_name, static_cast<size_t>(transition.effect_node)), "fade");

  size_t effects = 0;
  ASSERT_EQ(radicle_hkx_behavior_effect_count(hkx.get(), &effects), RADICLE_STATUS_OK);
  ASSERT_EQ(effects, 1U);
  radicle_hkx_behavior_effect_info effect{};
  ASSERT_EQ(radicle_hkx_behavior_effect_read_info(hkx.get(), 0, &effect), RADICLE_STATUS_OK);
  EXPECT_EQ(effect.node, static_cast<size_t>(transition.effect_node));
  EXPECT_FLOAT_EQ(effect.duration, 0.5f);
  EXPECT_FLOAT_EQ(effect.start_fraction, 0.25f);
  EXPECT_EQ(effect.flags, 6U);
  EXPECT_EQ(effect.end_mode, 2);
  EXPECT_EQ(effect.blend_curve, 3);
  EXPECT_EQ(radicle_hkx_behavior_effect_read_info(hkx.get(), 1, &effect), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_effect_read_info(hkx.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_effect_count(hkx.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_hkx_behavior_transition_read_info(hkx.get(), 1, &transition), RADICLE_STATUS_OK);
  EXPECT_EQ(transition.state, -1);
  EXPECT_EQ(transition.condition_kind, 1);
}

TEST(CApiHkxBehavior, BlendersClipsAndBindingsAreReadable) {
  const auto hkx = open(behavior_graph_packfile());
  radicle_hkx_behavior_blender_info blender{};
  ASSERT_EQ(radicle_hkx_behavior_blender_read_info(hkx.get(), 1, &blender), RADICLE_STATUS_OK);
  EXPECT_EQ(blender.parametric, 1);
  EXPECT_EQ(blender.resolved, 1);
  ASSERT_EQ(blender.child_count, 3U);
  radicle_hkx_behavior_blend_child_info child{};
  ASSERT_EQ(radicle_hkx_behavior_blend_child_read_info(hkx.get(), blender.first_child + 2, &child), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(child.weight, 50.0f);
  EXPECT_FLOAT_EQ(child.contribution, 0.5f);

  ASSERT_EQ(radicle_hkx_behavior_blender_read_info(hkx.get(), 2, &blender), RADICLE_STATUS_OK);
  EXPECT_EQ(blender.resolved, 0);
  EXPECT_EQ(blender.blend_parameter_variable, 1);

  radicle_hkx_behavior_clip_info clip{};
  ASSERT_EQ(radicle_hkx_behavior_clip_read_info(hkx.get(), 0, &clip), RADICLE_STATUS_OK);
  EXPECT_EQ(clip.mode, 1);
  EXPECT_FLOAT_EQ(clip.playback_speed, 1.5f);
  ASSERT_EQ(clip.trigger_count, 1U);
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_clip_animation, 0), "Animations\\A.hkt");
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_clip_bundle, 0), "");
  radicle_hkx_behavior_clip_trigger_info trigger{};
  ASSERT_EQ(radicle_hkx_behavior_clip_trigger_read_info(hkx.get(), 0, 0, &trigger), RADICLE_STATUS_OK);
  EXPECT_EQ(trigger.event_id, 2);
  EXPECT_EQ(trigger.acyclic, 1);

  radicle_hkx_behavior_binding_info binding{};
  ASSERT_EQ(radicle_hkx_behavior_binding_read_info(hkx.get(), 1, &binding), RADICLE_STATUS_OK);
  EXPECT_EQ(binding.owner_kind, 3);
  EXPECT_EQ(binding.variable_index, 1);
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_binding_member_path, 1), "blendParameter");
}

TEST(CApiHkxBehavior, OutOfRangeIndexesAndMissingArgumentsAreInvalidArguments) {
  const auto hkx = open(behavior_graph_packfile());
  radicle_hkx_behavior_event_info event{};
  EXPECT_EQ(radicle_hkx_behavior_event_read_info(hkx.get(), 3, &event), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_event_read_info(hkx.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_event_read_info(nullptr, 0, &event), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_read_summary(hkx.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_hkx_behavior_event_name(hkx.get(), 99, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_event_name(hkx.get(), 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_hkx_behavior_clip_trigger_info trigger{};
  EXPECT_EQ(radicle_hkx_behavior_clip_trigger_read_info(hkx.get(), 0, 1, &trigger), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_clip_trigger_read_info(hkx.get(), 9, 0, &trigger), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiHkxBehavior, AFileWithoutAGraphIsNotFoundAndAForeignGraphIsAParseError) {
  const auto* layout = fo4::hkx::find_layout("hkbClipGenerator");
  const auto without = open(single_object_packfile("hkbClipGenerator", layout->signature,
                                                   std::vector<std::byte>(layout->size, std::byte{0}), {}));
  radicle_hkx_behavior_summary summary{};
  EXPECT_EQ(radicle_hkx_behavior_read_summary(without.get(), &summary), RADICLE_STATUS_NOT_FOUND);

  BehaviorFixtureOptions options;
  options.machine_signature = 1234U;
  const auto foreign = open(behavior_graph_packfile(options));
  EXPECT_EQ(radicle_hkx_behavior_read_summary(foreign.get(), &summary), RADICLE_STATUS_PARSE_ERROR);
}

TEST(CApiHkxBehavior, GeneratorKindsAndEdgesAreReadable) {
  const auto hkx = open(generator_graph_packfile());
  size_t count = 0;
  ASSERT_EQ(radicle_hkx_behavior_edge_count(hkx.get(), &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 15U);

  radicle_hkx_behavior_node_info node{};
  ASSERT_EQ(radicle_hkx_behavior_node_read_info(hkx.get(), 4, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node.kind, 5);
  EXPECT_EQ(node.kind_index, 0U);
  EXPECT_EQ(text_of(hkx.get(), radicle_hkx_behavior_node_class, 4), "hkbLayerGenerator");
  ASSERT_EQ(radicle_hkx_behavior_node_read_info(hkx.get(), 6, &node), RADICLE_STATUS_OK);
  EXPECT_EQ(node.kind, 12);

  radicle_hkx_behavior_edge_info edge{};
  ASSERT_EQ(radicle_hkx_behavior_edge_read_info(hkx.get(), 4, &edge), RADICLE_STATUS_OK);
  EXPECT_EQ(edge.parent, 4U);
  EXPECT_EQ(edge.child, 1);
  EXPECT_EQ(edge.role, 4);
  EXPECT_EQ(edge.slot, 1);
  EXPECT_FLOAT_EQ(edge.weight, 0.75f);
  EXPECT_EQ(edge.has_bone_weights, 1);
  ASSERT_EQ(radicle_hkx_behavior_edge_read_info(hkx.get(), 10, &edge), RADICLE_STATUS_OK);
  EXPECT_EQ(edge.role, 2);
  EXPECT_EQ(edge.child, 1);

  EXPECT_EQ(radicle_hkx_behavior_edge_read_info(hkx.get(), 15, &edge), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_edge_read_info(hkx.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_edge_count(hkx.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_hkx_behavior_edge_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiHkxBehavior, AGraphWithNoGeneratorsHasNoEdges) {
  const auto hkx = open(behavior_graph_packfile());
  size_t count = 99;
  ASSERT_EQ(radicle_hkx_behavior_edge_count(hkx.get(), &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0U);
}
