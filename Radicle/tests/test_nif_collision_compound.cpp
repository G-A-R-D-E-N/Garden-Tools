#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using fo4::nif::HknpCompoundChild;
using fo4::nif::HknpInstance;
using fo4::nif::HknpShape;
using fo4::nif::HknpShapeKind;
using fo4::nif::HknpSystem;
using fo4::nif::HknpVec3;

std::vector<std::byte> rebuilt_bytes(const fo4::nif::HknpPackfile& pack) {
  return fo4::nif::write_hknp_packfile(pack);
}

std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

std::uint16_t u16_at(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint16_t value = 0;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

float f32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  float value = 0.0f;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

HknpSystem decoded(const fo4::nif::HknpPackfile& pack) {
  return fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(rebuilt_bytes(pack)));
}

HknpCompoundChild box_child(const HknpVec3& center, const HknpVec3& half, const HknpVec3& translation) {
  HknpCompoundChild child;
  child.shape = fo4::nif::make_hknp_box(center, half, 1U);
  child.placement.translation = translation;
  return child;
}

std::uint32_t local_target(const fo4::nif::HknpPackObject& object, std::uint32_t source) {
  for (const auto& fixup : object.local) {
    if (fixup.source == source) return fixup.target;
  }
  return 0xFFFFFFFFU;
}

std::uint32_t global_target(const fo4::nif::HknpPackObject& object, std::uint32_t source) {
  for (const auto& fixup : object.global) {
    if (fixup.source == source) return fixup.object;
  }
  return 0xFFFFFFFFU;
}

std::uint32_t object_at(const fo4::nif::HknpPackfile& pack, const std::string& name) {
  for (std::size_t i = 0; i < pack.objects.size(); ++i) {
    if (pack.objects[i].class_name == name) return static_cast<std::uint32_t>(i);
  }
  return 0xFFFFFFFFU;
}

}  // namespace

// Detects: a dropped child, a lost child shape, or a body naming the data object instead of the compound.
TEST(HknpEncode, CompoundBodyDecodesToItsChildrenAndTheirPlacements) {
  const std::vector<HknpCompoundChild> children{
      box_child({0.0f, 0.0f, 0.0f}, {0.5f, 0.25f, 1.0f}, {0.0f, 0.0f, 0.0f}),
      box_child({0.25f, 0.0f, 0.0f}, {0.75f, 0.25f, 1.0f}, {1.5f, 0.0f, 0.0f}),
  };
  const auto pack = fo4::nif::author_hknp_compound_body(children, {});
  const auto system = decoded(pack);

  ASSERT_EQ(system.bodies.size(), 1U);
  const auto& body = system.bodies[0];
  ASSERT_GE(body.shape, 0);
  const auto& compound = system.shapes[static_cast<std::size_t>(body.shape)];
  EXPECT_EQ(compound.kind, HknpShapeKind::compound);
  EXPECT_EQ(compound.class_name, "hknpDynamicCompoundShape");
  ASSERT_EQ(compound.instances.size(), 2U);
  for (std::size_t i = 0; i < compound.instances.size(); ++i) {
    const auto& instance = compound.instances[i];
    ASSERT_GE(instance.shape, 0);
    const auto& child = system.shapes[static_cast<std::size_t>(instance.shape)];
    EXPECT_EQ(child.kind, HknpShapeKind::convex);
    EXPECT_EQ(child.class_name, "hknpConvexPolytopeShape");
    EXPECT_EQ(child.vertices.size(), 8U);
    for (std::size_t k = 0; k < 3; ++k) {
      EXPECT_NEAR(instance.translation[k], children[i].placement.translation[k], 1.0e-4f);
      EXPECT_NEAR(instance.scale[k], 1.0f, 1.0e-6f);
    }
  }
}

