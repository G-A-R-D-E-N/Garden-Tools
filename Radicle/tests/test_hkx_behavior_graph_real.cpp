#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/hkx/clip_roles.hpp"
#include "fo4/hkx/packfile.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using fo4::hkx::HkxGraphEdge;
using fo4::hkx::HkxGraphEdgeRole;
using fo4::hkx::HkxGraphEffect;
using fo4::hkx::HkxGraphModel;
using fo4::hkx::HkxGraphNodeKind;
using fo4::hkx::HkxPackfile;
using fo4::hkx::HkxStatus;

namespace {

std::filesystem::path repository_root() {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::current_path(ec);
  for (int level = 0; level < 8 && !dir.empty(); ++level) {
    if (std::filesystem::is_directory(dir / "Radicle" / "testdata" / "hkx", ec)) return dir;
    if (dir == dir.root_path()) break;
    dir = dir.parent_path();
  }
  return {};
}

std::vector<std::byte> read_binary_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::byte> bytes;
  char buffer[4096];
  while (stream.read(buffer, static_cast<std::streamsize>(sizeof buffer)) || stream.gcount() > 0) {
    for (std::streamsize i = 0; i < stream.gcount(); ++i) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[i])));
    }
  }
  return bytes;
}

// Constraint: CI restores the committed HKX fixture, so a missing file there fails instead of skipping; the caller returns when bytes stays empty.
void load_fixture(const char* under_meshes, std::vector<std::byte>& bytes) {
  const auto root = repository_root();
  const auto file = root / "Radicle" / "tests" / "fixtures" / "hkx" / "Meshes" / "Actors" / under_meshes;
  if (root.empty() || !std::filesystem::exists(file)) {
    if (std::getenv("CI") != nullptr) {
      ADD_FAILURE() << "CI is missing the committed " << file.string();
      return;
    }
    GTEST_SKIP() << "no committed " << file.string();
  }
  bytes = read_binary_file(file);
}

bool decode_fixture(const char* under_meshes, HkxGraphModel& model, HkxPackfile& packfile) {
  std::vector<std::byte> bytes;
  load_fixture(under_meshes, bytes);
  if (bytes.empty()) return false;
  EXPECT_EQ(fo4::hkx::read_packfile(bytes, packfile), HkxStatus::Ok);
  EXPECT_EQ(fo4::hkx::read_behavior_model(packfile, model), HkxStatus::Ok);
  return true;
}

std::string event_name(const HkxGraphModel& model, std::int32_t id) {
  return id >= 0 && static_cast<std::size_t>(id) < model.events.size() ? model.events[static_cast<std::size_t>(id)].name : std::string{};
}

const fo4::hkx::HkxGraphBlender* blender_named(const HkxGraphModel& model, const std::string& name) {
  for (const auto& blender : model.blenders) {
    if (model.nodes[blender.node].name == name) return &blender;
  }
  return nullptr;
}

// Invariant: every index a row stores is valid in its table or -1, and the runs tile their tables.
void expect_consistent(const HkxGraphModel& model) {
  const auto node_ok = [&](std::int32_t node) { return node == -1 || (node >= 0 && static_cast<std::size_t>(node) < model.nodes.size()); };
  EXPECT_TRUE(node_ok(model.root_node));
  std::size_t transitions = 0;
  for (const auto& machine : model.state_machines) {
    EXPECT_LT(machine.node, model.nodes.size());
    EXPECT_LE(machine.first_state + machine.state_count, model.states.size());
    EXPECT_LE(machine.first_wildcard + machine.wildcard_count, model.transitions.size());
    transitions += machine.wildcard_count;
  }
  for (const auto& state : model.states) {
    EXPECT_TRUE(node_ok(state.generator_node));
    EXPECT_LE(state.first_transition + state.transition_count, model.transitions.size());
    transitions += state.transition_count;
  }
  EXPECT_EQ(transitions, model.transitions.size());
  for (const auto& transition : model.transitions) EXPECT_TRUE(node_ok(transition.effect_node));
  for (const auto& blender : model.blenders) {
    EXPECT_LE(blender.first_child + blender.child_count, model.blend_children.size());
    float total = 0.0f;
    bool positive = false;
    for (std::size_t i = 0; i < blender.child_count; ++i) {
      const auto& child = model.blend_children[blender.first_child + i];
      EXPECT_TRUE(node_ok(child.generator_node));
      total += child.contribution;
      positive = positive || child.weight > 0.0f;
    }
    if (blender.resolved && positive) {
      EXPECT_NEAR(total, 1.0f, 1e-4f);
    }
  }
  std::size_t previous_parent = 0;
  for (const auto& edge : model.edges) {
    EXPECT_LT(edge.parent, model.nodes.size());
    EXPECT_GE(edge.child, 0);
    EXPECT_LT(static_cast<std::size_t>(edge.child), model.nodes.size());
    EXPECT_GE(edge.parent, previous_parent);
    EXPECT_GE(model.nodes[edge.parent].kind, HkxGraphNodeKind::Selector);
    previous_parent = edge.parent;
  }
  for (const auto& clip : model.clips) {
    EXPECT_LT(clip.node, model.nodes.size());
    EXPECT_EQ(model.nodes[clip.node].kind, HkxGraphNodeKind::Clip);
  }
  for (const auto& variable : model.variables) {
    if (variable.has_vector) {
      EXPECT_TRUE(variable.type >= 6 && variable.type <= 8) << variable.name;
    }
  }
}

}  // namespace

