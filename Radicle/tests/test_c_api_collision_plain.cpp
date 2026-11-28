#include "fo4/c_api_collision_plain.h"

#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "nif_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using nif_fixture::Block;
using nif_fixture::Writer;

class Nif {
public:
  explicit Nif(const std::vector<std::byte>& bytes) {
    const auto status = radicle_nif_read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &handle_);
    EXPECT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();
  }
  ~Nif() { radicle_nif_destroy(handle_); }
  Nif(const Nif&) = delete;
  Nif& operator=(const Nif&) = delete;

  radicle_nif* get() const { return handle_; }

private:
  radicle_nif* handle_{nullptr};
};

class Plain {
public:
  explicit Plain(const radicle_nif* nif) {
    const auto status = radicle_nif_collision_plain(nif, &handle_);
    EXPECT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();
  }
  ~Plain() { radicle_collision_plain_destroy(handle_); }
  Plain(const Plain&) = delete;
  Plain& operator=(const Plain&) = delete;

  radicle_collision_plain* get() const { return handle_; }

private:
  radicle_collision_plain* handle_{nullptr};
};

Block system_block(const fo4::nif::HknpPackfile& pack) {
  const auto payload = fo4::nif::write_hknp_packfile(pack);
  Writer w;
  w.u32(static_cast<std::uint32_t>(payload.size()));
  for (const auto value : payload) w.u8(std::to_integer<std::uint8_t>(value));
  return Block{"bhkPhysicsSystem", w.bytes()};
}

std::vector<std::byte> nif_with(const std::vector<Block>& blocks) { return nif_fixture::build_nif({}, blocks, {}); }

constexpr float k_units = 69.99125F;

fo4::nif::Transform xf(const std::array<float, 3>& t, const std::array<float, 9>& r, float s) {
  fo4::nif::Transform out;
  std::copy(t.begin(), t.end(), out.t);
  std::copy(r.begin(), r.end(), out.r);
  out.s = s;
  return out;
}

Block object_block(std::int32_t target, std::int32_t data, std::uint32_t body_id) {
  Writer w;
  w.i32(target);
  w.u16(0);
  w.i32(data);
  w.u32(body_id);
  return Block{"bhkNPCollisionObject", w.bytes()};
}

constexpr std::array<float, 9> k_identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
constexpr std::array<float, 9> k_quarter_turn_z{0, -1, 0, 1, 0, 0, 0, 0, 1};

std::vector<std::byte> owned_nif(const std::vector<Block>& objects) {
  std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, xf({1000, 2000, 3000}, k_identity, 5.0F), {1}),
      nif_fixture::node_block("NiNode", 0, xf({2 * k_units, 0, 0}, k_quarter_turn_z, 1.0F), {2}),
      nif_fixture::node_block("NiNode", 0, xf({k_units, 0, 3 * k_units}, k_identity, 2.0F), {}),
      system_block(fo4::nif::author_hknp_static_body(fo4::nif::make_hknp_box({1, 2, 3}, {0.5F, 0.25F, 0.125F}, 77U), {})),
  };
  blocks.insert(blocks.end(), objects.begin(), objects.end());
  return nif_fixture::build_nif({"node"}, blocks, {0});
}

struct OwnerFrame {
  std::array<float, 3> translation{};
  std::array<float, 9> rotation{};
  float scale{0.0F};
};

void expect_frame(const OwnerFrame& frame, const std::array<float, 3>& translation, const std::array<float, 9>& rotation,
                  float scale) {
  for (std::size_t i = 0; i < 3; ++i) EXPECT_NEAR(frame.translation[i], translation[i], 1.0e-4F) << "translation " << i;
  for (std::size_t i = 0; i < 9; ++i) EXPECT_NEAR(frame.rotation[i], rotation[i], 1.0e-5F) << "rotation " << i;
  EXPECT_NEAR(frame.scale, scale, 1.0e-6F);
}

