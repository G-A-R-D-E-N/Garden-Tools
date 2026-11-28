#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fo4/error.hpp"
#include "fo4/nif/block_ops.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_assembly.hpp"
#include "fo4/nif/shape_ops.hpp"

#include "nif_fixture.hpp"

namespace {

using Blocks = std::vector<nif_fixture::Block>;

std::vector<std::byte> build(const std::vector<std::string>& strings, const Blocks& blocks) {
  return nif_fixture::build_nif(strings, blocks, {0});
}

nif_fixture::Block node(std::int32_t name, const std::vector<std::int32_t>& children) {
  return nif_fixture::node_block("NiNode", name, fo4::nif::Transform{}, children);
}

nif_fixture::Block shape(std::int32_t name) {
  return nif_fixture::shape_block("BSTriShape", name, fo4::nif::Transform{}, 0, 8, -1, -1, -1, {}, {});
}

nif_fixture::Block texture_set() {
  return nif_fixture::texture_set_block({"textures\\a.dds"});
}

nif_fixture::Block with_collision(nif_fixture::Block block, std::int32_t object) {
  // Constraint: with no extra-data refs the NiAVObject collision reference is the int32 at byte 68.
  const auto bits = static_cast<std::uint32_t>(object);
  for (std::size_t i = 0; i < 4; ++i) block.data[68 + i] = std::byte{static_cast<std::uint8_t>(bits >> (8 * i))};
  return block;
}

nif_fixture::Block collision_object() {
  nif_fixture::Writer w;
  w.i32(1);
  w.u16(0x80);
  w.i32(-1);
  w.u32(0);
  return nif_fixture::Block{"bhkNPCollisionObject", w.bytes()};
}

std::vector<std::byte> block_bytes(const fo4::nif::NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  return {nif.bytes().begin() + static_cast<std::ptrdiff_t>(info.offset),
          nif.bytes().begin() + static_cast<std::ptrdiff_t>(info.offset + info.size)};
}

}  // namespace

TEST(BlockOps, SortsByNameAndFillsTheNullSlotsWithMinusOne) {
  // Constraint: the on-disk child count never changes, so null slots move to the tail instead of being lost.
  const auto source = build({"zeta", "alpha"}, {node(-1, {-1, 1, 2}), node(0, {}), node(1, {})});
  const auto expected = build({"zeta", "alpha"}, {node(-1, {2, 1, -1}), node(0, {}), node(1, {})});
  EXPECT_EQ(fo4::nif::sort_children_by_name(fo4::nif::read_nif(source), 0), expected);
}

TEST(BlockOps, SortsShapeChildrenByTheirBlockName) {
  // Constraint: shape children sort by the NiObjectNET name, not by an empty key, so they do not all sort first.
  const auto source = build({"beta", "alpha"}, {node(-1, {1, 2}), shape(0), shape(1)});
  const auto expected = build({"beta", "alpha"}, {node(-1, {2, 1}), shape(0), shape(1)});
  EXPECT_EQ(fo4::nif::sort_children_by_name(fo4::nif::read_nif(source), 0), expected);
}

TEST(BlockOps, SortTiesUseTheBlockIndexNotTheExistingOrder) {
  // Constraint: equal names order by block index, so the result does not depend on the input array order.
  const auto source = build({"same"}, {node(-1, {2, 1}), node(0, {}), node(0, {})});
  const auto expected = build({"same"}, {node(-1, {1, 2}), node(0, {}), node(0, {})});
  EXPECT_EQ(fo4::nif::sort_children_by_name(fo4::nif::read_nif(source), 0), expected);
}

TEST(BlockOps, SortRefusesNonNodesUnderTwoChildrenAndTrailingData) {
  const auto non_node = fo4::nif::read_nif(build({}, {node(-1, {1}), shape(-1)}));
  EXPECT_THROW(fo4::nif::sort_children_by_name(non_node, 1), fo4::parse_error);
  EXPECT_THROW(fo4::nif::sort_children_by_name(non_node, 0), fo4::parse_error);
  auto with_tail = node(-1, {1, 2});
  with_tail.data.insert(with_tail.data.end(), 4, std::byte{0});
  const auto tailed = fo4::nif::read_nif(build({}, {with_tail, node(-1, {}), node(-1, {})}));
  EXPECT_THROW(fo4::nif::sort_children_by_name(tailed, 0), fo4::parse_error);
}

