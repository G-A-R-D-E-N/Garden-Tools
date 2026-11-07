#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/nif/structure.hpp"
#include "nif_build.hpp"
#include "../src/nif/write_refs.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::nif::add_block;
using fo4::nif::attach_alpha_property;
using fo4::nif::create_texture_set;
using fo4::nif::read_nif;
using fo4::nif::remove_block;
using fo4::nif::reparent_block;
using nif_build::Block;
using nif_build::expect_parse_error;
using nif_build::Writer;

const fo4::nif::Transform k_identity{};

Block texture_set(const std::vector<std::string>& textures) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(textures.size()));
  for (const auto& texture : textures) w.sized(texture);
  return Block{"BSShaderTextureSet", w.bytes()};
}

Block lighting(std::int32_t texture_set_ref) {
  nif_build::LightingFields fields;
  fields.texture_set = texture_set_ref;
  return nif_build::full_lighting_shader_block(fields);
}

Block effect_with_refs(std::int32_t extra1, std::int32_t extra2, std::int32_t controller) {
  const auto base = nif_build::effect_shader_block(-1, {"textures\\effect_d.dds"});
  Writer w;
  w.i32(-1);
  w.u32(2);
  w.i32(extra1);
  w.i32(extra2);
  w.i32(controller);
  w.append(std::vector<std::byte>(base.data.begin() + 12, base.data.end()));
  return Block{base.type, w.bytes()};
}

Block tri_shape(std::int32_t shader, std::int32_t alpha) {
  return nif_build::shape_with_refs("BSTriShape", 1, k_identity, nif_build::k_full, 16, -1, shader, alpha,
                                    nif_build::triangle_vertices(), {0, 1, 2});
}

std::vector<std::byte> node_payload() {
  return nif_build::named_node_block("NiNode", -1, k_identity, {}).data;
}

Block connect_point(bool parents) {
  Writer w;
  w.i32(-1);
  if (!parents) w.u8(1);
  w.u32(2);
  for (int i = 0; i < 2; ++i) {
    if (parents) w.sized("parent");
    w.sized("point");
    if (parents) for (int j = 0; j < 8; ++j) w.f32(static_cast<float>(j));
  }
  return Block{parents ? "BSConnectPoint::Parents" : "BSConnectPoint::Children", w.bytes()};
}

Block node_with_connect_points(std::int32_t first, std::int32_t second, const std::vector<std::int32_t>& children) {
  Writer w;
  w.i32(0);
  w.u32(2);
  w.i32(first);
  w.i32(second);
  w.i32(-1);
  w.u32(0);
  w.transform(k_identity);
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  return Block{"NiNode", w.bytes()};
}

std::vector<std::byte> middle_file() {
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, k_identity, {1, 2, 3}),
      nif_build::named_node_block("NiNode", 1, k_identity, {}),
      nif_build::named_node_block("NiNode", 2, k_identity, {}),
      tri_shape(4, 6),
      lighting(5),
      texture_set({"textures\\shape_d.dds"}),
      nif_build::alpha_block(0x01ED, 0x80),
  };
  return nif_build::build_nif({"root", "a", "mid", "shape"}, blocks, {0});
}

std::vector<std::byte> tree_file() {
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, k_identity, {1, 2}),
      nif_build::named_node_block("NiNode", 1, k_identity, {3}),
      nif_build::named_node_block("NiNode", 2, k_identity, {}),
      nif_build::named_node_block("NiNode", 3, k_identity, {}),
  };
  return nif_build::build_nif({"root", "a", "mid", "leaf"}, blocks, {0});
}

std::vector<std::byte> bare_shape_file() {
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, k_identity, {1}),
      tri_shape(-1, -1),
  };
  return nif_build::build_nif({"root", "shape"}, blocks, {0});
}

std::vector<std::byte> shared_shader_file() {
  const std::vector<Block> blocks{
      nif_build::named_node_block("NiNode", 0, k_identity, {1, 2}),
      tri_shape(3, -1),
      tri_shape(3, -1),
      lighting(-1),
  };
  return nif_build::build_nif({"root", "shape"}, blocks, {0});
}

}  // namespace

