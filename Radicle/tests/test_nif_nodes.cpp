#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using fo4::nif::Shape;
using fo4::nif::Transform;
using namespace nif_build;

TEST(NifNodes, ExposesTheNodeTreeWithParentsChildrenAndLocalTransforms) {
  const Transform root{{1.0f, 2.0f, 3.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
  const Transform child{{4.0f, 5.0f, 6.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 3.0f};
  const auto nif = fo4::nif::read_nif(build_nif(
      {"root", "child"},
      {named_node_block("BSFadeNode", 0, root, {1, 3}, root_trailer()),
       named_node_block("NiNode", 1, child, {2}), triangle_block("BSTriShape", Transform{}),
       named_node_block("BSValueNode", -1, Transform{}, {})},
      {0}));

  const auto& nodes = nif.nodes();
  ASSERT_EQ(nodes.size(), 3U);

  EXPECT_EQ(nodes[0].block, 0U);
  EXPECT_EQ(nodes[0].type, "BSFadeNode");
  EXPECT_EQ(nodes[0].name, "root");
  EXPECT_EQ(nodes[0].parent, -1);
  EXPECT_EQ(nodes[0].children, (std::vector<std::uint32_t>{1, 3}));
  expect_floats({nodes[0].local.t[0], nodes[0].local.t[1], nodes[0].local.t[2]}, {1.0f, 2.0f, 3.0f});
  EXPECT_FLOAT_EQ(nodes[0].local.s, 2.0f);
  EXPECT_EQ(nodes[0].local.r[1], -1.0f);

  EXPECT_EQ(nodes[1].block, 1U);
  EXPECT_EQ(nodes[1].type, "NiNode");
  EXPECT_EQ(nodes[1].name, "child");
  EXPECT_EQ(nodes[1].parent, 0);
  EXPECT_EQ(nodes[1].children, (std::vector<std::uint32_t>{2}));
  expect_floats({nodes[1].local.t[0], nodes[1].local.t[1], nodes[1].local.t[2]}, {4.0f, 5.0f, 6.0f});
  EXPECT_FLOAT_EQ(nodes[1].local.s, 3.0f);

  EXPECT_EQ(nodes[2].block, 3U);
  EXPECT_EQ(nodes[2].type, "BSValueNode");
  EXPECT_EQ(nodes[2].name, "");
  EXPECT_EQ(nodes[2].parent, 0);
  EXPECT_TRUE(nodes[2].children.empty());

  EXPECT_EQ(nif.node_for_block(0), 0);
  EXPECT_EQ(nif.node_for_block(1), 1);
  EXPECT_EQ(nif.node_for_block(2), -1);
  EXPECT_EQ(nif.node_for_block(3), 2);
  EXPECT_EQ(nif.node_for_block(4), -1);

  EXPECT_EQ(fo4::nif::shape_parent_node(nif, 2), 1);
  EXPECT_EQ(fo4::nif::shape_parent_node(nif, 1), 0);
  EXPECT_EQ(fo4::nif::shape_parent_node(nif, 0), -1);
}

TEST(NifNodes, DoesNotTreatFurnitureMarkerNodesAsSceneNodes) {
  Writer w;
  w.u32(1);
  w.f32(1.0f);
  w.f32(2.0f);
  w.f32(3.0f);
  w.u32(0);
  const Block markers{"BSFurnitureMarkerNode", w.bytes()};
  const auto nif =
      fo4::nif::read_nif(build_nif({"root"}, {node_block("NiNode", Transform{}, {1}), markers}, {0}));
  ASSERT_EQ(nif.nodes().size(), 1U);
  EXPECT_EQ(nif.nodes()[0].children, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(nif.parent(1), 0);
  EXPECT_EQ(nif.node_for_block(1), -1);
}

TEST(NifGeometry, WarnsOnABadShaderBlockAndKeepsTheGeometry) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"shape"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 1, -1, triangle_vertices(), {0, 1, 2}),
       lighting_shader_block(-1, 99)},
      {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& shape = shapes[0];
  EXPECT_TRUE(shape.material.empty());
  EXPECT_TRUE(shape.textures.empty());
  EXPECT_NE(shape.warning.find("texture set reference is out of range"), std::string::npos) << shape.warning;
  expect_floats(shape.verts, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 2}));
  EXPECT_EQ(shape.shader_ref, 1);
}