TEST(HkxBehaviorGraphRealData, ReadsTheLocomotionBlendGraph) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/Locomotion_8wayBlend.hkx", model, packfile)) return;
  expect_consistent(model);

  EXPECT_EQ(model.graph_name, "Locomotion_8wayBlend.hkb");
  ASSERT_EQ(model.events.size(), 26U);
  EXPECT_EQ(model.events[0].name, "FootLeft");
  EXPECT_EQ(model.events[5].name, "walkStart");
  ASSERT_EQ(model.variables.size(), 9U);
  EXPECT_EQ(model.variables[0].name, "Direction");
  EXPECT_FLOAT_EQ(model.variables[0].real_value, 1.0f);
  EXPECT_EQ(model.variables[1].name, "Speed");
  EXPECT_EQ(model.variables[7].name, "iLocomotionSpeedState");
  EXPECT_EQ(model.variables[7].type, static_cast<std::int8_t>(fo4::hkx::HkxVariableType::Int32));

  ASSERT_EQ(model.state_machines.size(), 2U);
  ASSERT_EQ(model.states.size(), 5U);
  EXPECT_EQ(model.nodes[model.state_machines[0].node].name, "LocomotionRoot");
  EXPECT_EQ(model.nodes[model.state_machines[1].node].name, "MeleeRun_SM");
  EXPECT_EQ(model.root_node, static_cast<std::int32_t>(model.state_machines[0].node));
  EXPECT_EQ(model.states[0].name, "MeleeWalkState");
  EXPECT_EQ(model.states[0].state_id, 0);
  EXPECT_EQ(model.states[1].name, "MeleeWalkRunState");
  EXPECT_EQ(model.states[1].state_id, 2);
  EXPECT_EQ(model.states[2].name, "MeleeRunState");
  EXPECT_EQ(model.states[2].state_id, 1);
  ASSERT_GE(model.states[2].generator_node, 0);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.states[2].generator_node)].name, "MeleeRun_SM");
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.states[2].generator_node)].kind, HkxGraphNodeKind::StateMachine);

  ASSERT_EQ(model.transitions.size(), 5U);
  EXPECT_EQ(model.state_machines[0].wildcard_count, 3U);
  EXPECT_EQ(model.transitions[0].state, -1);
  EXPECT_EQ(event_name(model, model.transitions[0].event_id), "walkStart");
  EXPECT_EQ(model.transitions[0].to_state_id, 0);
  EXPECT_EQ(model.transitions[0].flags, 2560U);
  ASSERT_GE(model.transitions[0].effect_node, 0);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.transitions[0].effect_node)].name, "blend_0.50s_Sync_0.5s");
  ASSERT_EQ(model.effects.size(), 2U);
  const auto effect_of = [&](std::int32_t node) -> const HkxGraphEffect* {
    for (const auto& effect : model.effects) {
      if (static_cast<std::int32_t>(effect.node) == node) return &effect;
    }
    return nullptr;
  };
  const HkxGraphEffect* sync_half_second = effect_of(model.transitions[0].effect_node);
  ASSERT_NE(sync_half_second, nullptr);
  EXPECT_FLOAT_EQ(sync_half_second->duration, 0.5f);
  EXPECT_FLOAT_EQ(sync_half_second->start_fraction, 0.0f);
  EXPECT_EQ(sync_half_second->flags, 0U);
  EXPECT_EQ(sync_half_second->end_mode, 0);
  EXPECT_EQ(sync_half_second->blend_curve, 0);
  const HkxGraphEffect* eight_frames = nullptr;
  for (const auto& effect : model.effects) {
    if (model.nodes[effect.node].name == "blend_0.50s_Sync_8f") eight_frames = &effect;
  }
  ASSERT_NE(eight_frames, nullptr);
  EXPECT_FLOAT_EQ(eight_frames->duration, 8.0f / 30.0f);
  EXPECT_EQ(event_name(model, model.transitions[1].event_id), "runStart");
  EXPECT_EQ(model.transitions[1].to_state_id, 1);
  EXPECT_EQ(model.transitions[2].to_state_id, 2);
  EXPECT_EQ(model.transitions[3].state, 3);
  EXPECT_EQ(event_name(model, model.transitions[3].event_id), "runbackstart");
  EXPECT_EQ(model.transitions[3].to_state_id, 1);
  EXPECT_EQ(event_name(model, model.transitions[4].event_id), "runForwardStart");
  EXPECT_EQ(model.transitions[4].to_state_id, 0);

  ASSERT_EQ(model.blenders.size(), 12U);
  ASSERT_EQ(model.blend_children.size(), 42U);
  const auto* eight_way = blender_named(model, "WalkForwardBlend");
  ASSERT_NE(eight_way, nullptr);
  EXPECT_EQ(eight_way->child_count, 8U);
  EXPECT_FLOAT_EQ(model.blend_children[eight_way->first_child + 1].weight, 0.125f);
  EXPECT_FLOAT_EQ(model.blend_children[eight_way->first_child + 7].weight, 0.875f);

  const auto* walk_run = blender_named(model, "MeleeWalkRunBlend_Forward");
  ASSERT_NE(walk_run, nullptr);
  EXPECT_TRUE(walk_run->parametric);
  EXPECT_EQ(walk_run->blend_parameter_variable, 1);
  EXPECT_EQ(model.variables[1].name, "Speed");
  EXPECT_FALSE(walk_run->resolved);
  ASSERT_EQ(walk_run->child_count, 2U);
  EXPECT_FLOAT_EQ(model.blend_children[walk_run->first_child].weight, 80.0f);
  EXPECT_FLOAT_EQ(model.blend_children[walk_run->first_child + 1].weight, 370.0f);

  ASSERT_EQ(model.clips.size(), 34U);
  EXPECT_EQ(model.clips[0].animation, "Animations\\1HM\\WalkForward.hkt");
  EXPECT_EQ(model.nodes[model.clips[0].node].name, "WalkForward");
  EXPECT_EQ(model.clips[0].mode, 1);
}

