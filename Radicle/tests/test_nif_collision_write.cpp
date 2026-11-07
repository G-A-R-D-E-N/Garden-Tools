#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_build.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"

#include "nif_fixture.hpp"

namespace {

constexpr std::uint32_t k_system_block = 1;

std::vector<std::byte> box_payload(float half) {
  const auto shape = fo4::nif::make_hknp_box(fo4::nif::HknpVec3{0.0f, 0.0f, 0.0f},
                                             fo4::nif::HknpVec3{half, half, half}, 0x1234U);
  return fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, fo4::nif::HknpStaticBody{}));
}

std::vector<std::byte> prefixed(const std::vector<std::byte>& payload, std::size_t declared) {
  nif_fixture::Writer w;
  w.u32(static_cast<std::uint32_t>(declared));
  w.append(payload);
  return w.bytes();
}

std::vector<std::byte> collision_object_bytes() {
  nif_fixture::Writer w;
  w.i32(-1);
  w.u16(1);
  w.i32(static_cast<std::int32_t>(k_system_block));
  w.u32(42);
  return w.bytes();
}

std::vector<std::byte> collision_nif(const std::vector<std::byte>& block_data) {
  const std::vector<nif_fixture::Block> blocks{
      nif_fixture::node_block("NiNode", -1, fo4::nif::Transform{}, {1}),
      nif_fixture::Block{"bhkPhysicsSystem", block_data},
      nif_fixture::Block{"bhkNPCollisionObject", collision_object_bytes()},
  };
  const std::array<std::string, 4> exports{"AAA", "", "", ""};
  return nif_fixture::build_nif({"root_name"}, blocks, {0}, exports, 9, {0});
}

std::vector<std::byte> block_bytes(const fo4::nif::NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  return {nif.bytes().begin() + static_cast<std::ptrdiff_t>(info.offset),
          nif.bytes().begin() + static_cast<std::ptrdiff_t>(info.offset + info.size)};
}

const fo4::nif::CollisionBlob* blob_at(const std::vector<fo4::nif::CollisionBlob>& blobs, std::uint32_t block) {
  const auto found = std::find_if(blobs.begin(), blobs.end(),
                                  [block](const fo4::nif::CollisionBlob& blob) { return blob.block == block; });
  return found == blobs.end() ? nullptr : &*found;
}

}  // namespace

TEST(CollisionWrite, ReplacesTheBlobAndKeepsTheOtherBlocks) {
  const auto original_payload = box_payload(0.5f);
  const auto replaced_payload = box_payload(1.0f);
  const auto original = collision_nif(prefixed(original_payload, original_payload.size()));
  const auto nif = fo4::nif::read_nif(original);
  const auto replaced = fo4::nif::replace_collision_blob(nif, k_system_block, replaced_payload);
  const auto out = fo4::nif::read_nif(replaced);
  EXPECT_EQ(block_bytes(out, 0), block_bytes(nif, 0)) << "a non-collision block changed";
  EXPECT_EQ(block_bytes(out, 2), block_bytes(nif, 2)) << "the collision object changed";
  const auto blobs = fo4::nif::read_collision_blobs(out);
  const auto* blob = blob_at(blobs, k_system_block);
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->bytes, replaced_payload);
  EXPECT_EQ(blob->owner, 2);
  EXPECT_EQ(blob->body_id, 42U);
  EXPECT_EQ(out.blocks()[k_system_block].size, 4 + replaced_payload.size());
  const auto system = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob->bytes));
  ASSERT_EQ(system.shapes.size(), 1U);
  EXPECT_EQ(system.shapes[0].kind, fo4::nif::HknpShapeKind::convex);
}

TEST(CollisionWrite, ReplacingWithTheSamePayloadReproducesTheFile) {
  // Constraint: the fixture carries strings, an export string, groups and a max string length so header order slips show.
  const auto payload = box_payload(0.5f);
  const auto original = collision_nif(prefixed(payload, payload.size()));
  const auto nif = fo4::nif::read_nif(original);
  EXPECT_EQ(fo4::nif::replace_collision_blob(nif, k_system_block, payload), original);
}

TEST(CollisionWrite, KeepsBytesAfterTheStoredBlob) {
  // Constraint: the reader accepts trailing bytes after the stored blob, so a replacement must preserve them.
  const auto payload = box_payload(0.5f);
  const auto replacement = box_payload(1.0f);
  const std::vector<std::byte> tail{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}, std::byte{0xDD}};
  auto block_data = prefixed(payload, payload.size());
  block_data.insert(block_data.end(), tail.begin(), tail.end());
  const auto nif = fo4::nif::read_nif(collision_nif(block_data));
  const auto out = fo4::nif::read_nif(fo4::nif::replace_collision_blob(nif, k_system_block, replacement));
  const auto raw = block_bytes(out, k_system_block);
  ASSERT_EQ(raw.size(), 4 + replacement.size() + tail.size());
  EXPECT_EQ(std::vector<std::byte>(raw.end() - 4, raw.end()), tail);
  const auto blobs = fo4::nif::read_collision_blobs(out);
  const auto* blob = blob_at(blobs, k_system_block);
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->bytes, replacement);
}

TEST(CollisionWrite, RefusesBlocksAndPayloadsWithTheDocumentedErrorTypes) {
  const auto payload = box_payload(0.5f);
  const auto nif = fo4::nif::read_nif(collision_nif(prefixed(payload, payload.size())));
  EXPECT_THROW(fo4::nif::replace_collision_blob(nif, 0, payload), fo4::parse_error);
  EXPECT_THROW(fo4::nif::replace_collision_blob(nif, 9, payload), fo4::parse_error);
  const auto overlong = fo4::nif::read_nif(collision_nif(prefixed(payload, payload.size() + 1)));
  EXPECT_THROW(fo4::nif::replace_collision_blob(overlong, k_system_block, payload), fo4::parse_error);
  EXPECT_THROW(fo4::nif::replace_collision_blob(nif, k_system_block, {}), std::invalid_argument);
  const std::vector<std::byte> garbage{std::byte{1}, std::byte{2}, std::byte{3}};
  EXPECT_THROW(fo4::nif::replace_collision_blob(nif, k_system_block, garbage), fo4::parse_error);
}

TEST(CollisionWrite, EncodesAPayloadFromMeshGeometryThatDecodesBack) {
  std::vector<float> verts;
  for (const float x : {-0.5f, 0.5f}) {
    for (const float y : {-0.5f, 0.5f}) {
      for (const float z : {-0.5f, 0.5f}) verts.insert(verts.end(), {x, y, z});
    }
  }
  const std::vector<std::uint16_t> tris{0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1,
                                        2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
  const auto payload = fo4::nif::encode_collision_payload(verts, tris, 0x1234U, fo4::nif::HknpStaticBody{});
  ASSERT_FALSE(payload.empty());
  const auto hull = fo4::nif::build_convex_hull_from_mesh(verts, tris, 0x1234U);
  const auto direct = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(hull.shape, fo4::nif::HknpStaticBody{}));
  EXPECT_EQ(payload, direct);
  const auto system = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(payload));
  ASSERT_EQ(system.shapes.size(), 1U);
  EXPECT_EQ(system.shapes[0].vertices.size(), hull.shape.vertices.size());
}
