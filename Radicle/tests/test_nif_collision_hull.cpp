#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_hull.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::nif::HknpShape;
using fo4::nif::HknpTriangle;
using fo4::nif::HknpVec3;
using fo4::nif::HknpVec4;

constexpr float k_pi = 3.14159265358979323846f;

std::vector<HknpVec3> box_points(const HknpVec3& center, const HknpVec3& half) {
  std::vector<HknpVec3> points;
  for (int i = 0; i < 8; ++i) {
    points.push_back({center[0] + ((i & 1) != 0 ? half[0] : -half[0]),
                      center[1] + ((i & 2) != 0 ? half[1] : -half[1]),
                      center[2] + ((i & 4) != 0 ? half[2] : -half[2])});
  }
  return points;
}

std::vector<HknpTriangle> box_triangles() {
  constexpr std::array<std::array<std::uint32_t, 4>, 6> quads{{{1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3},
                                                              {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 2, 3, 1}}};
  std::vector<HknpTriangle> triangles;
  for (const auto& quad : quads) {
    triangles.push_back({quad[0], quad[1], quad[2]});
    triangles.push_back({quad[0], quad[2], quad[3]});
  }
  return triangles;
}

std::vector<HknpVec3> pyramid_points() {
  return {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}, {1, 1, 3}, {1, 1, 0.5f}};
}

std::vector<HknpTriangle> pyramid_triangles() {
  return {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}, {0, 2, 1}, {0, 3, 2}, {0, 1, 5}};
}

// Constraint: a 24-sided prism's sides bend by 360/24 degrees, so its face angles have a known value.
std::vector<HknpVec3> prism_points(int sides, float radius, float half_height) {
  std::vector<HknpVec3> points;
  for (int ring = 0; ring < 2; ++ring) {
    const float z = ring == 0 ? -half_height : half_height;
    for (int i = 0; i < sides; ++i) {
      const float angle = 2.0f * k_pi * static_cast<float>(i) / static_cast<float>(sides);
      points.push_back({radius * std::cos(angle), radius * std::sin(angle), z});
    }
  }
  return points;
}

std::vector<HknpTriangle> prism_triangles(int sides) {
  std::vector<HknpTriangle> triangles;
  for (int i = 0; i < sides; ++i) {
    const auto bottom = static_cast<std::uint32_t>(i);
    const auto next = static_cast<std::uint32_t>((i + 1) % sides);
    const auto top = static_cast<std::uint32_t>(sides + i);
    const auto top_next = static_cast<std::uint32_t>(sides + (i + 1) % sides);
    triangles.push_back({bottom, next, top_next});
    triangles.push_back({bottom, top_next, top});
    // Constraint: a cap fan stops before the last side, because its next ring index would leave the point list.
    if (i >= 2 && i + 1 < sides) {
      triangles.push_back({0, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 1)});
      triangles.push_back({static_cast<std::uint32_t>(sides),
                           static_cast<std::uint32_t>(sides + i + 1),
                           static_cast<std::uint32_t>(sides + i)});
    }
  }
  return triangles;
}

float plane_distance(const HknpVec4& plane, const HknpVec3& point) {
  return plane[0] * point[0] + plane[1] * point[1] + plane[2] * point[2] + plane[3];
}

HknpVec3 loop_normal(const HknpShape& shape, const std::vector<std::uint8_t>& loop) {
  HknpVec3 normal{0.0f, 0.0f, 0.0f};
  for (std::size_t i = 0; i < loop.size(); ++i) {
    const HknpVec3& a = shape.vertices[loop[i]];
    const HknpVec3& b = shape.vertices[loop[(i + 1) % loop.size()]];
    normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
    normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
    normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
  }
  return normal;
}

}  // namespace