// Detects: an add whose remove leaves a child list, block table or type table changed, so the file no longer returns to its original bytes.
TEST(NifStructure, AddThenRemoveReturnsTheOriginalBytes) {
  const auto bytes = middle_file();
  const auto nif = read_nif(bytes);

  const auto added = read_nif(add_block(nif, 2, "NiNode", node_payload()));
  ASSERT_EQ(added.blocks().size(), 8U);
  EXPECT_EQ(added.children(2), (std::vector<std::uint32_t>{7}));
  EXPECT_EQ(remove_block(added, 7), bytes);

  Writer flags;
  flags.i32(-1);
  flags.u32(0x20);
  const auto with_flags = read_nif(add_block(nif, 1, "BSXFlags", flags.bytes()));
  EXPECT_EQ(with_flags.blocks()[7].type, "BSXFlags");
  EXPECT_EQ(remove_block(with_flags, 7), bytes);
}

// Detects: removing a middle block that leaves a later shape, shader, alpha, texture set or child entry pointing at the old index instead of the block it named.
TEST(NifStructure, RemovingAMiddleBlockRepointsEveryReference) {
  const auto nif = read_nif(middle_file());
  const auto after = read_nif(remove_block(nif, 2));

  ASSERT_EQ(after.blocks().size(), 6U);
  EXPECT_EQ(after.blocks()[2].type, "BSTriShape");
  EXPECT_EQ(after.children(0), (std::vector<std::uint32_t>{1, 2}));
  const auto shapes = fo4::nif::read_shapes(after);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].block, 2U);
  EXPECT_EQ(shapes[0].shader_ref, 3);
  EXPECT_EQ(shapes[0].alpha_ref, 5);
  const auto shader = fo4::nif::read_lighting_shader(after, 3);
  EXPECT_EQ(shader.texture_set_ref, 4);
  EXPECT_EQ(shader.textures, (std::vector<std::string>{"textures\\shape_d.dds"}));
  EXPECT_EQ(fo4::nif::read_alpha(after, 5).flags, std::uint16_t{0x01ED});
}

// Detects: removing an earlier block must renumber effect-shader extra-data and controller references without changing its other fields.
TEST(NifStructure, RemovingMiddleBlockRenumbersEffectShaderReferences) {
  const auto effect = effect_with_refs(4, 5, 6);
  Writer extra;
  extra.i32(-1);
  extra.u32(0);
  const Block flags{"BSXFlags", extra.bytes()};
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "shape"}, {nif_build::named_node_block("NiNode", 0, k_identity, {1, 2, 6}),
      nif_build::named_node_block("NiNode", -1, k_identity, {}), tri_shape(3, -1), effect, flags, flags,
      nif_build::named_node_block("NiNode", -1, k_identity, {})}, {0}));
  const auto after = read_nif(remove_block(nif, 1));
  ASSERT_EQ(after.blocks().size(), 6U);
  EXPECT_EQ(after.children(0), (std::vector<std::uint32_t>{1, 5}));
  EXPECT_EQ(fo4::nif::read_shapes(after).at(0).shader_ref, 2);
  const auto expected = effect_with_refs(3, 4, 5).data;
  const auto& info = after.blocks()[2];
  ASSERT_EQ(info.size, expected.size());
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), after.bytes().begin() + info.offset));
  EXPECT_EQ(fo4::nif::read_effect_shader(after, 2).source_texture, "textures\\effect_d.dds");
  EXPECT_THROW(remove_block(nif, 4), fo4::parse_error);
  EXPECT_THROW(remove_block(nif, 6), fo4::parse_error);
}