// Constraint: node chain root -> mid (2, 0, 0) m turned a quarter about z -> leaf (1, 0, 3) m scale 2, the root left out, in NIF axes; engine axes are (x, z, -y).
const std::array<float, 3> k_leaf_translation{2.0F, 3.0F, -1.0F};
const std::array<float, 9> k_leaf_rotation{0, 0, 1, 0, 1, 0, -1, 0, 0};

}  // namespace

TEST(CapiCollisionPlain, ReadsABoxBodyAndShapeInEngineAxes) {
  const auto bytes = nif_with(
      {system_block(fo4::nif::author_hknp_static_body(fo4::nif::make_hknp_box({1, 2, 3}, {0.5f, 0.25f, 0.125f}, 77U), {}))});
  Nif nif(bytes);
  Plain plain(nif.get());
  ASSERT_NE(plain.get(), nullptr);

  size_t bodies = 99;
  size_t shapes = 99;
  ASSERT_EQ(radicle_collision_plain_body_count(plain.get(), &bodies), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_collision_plain_shape_count(plain.get(), &shapes), RADICLE_STATUS_OK);
  EXPECT_EQ(bodies, 1U);
  EXPECT_EQ(shapes, 1U);

  radicle_collision_plain_body body{};
  ASSERT_EQ(radicle_collision_plain_read_body(plain.get(), 0, &body), RADICLE_STATUS_OK);
  EXPECT_EQ(body.collision_index, 0U);
  EXPECT_EQ(body.body, 0U);
  EXPECT_EQ(body.first_shape, 0U);
  EXPECT_EQ(body.shape_count, 1U);
  EXPECT_EQ(body.skipped_shapes, 0U);
  EXPECT_EQ(body.has_mass, 0);

  radicle_collision_plain_shape shape{};
  ASSERT_EQ(radicle_collision_plain_read_shape(plain.get(), 0, &shape), RADICLE_STATUS_OK);
  EXPECT_EQ(shape.kind, static_cast<uint32_t>(RADICLE_COLLISION_SHAPE_BOX));
  EXPECT_EQ(shape.material, 77U);
  EXPECT_NEAR(shape.origin[0], 1.0f, 1.0e-4f);
  EXPECT_NEAR(shape.origin[1], 3.0f, 1.0e-4f);
  EXPECT_NEAR(shape.origin[2], -2.0f, 1.0e-4f);
  EXPECT_NEAR(shape.half_extents[0], 0.5f, 1.0e-4f);
  EXPECT_NEAR(shape.half_extents[1], 0.125f, 1.0e-4f);
  EXPECT_NEAR(shape.half_extents[2], 0.25f, 1.0e-4f);
  EXPECT_EQ(shape.point_count, 0U);

  float* points = reinterpret_cast<float*>(0x1);
  size_t count = 99;
  ASSERT_EQ(radicle_collision_plain_shape_points(plain.get(), 0, &points, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(points, nullptr);
  EXPECT_EQ(count, 0U);

  uint32_t skipped = 99;
  uint32_t undecodable = 99;
  ASSERT_EQ(radicle_collision_plain_counts(plain.get(), &skipped, &undecodable), RADICLE_STATUS_OK);
  EXPECT_EQ(skipped, 0U);
  EXPECT_EQ(undecodable, 0U);
}

TEST(CapiCollisionPlain, HandsBackHullPointsAsCallerOwnedFloats) {
  fo4::nif::HknpShape convex;
  convex.kind = fo4::nif::HknpShapeKind::convex;
  convex.class_name = "hknpConvexShape";
  convex.shape_flags = 0x01000143U;
  convex.convex_radius = 0.04f;
  convex.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 2, 0}, {0, 0, 3}};
  const auto bytes = nif_with({system_block(fo4::nif::author_hknp_static_body(convex, {}))});
  Nif nif(bytes);
  Plain plain(nif.get());

  radicle_collision_plain_shape shape{};
  ASSERT_EQ(radicle_collision_plain_read_shape(plain.get(), 0, &shape), RADICLE_STATUS_OK);
  EXPECT_EQ(shape.kind, static_cast<uint32_t>(RADICLE_COLLISION_SHAPE_CONVEX_HULL));
  EXPECT_EQ(shape.point_count, 4U);
  EXPECT_FLOAT_EQ(shape.margin, 0.04f);

  float* points = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_collision_plain_shape_points(plain.get(), 0, &points, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 12U);
  ASSERT_NE(points, nullptr);
  EXPECT_FLOAT_EQ(points[8], -2.0f);
  EXPECT_FLOAT_EQ(points[10], 3.0f);
  radicle_free_floats(points);

  uint32_t* indices = reinterpret_cast<uint32_t*>(0x1);
  size_t index_count = 99;
  ASSERT_EQ(radicle_collision_plain_shape_indices(plain.get(), 0, &indices, &index_count), RADICLE_STATUS_OK);
  EXPECT_EQ(indices, nullptr);
  EXPECT_EQ(index_count, 0U);
}

