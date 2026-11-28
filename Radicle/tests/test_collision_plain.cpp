#include "fo4/nif/collision.hpp"
#include "fo4/nif/collision_plain.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using fo4::nif::HknpShape;
using fo4::nif::HknpShapeKind;
using fo4::nif::HknpSystem;
using fo4::nif::HknpVec3;
using fo4::nif::PlainCollision;
using fo4::nif::PlainShapeKind;

fo4::nif::CollisionBlob blob_of(const fo4::nif::HknpPackfile& pack) {
  fo4::nif::CollisionBlob blob;
  blob.type = "bhkPhysicsSystem";
  blob.bytes = fo4::nif::write_hknp_packfile(pack);
  return blob;
}

PlainCollision plain_of(const fo4::nif::HknpPackfile& pack) {
  const std::vector<fo4::nif::CollisionBlob> blobs{blob_of(pack)};
  return fo4::nif::build_plain_collision(blobs);
}

void expect_near3(const std::array<float, 3>& got, const std::array<float, 3>& want, float tolerance = 1.0e-4f) {
  for (std::size_t k = 0; k < 3; ++k) EXPECT_NEAR(got[k], want[k], tolerance) << "axis " << k;
}

HknpShape sphere_at(const HknpVec3& centre, float radius) {
  HknpShape shape;
  shape.kind = HknpShapeKind::sphere;
  shape.class_name = "hknpSphereShape";
  shape.point_a = centre;
  shape.radius = radius;
  return shape;
}

fo4::nif::HknpCompoundChild child_of(HknpShape shape, const fo4::nif::HknpInstance& placement) {
  fo4::nif::HknpCompoundChild child;
  child.shape = std::move(shape);
  child.placement = placement;
  return child;
}

HknpSystem mesh_system(const fo4::nif::HknpVec3& scale) {
  HknpSystem system;
  HknpShape mesh;
  mesh.kind = HknpShapeKind::compressed_mesh;
  mesh.material_crc = 5;
  mesh.mesh_vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 2}};
  fo4::nif::HknpMeshTriangle quad;
  quad.indices = {0, 1, 2, 3};
  quad.quad = true;
  quad.material_crc = 9;
  fo4::nif::HknpMeshTriangle triangle;
  triangle.indices = {3, 2, 1, 0};
  triangle.material_crc = 11;
  mesh.mesh_triangles = {quad, triangle};
  system.shapes.push_back(mesh);
  fo4::nif::HknpBody body;
  body.shape = 0;
  body.shape_object = 0;
  if (scale != HknpVec3{1, 1, 1}) {
    HknpShape compound;
    compound.kind = HknpShapeKind::compound;
    fo4::nif::HknpInstance instance;
    instance.scale = scale;
    instance.shape = 0;
    instance.child_object = 0;
    compound.instances.push_back(instance);
    system.shapes.push_back(compound);
    body.shape = 1;
  }
  system.bodies.push_back(body);
  return system;
}

}  // namespace

TEST(CollisionPlain, BoxKeepsCentreExtentsMaterialAndLayerInEngineAxes) {
  fo4::nif::HknpStaticBody body;
  body.position = {10.0f, 20.0f, 30.0f};
  body.packed_filter = 0x0A0000F5U;
  const auto plain =
      plain_of(fo4::nif::author_hknp_static_body(fo4::nif::make_hknp_box({1, 2, 3}, {0.5f, 0.25f, 0.125f}, 77U), body));
  ASSERT_EQ(plain.shapes.size(), 1U);
  ASSERT_EQ(plain.bodies.size(), 1U);
  EXPECT_EQ(plain.undecodable_systems, 0U);
  const auto& shape = plain.shapes[0];
  EXPECT_EQ(shape.kind, PlainShapeKind::box);
  expect_near3(shape.origin, {1.0f, 3.0f, -2.0f});
  expect_near3(shape.half_extents, {0.5f, 0.125f, 0.25f});
  EXPECT_EQ(shape.material, 77U);
  EXPECT_EQ(shape.basis, (std::array<float, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1}));
  const auto& placed = plain.bodies[0];
  EXPECT_EQ(placed.layer, 0xF5U);
  EXPECT_FALSE(placed.dynamic);
  EXPECT_FALSE(placed.has_mass);
  EXPECT_FALSE(placed.has_center_of_mass);
  expect_near3(placed.origin, {10.0f, 30.0f, -20.0f});
  EXPECT_EQ(placed.first_shape, 0U);
  EXPECT_EQ(placed.shape_count, 1U);
}