// Detects: removing an earlier block must renumber every skin reference without changing scales.
TEST(NifStructure, RemovingMiddleBlockRenumbersSkinInstanceReferences) {
  Writer skin;
  skin.i32(0); skin.i32(4); skin.u32(3);
  skin.i32(3); skin.i32(-1); skin.i32(5);
  skin.u32(1); skin.f32(1.0f); skin.f32(2.0f); skin.f32(3.0f);
  const Block instance{"BSSkin::Instance", skin.bytes()};
  const auto bytes = nif_build::build_nif({"root"},
      {nif_build::named_node_block("NiNode", 0, k_identity, {1, 3, 4, 5}),
       nif_build::named_node_block("NiNode", -1, k_identity, {}), instance,
       nif_build::named_node_block("NiNode", -1, k_identity, {}),
       nif_build::named_node_block("NiNode", -1, k_identity, {}),
       nif_build::named_node_block("NiNode", -1, k_identity, {})}, {0});
  const auto nif = read_nif(bytes);
  const auto added = read_nif(add_block(nif, 0, "NiNode", node_payload()));
  EXPECT_EQ(remove_block(added, 6), bytes);
  EXPECT_THROW(remove_block(nif, 3), fo4::parse_error);
  const auto after = read_nif(remove_block(nif, 1));
  Writer expected;
  expected.i32(0); expected.i32(3); expected.u32(3);
  expected.i32(2); expected.i32(-1); expected.i32(4);
  expected.u32(1); expected.f32(1.0f); expected.f32(2.0f); expected.f32(3.0f);
  const auto& info = after.blocks()[1];
  ASSERT_EQ(info.size, expected.bytes().size());
  EXPECT_TRUE(std::equal(expected.bytes().begin(), expected.bytes().end(), after.bytes().begin() + info.offset));
}

// Detects: malformed skin counts, truncated scales and unsupported versions must not be accepted for structural edits.
TEST(NifStructure, SkinInstanceRefusesMalformedPayloadsAndUnsupportedVersions) {
  const auto block = nif_build::skin_instance_block(-1, {0, -1}, 0);
  const auto nif = read_nif(nif_build::build_nif({"root"},
      {nif_build::named_node_block("NiNode", 0, k_identity, {}), block}, {0}));
  auto model = fo4::nif::write_detail::load_model(nif);
  EXPECT_EQ(fo4::nif::write_detail::ref_offsets(model, 1), (std::vector<std::size_t>{0, 4, 12, 16}));
  const auto empty = nif_build::skin_instance_block(-1, {}, -1);
  model.blocks[1] = empty.data;
  EXPECT_EQ(fo4::nif::write_detail::ref_offsets(model, 1), (std::vector<std::size_t>{0, 4}));
  for (int version_field = 0; version_field < 3; ++version_field) {
    auto wrong = fo4::nif::write_detail::load_model(nif);
    if (version_field == 0) ++wrong.version;
    if (version_field == 1) ++wrong.user_version;
    if (version_field == 2) ++wrong.bs_version;
    EXPECT_THROW(fo4::nif::write_detail::ref_offsets(wrong, 1), fo4::parse_error);
  }
  auto short_bones = block.data; short_bones.resize(15);
  auto no_scales = block.data; no_scales.resize(20);
  auto short_scales = block.data; short_scales[20] = std::byte{1};
  auto too_many_bones = block.data;
  for (std::size_t i = 8; i < 12; ++i) too_many_bones[i] = std::byte{0xFF};
  auto too_many_scales = block.data;
  for (std::size_t i = 20; i < 24; ++i) too_many_scales[i] = std::byte{0xFF};
  auto trailing = block.data; trailing.push_back(std::byte{0});
  for (const auto& bad : {short_bones, no_scales, short_scales, too_many_bones, too_many_scales, trailing}) {
    auto malformed = fo4::nif::write_detail::load_model(nif);
    malformed.blocks[1] = bad;
    EXPECT_THROW(fo4::nif::write_detail::ref_offsets(malformed, 1), fo4::parse_error);
    EXPECT_EQ(malformed.blocks[1], bad);
  }
}

// Detects: unsupported effect-shader versions must be refused without modifying the payload.
TEST(NifStructure, EffectShaderReferenceLayoutRefusesUnsupportedVersions) {
  const auto block = nif_build::effect_shader_block(-1, {});
  const auto nif = read_nif(nif_build::build_nif(
      {"root"}, {nif_build::named_node_block("NiNode", 0, k_identity, {}), block}, {0}));
  for (int version_field = 0; version_field < 3; ++version_field) {
    auto model = fo4::nif::write_detail::load_model(nif);
    if (version_field == 0) ++model.version;
    if (version_field == 1) ++model.user_version;
    if (version_field == 2) ++model.bs_version;
    EXPECT_THROW(fo4::nif::write_detail::ref_offsets(model, 1), fo4::parse_error);
    EXPECT_EQ(model.blocks[1], block.data);
  }
}