TEST(NifGeometry, ReadsAMaterialThroughAHealthyShaderBlock) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"shape", "materials\\body.bgsm"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 1, -1, triangle_vertices(), {0, 1, 2}),
       lighting_shader_block(1, -1)},
      {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].material, "materials\\body.bgsm");
  EXPECT_TRUE(shapes[0].warning.empty());
}

TEST(NifSkin, RejectsBoneDataWhoseCountDisagreesWithTheSkin) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"shape"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_has_skin, 20, 1, -1, -1, skinned_vertices(2), {}),
       skin_instance_block(2, {3, 3}), bone_data_block(1), named_node_block("NiNode", -1, Transform{}, {})},
      {0}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); }, "bone data holds 1 transforms for a skin with 2 bones");
}

TEST(NifSkin, RejectsABoneIndexOfAVertexPastTheEndOfTheSkin) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"shape"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_has_skin, 20, 1, -1, -1, skinned_vertices(2), {}),
       skin_instance_block(2, {3, 3, 3, 3}), bone_data_block(4), named_node_block("NiNode", -1, Transform{}, {})},
      {0}));
  expect_parse_error([&] { fo4::nif::read_shapes(nif); },
                     "bone index is past the end of the skin's bone list");
}

TEST(NifSkin, ReadsABoneListTheVertexIndicesStayInside) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"shape"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_has_skin, 20, 1, -1, -1, skinned_vertices(2), {}),
       skin_instance_block(2, {3, 3, 3, 3, 3}), bone_data_block(5),
       named_node_block("NiNode", -1, Transform{}, {})},
      {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].bones.size(), 5U);
  EXPECT_EQ(shapes[0].bone_indices, (std::vector<std::uint8_t>{1, 2, 3, 4, 1, 2, 3, 4}));
  EXPECT_TRUE(shapes[0].warning.empty());
}
TEST(NifNodes, RejectsANodeThatIsItsOwnChild) {
  const auto bytes = build_nif({"node"}, {node_block("NiNode", Transform{}, {0}, root_trailer())}, {0});
  expect_parse_error([&] { fo4::nif::read_nif(bytes); }, "node parent chain contains a cycle");
}

TEST(NifNodes, RejectsTwoNodesThatParentEachOther) {
  const auto bytes = build_nif(
      {"node"}, {node_block("NiNode", Transform{}, {1}, root_trailer()), node_block("NiNode", Transform{}, {0})}, {0});
  expect_parse_error([&] { fo4::nif::read_nif(bytes); }, "node parent chain contains a cycle");
}

TEST(NifNodes, ReadsAVeryDeepAcyclicChain) {
  constexpr std::int32_t depth = 10000;
  std::vector<Block> blocks;
  blocks.reserve(depth);
  for (std::int32_t i = 0; i < depth; ++i) {
    const std::vector<std::int32_t> children = i + 1 < depth ? std::vector<std::int32_t>{i + 1} : std::vector<std::int32_t>{};
    blocks.push_back(node_block("NiNode", Transform{}, children, i == 0 ? root_trailer() : std::vector<std::byte>{}));
  }
  const auto nif = fo4::nif::read_nif(build_nif({"node"}, blocks, {0}));
  ASSERT_EQ(nif.nodes().size(), static_cast<std::size_t>(depth));
  EXPECT_EQ(nif.nodes().back().parent, depth - 2);
}

TEST(NifSkin, DropsTheWeightsOfASkinThatListsNoBones) {
  const auto nif = fo4::nif::read_nif(build_nif(
      {"shape"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_has_skin, 20, 1, -1, -1, skinned_vertices(2), {}),
       skin_instance_block(2, {}), bone_data_block(0)},
      {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_TRUE(shapes[0].bones.empty());
  EXPECT_TRUE(shapes[0].weights.empty());
  EXPECT_TRUE(shapes[0].bone_indices.empty());
  EXPECT_NE(shapes[0].warning.find("lists no bones"), std::string::npos);
}