TEST(BlockOps, AppendsAnUnparentedChildAndLeavesOtherBlocksAlone) {
  // Constraint: only the node's child list grows; every other block stays byte-identical.
  const auto source = build({}, {node(-1, {1}), node(-1, {}), node(-1, {})});
  const auto expected = build({}, {node(-1, {1, 2}), node(-1, {}), node(-1, {})});
  const auto nif = fo4::nif::read_nif(source);
  const auto out_bytes = fo4::nif::append_child(nif, 0, 2);
  EXPECT_EQ(out_bytes, expected);
  const auto out = fo4::nif::read_nif(out_bytes);
  EXPECT_EQ(out.children(0), (std::vector<std::uint32_t>{1, 2}));
  EXPECT_EQ(out.parent(2), 0);
  EXPECT_EQ(block_bytes(out, 1), block_bytes(nif, 1));
  EXPECT_EQ(block_bytes(out, 2), block_bytes(nif, 2));
}

TEST(BlockOps, AppendRefusesParentedRootsAncestorsAndForeignBlocks) {
  const auto nif = fo4::nif::read_nif(build({}, {node(-1, {1}), node(-1, {}), node(-1, {})}));
  EXPECT_THROW(fo4::nif::append_child(nif, 0, 1), fo4::parse_error);
  EXPECT_THROW(fo4::nif::append_child(nif, 2, 2), fo4::parse_error);
  EXPECT_THROW(fo4::nif::append_child(nif, 1, 0), fo4::parse_error);
  EXPECT_THROW(fo4::nif::append_child(nif, 0, 9), fo4::parse_error);
  const auto foreign = fo4::nif::read_nif(build({}, {node(-1, {1}), texture_set()}));
  EXPECT_THROW(fo4::nif::append_child(foreign, 0, 1), fo4::parse_error);
}

TEST(BlockOps, AppendRefusesAnOrphanCycle) {
  // Constraint: an orphan subtree can still be made cyclic, so the ancestor walk must refuse it too.
  const auto nif = fo4::nif::read_nif(build({}, {node(-1, {}), node(-1, {2}), node(-1, {})}));
  EXPECT_EQ(nif.parent(1), -1);
  EXPECT_THROW(fo4::nif::append_child(nif, 2, 1), fo4::parse_error);
}

TEST(BlockOps, AppendRefusesACycleThroughASharedChild) {
  // Constraint: block 2 is listed by both 0 and 1, so only the child lists show that 1 already reaches 2.
  const auto nif = fo4::nif::read_nif(build({}, {node(-1, {2}), node(-1, {2}), node(-1, {})}));
  EXPECT_EQ(nif.parent(2), 0);
  EXPECT_EQ(nif.parent(1), -1);
  EXPECT_THROW(fo4::nif::append_child(nif, 2, 1), fo4::parse_error);
}

TEST(BlockOps, DuplicatesLeafNodesAndShapesAndRefusesNodesWithChildren) {
  // Constraint: a copied shape shares its skin/shader/texture references, so only the block record is duplicated.
  const auto nodes = fo4::nif::read_nif(build({}, {node(-1, {1}), node(-1, {})}));
  const auto duplicated_node = fo4::nif::duplicate_block(nodes, 1);
  const auto expected_node = build({}, {node(-1, {1}), node(-1, {}), node(-1, {})});
  EXPECT_EQ(duplicated_node, expected_node);
  EXPECT_THROW(fo4::nif::duplicate_block(nodes, 0), fo4::parse_error);
  const auto shapes = fo4::nif::read_nif(build({}, {node(-1, {1}), shape(-1)}));
  const auto duplicated_shape = fo4::nif::duplicate_block(shapes, 1);
  const auto expected_shape = build({}, {node(-1, {1}), shape(-1), shape(-1)});
  EXPECT_EQ(duplicated_shape, expected_shape);
  const auto foreign = fo4::nif::read_nif(build({}, {node(-1, {1}), texture_set()}));
  EXPECT_THROW(fo4::nif::duplicate_block(foreign, 1), fo4::parse_error);
}

