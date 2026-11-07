#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_attach.hpp"
#include "fo4/nif/collision_lint.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::nif::CollisionBlob;
using fo4::nif::NifFile;
using fo4::nif::read_collision_blobs;
using fo4::nif::read_nif;
using nif_build::Block;
using nif_build::Writer;

constexpr std::uint32_t k_box_crc = 0x064003D4U;
constexpr std::uint16_t k_owner_flags = 0x80;
constexpr std::uint32_t k_bsx_havok = 2;

std::vector<std::byte> box_packfile() {
  return fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(
      fo4::nif::make_hknp_box({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, k_box_crc), {}));
}

Block named_node(std::int32_t name, std::int32_t collision, const std::vector<std::int32_t>& extras,
                 const std::vector<std::int32_t>& children) {
  Writer w;
  w.i32(name);
  w.u32(static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) w.i32(extra);
  w.i32(-1);
  w.u32(0);
  w.transform(fo4::nif::Transform{});
  w.i32(collision);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  return Block{"NiNode", w.bytes()};
}

Block object_block(std::int32_t target, std::int32_t data, std::uint32_t body) {
  Writer w;
  w.i32(target);
  w.u16(k_owner_flags);
  w.i32(data);
  w.u32(body);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

Block system_block(const std::vector<std::byte>& packfile) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(packfile.size()));
  w.append(packfile);
  return Block{"bhkPhysicsSystem", w.bytes()};
}

Block bsx_block(std::uint32_t flags) {
  Writer w;
  w.i32(-1);
  w.u32(flags);
  return Block{"BSXFlags", w.bytes()};
}

Block cloth_block(std::int32_t name, const std::vector<std::byte>& payload) {
  Writer w;
  w.i32(name);
  w.append(payload);
  return Block{"BSClothExtraData", w.bytes()};
}

Block subindex_block(std::int32_t alpha) {
  auto shape = nif_build::shape_with_refs("BSSubIndexTriShape", -1, fo4::nif::Transform{}, nif_build::k_full, 16, -1,
                                          -1, alpha, nif_build::triangle_vertices(), {0, 1, 2});
  Writer segments;
  for (const std::uint32_t value : {1U, 1U, 1U, 0U, 1U, 0U, 0U}) segments.u32(value);
  const auto tail = segments.bytes();
  shape.data.insert(shape.data.end(), tail.begin(), tail.end());
  return shape;
}

std::vector<std::byte> fixture_a() {
  return nif_build::build_nif({"root", "col", "other", "tri"},
                              {named_node(0, -1, {6}, {1, 2, 5}), named_node(1, 3, {}, {}), named_node(2, -1, {}, {}),
                               object_block(1, 4, 0), system_block(box_packfile()),
                               nif_build::triangle_block("BSTriShape", fo4::nif::Transform{}), bsx_block(k_bsx_havok)},
                              {0});
}

NifFile cloth_donor() {
  return read_nif(nif_build::build_nif({"cloth"}, {named_node(0, -1, {1}, {}), cloth_block(0, box_packfile())}, {0}));
}

std::uint32_t node_block(const NifFile& nif, std::string_view name) {
  for (const auto& node : nif.nodes()) {
    if (node.name == name) return node.block;
  }
  throw std::runtime_error("no node named " + std::string(name));
}

std::vector<std::string> child_labels(const NifFile& nif, std::uint32_t parent) {
  std::vector<std::string> labels;
  for (const auto child : nif.children(parent)) {
    const auto node = nif.node_for_block(child);
    labels.push_back(node >= 0 ? nif.nodes()[static_cast<std::size_t>(node)].name : nif.blocks()[child].type);
  }
  return labels;
}

std::int32_t int32_at(const NifFile& nif, std::uint32_t block, std::size_t relative) {
  const auto at = nif.blocks()[block].offset + relative;
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(nif.bytes()[at + i])) << (8 * i);
  }
  return static_cast<std::int32_t>(value);
}

std::uint32_t block_of_type(const NifFile& nif, std::string_view type) {
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type == type) return block;
  }
  throw std::runtime_error("no block of type " + std::string(type));
}

std::uint32_t bsx_flags(const NifFile& nif) {
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type == "BSXFlags") return static_cast<std::uint32_t>(int32_at(nif, block, 4));
  }
  return 0xFFFFFFFFU;
}