TEST(HkxBehaviorGraphRealData, ReadsTheRaiderRootVariableAndEventTables) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/RaiderRootBehavior.hkx", model, packfile)) return;
  expect_consistent(model);

  EXPECT_EQ(model.graph_name, "RaiderRootBehavior.hkb");
  EXPECT_EQ(model.events.size(), 1058U);
  EXPECT_EQ(model.events[0].name, "moveStart");
  ASSERT_EQ(model.variables.size(), 299U);
  EXPECT_EQ(model.variables[0].name, "fSpeedWalk");
  EXPECT_FLOAT_EQ(model.variables[0].real_value, 85.0f);
  EXPECT_EQ(model.variables[1].name, "fSpeedRun");
  EXPECT_FLOAT_EQ(model.variables[1].real_value, 300.0f);

  EXPECT_EQ(model.state_machines.size(), 16U);
  EXPECT_EQ(model.states.size(), 48U);
  EXPECT_EQ(model.clips.size(), 58U);
  EXPECT_EQ(model.blenders.size(), 2U);
  EXPECT_EQ(model.blend_children.size(), 5U);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.root_node)].name, "RootBehavior");
  EXPECT_EQ(model.states[0].name, "RaiderRoot");
  EXPECT_EQ(model.states[1].name, "AnimateToRagdoll");
  EXPECT_EQ(model.states[1].state_id, 4);
}