// Constraint: a copy of a collision-bearing node or shape would share its collision object's target, so duplication must raise.
TEST(BlockOps, DuplicateRefusesBlocksThatCarryACollisionObject) {
  const auto nif = fo4::nif::read_nif(build(
      {}, {node(-1, {1, 2}), with_collision(node(-1, {}), 3), with_collision(shape(-1), 3), collision_object()}));
  EXPECT_THROW(fo4::nif::duplicate_block(nif, 1), fo4::parse_error);
  EXPECT_THROW(fo4::nif::duplicate_block(nif, 2), fo4::parse_error);
}

nif_fixture::Block translated_shape() {
  nif_fixture::Writer writer;
  for (const float value : {0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 3.0f, 0.0f, 1.0f}) {
    writer.f32(value);
  }
  fo4::nif::Transform xf;
  xf.t[0] = 5.0f;
  return nif_fixture::shape_block("BSTriShape", 1, xf, nif_fixture::k_full, 16, -1, -1, -1, writer.bytes(),
                                  {0, 1, 2});
}

// Invariant: donor blocks append with string links intact and roots reparented.
TEST(MergeInto, AppendsDonorBlocksAndReparentsRoots) {
  const auto base = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), shape(1)}));
  const std::vector<nif_fixture::Block> donor_blocks{
      nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", 1, fo4::nif::Transform{}, 0, 8, -1, 2, -1, {}, {}),
      nif_fixture::lighting_shader_block(1, 2, 3, 0x80400201U, 0x21U),
      nif_fixture::texture_set_block({"textures\\donor.dds"}),
  };
  const auto donor = fo4::nif::read_nif(
      nif_fixture::build_nif({"droot", "dshape", "materials\\donor.bgsm"}, donor_blocks, {0}));
  const auto merged = fo4::nif::read_nif(fo4::nif::merge_nif_into(base, donor, 0));
  const auto shapes = fo4::nif::read_shapes(merged);
  ASSERT_EQ(shapes.size(), 2U);
  EXPECT_EQ(shapes[1].material, "materials\\donor.bgsm");
  ASSERT_FALSE(shapes[1].textures.empty());
  EXPECT_EQ(shapes[1].textures[0], "textures\\donor.dds");
  EXPECT_EQ(merged.children(0), (std::vector<std::uint32_t>{1, 2}));
}

// Invariant: a nameless donor block merges without inventing a name.
TEST(MergeInto, KeepsANamelessDonorBlockNameless) {
  const auto base = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), shape(1)}));
  const std::vector<nif_fixture::Block> donor_blocks{
      nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", -1, fo4::nif::Transform{}, 0, 8, -1, 2, -1, {}, {}),
      nif_fixture::lighting_shader_block(1, 2, 3, 0x80400201U, 0x21U),
      nif_fixture::texture_set_block({"textures\\donor.dds"}),
  };
  const auto donor = fo4::nif::read_nif(
      nif_fixture::build_nif({"droot", "dshape", "materials\\donor.bgsm"}, donor_blocks, {0}));
  const auto merged = fo4::nif::read_nif(fo4::nif::merge_nif_into(base, donor, 0));
  const auto shapes = fo4::nif::read_shapes(merged);
  ASSERT_EQ(shapes.size(), 2U);
  EXPECT_TRUE(shapes[1].name.empty());
  EXPECT_EQ(shapes[1].material, "materials\\donor.bgsm");
}

// Constraint: a donor collision reference is a refusal.
TEST(MergeInto, RefusesDonorCollision) {
  const auto base = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), shape(1)}));
  const std::vector<nif_fixture::Block> donor_blocks{
      nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {1}),
      with_collision(nif_fixture::shape_block("BSTriShape", 1, fo4::nif::Transform{}, 0, 8, -1, -1, -1, {}, {}),
                     0),
  };
  const auto donor = fo4::nif::read_nif(nif_fixture::build_nif({"droot", "dshape"}, donor_blocks, {0}));
  EXPECT_THROW(fo4::nif::merge_nif_into(base, donor, 0), std::invalid_argument);
}