void expect_same_collision(const NifFile& before_nif, const CollisionBlob& before, const NifFile& after_nif,
                           const CollisionBlob& after) {
  EXPECT_EQ(before.bytes, after.bytes);
  EXPECT_EQ(before.flags, after.flags);
  EXPECT_EQ(before.body_id, after.body_id);
  ASSERT_GE(before.target_node, 0);
  ASSERT_GE(after.target_node, 0);
  EXPECT_EQ(before_nif.nodes()[static_cast<std::size_t>(before.target_node)].name,
            after_nif.nodes()[static_cast<std::size_t>(after.target_node)].name);
}

}  // namespace

// Detects: a strip then re-attach of a node's own blob changing its bytes, body, flags or target node, or changing the block count.
TEST(CollisionAttach, StripAndReattachReadsBackTheSameCollision) {
  const auto original = read_nif(fixture_a());
  const auto before = read_collision_blobs(original);
  ASSERT_EQ(before.size(), 1U);
  const auto stripped = read_nif(fo4::nif::remove_node_collision(original, "col"));
  EXPECT_EQ(stripped.blocks().size(), original.blocks().size() - 2);
  EXPECT_TRUE(read_collision_blobs(stripped).empty());
  EXPECT_EQ(child_labels(stripped, node_block(stripped, "root")),
            (std::vector<std::string>{"col", "other", "BSTriShape"}));

  const auto rebuilt = read_nif(fo4::nif::set_node_collision(stripped, "col", before[0].bytes, before[0].body_id));
  EXPECT_EQ(rebuilt.blocks().size(), original.blocks().size());
  EXPECT_EQ(rebuilt.roots(), original.roots());
  const auto after = read_collision_blobs(rebuilt);
  ASSERT_EQ(after.size(), 1U);
  expect_same_collision(original, before[0], rebuilt, after[0]);
  EXPECT_TRUE(fo4::nif::lint_collision(rebuilt).empty());
}

// Detects: a second replace of the same node stacking duplicate objects and systems instead of producing the bytes one replace gives.
TEST(CollisionAttach, SecondReplaceProducesTheSameBytesAsOneReplace) {
  const auto original = read_nif(fixture_a());
  const auto pack = box_packfile();
  const auto once = fo4::nif::set_node_collision(original, "col", pack, 0);
  const auto twice = fo4::nif::set_node_collision(read_nif(once), "col", pack, 0);
  EXPECT_EQ(twice, once);
  EXPECT_EQ(read_nif(twice).blocks().size(), original.blocks().size());
}

// Detects: removing one node's collision deleting a system that another object still names, which leaves that node without a body.
TEST(CollisionAttach, RemovingOneOfTwoObjectsKeepsTheSharedSystem) {
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "a", "b"},
      {named_node(0, -1, {}, {1, 2}), named_node(1, 3, {}, {}), named_node(2, 4, {}, {}), object_block(1, 5, 0),
       object_block(2, 5, 0), system_block(box_packfile())},
      {0}));
  const auto out = read_nif(fo4::nif::remove_node_collision(nif, "a"));
  EXPECT_EQ(out.blocks().size(), 5U);
  const auto blobs = read_collision_blobs(out);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 3);
  EXPECT_EQ(blobs[0].system_ref, 4);
  ASSERT_GE(blobs[0].target_node, 0);
  EXPECT_EQ(out.nodes()[static_cast<std::size_t>(blobs[0].target_node)].name, "b");
  EXPECT_EQ(blobs[0].bytes, box_packfile());
}