TEST(HkxBehaviorGraphRealData, ReadsTheSingleAnimFurnitureGraph) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/SingleAnimFurniture.hkx", model, packfile)) return;
  expect_consistent(model);
  EXPECT_EQ(model.graph_name, "SingleAnimFurniture.hkb");
  ASSERT_EQ(model.events.size(), 46U);
  EXPECT_EQ(model.events[0].name, "activateStartFromStand");
  ASSERT_EQ(model.variables.size(), 3U);
  EXPECT_EQ(model.variables[0].name, "bAnimationDriven");
  ASSERT_EQ(model.state_machines.size(), 1U);
  ASSERT_EQ(model.states.size(), 1U);
  EXPECT_EQ(model.states[0].name, "Activate");
  ASSERT_EQ(model.clips.size(), 1U);
  EXPECT_EQ(model.clips[0].animation, "Animations\\Furniture\\DoctorUseSyringe\\Activate.hkt");
  EXPECT_EQ(model.clips[0].mode, 0);
}

TEST(HkxBehaviorGraphRealData, ReadsTheCritterGraphWhoseOnlyStateHoldsAGraphSwap) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("DLC03/RadChicken/Behaviors/CritterRootBehavior.hkx", model, packfile)) return;
  expect_consistent(model);
  EXPECT_EQ(model.graph_name, "CritterRootBehavior.hkb");
  ASSERT_EQ(model.events.size(), 17U);
  EXPECT_EQ(model.events[0].name, "MoveStart");
  ASSERT_EQ(model.variables.size(), 9U);
  EXPECT_EQ(model.variables[4].name, "RunSpeedMult");
  EXPECT_FLOAT_EQ(model.variables[4].real_value, 1.0f);
  EXPECT_EQ(model.variables[7].name, "bGraphDriven");
  EXPECT_EQ(model.variables[7].int_value, 1);
  ASSERT_EQ(model.states.size(), 1U);
  EXPECT_EQ(model.states[0].name, "DefaultState");
  ASSERT_GE(model.states[0].generator_node, 0);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(model.states[0].generator_node)].class_name,
            "BSBehaviorGraphSwapGenerator");
  EXPECT_TRUE(model.clips.empty());
}

TEST(HkxBehaviorGraphRealData, TheClipsAgreeWithTheExistingClipRoleReader) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/Locomotion_8wayBlend.hkx", model, packfile)) return;
  std::vector<fo4::hkx::HkxBehaviorClip> clips;
  ASSERT_EQ(fo4::hkx::read_behavior_clips(packfile, clips), HkxStatus::Ok);
  ASSERT_EQ(clips.size(), model.clips.size());
  for (std::size_t i = 0; i < clips.size(); ++i) {
    EXPECT_EQ(clips[i].animation, model.clips[i].animation);
    EXPECT_EQ(clips[i].generator, model.nodes[model.clips[i].node].name);
    EXPECT_EQ(clips[i].mode, model.clips[i].mode);
    EXPECT_FLOAT_EQ(clips[i].playback_speed, model.clips[i].playback_speed);
  }
}

TEST(HkxBehaviorGraphRealData, EveryCommittedBehaviorFixtureDecodesConsistently) {
  const char* const files[] = {
      "Character/Behaviors/WeaponBehavior.hkx",
      "Character/Behaviors/SyncedAnimBehavior.hkx",
  };
  for (const char* file : files) {
    HkxGraphModel model;
    HkxPackfile packfile;
    if (!decode_fixture(file, model, packfile)) return;
    SCOPED_TRACE(file);
    expect_consistent(model);
    EXPECT_FALSE(model.events.empty());
    EXPECT_FALSE(model.state_machines.empty());
  }
}