// Detects: a tree whose record count, root links or leaf parent tags drift from the measured layout.
TEST(HknpEncode, CompoundDataWritesTheMeasuredTreeRecords) {
  const std::vector<HknpCompoundChild> children{
      box_child({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}),
      box_child({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, {4.0f, 0.0f, 0.0f}),
  };
  const auto pack = fo4::nif::author_hknp_compound_body(children, {});
  const auto system = decoded(pack);
  const auto& compound = system.shapes[static_cast<std::size_t>(system.bodies[0].shape)];
  ASSERT_EQ(compound.instances.size(), 2U);

  const std::uint32_t data_index = object_at(pack, "hknpDynamicCompoundShapeData");
  ASSERT_NE(data_index, 0xFFFFFFFFU);
  const auto& data = pack.objects[data_index];
  EXPECT_EQ(u32_at(data.bytes, 0x18), 5U);
  EXPECT_EQ(u32_at(data.bytes, 0x1c), 0x80000005U);
  EXPECT_EQ(u32_at(data.bytes, 0x20), 4U);
  EXPECT_EQ(u32_at(data.bytes, 0x28), 2U);
  EXPECT_EQ(data.bytes.size(), 0x60U + 4U * 0x20U);

  // Constraint: record 0 is padding, so the root must be record 1 and its children the two leaves after it.
  EXPECT_EQ(u32_at(data.bytes, 0x40), 0U);
  EXPECT_EQ(u32_at(data.bytes, 0x60 + 0x0c), 0x3f000000U);
  EXPECT_EQ(u16_at(data.bytes, 0x60 + 0x1c), 2U);
  EXPECT_EQ(u16_at(data.bytes, 0x60 + 0x1e), 3U);
  for (std::size_t record : {2U, 3U}) {
    const std::size_t at = 0x60 + (record - 1) * 0x20;
    EXPECT_EQ(u32_at(data.bytes, at + 0x0c), 0x3f000001U);
    EXPECT_EQ(u16_at(data.bytes, at + 0x1c), 0U);
  }
  // Constraint: a leaf's back-pointer must be its record index, folded into the translation's w lane.
  const auto& compound_object = pack.objects[object_at(pack, "hknpDynamicCompoundShape")];
  const std::uint32_t first_w = u32_at(compound_object.bytes, 0xd0 + 0x3c) & 0x00ffffffU;
  const std::uint32_t second_w = u32_at(compound_object.bytes, 0xd0 + 0x80 + 0x3c) & 0x00ffffffU;
  EXPECT_NE(first_w, second_w);
  EXPECT_TRUE(first_w == 2U || first_w == 3U);
  EXPECT_TRUE(second_w == 2U || second_w == 3U);
}

// Detects: a header box that misses a placed child, or sits away from +0x80 and +0x90.
TEST(HknpEncode, CompoundBoundsEncloseEveryPlacedChild) {
  const std::vector<HknpCompoundChild> children{
      box_child({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}),
      box_child({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, {4.0f, 0.0f, 0.0f}),
      box_child({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 6.0f, 0.0f}),
  };
  const auto pack = fo4::nif::author_hknp_compound_body(children, {});
  const auto& compound_object = pack.objects[object_at(pack, "hknpDynamicCompoundShape")];
  const float low_x = f32_at(compound_object.bytes, 0x80);
  const float low_y = f32_at(compound_object.bytes, 0x84);
  const float high_x = f32_at(compound_object.bytes, 0x90);
  const float high_y = f32_at(compound_object.bytes, 0x94);
  EXPECT_LE(low_x, -0.5f);
  EXPECT_GE(high_x, 4.5f);
  EXPECT_LE(low_y, -0.5f);
  EXPECT_GE(high_y, 7.0f);
  EXPECT_EQ(u32_at(compound_object.bytes, 0x70), 0xffffffffU);
  EXPECT_EQ(u32_at(compound_object.bytes, 0xa0), 1U);

  const auto system = decoded(pack);
  ASSERT_EQ(system.bodies.size(), 1U);
  EXPECT_EQ(system.shapes[static_cast<std::size_t>(system.bodies[0].shape)].instances.size(), 3U);
}

// Detects: a compound that bounds a mesh child by the wrong box, so broad-phase never considers the mesh.
TEST(HknpEncode, CompoundBoundsAMeshChildFromItsTriangles) {
  HknpShape mesh;
  mesh.kind = HknpShapeKind::compressed_mesh;
  mesh.class_name = "hknpCompressedMeshShape";
  mesh.material_crc = 1U;
  mesh.mesh_vertices = {{1.0f, 2.0f, 3.0f}, {4.0f, 6.0f, 9.0f}, {1.0f, 6.0f, 3.0f}};
  mesh.mesh_triangles.push_back(fo4::nif::HknpMeshTriangle{{0U, 1U, 2U, 0U}, false, 1U});
  HknpCompoundChild child;
  child.shape = mesh;
  const auto pack = fo4::nif::author_hknp_compound_body({child}, {});
  const auto& compound_object = pack.objects[object_at(pack, "hknpDynamicCompoundShape")];
  EXPECT_NEAR(f32_at(compound_object.bytes, 0x80), 1.0f, 1.0e-4f);
  EXPECT_NEAR(f32_at(compound_object.bytes, 0x84), 2.0f, 1.0e-4f);
  EXPECT_NEAR(f32_at(compound_object.bytes, 0x88), 3.0f, 1.0e-4f);
  EXPECT_NEAR(f32_at(compound_object.bytes, 0x90), 4.0f, 1.0e-4f);
  EXPECT_NEAR(f32_at(compound_object.bytes, 0x94), 6.0f, 1.0e-4f);
  EXPECT_NEAR(f32_at(compound_object.bytes, 0x98), 9.0f, 1.0e-4f);
}

// Detects: an empty, oversized, non-finite or stacked compound written as an unreadable packfile.
TEST(HknpEncode, CompoundAuthorRejectsInputItCannotBound) {
  EXPECT_THROW(fo4::nif::author_hknp_compound_body({}, {}), std::invalid_argument);

  HknpCompoundChild nested;
  nested.shape.kind = HknpShapeKind::compound;
  nested.shape.class_name = "hknpDynamicCompoundShape";
  EXPECT_THROW(fo4::nif::author_hknp_compound_body({nested}, {}), std::invalid_argument);

  HknpCompoundChild mesh;
  mesh.shape.kind = HknpShapeKind::compressed_mesh;
  mesh.shape.class_name = "hknpCompressedMeshShape";
  EXPECT_THROW(fo4::nif::author_hknp_compound_body({mesh}, {}), std::invalid_argument);

  auto child = box_child({}, {1.0f, 1.0f, 1.0f}, {});
  child.placement.scale = {0.0f, 1.0f, 1.0f};
  EXPECT_THROW(fo4::nif::author_hknp_compound_body({child}, {}), std::invalid_argument);

  child = box_child({}, {1.0f, 1.0f, 1.0f}, {});
  child.placement.translation = {0.0f, std::nanf(""), 0.0f};
  EXPECT_THROW(fo4::nif::author_hknp_compound_body({child}, {}), std::invalid_argument);
}

// Detects: a multi-body root with misplaced arrays, wrong motion or shape links, or a dynamic body without readable mass.
TEST(HknpEncode, SystemRootWritesTheMeasuredMultiBodyLayout) {
  const HknpShape child_box = fo4::nif::make_hknp_box({}, {0.25f, 0.25f, 0.25f}, 1U);
  fo4::nif::HknpSystemBody ground;
  ground.shape = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);

  fo4::nif::HknpSystemBody moving;
  HknpCompoundChild child;
  child.shape = child_box;
  moving.children = {child};
  moving.position = {0.0f, 2.0f, 0.0f};
  // Constraint: a quarter turn about z moves the shape's centre onto +y, so a recorded centre that skipped the rotation lands on x instead.
  moving.orientation = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
  moving.mass = 4.0f;
  moving.mass_properties = child_box.mass_properties;
  moving.mass_properties.center = {0.5f, 0.0f, 0.0f};

  const auto pack = fo4::nif::author_hknp_system({ground, moving});
  ASSERT_EQ(pack.objects.size(), 9U);
  const auto& root = pack.objects[0];
  EXPECT_EQ(root.class_name, "hknpPhysicsSystemData");
  EXPECT_EQ(root.bytes.size(), 0x2a0U);
  EXPECT_EQ(pack.objects[1].class_name, "hknpConvexPolytopeShape");
  EXPECT_EQ(pack.objects[4].class_name, "hknpDynamicCompoundShape");
  EXPECT_EQ(pack.objects.back().class_name, "hknpDynamicCompoundShapeData");

  EXPECT_EQ(u32_at(root.bytes, 0x18), 2U);
  EXPECT_EQ(u32_at(root.bytes, 0x1c), 0x80000002U);
  EXPECT_EQ(local_target(root, 0x10), 0x80U);
  EXPECT_EQ(u32_at(root.bytes, 0x28), 1U);
  EXPECT_EQ(local_target(root, 0x20), 0x120U);
  EXPECT_EQ(u32_at(root.bytes, 0x38), 1U);
  EXPECT_EQ(local_target(root, 0x30), 0x160U);
  EXPECT_EQ(u32_at(root.bytes, 0x48), 2U);
  EXPECT_EQ(local_target(root, 0x40), 0x1d0U);
  EXPECT_EQ(u32_at(root.bytes, 0x58), 0U);
  EXPECT_EQ(u32_at(root.bytes, 0x68), 2U);
  EXPECT_EQ(local_target(root, 0x60), 0x290U);

  // Constraint: the motion record holds gravity factor 1, the two velocity caps and the two dampings, as every measured one does.
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x08), 0x3f800000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x0c), 0x3f800000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x10), 0x42d0c000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x14), 0x41fc9000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x18), 0x3dcd0000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x1c), 0x3d4d0000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x20), 0x3e2e147bU);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x24), 0x3efb22d2U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x28), 0x3b23d70bU);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x2c), 0x3b23d70bU);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x30), 0x3f800000U);
  EXPECT_EQ(u32_at(root.bytes, 0x120 + 0x34), 0x999b67ffU);

  const float volume = moving.mass_properties.volume;
  const float density = 4.0f / volume;
  const std::size_t inertia = 0x160;
  EXPECT_EQ(u16_at(root.bytes, inertia), 0U);
  EXPECT_EQ(u16_at(root.bytes, inertia + 2), 1U);
  EXPECT_FLOAT_EQ(f32_at(root.bytes, inertia + 4), 0.25f);
  EXPECT_NEAR(f32_at(root.bytes, inertia + 8), density, 1.0e-5f * density);
  EXPECT_EQ(u32_at(root.bytes, inertia + 0x0c), 0x5f7ffff0U);
  EXPECT_EQ(u32_at(root.bytes, inertia + 0x10), 0x5f7ffff0U);
  for (std::size_t k = 0; k < 3; ++k) {
    const float inverse = 1.0f / (moving.mass_properties.inertia_raw[k] * density);
    EXPECT_NEAR(f32_at(root.bytes, inertia + 0x20 + 4 * k), inverse, 1.0e-4f * inverse);
  }
  // Constraint: the recorded centre is the shape centre carried to the body, which vanilla does on 1128 of 1255 measured records.
  EXPECT_NEAR(f32_at(root.bytes, inertia + 0x30), 0.0f, 1.0e-5f);
  EXPECT_NEAR(f32_at(root.bytes, inertia + 0x34), 2.5f, 1.0e-5f);
  EXPECT_NEAR(f32_at(root.bytes, inertia + 0x38), 0.0f, 1.0e-5f);
  EXPECT_FLOAT_EQ(f32_at(root.bytes, inertia + 0x2c), 1.0f);
  EXPECT_FLOAT_EQ(f32_at(root.bytes, inertia + 0x3c), 0.0f);
  EXPECT_FLOAT_EQ(f32_at(root.bytes, inertia + 0x4c), 1.0f);

  // Constraint: the static body stores the static motion index and no flags, and the moving one its ordinal and the dynamic flags.
  EXPECT_EQ(u32_at(root.bytes, 0x1d0 + 0x0c), 0x7fffffffU);
  EXPECT_EQ(static_cast<int>(std::to_integer<std::uint8_t>(root.bytes[0x1d0 + 0x12])), 0);
  EXPECT_EQ(u32_at(root.bytes, 0x1d0 + 0x18), 0U);
  EXPECT_EQ(u32_at(root.bytes, 0x230 + 0x0c), 0U);
  EXPECT_EQ(static_cast<int>(std::to_integer<std::uint8_t>(root.bytes[0x230 + 0x12])), 1);
  EXPECT_EQ(u32_at(root.bytes, 0x230 + 0x18), 0x80000000U);
  EXPECT_FLOAT_EQ(f32_at(root.bytes, 0x230 + 0x34), 2.0f);
  // Constraint: every measured vanilla root lists both cinfo shape pointers before any shape reference, so this order is required.
  ASSERT_EQ(root.global.size(), 4U);
  EXPECT_EQ(root.global[0].source, 0x1d0U);
  EXPECT_EQ(root.global[1].source, 0x230U);
  EXPECT_EQ(root.global[2].source, 0x290U);
  EXPECT_EQ(root.global[3].source, 0x298U);
  EXPECT_EQ(global_target(root, 0x1d0), 1U);
  EXPECT_EQ(global_target(root, 0x230), 4U);
  EXPECT_EQ(global_target(root, 0x290), 1U);
  EXPECT_EQ(global_target(root, 0x298), 4U);

  const auto system = decoded(pack);
  ASSERT_EQ(system.bodies.size(), 2U);
  ASSERT_EQ(system.motions.size(), 1U);
  ASSERT_EQ(system.inertias.size(), 1U);
  EXPECT_FALSE(system.bodies[0].dynamic);
  EXPECT_TRUE(system.bodies[1].dynamic);
  EXPECT_NEAR(system.inertias[0].mass, 4.0f, 1.0e-4f);
  // Constraint: the measured template stores linear damping rounded to 16 bits, so it reads 0.1001 rather than 0.1.
  EXPECT_NEAR(system.motions[0].linear_damping, 0.1f, 2.0e-4f);
  ASSERT_GE(system.bodies[1].shape, 0);
  const auto& compound = system.shapes[static_cast<std::size_t>(system.bodies[1].shape)];
  EXPECT_EQ(compound.kind, HknpShapeKind::compound);
  EXPECT_EQ(compound.instances.size(), 1U);
}

