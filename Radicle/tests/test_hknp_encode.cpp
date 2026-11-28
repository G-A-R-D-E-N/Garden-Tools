#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include "hknp_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace fo4::test::hknp;

namespace {

float distance_to_segment(const fo4::nif::HknpVec3& p, const fo4::nif::HknpVec3& a, const fo4::nif::HknpVec3& b) {
  std::array<float, 3> ab{};
  std::array<float, 3> ap{};
  float len_sq = 0.0f;
  float ap_dot_ab = 0.0f;
  for (std::size_t k = 0; k < 3; ++k) {
    ab[k] = b[k] - a[k];
    ap[k] = p[k] - a[k];
    len_sq += ab[k] * ab[k];
    ap_dot_ab += ap[k] * ab[k];
  }
  const float t = len_sq > 0.0f ? std::clamp(ap_dot_ab / len_sq, 0.0f, 1.0f) : 0.0f;
  float sum = 0.0f;
  for (std::size_t k = 0; k < 3; ++k) {
    const float d = ap[k] - t * ab[k];
    sum += d * d;
  }
  return std::sqrt(sum);
}

fo4::nif::HknpSystem decoded_after_rebuild(const fo4::nif::HknpShape& shape) {
  const auto bytes = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, {}));
  return fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(bytes));
}

}  // namespace

TEST(HknpEncode, BoxAuthoredBytesSurviveReadThenWriteAndDecodeToItsMass) {
  const auto shape = fo4::nif::make_hknp_box({1.0f, 2.0f, 3.0f}, {2.0f, 1.0f, 0.5f}, 0x064003D4U);
  const auto bytes = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(shape, {}));
  EXPECT_EQ(fo4::nif::write_hknp_packfile(fo4::nif::read_hknp_packfile(bytes)), bytes);
  const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(bytes));
  ASSERT_EQ(sys.bodies.size(), 1U);
  EXPECT_FALSE(sys.bodies[0].dynamic);
  EXPECT_EQ(sys.bodies[0].packed_filter, 1U);
  ASSERT_EQ(sys.shapes.size(), 1U);
  const auto& decoded = sys.shapes[0];
  EXPECT_EQ(decoded.kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(decoded.vertices.size(), 8U);
  EXPECT_EQ(decoded.faces.size(), 6U);
  EXPECT_EQ(decoded.planes.size(), 8U);
  ASSERT_TRUE(decoded.has_mass_properties);
  EXPECT_FLOAT_EQ(decoded.mass_properties.volume, 8.0f);
  EXPECT_FLOAT_EQ(decoded.mass_properties.mass, 8.0f);
  EXPECT_NEAR(decoded.mass_properties.center[0], 1.0f, 1e-3f);
  EXPECT_NEAR(decoded.mass_properties.center[1], 2.0f, 1e-3f);
  EXPECT_NEAR(decoded.mass_properties.center[2], 3.0f, 1e-3f);
}