namespace {

std::int32_t node_named(const HkxGraphModel& model, const std::string& name) {
  for (std::size_t i = 0; i < model.nodes.size(); ++i) {
    if (model.nodes[i].name == name) return static_cast<std::int32_t>(i);
  }
  return -1;
}

std::size_t edges_with_role(const HkxGraphModel& model, HkxGraphEdgeRole role) {
  std::size_t count = 0;
  for (const auto& edge : model.edges) {
    if (edge.role == role) ++count;
  }
  return count;
}

std::vector<HkxGraphEdge> edges_of(const HkxGraphModel& model, const std::string& parent) {
  std::vector<HkxGraphEdge> out;
  const std::int32_t index = node_named(model, parent);
  if (index < 0) return out;
  for (const auto& edge : model.edges) {
    if (edge.parent == static_cast<std::size_t>(index)) out.push_back(edge);
  }
  return out;
}

std::size_t nodes_of_kind(const HkxGraphModel& model, HkxGraphNodeKind kind) {
  std::size_t count = 0;
  for (const auto& node : model.nodes) {
    if (node.kind == kind) ++count;
  }
  return count;
}

const std::string& child_name(const HkxGraphModel& model, const HkxGraphEdge& edge) {
  return model.nodes[static_cast<std::size_t>(edge.child)].name;
}

}  // namespace

TEST(HkxBehaviorGraphRealData, ReadsTheOffsetSelectorAndPoseMatchingEdgesOfTheRaiderRootGraph) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/RaiderRootBehavior.hkx", model, packfile)) return;
  expect_consistent(model);
  EXPECT_EQ(model.nodes.size(), 104U);
  ASSERT_EQ(model.edges.size(), 71U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::Wrapped), 10U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::OffsetClip), 1U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::Selected), 52U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::BlendChild), 8U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::Selector), 8U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::PoseMatching), 4U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::ModifierGenerator), 9U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::OffsetAnimation), 1U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::BehaviorGraphSwap), 1U);

  const auto offset = edges_of(model, "PipboyOffsetAnimationGen");
  ASSERT_EQ(offset.size(), 2U);
  EXPECT_EQ(offset[0].role, HkxGraphEdgeRole::Wrapped);
  EXPECT_EQ(child_name(model, offset[0]), "SwapGenerator_CameraPitch_Blend");
  EXPECT_EQ(offset[1].role, HkxGraphEdgeRole::OffsetClip);
  EXPECT_EQ(child_name(model, offset[1]), "PipboyOffset");
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(offset[1].child)].kind, HkxGraphNodeKind::Clip);

  const auto selector = edges_of(model, "DeathHitLocationAnimMSG");
  ASSERT_EQ(selector.size(), 25U);
  EXPECT_EQ(selector[0].slot, 0);
  EXPECT_EQ(child_name(model, selector[0]), "DeathAnimChestMSG");
  EXPECT_EQ(selector[1].slot, 1);
  EXPECT_EQ(child_name(model, selector[1]), "DeathAnimHead State");
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(selector[1].child)].kind, HkxGraphNodeKind::StateMachine);
  EXPECT_EQ(selector[24].slot, 24);

  const auto pose = edges_of(model, "Reanimate Pose Matcher");
  ASSERT_EQ(pose.size(), 2U);
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(node_named(model, "Reanimate Pose Matcher"))].kind,
            HkxGraphNodeKind::PoseMatching);
  EXPECT_EQ(child_name(model, pose[0]), "GetUpFaceDown");
  EXPECT_EQ(child_name(model, pose[1]), "GetUpFaceUp");
  for (const auto& edge : pose) {
    EXPECT_EQ(edge.role, HkxGraphEdgeRole::BlendChild);
    EXPECT_FLOAT_EQ(edge.weight, 1.0f);
    EXPECT_TRUE(edge.has_bone_weights);
  }
  for (const auto& blender : model.blenders) {
    EXPECT_EQ(model.nodes[blender.node].kind, HkxGraphNodeKind::Blender);
  }
}