// Detects: a drift in the hull integration or its 1.5 times inertia factor against the measured box values.
TEST(HknpHull, BoxTrianglesReproduceTheMeasuredBoxMass) {
  const HknpVec3 center{0.5f, -1.0f, 2.0f};
  const HknpVec3 half{1.0f, 2.0f, 3.0f};
  const auto hull = fo4::nif::make_hknp_hull(box_points(center, half), box_triangles(), 0x064003D4U);
  const auto box = fo4::nif::make_hknp_box(center, half, 0x064003D4U);

  EXPECT_EQ(hull.kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(hull.class_name, "hknpConvexPolytopeShape");
  EXPECT_EQ(hull.vertices.size(), 8U);
  EXPECT_EQ(hull.faces.size(), 6U);
  EXPECT_EQ(hull.planes.size(), 8U);
  EXPECT_TRUE(hull.has_mass_properties);

  const float volume = box.mass_properties.volume;
  EXPECT_NEAR(hull.mass_properties.volume, volume, 1.0e-4f * volume);
  EXPECT_FLOAT_EQ(hull.mass_properties.mass, hull.mass_properties.volume);
  EXPECT_EQ(hull.mass_properties.packed_orientation, box.mass_properties.packed_orientation);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    EXPECT_NEAR(hull.mass_properties.center[axis], box.mass_properties.center[axis], 1.0e-4f);
    EXPECT_NEAR(hull.mass_properties.inertia_raw[axis], box.mass_properties.inertia_raw[axis],
                1.0e-4f * box.mass_properties.inertia_raw[axis]);
  }
}

// Detects: a hull point outside a face plane, a loop wound inward, or an interior point kept as a vertex.
TEST(HknpHull, PointsStayInsideAndEveryFaceWindsOutward) {
  const auto points = pyramid_points();
  const auto hull = fo4::nif::make_hknp_hull(points, pyramid_triangles(), 0x064003D4U);

  EXPECT_EQ(hull.faces.size(), 5U);
  ASSERT_EQ(hull.planes.size(), 8U);
  ASSERT_EQ(hull.face_min_half_angles.size(), hull.faces.size());

  for (const auto& point : points) {
    for (std::size_t f = 0; f < hull.faces.size(); ++f) {
      EXPECT_LE(plane_distance(hull.planes[f], point), 1.0e-3f);
    }
  }
  for (std::size_t f = 0; f < hull.faces.size(); ++f) {
    const auto& plane = hull.planes[f];
    const float length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
    EXPECT_NEAR(length, 1.0f, 1.0e-4f);
    EXPECT_NEAR(plane_distance(plane, hull.vertices[hull.faces[f].front()]), 0.0f, 1.0e-4f);
    const HknpVec3 normal = loop_normal(hull, hull.faces[f]);
    EXPECT_GT(normal[0] * plane[0] + normal[1] * plane[1] + normal[2] * plane[2], 0.0f);
  }
  for (const auto& vertex : hull.vertices) {
    const bool interior = std::fabs(vertex[0] - 1.0f) < 1.0e-4f && std::fabs(vertex[1] - 1.0f) < 1.0e-4f &&
                          std::fabs(vertex[2] - 0.5f) < 1.0e-4f;
    EXPECT_FALSE(interior);
  }
}

// Detects: a hull whose vertex order, loops, planes or mass drift across the packfile round trip.
TEST(HknpHull, AuthoredHullDecodesBackToItsPlanesAndFaces) {
  const auto hull = fo4::nif::make_hknp_hull(pyramid_points(), pyramid_triangles(), 0x064003D4U);
  const auto bytes = fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_static_body(hull, {}));
  const auto system = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(bytes));

  ASSERT_EQ(system.shapes.size(), 1U);
  const auto& decoded = system.shapes[0];
  EXPECT_EQ(decoded.kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(decoded.class_name, "hknpConvexPolytopeShape");
  EXPECT_TRUE(decoded.has_mass_properties);
  ASSERT_EQ(decoded.vertices.size(), hull.vertices.size());
  ASSERT_EQ(decoded.planes.size(), hull.planes.size());
  ASSERT_EQ(decoded.faces.size(), hull.faces.size());
  // Constraint: the stored counts are padded to four, which no measured vanilla hull leaves unpadded.
  EXPECT_EQ(decoded.vertices.size() % 4U, 0U);
  EXPECT_EQ(decoded.planes.size() % 4U, 0U);

  for (std::size_t v = 0; v < hull.vertices.size(); ++v) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      EXPECT_NEAR(decoded.vertices[v][axis], hull.vertices[v][axis], 2.0e-3f);
    }
  }
  for (std::size_t p = 0; p < hull.planes.size(); ++p) {
    for (std::size_t lane = 0; lane < 4; ++lane) {
      EXPECT_NEAR(decoded.planes[p][lane], hull.planes[p][lane], 1.0e-4f);
    }
  }
  for (std::size_t f = 0; f < hull.faces.size(); ++f) {
    EXPECT_EQ(decoded.faces[f], hull.faces[f]);
    EXPECT_EQ(decoded.face_min_half_angles[f], hull.face_min_half_angles[f]);
  }
  EXPECT_NEAR(decoded.mass_properties.volume, hull.mass_properties.volume, 1.0e-4f * hull.mass_properties.volume);
  EXPECT_NEAR(decoded.mass_properties.mass, hull.mass_properties.mass, 1.0e-4f * hull.mass_properties.mass);
}