std::int32_t i32_at(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(nif.bytes()[nif.blocks()[block].offset + at + i]) << (8 * i);
  }
  return static_cast<std::int32_t>(value);
}

// Regression: the merge read NiExtraData blocks as if an extra-data list followed the name. A real BSXFlags value
// was taken for a list length (refused as truncated) and a NiStringExtraData value was never remapped, so a donor
// with ordinary extra data either could not merge or pointed at the base file's strings. Text keys were copied
// with donor string indices too.
TEST(MergeInto, RemapsExtraDataStringsAndKeepsTheirPayload) {
  const auto base = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), shape(1)}));
  nif_fixture::Writer bsx;
  bsx.i32(1);
  bsx.u32(0x82);
  nif_fixture::Writer prn;
  prn.i32(2);
  prn.i32(3);
  nif_fixture::Writer text;
  text.i32(-1);
  text.u32(2);
  text.f32(0.0f);
  text.i32(4);
  text.f32(1.0f);
  text.i32(5);
  const std::vector<nif_fixture::Block> donor_blocks{
      nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {}),
      nif_fixture::Block{"BSXFlags", bsx.bytes()},
      nif_fixture::Block{"NiStringExtraData", prn.bytes()},
      nif_fixture::Block{"NiTextKeyExtraData", text.bytes()},
  };
  const auto donor = fo4::nif::read_nif(
      nif_fixture::build_nif({"droot", "BSX", "Prn", "WeaponBack", "start", "end"}, donor_blocks, {0}));
  const auto merged = fo4::nif::read_nif(fo4::nif::merge_nif_into(base, donor, 0));
  ASSERT_EQ(merged.blocks().size(), 6U);
  const auto text_of = [&](std::uint32_t block, std::size_t at) {
    return merged.strings().at(static_cast<std::size_t>(i32_at(merged, block, at)));
  };
  EXPECT_EQ(text_of(3, 0), "BSX");
  EXPECT_EQ(i32_at(merged, 3, 4), 0x82);
  EXPECT_EQ(text_of(4, 0), "Prn");
  EXPECT_EQ(text_of(4, 4), "WeaponBack");
  EXPECT_EQ(i32_at(merged, 5, 0), -1);
  EXPECT_EQ(text_of(5, 12), "start");
  EXPECT_EQ(text_of(5, 20), "end");
}

// Regression: list counts were scaled in 32 bits before the size check, so a count of 0x40000000 wrapped to
// zero, passed, and the remap loop ran off the end of the block. A corrupt donor must be a refusal.
TEST(MergeInto, RefusesADonorWhoseListCountWrapsTheSizeCheck) {
  const auto base = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), shape(1)}));
  nif_fixture::Writer alpha;
  alpha.i32(-1);
  alpha.u32(0x40000000U);
  alpha.i32(-1);
  alpha.u16(0x12ED);
  alpha.u8(128);
  const std::vector<nif_fixture::Block> donor_blocks{
      nif_fixture::node_block("NiNode", 0, fo4::nif::Transform{}, {}),
      nif_fixture::Block{"NiAlphaProperty", alpha.bytes()},
  };
  const auto donor = fo4::nif::read_nif(nif_fixture::build_nif({"droot"}, donor_blocks, {0}));
  EXPECT_THROW(fo4::nif::merge_nif_into(base, donor, 0), fo4::parse_error);
}