// Detects: malformed effect-shader counts, strings, or payload tails must not be accepted as a safe reference layout.
TEST(NifStructure, MalformedEffectShaderRefusesStructuralEdits) {
  const auto valid = nif_build::effect_shader_block(-1, {"textures\\effect_d.dds"});
  auto truncated = valid;
  truncated.data.pop_back();
  auto trailing = valid;
  trailing.data.push_back(std::byte{0});
  auto bad_count = valid;
  for (std::size_t i = 4; i < 8; ++i) bad_count.data[i] = std::byte{0xFF};
  auto bad_string = valid;
  for (std::size_t i = 36; i < 40; ++i) bad_string.data[i] = std::byte{0xFF};
  for (const auto& block : {truncated, trailing, bad_count, bad_string}) {
    const auto nif = read_nif(nif_build::build_nif(
        {"root"}, {nif_build::named_node_block("NiNode", 0, k_identity, {1}),
                   nif_build::named_node_block("NiNode", -1, k_identity, {}), block}, {0}));
    EXPECT_THROW(remove_block(nif, 1), fo4::parse_error);
  }
}

// Detects: middle-block removal must preserve connect-point payloads and renumber their owning node's references.
TEST(NifStructure, RemovingMiddleBlockPreservesConnectPointsAndRenumbersTheirOwner) {
  const auto parents = connect_point(true);
  const auto children = connect_point(false);
  const auto root = node_with_connect_points(3, 4, {1, 2});
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "a", "b"}, {root, nif_build::named_node_block("NiNode", 1, k_identity, {}),
                            nif_build::named_node_block("NiNode", 2, k_identity, {}), parents, children}, {0}));
  const auto after = read_nif(remove_block(nif, 1));
  ASSERT_EQ(after.blocks().size(), 4U);
  EXPECT_EQ(after.children(0), (std::vector<std::uint32_t>{1}));
  const auto expected_root = node_with_connect_points(2, 3, {1}).data;
  const auto& root_info = after.blocks()[0];
  ASSERT_EQ(root_info.size, expected_root.size());
  EXPECT_TRUE(std::equal(expected_root.begin(), expected_root.end(), after.bytes().begin() + root_info.offset));
  for (const auto& [index, block] : {std::pair{2U, parents}, std::pair{3U, children}}) {
    const auto& info = after.blocks()[index];
    ASSERT_EQ(info.size, block.data.size());
    EXPECT_TRUE(std::equal(block.data.begin(), block.data.end(), after.bytes().begin() + info.offset));
  }
}

// Detects: truncated connect-point strings, excessive counts, and trailing bytes must be refused as unknown layouts.
TEST(NifStructure, MalformedConnectPointsRefuseStructuralEdits) {
  for (const bool parents : {true, false}) {
    auto truncated = connect_point(parents);
    truncated.data.pop_back();
    auto trailing = connect_point(parents);
    trailing.data.push_back(std::byte{0});
    Writer excessive;
    excessive.i32(-1);
    if (!parents) excessive.u8(1);
    excessive.u32(0xFFFFFFFFU);
    const Block oversized{truncated.type, excessive.bytes()};
    Writer bad_string;
    bad_string.i32(-1);
    if (!parents) bad_string.u8(1);
    bad_string.u32(1);
    bad_string.u32(0xFFFFFFFFU);
    for (int i = 0; i < (parents ? 36 : 0); ++i) bad_string.u8(0);
    const Block oversized_string{truncated.type, bad_string.bytes()};
    for (const auto& block : {truncated, trailing, oversized, oversized_string}) {
      const auto nif = read_nif(nif_build::build_nif(
          {"root"}, {nif_build::named_node_block("NiNode", 0, k_identity, {1}),
                     nif_build::named_node_block("NiNode", -1, k_identity, {}), block}, {0}));
      EXPECT_THROW(remove_block(nif, 1), fo4::parse_error);
    }
  }
}