// Detects: a removal that leaves a reference pointing at the wrong block after the table shifts, including the root list.
TEST(CollisionAttach, RemovalRenumbersChildReferencesAndRoots) {
  const auto nif = read_nif(nif_build::build_nif(
      {"col", "root"},
      {system_block(box_packfile()), object_block(2, 0, 0), named_node(0, 1, {}, {}), named_node(1, -1, {}, {2})}, {3}));
  const auto out = read_nif(fo4::nif::remove_node_collision(nif, "col"));
  EXPECT_EQ(out.blocks().size(), 2U);
  EXPECT_EQ(out.roots(), (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(child_labels(out, 1), (std::vector<std::string>{"col"}));
  EXPECT_TRUE(read_collision_blobs(out).empty());
}

// Detects: a removal that renumbers around a block type with no reference layout, which would silently break its references.
TEST(CollisionAttach, RefusesRemovalWhenABlockTypeHasNoReferenceLayout) {
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "col", "other", "tri"},
      {named_node(0, -1, {6}, {1, 2, 5}), named_node(1, 3, {}, {}), named_node(2, -1, {}, {}), object_block(1, 4, 0),
       system_block(box_packfile()), nif_build::triangle_block("BSTriShape", fo4::nif::Transform{}), bsx_block(k_bsx_havok),
       Block{"NiControllerManager", {std::byte{0}}}},
      {0}));
  const auto before = nif.bytes();
  try {
    fo4::nif::remove_node_collision(nif, "col");
    ADD_FAILURE() << "expected a refusal for NiControllerManager";
  } catch (const fo4::parse_error& error) {
    EXPECT_NE(error.parse_message().find("no reference layout"), std::string::npos) << error.parse_message();
  }
  EXPECT_EQ(nif.bytes(), before);
  EXPECT_NO_THROW(fo4::nif::set_node_collision(nif, "other", box_packfile(), 0));
}

// Detects: a missing node name silently attaching to a different node or returning a changed file.
TEST(CollisionAttach, RefusesAMissingNodeWithoutChangingTheInput) {
  const auto nif = read_nif(fixture_a());
  const auto before = nif.bytes();
  EXPECT_THROW(fo4::nif::set_node_collision(nif, "missing", box_packfile(), 0), fo4::parse_error);
  EXPECT_THROW(fo4::nif::remove_node_collision(nif, "missing"), fo4::parse_error);
  EXPECT_THROW(fo4::nif::copy_cloth_extra_data(nif, "missing", cloth_donor()), fo4::parse_error);
  EXPECT_EQ(nif.bytes(), before);
}

// Detects: bytes that are not one packfile being written into a bhkPhysicsSystem, which the engine then fails to load.
TEST(CollisionAttach, RefusesBytesThatAreNotAPackfile) {
  const auto nif = read_nif(fixture_a());
  const auto before = nif.bytes();
  EXPECT_THROW(fo4::nif::set_node_collision(nif, "col", std::vector<std::byte>(16, std::byte{0x41}), 0),
               fo4::parse_error);
  EXPECT_EQ(nif.bytes(), before);
}

// Detects: a body id the packfile does not hold being attached, which points the object at no body.
TEST(CollisionAttach, RefusesABodyIdThePackfileDoesNotHave) {
  const auto nif = read_nif(fixture_a());
  EXPECT_THROW(fo4::nif::set_node_collision(nif, "col", box_packfile(), 1), std::invalid_argument);
}

// Detects: copying a donor cloth blob that loses its payload, or leaves its name pointing at the donor's string table.
TEST(CollisionAttach, CopiesTheDonorClothBlobOntoTheNamedNode) {
  const auto donor = cloth_donor();
  const auto target = read_nif(fixture_a());
  const auto out = read_nif(fo4::nif::copy_cloth_extra_data(target, "other", donor));
  ASSERT_EQ(out.blocks().size(), target.blocks().size() + 1);
  const auto cloth = static_cast<std::uint32_t>(target.blocks().size());
  EXPECT_EQ(out.blocks()[cloth].type, "BSClothExtraData");
  const auto& copied = out.blocks()[cloth];
  const auto& original = donor.blocks()[1];
  ASSERT_EQ(copied.size, original.size);
  EXPECT_TRUE(std::equal(out.bytes().begin() + static_cast<std::ptrdiff_t>(copied.offset) + 4,
                         out.bytes().begin() + static_cast<std::ptrdiff_t>(copied.offset + copied.size),
                         donor.bytes().begin() + static_cast<std::ptrdiff_t>(original.offset) + 4));
  EXPECT_EQ(out.strings()[static_cast<std::size_t>(int32_at(out, cloth, 0))], "cloth");
  const auto other = node_block(out, "other");
  EXPECT_EQ(int32_at(out, other, 4), 1);
  EXPECT_EQ(int32_at(out, other, 8), static_cast<std::int32_t>(cloth));
}