// Detects: a hull whose counts are not padded to four, or whose padded vertex slots do not repeat the last vertex.
TEST(HknpHull, PaddedCountsMatchTheMeasuredVanillaLayout) {
  const auto hull = fo4::nif::make_hknp_hull(pyramid_points(), pyramid_triangles(), 0x064003D4U);
  ASSERT_EQ(hull.vertices.size(), 8U);
  ASSERT_EQ(hull.planes.size(), 8U);
  const HknpVec3 last_used = hull.vertices[4];
  for (std::size_t v = 5; v < hull.vertices.size(); ++v) {
    EXPECT_EQ(hull.vertices[v], last_used);
  }
  EXPECT_EQ(hull.planes[5], (HknpVec4{0.0f, 0.0f, 1.0f, 0.0f}));
  EXPECT_EQ(hull.planes[6], (HknpVec4{0.0f, 0.0f, 0.0f, 0.0f}));
  EXPECT_EQ(hull.planes[7], (HknpVec4{0.0f, 0.0f, 0.0f, 0.0f}));
}

// Detects: a thin sheet or a fine small sphere rejected as degenerate.
TEST(HknpHull, ThinFacesAndASmallSphereStillBuild) {
  const HknpVec3 half{0.05f, 0.05f, 0.00005f};
  const auto sheet = fo4::nif::make_hknp_hull(box_points({0.0f, 0.0f, 0.0f}, half), box_triangles(), 0);
  EXPECT_EQ(sheet.faces.size(), 6U);
  EXPECT_NEAR(sheet.mass_properties.volume, 8.0f * half[0] * half[1] * half[2], 1.0e-3f * 8.0f * half[0] * half[1] * half[2]);

  constexpr int rings = 10;
  constexpr int segments = 20;
  constexpr float radius = 0.005f;
  std::vector<HknpVec3> sphere;
  for (int ring = 1; ring < rings; ++ring) {
    const float polar = k_pi * static_cast<float>(ring) / static_cast<float>(rings);
    for (int segment = 0; segment < segments; ++segment) {
      const float azimuth = 2.0f * k_pi * static_cast<float>(segment) / static_cast<float>(segments);
      sphere.push_back({radius * std::sin(polar) * std::cos(azimuth), radius * std::sin(polar) * std::sin(azimuth),
                        radius * std::cos(polar)});
    }
  }
  sphere.push_back({0.0f, 0.0f, radius});
  sphere.push_back({0.0f, 0.0f, -radius});
  std::vector<HknpTriangle> sphere_triangles;
  for (std::size_t i = 0; i < sphere.size(); ++i) {
    sphere_triangles.push_back({static_cast<std::uint32_t>(i),
                                static_cast<std::uint32_t>((i + 1) % sphere.size()),
                                static_cast<std::uint32_t>((i + 2) % sphere.size())});
  }
  const auto ball = fo4::nif::make_hknp_hull(sphere, sphere_triangles, 0);
  EXPECT_GE(ball.vertices.size(), sphere.size());
  EXPECT_LE(ball.vertices.size(), 252U);
  EXPECT_GT(ball.mass_properties.volume, 0.0f);
  const double ideal = 4.0 / 3.0 * 3.14159265358979323846 * radius * radius * radius;
  EXPECT_NEAR(ball.mass_properties.volume, ideal, 0.15 * ideal);
}