// Detects: unsupported NIF versions must be refused without changing connect-point bytes.
TEST(NifStructure, ConnectPointReferenceLayoutRefusesUnsupportedVersions) {
  for (const bool parents : {true, false}) {
    const auto block = connect_point(parents);
    const auto nif = read_nif(nif_build::build_nif({"root"},
        {nif_build::named_node_block("NiNode", 0, k_identity, {}), block}, {0}));
    for (int version_field = 0; version_field < 3; ++version_field) {
      auto model = fo4::nif::write_detail::load_model(nif);
      if (version_field == 0) ++model.version;
      if (version_field == 1) ++model.user_version;
      if (version_field == 2) ++model.bs_version;
      EXPECT_THROW(fo4::nif::write_detail::ref_offsets(model, 1), fo4::parse_error);
      EXPECT_EQ(model.blocks[1], block.data);
    }
  }
}

// Detects: removing a block another block still names, which would leave a dangling shader, alpha or texture set reference.
TEST(NifStructure, RemovingAReferencedBlockIsRefusedByName) {
  const auto nif = read_nif(middle_file());
  expect_parse_error([&] { remove_block(nif, 4); },
                     "block 4 (BSLightingShaderProperty) is still referenced by block 3 (BSTriShape)");
  expect_parse_error([&] { remove_block(nif, 0); }, "is a root");
}

// Detects: a node removed together with its children, which leaves the children as unreferenced blocks instead of refusing the removal.
TEST(NifStructure, RemovingANodeWithChildrenIsRefused) {
  const auto nif = read_nif(tree_file());
  expect_parse_error([&] { remove_block(nif, 1); }, "still has 1 children");
}

// Detects: a block type with no reference layout edited by a guessed offset, which would silently rewrite the wrong bytes of that block.
TEST(NifStructure, UnmappedBlockTypeIsRefusedByName) {
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "a"},
      {nif_build::named_node_block("BSFadeNode", 0, k_identity, {1}),
       nif_build::named_node_block("NiNode", 1, k_identity, {})},
      {0}));
  expect_parse_error([&] { add_block(nif, 0, "NiNode", node_payload()); },
                     "block type BSFadeNode has no reference layout");
  expect_parse_error([&] { remove_block(nif, 1); }, "block type BSFadeNode has no reference layout");
  expect_parse_error([&] { reparent_block(nif, 1, 0); }, "block type BSFadeNode has no reference layout");
}

// Detects: a reparent that leaves the block linked from its old parent as well as its new one, or drops the link altogether.
TEST(NifStructure, ReparentMovesOnlyTheChildLink) {
  const auto nif = read_nif(tree_file());
  const auto moved = read_nif(reparent_block(nif, 3, 2));

  ASSERT_EQ(moved.blocks().size(), 4U);
  EXPECT_EQ(moved.children(0), (std::vector<std::uint32_t>{1, 2}));
  EXPECT_TRUE(moved.children(1).empty());
  EXPECT_EQ(moved.children(2), (std::vector<std::uint32_t>{3}));
  EXPECT_EQ(moved.parent(3), 2);
}

// Detects: a reparent that moves a node into its own subtree, which builds a cycle that the reader only rejects after the bytes are written.
TEST(NifStructure, ReparentIntoOwnSubtreeIsRefused) {
  const auto nif = read_nif(tree_file());
  expect_parse_error([&] { reparent_block(nif, 1, 3); }, "make a cycle");
  expect_parse_error([&] { reparent_block(nif, 1, 1); }, "make a cycle");
  expect_parse_error([&] { reparent_block(nif, 0, 2); }, "is a root");
}

// Detects: an alpha reference written to the wrong slot, so the shape rereads with no alpha or with another block's alpha.
TEST(NifStructure, AttachedAlphaPropertyRereadsWithItsFlagsAndThreshold) {
  const auto nif = read_nif(bare_shape_file());
  const auto attached = read_nif(attach_alpha_property(nif, 1, 0x01ED, 0x80));

  ASSERT_EQ(attached.blocks().size(), 3U);
  EXPECT_EQ(attached.blocks()[2].type, "NiAlphaProperty");
  const auto shapes = fo4::nif::read_shapes(attached);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].alpha_ref, 2);
  EXPECT_EQ(shapes[0].shader_ref, -1);
  const auto alpha = fo4::nif::read_alpha(attached, 2);
  EXPECT_EQ(alpha.flags, std::uint16_t{0x01ED});
  EXPECT_EQ(alpha.threshold, std::uint8_t{0x80});
}

