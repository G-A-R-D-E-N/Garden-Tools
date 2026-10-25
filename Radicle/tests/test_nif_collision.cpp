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