TEST(HkxBehaviorGraphRealData, ReadsTheLayerTaggingAndReferenceNodesOfTheWeaponGraph) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/WeaponBehavior.hkx", model, packfile)) return;
  expect_consistent(model);
  EXPECT_EQ(model.nodes.size(), 1437U);
  ASSERT_EQ(model.edges.size(), 408U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::Wrapped), 342U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::Layer), 14U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::Selected), 47U);
  EXPECT_EQ(edges_with_role(model, HkxGraphEdgeRole::BlendChild), 5U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::Layer), 5U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::StateTagging), 28U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::CyclicBlendTransition), 28U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::DynamicAnimationTagging), 13U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::BehaviorReference), 3U);
  EXPECT_EQ(nodes_of_kind(model, HkxGraphNodeKind::ReferencePose), 3U);
  for (const auto& edge : model.edges) {
    if (edge.role == HkxGraphEdgeRole::Layer) {
      EXPECT_FLOAT_EQ(edge.weight, 1.0f);
    }
  }

  const auto layers = edges_of(model, "WeaponRoot_LayerGenerator");
  ASSERT_EQ(layers.size(), 3U);
  EXPECT_EQ(child_name(model, layers[0]), "WeaponRoot_MG");
  EXPECT_EQ(child_name(model, layers[1]), "PlayerStagger_MG");
  EXPECT_EQ(layers[1].slot, 1);
  EXPECT_EQ(layers[2].child, layers[0].child);
  EXPECT_EQ(layers[2].slot, 2);

  const auto tagging = edges_of(model, "WeaponRoot_iStateTaggingGenerator");
  ASSERT_EQ(tagging.size(), 1U);
  EXPECT_EQ(tagging[0].role, HkxGraphEdgeRole::Wrapped);
  EXPECT_EQ(child_name(model, tagging[0]), "WeaponRoot_LayerGenerator");
  EXPECT_EQ(model.nodes[static_cast<std::size_t>(tagging[0].child)].kind, HkxGraphNodeKind::Layer);

  const auto dynamic = edges_of(model, "DynamicIdleWithinWeapon_DynamicAnimationGenerator");
  ASSERT_EQ(dynamic.size(), 1U);
  EXPECT_EQ(child_name(model, dynamic[0]), "Idle");
}

TEST(HkxBehaviorGraphRealData, ReadsTheBoneSwitchOfTheSyncedAnimGraph) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (!decode_fixture("Character/Behaviors/SyncedAnimBehavior.hkx", model, packfile)) return;
  expect_consistent(model);
  EXPECT_EQ(model.nodes.size(), 174U);
  EXPECT_EQ(model.edges.size(), 4U);
  const auto bone_switch = edges_of(model, "BSBoneSwitchGenerator_PairedCamera");
  ASSERT_EQ(bone_switch.size(), 2U);
  EXPECT_EQ(bone_switch[0].role, HkxGraphEdgeRole::Wrapped);
  EXPECT_EQ(child_name(model, bone_switch[0]), "SyncedAnimModGen");
  EXPECT_FALSE(bone_switch[0].has_bone_weights);
  EXPECT_EQ(bone_switch[1].role, HkxGraphEdgeRole::BoneSwitchChild);
  EXPECT_EQ(bone_switch[1].slot, 0);
  EXPECT_EQ(child_name(model, bone_switch[1]), "PairedCamera01 StateMachine");
  EXPECT_TRUE(bone_switch[1].has_bone_weights);
}

TEST(HkxBehaviorGraphRealData, ReadsTheCyclicBlendWrappersAndTheEmptyGraphSwap) {
  HkxGraphModel model;
  HkxPackfile packfile;
  if (decode_fixture("Character/Behaviors/Locomotion_8wayBlend.hkx", model, packfile)) {
    expect_consistent(model);
    EXPECT_EQ(model.nodes.size(), 54U);
    ASSERT_EQ(model.edges.size(), 4U);
    for (const auto& edge : model.edges) {
      EXPECT_EQ(edge.role, HkxGraphEdgeRole::Wrapped);
      EXPECT_EQ(model.nodes[edge.parent].kind, HkxGraphNodeKind::CyclicBlendTransition);
      EXPECT_EQ(model.nodes[static_cast<std::size_t>(edge.child)].kind, HkxGraphNodeKind::Blender);
    }
    const auto walk = edges_of(model, "MeleeWalkState_CBT");
    ASSERT_EQ(walk.size(), 1U);
    EXPECT_EQ(child_name(model, walk[0]), "WalkForwardBlend");
  }
  HkxGraphModel critter;
  if (decode_fixture("DLC03/RadChicken/Behaviors/CritterRootBehavior.hkx", critter, packfile)) {
    EXPECT_EQ(nodes_of_kind(critter, HkxGraphNodeKind::BehaviorGraphSwap), 1U);
    EXPECT_TRUE(critter.edges.empty());
  }
}