// Detects: an attach that replaces an existing alpha property, or that gives a node an alpha slot it does not have.
TEST(NifStructure, AttachAlphaRefusesAShapeWithAlphaAndANodeWithoutASlot) {
  const auto nif = read_nif(bare_shape_file());
  const auto attached = read_nif(attach_alpha_property(nif, 1, 0x01ED, 0x80));
  expect_parse_error([&] { attach_alpha_property(attached, 1, 0x01ED, 0x80); }, "already has an alpha property");
  expect_parse_error([&] { attach_alpha_property(nif, 0, 0x01ED, 0x80); }, "has no alpha slot");
}

// Detects: a texture set written to the wrong shader slot, so the shader rereads with no textures or with another block's texture set.
TEST(NifStructure, CreatedTextureSetRereadsWithItsTextures) {
  const auto nif = read_nif(shared_shader_file());
  const std::vector<std::string> textures{"textures\\shape_d.dds", "textures\\shape_n.dds"};
  const auto created = read_nif(create_texture_set(nif, 1, textures));

  ASSERT_EQ(created.blocks().size(), 6U);
  EXPECT_EQ(created.blocks()[5].type, "BSShaderTextureSet");
  const auto shapes = fo4::nif::read_shapes(created);
  ASSERT_EQ(shapes.size(), 2U);
  const auto shader = fo4::nif::read_lighting_shader(created, static_cast<std::uint32_t>(shapes[0].shader_ref));
  EXPECT_EQ(shader.texture_set_ref, 5);
  EXPECT_EQ(shader.textures, textures);
}

// Detects: a texture set added to a shader shared with another shape, which would give that other shape a texture set it did not ask for.
TEST(NifStructure, CreatedTextureSetCopiesASharedShaderForOneShapeOnly) {
  const auto nif = read_nif(shared_shader_file());
  const auto created = read_nif(create_texture_set(nif, 1, {"textures\\shape_d.dds"}));

  const auto shapes = fo4::nif::read_shapes(created);
  ASSERT_EQ(shapes.size(), 2U);
  EXPECT_EQ(shapes[0].shader_ref, 4);
  EXPECT_EQ(shapes[1].shader_ref, 3);
  EXPECT_EQ(fo4::nif::read_lighting_shader(created, 3).texture_set_ref, -1);
  EXPECT_EQ(fo4::nif::read_lighting_shader(created, 4).texture_set_ref, 5);
}

// Detects: a texture set written to a shader that another block names, when that block has no reference layout, so whether the shader must be copied would be a guess.
TEST(NifStructure, CreateTextureSetRefusesWhenAnotherBlockHasNoLayout) {
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "shape"},
      {nif_build::named_node_block("BSFadeNode", 0, k_identity, {1}), tri_shape(2, -1), lighting(-1)},
      {0}));
  expect_parse_error([&] { create_texture_set(nif, 1, {"textures\\shape_d.dds"}); },
                     "block type BSFadeNode has no reference layout");
}

// Detects: a texture set written over a shader that already has one, onto an effect shader that stores its textures itself, or with no paths.
TEST(NifStructure, CreateTextureSetRefusesShadersThatCannotTakeOne) {
  const auto created = read_nif(create_texture_set(read_nif(shared_shader_file()), 1, {"textures\\shape_d.dds"}));
  expect_parse_error([&] { create_texture_set(created, 1, {"textures\\other_d.dds"}); },
                     "already has a texture set");

  const auto effect = read_nif(nif_build::build_nif(
      {"root", "shape"},
      {nif_build::named_node_block("NiNode", 0, k_identity, {1}), tri_shape(2, -1),
       nif_build::effect_shader_block(-1, {"textures\\effect_d.dds"})},
      {0}));
  expect_parse_error([&] { create_texture_set(effect, 1, {"textures\\other_d.dds"}); },
                     "BSEffectShaderProperty, which has no texture set slot");
  expect_parse_error([&] { create_texture_set(read_nif(shared_shader_file()), 1, {}); },
                     "at least one texture path");
}