TEST(CapiCollisionPlain, ANifWithoutCollisionHasNoBodies) {
  const auto bytes = nif_with({});
  Nif nif(bytes);
  Plain plain(nif.get());
  size_t bodies = 99;
  ASSERT_EQ(radicle_collision_plain_body_count(plain.get(), &bodies), RADICLE_STATUS_OK);
  EXPECT_EQ(bodies, 0U);
}

TEST(CapiCollisionPlain, RejectsMissingArgumentsAndOutOfRangeIndexes) {
  const auto bytes = nif_with({system_block(fo4::nif::author_hknp_static_body(fo4::nif::make_hknp_box({}, {1, 1, 1}, 1U), {}))});
  Nif nif(bytes);
  Plain plain(nif.get());

  radicle_collision_plain* out = reinterpret_cast<radicle_collision_plain*>(0x1);
  EXPECT_EQ(radicle_nif_collision_plain(nullptr, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_nif_collision_plain(nif.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  size_t count = 0;
  EXPECT_EQ(radicle_collision_plain_body_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_body_count(plain.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_shape_count(nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_collision_plain_body body{};
  EXPECT_EQ(radicle_collision_plain_read_body(plain.get(), 1, &body), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_read_body(plain.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_collision_plain_shape shape{};
  EXPECT_EQ(radicle_collision_plain_read_shape(plain.get(), 1, &shape), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_read_shape(plain.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  float* points = nullptr;
  EXPECT_EQ(radicle_collision_plain_shape_points(plain.get(), 1, &points, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_shape_points(plain.get(), 0, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  uint32_t* values = nullptr;
  EXPECT_EQ(radicle_collision_plain_shape_triangle_materials(plain.get(), 0, &values, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  uint32_t skipped = 0;
  EXPECT_EQ(radicle_collision_plain_counts(plain.get(), &skipped, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_collision_plain_destroy(nullptr);
}

// Detects (invariant): an owner frame that keeps the root, applies the chain parent-first, reads the rotation transposed or leaves translation in NIF units and axes; a quarter turn under a translated parent with a scaled root distinguishes each.
TEST(CapiCollisionPlain, OwnerTransformIsTheNodeChainWithoutTheRootInEngineAxesAndMetres) {
  const auto bytes = owned_nif({object_block(2, 3, 0)});
  Nif nif(bytes);
  OwnerFrame frame;
  ASSERT_EQ(radicle_nif_collision_owner_transform(nif.get(), 0, 0, frame.translation.data(), frame.rotation.data(), &frame.scale),
            RADICLE_STATUS_OK) << radicle_last_error();
  expect_frame(frame, k_leaf_translation, k_leaf_rotation, 2.0F);
}

// Detects: the owner to body join staying in the host: a plain body must carry its owner's block, node index and frame, and a second object naming the same body must not replace the first.
TEST(CapiCollisionPlain, ABodyCarriesItsFirstOwnersFrame) {
  const auto bytes = owned_nif({object_block(2, 3, 0), object_block(1, 3, 0)});
  Nif nif(bytes);
  Plain plain(nif.get());
  OwnerFrame frame;
  int32_t owner_block = -2;
  int32_t target_node = -2;
  ASSERT_EQ(radicle_collision_plain_body_owner(plain.get(), 0, &owner_block, &target_node, frame.translation.data(),
                                               frame.rotation.data(), &frame.scale),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(owner_block, 4);
  EXPECT_EQ(target_node, 2);
  expect_frame(frame, k_leaf_translation, k_leaf_rotation, 2.0F);
}

// Detects (invariant): a body no collision object names being dropped or given a made-up owner; it stays in the list with owner block -1 and an identity frame.
TEST(CapiCollisionPlain, AnUnownedBodyIsKeptWithNoOwnerAndAnIdentityFrame) {
  const auto bytes = owned_nif({});
  Nif nif(bytes);
  Plain plain(nif.get());
  size_t bodies = 0;
  ASSERT_EQ(radicle_collision_plain_body_count(plain.get(), &bodies), RADICLE_STATUS_OK);
  ASSERT_EQ(bodies, 1U);
  OwnerFrame frame;
  int32_t owner_block = -2;
  int32_t target_node = -2;
  ASSERT_EQ(radicle_collision_plain_body_owner(plain.get(), 0, &owner_block, &target_node, frame.translation.data(),
                                               frame.rotation.data(), &frame.scale),
            RADICLE_STATUS_OK);
  EXPECT_EQ(owner_block, -1);
  EXPECT_EQ(target_node, -1);
  expect_frame(frame, {0, 0, 0}, k_identity, 1.0F);
}

// Detects (invariant): an owner on the root, or on a block that is not a node, getting the root's transform or garbage instead of identity.
TEST(CapiCollisionPlain, ARootOrNonNodeTargetGivesAnIdentityFrame) {
  const auto bytes = owned_nif({object_block(0, 3, 0), object_block(-1, 3, 0), object_block(3, 3, 0)});
  Nif nif(bytes);
  for (size_t owner = 0; owner < 3; ++owner) {
    OwnerFrame frame;
    ASSERT_EQ(radicle_nif_collision_owner_transform(nif.get(), 0, owner, frame.translation.data(), frame.rotation.data(),
                                                    &frame.scale),
              RADICLE_STATUS_OK) << radicle_last_error();
    expect_frame(frame, {0, 0, 0}, k_identity, 1.0F);
  }
}

// Detects (invariant): the owner exports reading past a collision, owner or body index, or dereferencing a missing output.
TEST(CapiCollisionPlain, OwnerExportsRejectMissingArgumentsAndOutOfRangeIndexes) {
  const auto bytes = owned_nif({object_block(2, 3, 0)});
  Nif nif(bytes);
  Plain plain(nif.get());
  OwnerFrame f;
  EXPECT_EQ(radicle_nif_collision_owner_transform(nif.get(), 1, 0, f.translation.data(), f.rotation.data(), &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_owner_transform(nif.get(), 0, 1, f.translation.data(), f.rotation.data(), &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_owner_transform(nullptr, 0, 0, f.translation.data(), f.rotation.data(), &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_collision_owner_transform(nif.get(), 0, 0, nullptr, f.rotation.data(), &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  int32_t block = 0;
  int32_t node = 0;
  EXPECT_EQ(radicle_collision_plain_body_owner(plain.get(), 1, &block, &node, f.translation.data(), f.rotation.data(), &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_body_owner(nullptr, 0, &block, &node, f.translation.data(), f.rotation.data(), &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_collision_plain_body_owner(plain.get(), 0, &block, &node, f.translation.data(), nullptr, &f.scale),
            RADICLE_STATUS_INVALID_ARGUMENT);
}