// Detects: a small box five metres out losing its analytic inertia to single precision.
TEST(HknpHull, ADistantSmallBoxKeepsItsAnalyticInertia) {
  const HknpVec3 center{5.0f, -4.0f, 3.0f};
  const HknpVec3 half{0.025f, 0.015f, 0.01f};
  const auto hull = fo4::nif::make_hknp_hull(box_points(center, half), box_triangles(), 0);
  const double volume = 8.0 * static_cast<double>(half[0]) * half[1] * half[2];
  EXPECT_NEAR(hull.mass_properties.volume, volume, 1.0e-3 * volume);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    EXPECT_NEAR(hull.mass_properties.center[axis], center[axis], 1.0e-4f);
  }
  const double analytic[3] = {volume * (static_cast<double>(half[1]) * half[1] + static_cast<double>(half[2]) * half[2]) / 2.0,
                              volume * (static_cast<double>(half[0]) * half[0] + static_cast<double>(half[2]) * half[2]) / 2.0,
                              volume * (static_cast<double>(half[0]) * half[0] + static_cast<double>(half[1]) * half[1]) / 2.0};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    EXPECT_GT(hull.mass_properties.inertia_raw[axis], 0.0f);
    EXPECT_NEAR(hull.mass_properties.inertia_raw[axis], analytic[axis], 1.0e-3 * analytic[axis]);
  }
}

// Detects: a face angle stored as the box's 128 instead of the measured bend.
TEST(HknpHull, FaceAnglesFollowTheMeasuredBend) {
  const auto box = fo4::nif::make_hknp_hull(box_points({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}), box_triangles(), 0);
  for (const auto angle : box.face_min_half_angles) {
    EXPECT_NEAR(static_cast<int>(angle), 128, 1);
  }

  constexpr int sides = 24;
  const auto prism =
      fo4::nif::make_hknp_hull(prism_points(sides, 1.0f, 1.0f), prism_triangles(sides), 0);
  ASSERT_EQ(prism.faces.size(), static_cast<std::size_t>(sides + 2));
  for (std::size_t f = 0; f < prism.faces.size(); ++f) {
    if (prism.faces[f].size() == 4) {
      EXPECT_NEAR(static_cast<int>(prism.face_min_half_angles[f]), 21, 1);
    } else {
      EXPECT_NEAR(static_cast<int>(prism.face_min_half_angles[f]), 128, 1);
    }
  }
}

// Detects: loop starts that follow hash order, so the authored bytes change between runs.
TEST(HknpHull, LoopsStartAtTheirLowestVertexIndex) {
  const auto hull = fo4::nif::make_hknp_hull(box_points({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}), box_triangles(), 0);
  for (const auto& loop : hull.faces) {
    EXPECT_EQ(loop.front(), *std::min_element(loop.begin(), loop.end()));
  }
}