namespace {

nif_fixture::Block linked_node(const std::string& type, std::int32_t name, const std::vector<std::int32_t>& extras,
                               std::int32_t controller, const std::vector<std::int32_t>& children,
                               bool multi_bound = false, std::int32_t bound = -1) {
  nif_fixture::Writer w;
  w.i32(name);
  w.u32(static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) w.i32(extra);
  w.i32(controller);
  w.u32(0);
  w.transform(fo4::nif::Transform{});
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  if (multi_bound) {
    w.i32(bound);
    w.u32(0);
  }
  return nif_fixture::Block{type, w.bytes()};
}

void set_i32(nif_fixture::Block& block, std::size_t at, std::int32_t value) {
  const auto bits = static_cast<std::uint32_t>(value);
  for (std::size_t i = 0; i < 4; ++i) block.data[at + i] = std::byte{static_cast<std::uint8_t>(bits >> (8 * i))};
}

nif_fixture::Block u32_block(const std::string& type, std::int32_t name, std::uint32_t value) {
  nif_fixture::Writer w;
  w.i32(name);
  w.u32(value);
  return nif_fixture::Block{type, w.bytes()};
}

}  // namespace

// Invariant: a donor reference slot must be remapped from the offsets ref_offsets reports.
TEST(MergeInto, RemapsEveryReferenceSlotOfEveryMergeableBlockType) {
  const auto base = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), shape(1)}));
  auto shader = nif_fixture::lighting_shader_block(0, 3, 6, 0x80400201U, 0x21U, 1);
  set_i32(shader, 12, 11);
  set_i32(shader, 16, 13);
  nif_fixture::Writer alpha;
  alpha.i32(4);
  alpha.u32(1);
  alpha.i32(9);
  alpha.i32(13);
  alpha.u16(0x12ED);
  alpha.u8(128);
  nif_fixture::Writer dismember;
  dismember.i32(8);
  dismember.i32(-1);
  dismember.i32(0);
  dismember.u32(2);
  dismember.i32(1);
  dismember.i32(2);
  dismember.u32(1);
  dismember.u16(0);
  dismember.u16(0);
  nif_fixture::Writer real;
  real.i32(7);
  real.f32(1.5f);
  nif_fixture::Writer boolean;
  boolean.i32(8);
  boolean.u8(1);
  nif_fixture::Writer strings;
  strings.i32(9);
  strings.i32(10);
  nif_fixture::Writer keys;
  keys.i32(11);
  keys.u32(1);
  keys.f32(0.0f);
  keys.i32(12);
  nif_fixture::Writer parents;
  parents.i32(13);
  parents.u32(0);
  nif_fixture::Writer children;
  children.i32(14);
  children.u8(0);
  children.u32(0);
  nif_fixture::Writer key_data;
  key_data.u32(0);
  const std::vector<nif_fixture::Block> donor_blocks{
      linked_node("NiNode", 0, {9}, -1, {1}),
      linked_node("BSMultiBoundNode", 1, {}, -1, {-1, 2, 22}, true, 6),
      nif_fixture::shape_block("BSTriShape", 2, fo4::nif::Transform{}, 0, 8, 5, 3, 4, {}, {}),
      shader,
      nif_fixture::Block{"NiAlphaProperty", alpha.bytes()},
      nif_fixture::Block{"BSDismemberSkinInstance", dismember.bytes()},
      nif_fixture::texture_set_block({"textures\\donor.dds"}),
      nif_fixture::skin_instance_block(0, 8, {2, 1}),
      nif_fixture::bone_data_block({}),
      u32_block("BSXFlags", 5, 0x82),
      u32_block("NiIntegerExtraData", 6, 7),
      nif_fixture::Block{"NiFloatExtraData", real.bytes()},
      nif_fixture::Block{"NiBooleanExtraData", boolean.bytes()},
      nif_fixture::Block{"NiStringExtraData", strings.bytes()},
      nif_fixture::Block{"NiTextKeyExtraData", keys.bytes()},
      nif_fixture::Block{"BSConnectPoint::Parents", parents.bytes()},
      nif_fixture::Block{"BSConnectPoint::Children", children.bytes()},
      nif_fixture::Block{"NiTransformData", key_data.bytes()},
      nif_fixture::Block{"NiKeyframeData", key_data.bytes()},
      nif_fixture::Block{"NiPosData", key_data.bytes()},
      nif_fixture::Block{"NiFloatData", key_data.bytes()},
      nif_fixture::Block{"NiBoolData", key_data.bytes()},
      linked_node("NiLODNode", -1, {}, -1, {23}),
      linked_node("NiNode", -1, {}, -1, {}),
  };
  std::vector<std::string> donor_strings;
  for (int i = 0; i < 15; ++i) donor_strings.push_back("d" + std::to_string(i));
  const auto donor = fo4::nif::read_nif(nif_fixture::build_nif(donor_strings, donor_blocks, {0}));
  const auto merged = fo4::nif::read_nif(fo4::nif::merge_nif_into(base, donor, 0));
  ASSERT_EQ(merged.blocks().size(), 2U + donor_blocks.size());

  struct Slot {
    std::uint32_t block;
    std::size_t at;
    std::int32_t value;
  };
  const std::vector<Slot> expected{
      {2, 8, 11},  {2, 12, -1}, {2, 72, -1}, {2, 80, 3},
      {3, 8, -1},  {3, 76, -1}, {3, 80, 4},  {3, 84, 24}, {3, 88, 8},
      {4, 8, -1},  {4, 88, 7},  {4, 92, 5},  {4, 96, 6},
      {5, 12, 13}, {5, 16, 15}, {5, 44, 8},
      {6, 8, 11},  {6, 12, 15},
      {7, 0, 10},  {7, 4, -1},  {7, 8, 2},   {7, 16, 3},  {7, 20, 4},
      {9, 0, 2},   {9, 4, 10},  {9, 12, 4},  {9, 16, 3},
      {24, 76, 25},
  };
  for (const auto& slot : expected) {
    EXPECT_EQ(i32_at(merged, slot.block, slot.at), slot.value) << "block " << slot.block << " offset " << slot.at;
  }
  const auto text_of = [&](std::uint32_t block, std::size_t at) {
    return merged.strings().at(static_cast<std::size_t>(i32_at(merged, block, at)));
  };
  EXPECT_EQ(text_of(4, 0), "d2");
  EXPECT_EQ(text_of(5, 4), "d3");
  EXPECT_EQ(text_of(6, 0), "d4");
  EXPECT_EQ(text_of(11, 0), "d5");
  EXPECT_EQ(text_of(12, 0), "d6");
  EXPECT_EQ(text_of(13, 0), "d7");
  EXPECT_EQ(text_of(14, 0), "d8");
  EXPECT_EQ(text_of(15, 0), "d9");
  EXPECT_EQ(text_of(15, 4), "d10");
  EXPECT_EQ(text_of(16, 0), "d11");
  EXPECT_EQ(text_of(16, 12), "d12");
  EXPECT_EQ(text_of(17, 0), "d13");
  EXPECT_EQ(text_of(18, 0), "d14");
  for (const std::uint32_t d : {9U, 10U, 11U, 12U, 15U, 16U}) {
    const auto donor_payload = block_bytes(donor, d);
    const auto merged_payload = block_bytes(merged, d + 2);
    EXPECT_TRUE(std::equal(donor_payload.begin() + 4, donor_payload.end(), merged_payload.begin() + 4))
        << "block " << d;
  }
  for (std::uint32_t d = 17; d <= 21; ++d) EXPECT_EQ(block_bytes(merged, d + 2), block_bytes(donor, d));
}
// Invariant: baked verts carry the transform and the block transform resets to identity.
TEST(BakeShapeTransforms, BakesATranslatedStatic) {
  const auto nif = fo4::nif::read_nif(build({"root", "shape"}, {node(0, {1}), translated_shape()}));
  fo4::nif::BakeReport report;
  const auto baked = fo4::nif::read_nif(fo4::nif::bake_shape_transforms(nif, {1}, &report));
  EXPECT_EQ(report.baked, (std::vector<std::uint32_t>{1}));
  EXPECT_TRUE(report.skipped.empty());
  const auto shape = fo4::nif::read_shape(baked, 1);
  ASSERT_EQ(shape.verts.size(), 9U);
  EXPECT_FLOAT_EQ(shape.verts[0], 5.0f);
  EXPECT_FLOAT_EQ(shape.verts[3], 7.0f);
  EXPECT_FLOAT_EQ(shape.verts[6], 5.0f);
  const auto xf = fo4::nif::block_transform(baked, 1);
  EXPECT_FLOAT_EQ(xf.t[0], 0.0f);
  EXPECT_FLOAT_EQ(xf.t[1], 0.0f);
  EXPECT_FLOAT_EQ(xf.t[2], 0.0f);
}