TEST(CollisionPlain, SphereAndCapsuleEndsFollowTheAxisSwap) {
  const auto sphere = plain_of(fo4::nif::author_hknp_static_body(sphere_at({2, 3, 4}, 0.5f), {}));
  ASSERT_EQ(sphere.shapes.size(), 1U);
  EXPECT_EQ(sphere.shapes[0].kind, PlainShapeKind::sphere);
  expect_near3(sphere.shapes[0].origin, {2.0f, 4.0f, -3.0f});
  EXPECT_NEAR(sphere.shapes[0].radius, 0.5f, 1.0e-6f);

  HknpShape capsule;
  capsule.kind = HknpShapeKind::capsule;
  capsule.class_name = "hknpCapsuleShape";
  capsule.radius = 0.5f;
  capsule.point_a = {0, 0, 0};
  capsule.point_b = {0, 0, 2};
  const auto round = plain_of(fo4::nif::author_hknp_static_body(capsule, {}));
  ASSERT_EQ(round.shapes.size(), 1U);
  EXPECT_EQ(round.shapes[0].kind, PlainShapeKind::capsule);
  expect_near3(round.shapes[0].point_a, {0.0f, 0.0f, 0.0f});
  expect_near3(round.shapes[0].point_b, {0.0f, 2.0f, 0.0f});
  EXPECT_NEAR(round.shapes[0].radius, 0.5f, 1.0e-5f);
}

// Detects: a capsule whose two end points coincide coming back as a capsule, which leaves the host a zero-length axis to normalise; a coincident or near-coincident pair must be a sphere at that point, and a pair just past the tolerance must stay a capsule.
TEST(CollisionPlain, ACapsuleWithEqualEndPointsIsASphereAtThatPoint) {
  const auto capsule_with = [](const HknpVec3& a, const HknpVec3& b) {
    HknpShape capsule;
    capsule.kind = HknpShapeKind::capsule;
    capsule.class_name = "hknpCapsuleShape";
    capsule.radius = 0.5f;
    capsule.point_a = a;
    capsule.point_b = b;
    return plain_of(fo4::nif::author_hknp_static_body(capsule, {}));
  };
  const auto same = capsule_with({1, 2, 3}, {1, 2, 3});
  ASSERT_EQ(same.shapes.size(), 1U);
  EXPECT_EQ(same.shapes[0].kind, PlainShapeKind::sphere);
  expect_near3(same.shapes[0].origin, {1.0f, 3.0f, -2.0f});
  expect_near3(same.shapes[0].point_a, {0.0f, 0.0f, 0.0f}, 0.0f);
  expect_near3(same.shapes[0].point_b, {0.0f, 0.0f, 0.0f}, 0.0f);
  EXPECT_NEAR(same.shapes[0].radius, 0.5f, 1.0e-5f);

  const auto near = capsule_with({1, 2, 3}, {1, 2, 3.000005f});
  ASSERT_EQ(near.shapes.size(), 1U);
  EXPECT_EQ(near.shapes[0].kind, PlainShapeKind::sphere);

  const auto apart = capsule_with({1, 2, 3}, {1, 2, 3.0001f});
  ASSERT_EQ(apart.shapes.size(), 1U);
  EXPECT_EQ(apart.shapes[0].kind, PlainShapeKind::capsule);
}

