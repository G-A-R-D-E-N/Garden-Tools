#include "fo4/nif/collision.hpp"

#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using nif_build::Block;
using nif_build::Writer;

Block collision_block(std::string type, const std::vector<std::uint8_t>& payload) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(payload.size()));
  for (const auto value : payload) w.u8(value);
  return Block{std::move(type), w.bytes()};
}

Block raw_block(std::string type, const std::vector<std::uint8_t>& body) {
  Writer w;
  for (const auto value : body) w.u8(value);
  return Block{std::move(type), w.bytes()};
}

std::vector<std::byte> blob_bytes(const std::vector<std::uint8_t>& values) {
  std::vector<std::byte> out;
  out.reserve(values.size());
  for (const auto value : values) out.push_back(std::byte{value});
  return out;
}

Block collision_object_block(std::int32_t target,
                             std::int32_t data,
                             std::uint32_t body_id,
                             std::uint16_t flags = 0) {
  Writer w;
  w.i32(target);
  w.u16(flags);
  w.i32(data);
  w.u32(body_id);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

}  // namespace

TEST(NifCollision, ReadsPhysicsAndRagdollBlobsInBlockOrder) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {collision_block("bhkPhysicsSystem", {0x57, 0xE0, 0xE0, 0x57}),
                              collision_block("bhkRagdollSystem", {0x01, 0x02, 0x03})},
                           {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 2U);
  EXPECT_EQ(blobs[0].block, 0U);
  EXPECT_EQ(blobs[0].type, "bhkPhysicsSystem");
  EXPECT_EQ(blobs[0].bytes, blob_bytes({0x57, 0xE0, 0xE0, 0x57}));
  EXPECT_EQ(blobs[1].block, 1U);
  EXPECT_EQ(blobs[1].type, "bhkRagdollSystem");
  EXPECT_EQ(blobs[1].bytes, blob_bytes({0x01, 0x02, 0x03}));
}

TEST(NifCollision, ReadsAZeroLengthPayloadAsAnEmptyBlob) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif({}, {collision_block("bhkPhysicsSystem", {})}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].block, 0U);
  EXPECT_TRUE(blobs[0].bytes.empty());
}

TEST(NifCollision, IgnoresTrailingBytesAfterTheDeclaredLength) {
  Writer w;
  w.u32(2);
  w.u8(0xAA);
  w.u8(0xBB);
  w.u8(0x00);
  w.u8(0x00);
  const auto nif = fo4::nif::read_nif(nif_build::build_nif({}, {Block{"bhkPhysicsSystem", w.bytes()}}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].bytes, blob_bytes({0xAA, 0xBB}));
}

TEST(NifCollision, IgnoresBlocksThatAreNotCollisionSystems) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {collision_block("BSClothExtraData", {0x01, 0x02})}, {}));
  EXPECT_TRUE(fo4::nif::read_collision_blobs(nif).empty());
}

TEST(NifCollision, RejectsABlockShorterThanItsLengthPrefix) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {raw_block("bhkPhysicsSystem", {0x01, 0x00, 0x00})}, {}));
  nif_build::expect_parse_error([&] { fo4::nif::read_collision_blobs(nif); },
                                "collision block is shorter than its length prefix");
}

TEST(NifCollision, RejectsADeclaredLengthPastTheEndOfItsBlock) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {raw_block("bhkPhysicsSystem", {0x09, 0x00, 0x00, 0x00, 0xAA, 0xBB})}, {}));
  nif_build::expect_parse_error([&] { fo4::nif::read_collision_blobs(nif); },
                                "collision blob length runs past the end of its block");
}

TEST(NifCollision, AttachesTheOwningObjectFlagsSystemBodyIdAndTarget) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {"collision"},
      {nif_build::named_node_block("NiNode", 0, {}, {}),
       collision_block("bhkPhysicsSystem", {0x57, 0xE0, 0xE0, 0x57}),
       collision_object_block(0, 1, 0x1234, 0x0003)},
      {0}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 2);
  EXPECT_EQ(blobs[0].flags, std::uint16_t{0x0003});
  EXPECT_EQ(blobs[0].system_ref, 1);
  EXPECT_EQ(blobs[0].body_id, 0x1234U);
  EXPECT_EQ(blobs[0].target, 0);
  ASSERT_EQ(blobs[0].target_node, 0);
  EXPECT_EQ(nif.nodes()[static_cast<std::size_t>(blobs[0].target_node)].name, "collision");
}

TEST(NifCollision, AttachesAnObjectThatPrecedesItsSystem) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {"node"}, {collision_object_block(2, 1, 5), collision_block("bhkPhysicsSystem", {0x01}),
           nif_build::node_block("NiNode", {}, {})},
      {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 0);
  EXPECT_EQ(blobs[0].body_id, 5U);
  EXPECT_EQ(blobs[0].target, 2);
  EXPECT_EQ(blobs[0].target_node, 0);
}

TEST(NifCollision, KeepsASystemWithoutAnObjectUnattributed) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({}, {collision_block("bhkRagdollSystem", {0x01})}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, -1);
  EXPECT_EQ(blobs[0].flags, std::uint16_t{0});
  EXPECT_EQ(blobs[0].system_ref, -1);
  EXPECT_EQ(blobs[0].body_id, 0U);
  EXPECT_EQ(blobs[0].target, -1);
  EXPECT_EQ(blobs[0].target_node, -1);
}

TEST(NifCollision, IgnoresAnObjectWhoseDataIsNotACollisionSystem) {
  const auto nif = fo4::nif::read_nif(
      nif_build::build_nif({"node"}, {collision_block("bhkPhysicsSystem", {0x01}),
                                nif_build::node_block("NiNode", {}, {}), collision_object_block(1, 1, 9)},
                           {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, -1);
  EXPECT_EQ(blobs[0].flags, std::uint16_t{0});
  EXPECT_EQ(blobs[0].system_ref, -1);
  EXPECT_EQ(blobs[0].body_id, 0U);
}

TEST(NifCollision, KeepsTheFirstOfTwoObjectsNamingTheSameSystem) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {}, {collision_block("bhkPhysicsSystem", {0x01}), collision_object_block(-1, 0, 7),
           collision_object_block(-1, 0, 9)},
      {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 1);
  EXPECT_EQ(blobs[0].system_ref, 0);
  EXPECT_EQ(blobs[0].body_id, 7U);
}

TEST(NifCollision, IgnoresATargetOutsideTheBlockTable) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {}, {collision_block("bhkPhysicsSystem", {0x01}), collision_object_block(99, 0, 3)}, {}));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  EXPECT_EQ(blobs[0].owner, 1);
  EXPECT_EQ(blobs[0].body_id, 3U);
  EXPECT_EQ(blobs[0].target, -1);
  EXPECT_EQ(blobs[0].target_node, -1);
}

TEST(NifCollision, RejectsACollisionObjectShorterThanItsFields) {
  const auto nif = fo4::nif::read_nif(nif_build::build_nif(
      {}, {raw_block("bhkNPCollisionObject", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})}, {}));
  nif_build::expect_parse_error([&] { fo4::nif::read_collision_blobs(nif); },
                                "collision object is shorter than its fields");
}