// Detects: a body list, placement or mass the writer accepts instead of refusing.
TEST(HknpEncode, SystemAuthorRejectsBodiesItCannotWrite) {
  EXPECT_THROW(fo4::nif::author_hknp_system({}), std::invalid_argument);

  std::vector<fo4::nif::HknpSystemBody> many(256);
  for (auto& body : many) body.shape = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  EXPECT_THROW(fo4::nif::author_hknp_system(many), std::invalid_argument);

  fo4::nif::HknpSystemBody body;
  body.shape = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  body.mass = 1.0f;
  auto no_mass = body;
  no_mass.shape.mass_properties = {};
  EXPECT_THROW(fo4::nif::author_hknp_system({no_mass}), std::invalid_argument);

  auto zero_volume = body;
  zero_volume.shape.mass_properties.volume = 0.0f;
  EXPECT_THROW(fo4::nif::author_hknp_system({zero_volume}), std::invalid_argument);

  auto zero_inertia = body;
  zero_inertia.mass_properties = body.shape.mass_properties;
  zero_inertia.mass_properties.inertia_raw = {1.0f, 0.0f, 1.0f};
  EXPECT_THROW(fo4::nif::author_hknp_system({zero_inertia}), std::invalid_argument);

  // Constraint: the inverse inertia is 1 / (inertia * density), so a product that underflows to zero must raise instead of storing an infinite inverse.
  auto underflow = body;
  underflow.mass = 1.0e-2f;
  underflow.mass_properties = body.shape.mass_properties;
  underflow.mass_properties.volume = 1.0e38f;
  underflow.mass_properties.inertia_raw = {1.0e-10f, 1.0e-10f, 1.0e-10f};
  EXPECT_THROW(fo4::nif::author_hknp_system({underflow}), std::invalid_argument);

  // Constraint: a denormal mass has an infinite 1 / mass, so it must raise instead of storing that lane.
  auto denormal_mass = body;
  denormal_mass.mass = 1.0e-40f;
  denormal_mass.mass_properties = body.shape.mass_properties;
  denormal_mass.mass_properties.volume = 1.0f;
  denormal_mass.mass_properties.inertia_raw = {1.0e30f, 1.0e30f, 1.0e30f};
  EXPECT_THROW(fo4::nif::author_hknp_system({denormal_mass}), std::invalid_argument);

  // Constraint: a denormal inertia times density has an infinite 1 / (inertia * density), so it must raise.
  auto denormal_scaled = body;
  denormal_scaled.mass = 1.0f;
  denormal_scaled.mass_properties = body.shape.mass_properties;
  denormal_scaled.mass_properties.volume = 3.0e38f;
  denormal_scaled.mass_properties.inertia_raw = {0.5f, 0.5f, 0.5f};
  EXPECT_THROW(fo4::nif::author_hknp_system({denormal_scaled}), std::invalid_argument);

  fo4::nif::HknpSystemBody nan_body;
  nan_body.shape = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  nan_body.position = {0.0f, std::nanf(""), 0.0f};
  EXPECT_THROW(fo4::nif::author_hknp_system({nan_body}), std::invalid_argument);

  auto bad_mass = body;
  bad_mass.mass = -1.0f;
  EXPECT_THROW(fo4::nif::author_hknp_system({bad_mass}), std::invalid_argument);

  fo4::nif::HknpSystemBody nested;
  nested.shape.kind = HknpShapeKind::compound;
  nested.shape.class_name = "hknpDynamicCompoundShape";
  EXPECT_THROW(fo4::nif::author_hknp_system({nested}), std::invalid_argument);
}

// Constraint: a scaled convex object must raise until its +0x30 child fixup and its +0x50 floats are measured, because an unwritten fixup names no child.
TEST(HknpEncode, ScaledConvexAuthoringIsRefused) {
  auto scaled = fo4::nif::make_hknp_box({}, {1.0f, 2.0f, 3.0f}, 7U);
  scaled.kind = HknpShapeKind::scaled_convex;
  scaled.class_name = "hknpScaledConvexShape";
  scaled.scale = {2.0f, 2.0f, 0.5f};
  EXPECT_THROW(fo4::nif::author_hknp_static_body(scaled, {}), std::invalid_argument);

  HknpCompoundChild child;
  child.shape = scaled;
  EXPECT_THROW(fo4::nif::author_hknp_compound_body({child}, {}), std::invalid_argument);
}