TEST(CollisionPlain, ConvexHullKeepsPointsAndMargin) {
  HknpShape convex;
  convex.kind = HknpShapeKind::convex;
  convex.class_name = "hknpConvexShape";
  convex.shape_flags = 0x01000143U;
  convex.convex_radius = 0.04f;
  convex.material_crc = 3U;
  convex.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 2, 0}, {0, 0, 3}};
  const auto plain = plain_of(fo4::nif::author_hknp_static_body(convex, {}));
  ASSERT_EQ(plain.shapes.size(), 1U);
  const auto& shape = plain.shapes[0];
  EXPECT_EQ(shape.kind, PlainShapeKind::convex_hull);
  EXPECT_FLOAT_EQ(shape.margin, 0.04f);
  const std::vector<float> want{0, 0, 0, 1, 0, 0, 0, 0, -2, 0, 3, 0};
  ASSERT_EQ(shape.points.size(), want.size());
  for (std::size_t i = 0; i < want.size(); ++i) EXPECT_NEAR(shape.points[i], want[i], 1.0e-6f) << i;
}

TEST(CollisionPlain, CompoundInstanceRotatesScalesThenMoves) {
  fo4::nif::HknpInstance placement;
  placement.rotation_rows = {{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
  placement.translation = {0, 0, 3};
  placement.scale = {2, 2, 2};
  const auto plain = plain_of(fo4::nif::author_hknp_compound_body(
      {child_of(fo4::nif::make_hknp_box({1, 0, 0}, {0.5f, 0.25f, 0.125f}, 1U), placement)}, {}));
  ASSERT_EQ(plain.shapes.size(), 1U);
  const auto& shape = plain.shapes[0];
  EXPECT_EQ(shape.kind, PlainShapeKind::box);
  expect_near3(shape.origin, {0.0f, 3.0f, -2.0f});
  expect_near3(shape.half_extents, {1.0f, 0.25f, 0.5f});
  const std::array<float, 9> want{0, 0, 1, 0, 1, 0, -1, 0, 0};
  for (std::size_t i = 0; i < want.size(); ++i) EXPECT_NEAR(shape.basis[i], want[i], 1.0e-5f) << i;
}

TEST(CollisionPlain, NonUniformScaleMakesAHullForABoxAndSkipsASphere) {
  fo4::nif::HknpInstance stretched;
  stretched.scale = {1, 2, 1};
  const auto boxed = plain_of(fo4::nif::author_hknp_compound_body(
      {child_of(fo4::nif::make_hknp_box({0, 0, 0}, {1, 1, 1}, 1U), stretched)}, {}));
  ASSERT_EQ(boxed.shapes.size(), 1U);
  EXPECT_EQ(boxed.shapes[0].kind, PlainShapeKind::convex_hull);
  ASSERT_EQ(boxed.shapes[0].points.size(), 24U);
  float widest_z = 0.0f;
  for (std::size_t i = 2; i < 24; i += 3) widest_z = std::max(widest_z, std::fabs(boxed.shapes[0].points[i]));
  EXPECT_NEAR(widest_z, 1.98f, 1.0e-4f);

  const auto round =
      plain_of(fo4::nif::author_hknp_compound_body({child_of(sphere_at({0, 0, 0}, 1.0f), stretched)}, {}));
  EXPECT_TRUE(round.shapes.empty());
  EXPECT_EQ(round.skipped_shapes, 1U);
  ASSERT_EQ(round.bodies.size(), 1U);
  EXPECT_EQ(round.bodies[0].shape_count, 0U);
  EXPECT_EQ(round.bodies[0].skipped_shapes, 1U);
}

TEST(CollisionPlain, DynamicBodyCarriesMassAndCentreAndStaticCarriesNone) {
  fo4::nif::HknpSystemBody dynamic;
  dynamic.shape = fo4::nif::make_hknp_box({0.5f, 0, 0}, {1, 1, 1}, 1U);
  dynamic.mass = 4.0f;
  fo4::nif::HknpSystemBody fixed;
  fixed.shape = fo4::nif::make_hknp_box({}, {1, 1, 1}, 1U);
  const auto plain = plain_of(fo4::nif::author_hknp_system({dynamic, fixed}));
  ASSERT_EQ(plain.bodies.size(), 2U);
  EXPECT_TRUE(plain.bodies[0].dynamic);
  EXPECT_TRUE(plain.bodies[0].has_mass);
  EXPECT_NEAR(plain.bodies[0].mass, 4.0f, 1.0e-3f);
  ASSERT_TRUE(plain.bodies[0].has_center_of_mass);
  expect_near3(plain.bodies[0].center_of_mass, {0.5f, 0.0f, 0.0f}, 1.0e-3f);
  EXPECT_FALSE(plain.bodies[1].dynamic);
  EXPECT_FALSE(plain.bodies[1].has_mass);
  EXPECT_FALSE(plain.bodies[1].has_center_of_mass);
  EXPECT_EQ(plain.bodies[1].first_shape, 1U);
}

TEST(CollisionPlain, AnUndecodableSystemIsCountedAndTheOthersKeepTheirIndex) {
  fo4::nif::CollisionBlob junk;
  junk.type = "bhkPhysicsSystem";
  junk.bytes.assign(0x100, std::byte{0});
  const std::vector<fo4::nif::CollisionBlob> blobs{
      junk, blob_of(fo4::nif::author_hknp_static_body(fo4::nif::make_hknp_box({}, {1, 1, 1}, 1U), {}))};
  const auto plain = fo4::nif::build_plain_collision(blobs);
  EXPECT_EQ(plain.undecodable_systems, 1U);
  ASSERT_EQ(plain.bodies.size(), 1U);
  EXPECT_EQ(plain.bodies[0].system, 1U);
  ASSERT_EQ(plain.shapes.size(), 1U);
  EXPECT_EQ(plain.shapes[0].system, 1U);
}

TEST(CollisionPlain, MeshSplitsQuadsKeepsMaterialsAndSwapsAxes) {
  PlainCollision plain;
  fo4::nif::append_plain_collision(mesh_system({1, 1, 1}), 0, plain);
  ASSERT_EQ(plain.shapes.size(), 1U);
  const auto& mesh = plain.shapes[0];
  EXPECT_EQ(mesh.kind, PlainShapeKind::triangle_mesh);
  EXPECT_EQ(mesh.material, 5U);
  EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 3, 2, 1}));
  EXPECT_EQ(mesh.triangle_materials, (std::vector<std::uint32_t>{9, 9, 11}));
  ASSERT_EQ(mesh.points.size(), 12U);
  EXPECT_EQ(std::vector<float>(mesh.points.begin() + 9, mesh.points.end()), (std::vector<float>{0, 2, -1}));
}

TEST(CollisionPlain, AMirroredPlacementFlipsTriangleWinding) {
  PlainCollision plain;
  fo4::nif::append_plain_collision(mesh_system({-1, 1, 1}), 0, plain);
  ASSERT_EQ(plain.shapes.size(), 1U);
  EXPECT_EQ(plain.shapes[0].indices, (std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2, 3, 1, 2}));
  EXPECT_FLOAT_EQ(plain.shapes[0].points[3], -1.0f);
}

TEST(CollisionPlain, AnUnsupportedBodyShapeIsCountedAsSkipped) {
  HknpSystem system;
  fo4::nif::HknpBody body;
  body.shape = -1;
  body.shape_object = 3;
  system.bodies.push_back(body);
  PlainCollision plain;
  fo4::nif::append_plain_collision(system, 0, plain);
  EXPECT_EQ(plain.skipped_shapes, 1U);
  ASSERT_EQ(plain.bodies.size(), 1U);
  EXPECT_EQ(plain.bodies[0].shape_count, 0U);
  EXPECT_EQ(plain.bodies[0].skipped_shapes, 1U);
}