TEST(HknpEncode, BoxPlanesWindingAndRadiusAgreeForAnOffCentreBox) {
  const fo4::nif::HknpVec3 center{3.0f, -2.0f, 1.0f};
  const fo4::nif::HknpVec3 half{0.5f, 2.0f, 0.25f};
  const auto box = fo4::nif::make_hknp_box(center, half, 7U);
  ASSERT_EQ(box.faces.size(), 6U);
  ASSERT_EQ(box.planes.size(), 8U);
  EXPECT_FLOAT_EQ(box.convex_radius, 0.01f);
  for (std::size_t f = 0; f < box.faces.size(); ++f) {
    const auto& plane = box.planes[f];
    const auto& loop = box.faces[f];
    for (const auto index : loop) {
      const auto& v = box.vertices[index];
      EXPECT_NEAR(plane[0] * v[0] + plane[1] * v[1] + plane[2] * v[2] + plane[3], 0.0f, 1e-5f);
    }
    const auto& p0 = box.vertices[loop[0]];
    const auto& p1 = box.vertices[loop[1]];
    const auto& p2 = box.vertices[loop[2]];
    const fo4::nif::HknpVec3 e1{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const fo4::nif::HknpVec3 e2{p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
    const fo4::nif::HknpVec3 normal_of_winding{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                               e1[0] * e2[1] - e1[1] * e2[0]};
    EXPECT_GT(normal_of_winding[0] * plane[0] + normal_of_winding[1] * plane[1] + normal_of_winding[2] * plane[2],
              0.0f);
  }
  for (std::size_t k = 0; k < 3; ++k) {
    float high = box.vertices[0][k];
    float low = box.vertices[0][k];
    for (const auto& v : box.vertices) {
      high = std::max(high, v[k]);
      low = std::min(low, v[k]);
    }
    EXPECT_NEAR(high - center[k] + box.convex_radius, half[k], 1e-5f);
    EXPECT_NEAR(center[k] - low + box.convex_radius, half[k], 1e-5f);
  }
  EXPECT_FLOAT_EQ(fo4::nif::make_hknp_box({}, {0.005f, 0.005f, 0.005f}, 1U).convex_radius, 0.0025f);
  EXPECT_THROW(fo4::nif::make_hknp_box({}, {0.0f, 1.0f, 1.0f}, 1U), std::invalid_argument);
  EXPECT_THROW(fo4::nif::make_hknp_box({}, {1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f}, 1U),
               std::invalid_argument);
}

TEST(HknpEncode, SphereCapsuleAndConvexAuthoredValuesDecodeBack) {
  fo4::nif::HknpShape sphere;
  sphere.kind = fo4::nif::HknpShapeKind::sphere;
  sphere.class_name = "hknpSphereShape";
  sphere.radius = 0.5f;
  sphere.point_a = {2.0f, 3.0f, 4.0f};
  const auto sphere_sys = decoded_after_rebuild(sphere);
  ASSERT_EQ(sphere_sys.shapes.size(), 1U);
  EXPECT_EQ(sphere_sys.shapes[0].kind, fo4::nif::HknpShapeKind::sphere);
  EXPECT_EQ(sphere_sys.shapes[0].point_a, (fo4::nif::HknpVec3{2.0f, 3.0f, 4.0f}));
  EXPECT_FLOAT_EQ(sphere_sys.shapes[0].radius, 0.5f);

  fo4::nif::HknpShape capsule;
  capsule.kind = fo4::nif::HknpShapeKind::capsule;
  capsule.class_name = "hknpCapsuleShape";
  capsule.radius = 0.5f;
  capsule.point_a = {0.0f, 0.0f, 0.0f};
  capsule.point_b = {0.0f, 0.0f, 2.0f};
  const auto capsule_sys = decoded_after_rebuild(capsule);
  ASSERT_EQ(capsule_sys.shapes.size(), 1U);
  EXPECT_EQ(capsule_sys.shapes[0].kind, fo4::nif::HknpShapeKind::capsule);
  EXPECT_EQ(capsule_sys.shapes[0].point_a, capsule.point_a);
  EXPECT_EQ(capsule_sys.shapes[0].point_b, capsule.point_b);
  EXPECT_NEAR(capsule_sys.shapes[0].radius, 0.5f, 1e-6f);
  const auto& capsule_object = capsule_sys.packfile.objects[capsule_sys.shapes[0].object];
  const float pad = f32_in_object(capsule_object, 0x14) / 99.0f;
  for (std::size_t i = 0; i < 8; ++i) {
    EXPECT_LE(distance_to_segment(vec3_in_object(capsule_object, 0x70 + 16 * i), capsule.point_a, capsule.point_b),
              2.0f * pad);
  }

  fo4::nif::HknpShape convex;
  convex.kind = fo4::nif::HknpShapeKind::convex;
  convex.class_name = "hknpConvexShape";
  convex.shape_flags = 0x01000143U;
  convex.convex_radius = 0.04f;
  convex.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const auto convex_sys = decoded_after_rebuild(convex);
  ASSERT_EQ(convex_sys.shapes.size(), 1U);
  EXPECT_EQ(convex_sys.shapes[0].kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(convex_sys.shapes[0].vertices, convex.vertices);
  EXPECT_FLOAT_EQ(convex_sys.shapes[0].convex_radius, 0.04f);
}

TEST(HknpEncode, AuthorRejectsCompoundKindAndNonFiniteGeometry) {
  fo4::nif::HknpShape compound;
  compound.kind = fo4::nif::HknpShapeKind::compound;
  compound.class_name = "hknpStaticCompoundShape";
  EXPECT_THROW(fo4::nif::author_hknp_static_body(compound, {}), std::invalid_argument);

  auto box = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  box.vertices[2][1] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::author_hknp_static_body(box, {}), std::invalid_argument);

  fo4::nif::HknpShape sphere;
  sphere.kind = fo4::nif::HknpShapeKind::sphere;
  sphere.class_name = "hknpSphereShape";
  EXPECT_THROW(fo4::nif::author_hknp_static_body(sphere, {}), std::invalid_argument);
}

TEST(HknpEncode, AuthorAcceptsNonFiniteBytesOnlyInThePlanePaddingSlots) {
  // Detects: vanilla polytopes keep uninitialised bytes in the slots that round the plane list up to four.
  auto box = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  box.planes.resize(box.faces.size() + 2);
  box.planes.back()[0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_NO_THROW(fo4::nif::author_hknp_static_body(box, {}));

  box.planes.front()[0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::author_hknp_static_body(box, {}), std::invalid_argument);
}

TEST(HknpEncode, ShapeObjectsCarryTheMeasuredLayoutBytes) {
  fo4::nif::HknpShape sphere;
  sphere.kind = fo4::nif::HknpShapeKind::sphere;
  sphere.class_name = "hknpSphereShape";
  sphere.radius = 0.5f;
  sphere.point_a = {2.0f, 3.0f, 4.0f};
  const auto sphere_pack = fo4::nif::author_hknp_static_body(sphere, {});
  ASSERT_EQ(sphere_pack.objects[0].bytes.size(), 0x140U);
  EXPECT_EQ(u32_in_object(sphere_pack.objects[0], 0xe4), 1U);
  const auto& sphere_object = sphere_pack.objects[1];
  ASSERT_EQ(sphere_object.bytes.size(), 0x80U);
  EXPECT_EQ(u32_in_object(sphere_object, 0x10), 0x01000111U);
  EXPECT_EQ(u32_in_object(sphere_object, 0x30), 0x00100004U);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(vec3_in_object(sphere_object, 0x40 + 16 * i), sphere.point_a);
    EXPECT_EQ(u32_in_object(sphere_object, 0x4c + 16 * i), 0x3f000000U);
  }

  fo4::nif::HknpShape capsule;
  capsule.kind = fo4::nif::HknpShapeKind::capsule;
  capsule.class_name = "hknpCapsuleShape";
  capsule.radius = 0.5f;
  capsule.point_a = {0.0f, 0.0f, 0.0f};
  capsule.point_b = {0.0f, 0.0f, 2.0f};
  const auto capsule_pack = fo4::nif::author_hknp_static_body(capsule, {});
  const auto& capsule_object = capsule_pack.objects[1];
  ASSERT_EQ(capsule_object.bytes.size(), 0x1b0U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x10), 0x010001c3U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x15c), 0xff7fffeeU);
  EXPECT_EQ(u32_in_object(capsule_object, 0x16c), 0xff7fffeeU);
  EXPECT_EQ(u32_in_object(capsule_object, 0x174), 0x04040004U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x190), 0x03020607U);
  EXPECT_EQ(u32_in_object(capsule_object, 0x1a5), 0x00000406U);

  const auto box = fo4::nif::make_hknp_box({}, {1.0f, 1.0f, 1.0f}, 1U);
  const auto box_pack = fo4::nif::author_hknp_static_body(box, {});
  const auto& box_object = box_pack.objects[1];
  ASSERT_EQ(box_object.bytes.size(), 0x190U);
  EXPECT_EQ(u32_in_object(box_object, 0x30), 0x00200008U);
  EXPECT_EQ(u32_in_object(box_object, 0x40), 0x00900008U);
  EXPECT_EQ(u32_in_object(box_object, 0x44), 0x010c0006U);
  EXPECT_EQ(u32_in_object(box_object, 0x48), 0x01280018U);
  EXPECT_EQ(u32_in_object(box_pack.objects[2], 24), 0xF100U);
}