// Constraint: a skinned shape is reported skipped, never half-baked.
TEST(BakeShapeTransforms, SkipsASkinnedShape) {
  const auto nif = fo4::nif::read_nif(nif_fixture::full_nif());
  fo4::nif::BakeReport report;
  const auto kept = fo4::nif::bake_shape_transforms(nif, {1}, &report);
  EXPECT_TRUE(report.baked.empty());
  ASSERT_EQ(report.skipped.size(), 1U);
  EXPECT_NE(report.skipped[0].find("block 1"), std::string::npos);
  EXPECT_EQ(kept, nif_fixture::full_nif());
}

fo4::nif::Pose idle_pose() {
  fo4::nif::Pose pose;
  pose.name = "idle";
  fo4::nif::PoseBone bone;
  bone.translation = {{1.0f, 2.0f, 3.0f}};
  bone.rotation = {{0.0f, 0.0f, 0.0f, 1.0f}};
  bone.scale = 1.0f;
  pose.bones["RootBone"] = bone;
  return pose;
}

// Invariant: the named bone gains the pose translation with no other bone touched.
TEST(PoseLibrary, AppliesBoneLocalTransformsToTheSkin) {
  const auto nif = fo4::nif::read_nif(nif_fixture::full_nif());
  fo4::nif::PoseApplyReport report;
  const auto posed = fo4::nif::read_nif(fo4::nif::apply_pose_to_skin(nif, 2, idle_pose(), &report));
  EXPECT_EQ(report.applied, 1U);
  EXPECT_TRUE(report.unknown_pose_bones.empty());
  EXPECT_TRUE(report.unposed_bones.empty());
  const auto bones = fo4::nif::read_bones(posed, 2);
  ASSERT_EQ(bones.size(), 1U);
  EXPECT_FLOAT_EQ(bones[0].translation[0], 1.0f);
  EXPECT_FLOAT_EQ(bones[0].translation[1], 2.0f);
  EXPECT_FLOAT_EQ(bones[0].translation[2], 3.0f);
  const auto round_tripped = fo4::nif::read_pose_library(fo4::nif::write_pose_library({idle_pose()}));
  ASSERT_EQ(round_tripped.size(), 1U);
  EXPECT_EQ(round_tripped[0].name, "idle");
  ASSERT_EQ(round_tripped[0].bones.count("RootBone"), 1U);
}