// Detects: a flat or degenerate input producing a hull, or a hull over 252 vertices reaching the encoder.
TEST(HknpHull, RejectsInputThatFormsNoSolid) {
  const auto square = std::vector<HknpVec3>{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  const auto point = std::vector<HknpVec3>{{0, 0, 0}};
  const auto line = std::vector<HknpVec3>{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};

  EXPECT_THROW(fo4::nif::make_hknp_hull(point, {}, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::make_hknp_hull(point, {{0, 1, 0}}, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::make_hknp_hull(point, {{0, 1, 1}}, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::make_hknp_hull(square, {{0, 1, 2}, {0, 2, 3}}, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::make_hknp_hull(line, {{0, 1, 2}, {1, 2, 3}}, 0), std::invalid_argument);

  auto not_a_number = pyramid_points();
  not_a_number[4][2] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::make_hknp_hull(not_a_number, pyramid_triangles(), 0), std::invalid_argument);

  auto infinite = pyramid_points();
  infinite[0][0] = std::numeric_limits<float>::infinity();
  EXPECT_THROW(fo4::nif::make_hknp_hull(infinite, pyramid_triangles(), 0), std::invalid_argument);

  std::vector<HknpVec3> sphere;
  constexpr int rings = 12;
  constexpr int segments = 24;
  for (int ring = 1; ring < rings; ++ring) {
    const float polar = k_pi * static_cast<float>(ring) / static_cast<float>(rings);
    for (int segment = 0; segment < segments; ++segment) {
      const float azimuth = 2.0f * k_pi * static_cast<float>(segment) / static_cast<float>(segments);
      sphere.push_back({std::sin(polar) * std::cos(azimuth), std::sin(polar) * std::sin(azimuth),
                        std::cos(polar)});
    }
  }
  sphere.push_back({0.0f, 0.0f, 1.0f});
  sphere.push_back({0.0f, 0.0f, -1.0f});
  std::vector<HknpTriangle> sphere_triangles;
  for (std::size_t i = 0; i < sphere.size(); ++i) {
    sphere_triangles.push_back({static_cast<std::uint32_t>(i),
                                static_cast<std::uint32_t>((i + 1) % sphere.size()),
                                static_cast<std::uint32_t>((i + 2) % sphere.size())});
  }
  ASSERT_GT(sphere.size(), 252U);
  // Constraint: a size refusal carries its own type, because an import that cannot tell it from a flat mesh would offer a mass for a body it then refuses to write.
  EXPECT_THROW(fo4::nif::make_hknp_hull(sphere, sphere_triangles, 0), fo4::nif::hull_limit_error);

  // Constraint: the work limit must fire before the 252-vertex refusal, and its message names the limit and the box choice it leaves available.
  std::vector<HknpVec3> expensive;
  std::vector<HknpTriangle> expensive_triangles;
  constexpr std::uint32_t k_expensive = 8000;
  for (std::uint32_t i = 0; i < k_expensive; ++i) {
    const float z = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(k_expensive);
    const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float angle = static_cast<float>(i) * 2.39996323f;
    expensive.push_back({radius * std::cos(angle), radius * std::sin(angle), z});
  }
  for (std::uint32_t i = 1; i + 1 < k_expensive; ++i) expensive_triangles.push_back({i - 1, i, i + 1});
  try {
    static_cast<void>(fo4::nif::make_hknp_hull(expensive, expensive_triangles, 0));
    ADD_FAILURE() << "a hull past the work limit must raise";
  } catch (const fo4::nif::hull_limit_error& error) {
    EXPECT_NE(std::string(error.what()).find("300000000"), std::string::npos) << error.what();
    EXPECT_NE(std::string(error.what()).find("box collision choice still works"), std::string::npos) << error.what();
  } catch (const std::exception& error) {
    ADD_FAILURE() << "expected the work limit, got: " << error.what();
  }

  // Invariant: the limit no longer counts points, so a long vertex list whose triangles use six of them still builds.
  auto padded = pyramid_points();
  padded.insert(padded.end(), expensive.begin(), expensive.end());
  EXPECT_NO_THROW(fo4::nif::make_hknp_hull(padded, pyramid_triangles(), 0));
}

// Regression: a dense point list whose hull is a cheap tetrahedron must build, because the removed blanket cap refused meshes the work limit now accepts.
TEST(HknpHull, BuildsACheapHullFromADensePointList) {
  std::vector<HknpVec3> points{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 0, 2}};
  std::vector<HknpTriangle> triangles{{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
  const HknpVec3 inside{0.5f, 0.5f, 0.5f};
  for (int i = 0; i < 10000; ++i) {
    points.push_back(inside);
    triangles.push_back({static_cast<std::uint32_t>(points.size() - 1), 0, 1});
  }
  try {
    const auto hull = fo4::nif::make_hknp_hull(points, triangles, 0);
    EXPECT_EQ(hull.vertices.size(), 4U);
  } catch (const std::exception& error) {
    ADD_FAILURE() << error.what();
  }
}

namespace {

constexpr std::string_view kCubeObj =
    "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
    "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 2 3 7\nf 2 7 6\nf 3 4 8\nf 3 8 7\nf 4 1 5\nf 4 5 8\n";
constexpr std::string_view kFlatObj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3\nf 1 3 4\n";

}  // namespace

// Detects: a hull import that writes no body, drops the mass from the physics system, or writes a mass other than the one requested.
TEST(NifImportHull, HullChoiceWritesOneDynamicConvexBodyAtItsMass) {
  fo4::nif::ImportOptions options;
  options.hull_collision = true;
  options.body_mass = 12.0f;
  const auto bytes = fo4::nif::import_obj(kCubeObj, options);
  const auto blobs = fo4::nif::read_collision_blobs(fo4::nif::read_nif(bytes));
  ASSERT_EQ(blobs.size(), 1U);
  const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blobs[0].bytes));
  ASSERT_EQ(sys.bodies.size(), 1U);
  EXPECT_TRUE(sys.bodies[0].dynamic);
  EXPECT_EQ(sys.shapes[static_cast<std::size_t>(sys.bodies[0].shape)].kind, fo4::nif::HknpShapeKind::convex);
  ASSERT_EQ(sys.inertias.size(), 1U);
  EXPECT_NEAR(1.0f / sys.inertias[0].inverse_mass, 12.0f, 1.0e-3f);
}

// Detects: a body mass accepted with no collision choice, or the box and hull choices accepted together.
TEST(NifImportHull, MassNeedsACollisionChoiceAndTheChoicesAreExclusive) {
  fo4::nif::ImportOptions options;
  options.body_mass = 5.0f;
  EXPECT_THROW(fo4::nif::import_obj(kCubeObj, options), std::invalid_argument);
  options.hull_collision = true;
  options.box_collision = true;
  EXPECT_THROW(fo4::nif::import_obj(kCubeObj, options), std::invalid_argument);
}

// Detects: a flat OBJ, which forms no solid, imported with the hull choice as a body instead of a refusal.
TEST(NifImportHull, FlatMeshIsRefusedByTheHullChoice) {
  fo4::nif::ImportOptions options;
  options.hull_collision = true;
  EXPECT_THROW(fo4::nif::import_obj(kFlatObj, options), std::invalid_argument);
}

namespace {

std::vector<fo4::nif::HknpVec3> rotated_about_z(std::vector<fo4::nif::HknpVec3> points, float angle) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  for (auto& p : points) {
    const float x = p[0];
    const float y = p[1];
    p[0] = c * x - s * y;
    p[1] = s * x + c * y;
  }
  return points;
}

fo4::nif::HknpVec3 rotated_by(const fo4::nif::HknpVec4& q, const fo4::nif::HknpVec3& v) {
  const float x = q[0];
  const float y = q[1];
  const float z = q[2];
  const float w = q[3];
  const fo4::nif::HknpVec3 t{2.0f * (y * v[2] - z * v[1]), 2.0f * (z * v[0] - x * v[2]), 2.0f * (x * v[1] - y * v[0])};
  return {v[0] + w * t[0] + (y * t[2] - z * t[1]), v[1] + w * t[1] + (z * t[0] - x * t[2]),
          v[2] + w * t[2] + (x * t[1] - y * t[0])};
}

constexpr float k_quarter_turn_sixth = 3.14159265358979f / 6.0f;

}  // namespace

// Detects: a hull whose stored moments depend on how the box is turned, so a rotated long box would store different moments than the same box unrotated.
TEST(HknpHull, RotatedLongBoxStoresTheSameSortedMomentsAsTheUnrotatedBox) {
  const fo4::nif::HknpVec3 center{0.0f, 0.0f, 0.0f};
  const fo4::nif::HknpVec3 half{1.0f, 0.1f, 0.1f};
  const auto plain = fo4::nif::make_hknp_hull(box_points(center, half), box_triangles(), 0);
  const auto turned = fo4::nif::make_hknp_hull(rotated_about_z(box_points(center, half), k_quarter_turn_sixth),
                                               box_triangles(), 0);
  auto a = plain.mass_properties.inertia_raw;
  auto b = turned.mass_properties.inertia_raw;
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  for (std::size_t k = 0; k < 3; ++k) EXPECT_NEAR(b[k], a[k], 1.0e-4f * a[2]);
}

// Detects: a frame that does not take the stored long axis onto the rotated box's long axis, so a dynamic body's inertia would sit on the wrong axes.
TEST(HknpHull, FrameTakesTheStoredLongAxisOntoTheRotatedLongAxis) {
  fo4::nif::HknpVec4 frame{};
  const auto hull = fo4::nif::make_hknp_hull(
      rotated_about_z(box_points({0.0f, 0.0f, 0.0f}, {1.0f, 0.1f, 0.1f}), k_quarter_turn_sixth), box_triangles(), 0,
      &frame);
  static_cast<void>(hull);
  const auto axis = rotated_by(frame, {1.0f, 0.0f, 0.0f});
  EXPECT_NEAR(axis[0], std::cos(k_quarter_turn_sixth), 1.0e-4f);
  EXPECT_NEAR(axis[1], std::sin(k_quarter_turn_sixth), 1.0e-4f);
  EXPECT_NEAR(axis[2], 0.0f, 1.0e-4f);
}

// Detects: an axis-aligned hull whose frame is not the identity, which would turn a plain box's dynamic inertia for no reason.
TEST(HknpHull, AxisAlignedHullKeepsTheIdentityFrame) {
  fo4::nif::HknpVec4 frame{};
  static_cast<void>(fo4::nif::make_hknp_hull(box_points({0.0f, 0.0f, 0.0f}, {1.0f, 0.1f, 0.1f}), box_triangles(), 0,
                                             &frame));
  EXPECT_NEAR(frame[0], 0.0f, 1.0e-6f);
  EXPECT_NEAR(frame[1], 0.0f, 1.0e-6f);
  EXPECT_NEAR(frame[2], 0.0f, 1.0e-6f);
  EXPECT_NEAR(frame[3], 1.0f, 1.0e-6f);
}

namespace {

constexpr std::string_view kMeshCubeObj =
    "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
    "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 2 3 7\nf 2 7 6\nf 3 4 8\nf 3 8 7\nf 4 1 5\nf 4 5 8\n";

}  // namespace

// Detects: a mesh-collision import that writes a box or hull instead of the mesh, drops the triangles, or accepts a mass or a second collision choice.
TEST(NifImportHull, MeshChoiceWritesACompressedMeshBodyAndRefusesMassAndSecondChoice) {
  fo4::nif::ImportOptions options;
  options.mesh_collision = true;
  const auto bytes = fo4::nif::import_obj(kMeshCubeObj, options);
  const auto blobs = fo4::nif::read_collision_blobs(fo4::nif::read_nif(bytes));
  ASSERT_EQ(blobs.size(), 1U);
  const auto sys = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blobs[0].bytes));
  ASSERT_EQ(sys.bodies.size(), 1U);
  EXPECT_FALSE(sys.bodies[0].dynamic);
  const auto& shape = sys.shapes[static_cast<std::size_t>(sys.bodies[0].shape)];
  EXPECT_EQ(shape.kind, fo4::nif::HknpShapeKind::compressed_mesh);
  EXPECT_EQ(shape.mesh_triangles.size(), 12U);
  options.body_mass = 3.0f;
  EXPECT_THROW(fo4::nif::import_obj(kMeshCubeObj, options), std::invalid_argument);
  options.body_mass = 0.0f;
  options.box_collision = true;
  EXPECT_THROW(fo4::nif::import_obj(kMeshCubeObj, options), std::invalid_argument);
}

// Detects: a near-coplanar neighbour splitting the visible region, so valid point clouds are refused as not converging.
TEST(HknpHull, RandomPointsOnASphereAreAcceptedAtEveryScale) {
  for (const float radius : {0.05f, 0.3f, 1.0f, 3.0f}) {
    std::mt19937 random(static_cast<std::uint32_t>(radius * 1000.0f));
    std::normal_distribution<float> normal(0.0f, 1.0f);
    for (int round = 0; round < 20; ++round) {
      std::vector<HknpVec3> points;
      std::vector<HknpTriangle> triangles;
      for (std::uint32_t i = 0; i < 99; i += 3) {
        for (int corner = 0; corner < 3; ++corner) {
          const float x = normal(random);
          const float y = normal(random);
          const float z = normal(random);
          const float scale = radius / std::sqrt(x * x + y * y + z * z);
          points.push_back(HknpVec3{x * scale, y * scale, z * scale});
        }
        triangles.push_back(HknpTriangle{i, i + 1, i + 2});
      }
      EXPECT_NO_THROW(fo4::nif::make_hknp_hull(points, triangles, 0x064003D4U)) << radius << " round " << round;
    }
  }
}