// Detects: the Havok flag edit changing bytes on a repeat call or losing the bit when turned off and back on.
TEST(CollisionAttach, SetsAndClearsTheRootHavokFlagIdempotently) {
  const auto nif = read_nif(fixture_a());
  EXPECT_EQ(fo4::nif::set_havok_flag(nif, true), fo4::nif::write_nif(nif));
  const auto off = read_nif(fo4::nif::set_havok_flag(nif, false));
  EXPECT_EQ(bsx_flags(off), 0U);
  EXPECT_EQ(fo4::nif::set_havok_flag(off, true), fo4::nif::write_nif(nif));
}

// Detects: enabling the Havok flag on a root with no BSX leaving the file without any flag block.
TEST(CollisionAttach, AddsAHavokBsxWhenTheRootHasNone) {
  const auto nif = read_nif(nif_build::build_nif({"root"}, {named_node(0, -1, {}, {})}, {0}));
  const auto out = read_nif(fo4::nif::set_havok_flag(nif, true));
  EXPECT_EQ(bsx_flags(out), k_bsx_havok);
  EXPECT_EQ(int32_at(out, 0, 4), 1);
  EXPECT_EQ(fo4::nif::write_nif(nif), fo4::nif::set_havok_flag(nif, false));
}

// Detects: a remove on a node with no collision altering the file, so a caller that strips a clean node gets a changed NIF.
TEST(CollisionAttach, RemovingFromANodeWithoutCollisionReturnsTheSameFile) {
  const auto nif = read_nif(fixture_a());
  EXPECT_EQ(fo4::nif::remove_node_collision(nif, "other"), fo4::nif::write_nif(nif));
}

// Detects: a block's huge extra-data count allocating gigabytes before the size check, instead of a parse error.
TEST(CollisionAttach, RefusesAHugeReferenceCountWithoutAllocatingForIt) {
  auto shape = nif_build::triangle_block("BSTriShape", fo4::nif::Transform{});
  for (std::size_t i = 4; i < 8; ++i) shape.data[i] = std::byte{0xFF};
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "col", "other", "tri"},
      {named_node(0, -1, {6}, {1, 2, 5}), named_node(1, 3, {}, {}), named_node(2, -1, {}, {}), object_block(1, 4, 0),
       system_block(box_packfile()), shape, bsx_block(k_bsx_havok)},
      {0}));
  EXPECT_THROW(fo4::nif::remove_node_collision(nif, "col"), fo4::parse_error);
}

// Detects: a collision removal refused beside a segmented BSSubIndexTriShape, or a shape whose alpha reference and segment bytes are not carried through the block renumbering.
TEST(CollisionAttach, RemovesCollisionBesideASubIndexShapeAndRenumbersItsReferences) {
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "col", "other"},
      {named_node(0, -1, {}, {1, 2, 5}), named_node(1, 3, {}, {}), named_node(2, -1, {}, {}), object_block(1, 4, 0),
       system_block(box_packfile()), subindex_block(6), nif_build::alpha_block(0, 0)},
      {0}));
  const auto out = read_nif(fo4::nif::remove_node_collision(nif, "col"));
  EXPECT_EQ(out.blocks().size(), nif.blocks().size() - 2);
  EXPECT_EQ(child_labels(out, node_block(out, "root")),
            (std::vector<std::string>{"col", "other", "BSSubIndexTriShape"}));
  const auto subindex = block_of_type(out, "BSSubIndexTriShape");
  EXPECT_EQ(out.blocks()[subindex].size, nif.blocks()[5].size);
  EXPECT_EQ(int32_at(out, subindex, 96), 4);
  EXPECT_EQ(out.blocks()[4].type, "NiAlphaProperty");
}

// Detects: a segmented shape whose data size runs past its block, which the reference walk must refuse instead of reading the references of a truncated block.
TEST(CollisionAttach, RefusesASubIndexShapeWhoseDataRunsPastItsBlock) {
  auto shape = subindex_block(-1);
  shape.data.resize(shape.data.size() - 40);
  const auto nif = read_nif(nif_build::build_nif(
      {"root", "col", "other", "tri"},
      {named_node(0, -1, {}, {1, 2, 5}), named_node(1, 3, {}, {}), named_node(2, -1, {}, {}), object_block(1, 4, 0),
       system_block(box_packfile()), shape, bsx_block(k_bsx_havok)},
      {0}));
  EXPECT_THROW(fo4::nif::remove_node_collision(nif, "col"), fo4::parse_error);
}