// Invariant: pose bones the skin lacks are reported unknown with nothing applied.
TEST(PoseLibrary, ReportsBonesTheSkinDoesNotHave) {
  const auto nif = fo4::nif::read_nif(nif_fixture::full_nif());
  fo4::nif::Pose pose;
  pose.name = "ghost";
  pose.bones["Ghost"] = fo4::nif::PoseBone{};
  fo4::nif::PoseApplyReport report;
  fo4::nif::apply_pose_to_skin(nif, 2, pose, &report);
  EXPECT_EQ(report.applied, 0U);
  EXPECT_EQ(report.unknown_pose_bones, (std::vector<std::string>{"Ghost"}));
  EXPECT_EQ(report.unposed_bones, (std::vector<std::string>{"RootBone"}));
}

TEST(BlockOps, DuplicateThenAttachProducesASecondChild) {
  const auto nif = fo4::nif::read_nif(build({}, {node(-1, {1}), node(-1, {}), node(-1, {})}));
  const auto duplicated = fo4::nif::read_nif(fo4::nif::duplicate_block(nif, 2));
  ASSERT_EQ(duplicated.blocks().size(), 4U);
  EXPECT_EQ(duplicated.parent(3), -1);
  const auto attached = fo4::nif::read_nif(fo4::nif::append_child(duplicated, 0, 3));
  EXPECT_EQ(attached.children(0), (std::vector<std::uint32_t>{1, 3}));
  EXPECT_EQ(attached.parent(3), 0);
}
