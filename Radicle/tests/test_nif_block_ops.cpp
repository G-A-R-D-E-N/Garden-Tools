#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fo4/error.hpp"
#include "fo4/nif/block_ops.hpp"
#include "fo4/nif/nif.hpp"

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

TEST(BlockOps, DuplicateThenAttachProducesASecondChild) {
  const auto nif = fo4::nif::read_nif(build({}, {node(-1, {1}), node(-1, {}), node(-1, {})}));
  const auto duplicated = fo4::nif::read_nif(fo4::nif::duplicate_block(nif, 2));
  ASSERT_EQ(duplicated.blocks().size(), 4U);
  EXPECT_EQ(duplicated.parent(3), -1);
  const auto attached = fo4::nif::read_nif(fo4::nif::append_child(duplicated, 0, 3));
  EXPECT_EQ(attached.children(0), (std::vector<std::uint32_t>{1, 3}));
  EXPECT_EQ(attached.parent(3), 0);
}
