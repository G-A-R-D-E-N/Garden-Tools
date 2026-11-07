#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "fo4/nif/collision_build.hpp"
#include "fo4/nif/hknp_encode.hpp"

namespace {

constexpr float k_scale = fo4::nif::k_hknp_game_units_per_metre;
constexpr float k_half = 0.5f;

std::vector<float> cube_vertices() {
  std::vector<float> out;
  for (const float x : {-k_half, k_half}) {
    for (const float y : {-k_half, k_half}) {
      for (const float z : {-k_half, k_half}) out.insert(out.end(), {x, y, z});
    }
  }
  return out;
}

std::vector<std::uint16_t> cube_triangles() {
  return {0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6,
          0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
}

}  // namespace

TEST(CollisionBuild, BuildsAnExactConvexHullFromACube) {
  // Constraint: captures the failure where the encoder grows the hull by a stored radius.
  const auto hull = fo4::nif::build_convex_hull_from_mesh(cube_vertices(), cube_triangles(), 0x1234U);
  EXPECT_EQ(hull.shape.kind, fo4::nif::HknpShapeKind::convex);
  EXPECT_EQ(hull.shape.class_name, "hknpConvexShape");
  EXPECT_EQ(hull.shape.shape_flags, fo4::nif::k_convex_shape_flags);
  EXPECT_EQ(hull.shape.convex_radius, 0.0f);
  EXPECT_TRUE(hull.shape.faces.empty());
  EXPECT_TRUE(hull.shape.planes.empty());
  EXPECT_EQ(hull.shape.vertices.size(), 8U);
  EXPECT_EQ(hull.distinct_positions, 8U);
  EXPECT_EQ(hull.face_indices.size(), 36U);
  EXPECT_EQ(hull.triangles_considered, 12U);
  EXPECT_NEAR(hull.nearest_face_distance_metres, k_half / k_scale, 1e-7);
  EXPECT_NEAR(hull.volume_metres3, std::pow(1.0f / k_scale, 3.0f), 1e-9);
  EXPECT_EQ(hull.shape.material_crc, 0x1234U);
  for (const auto& vertex : hull.shape.vertices) {
    for (const float axis : vertex) EXPECT_NEAR(std::fabs(axis), k_half / k_scale, 1e-7);
  }
  for (const auto index : hull.face_indices) {
    EXPECT_LT(static_cast<std::size_t>(index), hull.shape.vertices.size());
  }
  EXPECT_EQ(hull.face_indices[0], 0U);
  EXPECT_EQ(hull.face_indices[1], 1U);
  EXPECT_EQ(hull.face_indices[2], 3U);
}

TEST(CollisionBuild, RefusesARotatedFlatQuadWhoseAabbIsNotThin) {
  // Constraint: catches flatness measured on an AABB instead of the hull's own face planes.
  const std::vector<float> verts{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f};
  const std::vector<std::uint16_t> tris{0, 1, 2, 0, 2, 3};
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, tris, 0), std::invalid_argument);
}

TEST(CollisionBuild, BuildsATetrahedronWithItsCentroidFaceDistanceInMetres) {
  // Constraint: pins the metre conversion of the informational distance.
  const std::vector<float> verts{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
  const std::vector<std::uint16_t> tris{0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
  const auto hull = fo4::nif::build_convex_hull_from_mesh(verts, tris, 0);
  EXPECT_EQ(hull.shape.vertices.size(), 4U);
  EXPECT_EQ(hull.shape.convex_radius, 0.0f);
  EXPECT_EQ(hull.face_indices.size(), 12U);
  EXPECT_NEAR(hull.nearest_face_distance_metres, 0.14433756 / k_scale, 1e-7);
}

TEST(CollisionBuild, KeepsANonZeroOriginHullPositioned) {
  // Constraint: a mesh far from the origin must produce hull vertices at that offset divided by the unit scale.
  const std::vector<float> offset{100.0f, 200.0f, 300.0f};
  auto verts = cube_vertices();
  for (std::size_t i = 0; i + 2 < verts.size(); i += 3) {
    verts[i] += offset[0];
    verts[i + 1] += offset[1];
    verts[i + 2] += offset[2];
  }
  const auto hull = fo4::nif::build_convex_hull_from_mesh(verts, cube_triangles(), 0);
  EXPECT_EQ(hull.shape.vertices.size(), 8U);
  EXPECT_NEAR(hull.nearest_face_distance_metres, k_half / k_scale, 1e-7);
  for (const auto& vertex : hull.shape.vertices) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      EXPECT_NEAR(std::fabs(vertex[axis] - offset[axis] / k_scale), k_half / k_scale, 1e-5);
    }
  }
}

TEST(CollisionBuild, DeduplicatesSplitRenderVerticesBeforeTheVertexCap) {
  // Constraint: forty cube copies are 320 raw vertices but eight distinct positions, so dedupe must precede the cap.
  const auto base = cube_vertices();
  const auto base_tris = cube_triangles();
  std::vector<float> verts;
  std::vector<std::uint16_t> tris;
  for (std::size_t copy = 0; copy < 40; ++copy) {
    verts.insert(verts.end(), base.begin(), base.end());
    for (const auto index : base_tris) {
      tris.push_back(static_cast<std::uint16_t>(index + 8 * copy));
    }
  }
  const auto hull = fo4::nif::build_convex_hull_from_mesh(verts, tris, 0);
  EXPECT_EQ(hull.distinct_positions, 8U);
  EXPECT_EQ(hull.shape.vertices.size(), 8U);
  EXPECT_EQ(hull.face_indices.size(), 3U * 40U * 12U);
  for (const auto index : hull.face_indices) {
    EXPECT_LT(static_cast<std::size_t>(index), hull.shape.vertices.size());
  }
}

TEST(CollisionBuild, IgnoresUnreferencedVerticesBeforeTheHull) {
  // Constraint: the compact map must skip unreferenced prefixes instead of shifting hull indices.
  const auto base = cube_vertices();
  const auto base_tris = cube_triangles();
  std::vector<float> verts{9.0f, 9.0f, 9.0f, 8.0f, 8.0f, 8.0f};
  verts.insert(verts.end(), base.begin(), base.end());
  std::vector<std::uint16_t> tris;
  for (const auto index : base_tris) tris.push_back(static_cast<std::uint16_t>(index + 2));
  const auto hull = fo4::nif::build_convex_hull_from_mesh(verts, tris, 0);
  EXPECT_EQ(hull.shape.vertices.size(), 8U);
  EXPECT_EQ(hull.face_indices[0], 0U);
  EXPECT_EQ(hull.face_indices[1], 1U);
  EXPECT_EQ(hull.face_indices[2], 3U);
  for (const auto index : hull.face_indices) {
    EXPECT_LT(static_cast<std::size_t>(index), 8U);
  }
}

TEST(CollisionBuild, RefusesANonConvexMeshThatDropsAReferencedVertex) {
  // Constraint: an interior vertex must be refused, not silently dropped from the hull.
  auto verts = cube_vertices();
  verts.insert(verts.end(), {0.0f, 0.0f, 0.0f});
  auto tris = cube_triangles();
  tris.insert(tris.end(), {0, 8, 1, 2, 8, 3});
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, tris, 0), std::invalid_argument);
}

TEST(CollisionBuild, RefusesAnAxisAlignedFlatMesh) {
  const std::vector<float> verts{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  const std::vector<std::uint16_t> tris{0, 1, 2, 0, 2, 3};
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, tris, 0), std::invalid_argument);
}

TEST(CollisionBuild, RefusesInvalidInputs) {
  const std::vector<float> verts{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
  const std::vector<std::uint16_t> out_of_range{0, 1, 9};
  const std::vector<std::uint16_t> one_face{0, 2, 1};
  const std::vector<std::uint16_t> partial_face{0, 1};
  const std::vector<float> empty_verts;
  const std::vector<std::uint16_t> empty_tris;
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, out_of_range, 0), std::invalid_argument);
  auto bad = verts;
  bad[4] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(bad, one_face, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(empty_verts, empty_tris, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, partial_face, 0), std::invalid_argument);
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, one_face, 0, -1.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, one_face, 0, 1e-3f, 0.0f), std::invalid_argument);
}

TEST(CollisionBuild, AcceptsAToleranceOfZeroByClampingIt) {
  // Constraint: tolerance 0 is clamped to a positive floor instead of dropping valid faces by round-off.
  const auto clamped = fo4::nif::build_convex_hull_from_mesh(cube_vertices(), cube_triangles(), 0, 0.0f);
  const auto defaulted = fo4::nif::build_convex_hull_from_mesh(cube_vertices(), cube_triangles(), 0);
  EXPECT_EQ(clamped.shape.vertices, defaulted.shape.vertices);
  EXPECT_EQ(clamped.face_indices, defaulted.face_indices);
}

TEST(CollisionBuild, IsDeterministicAcrossBuilds) {
  const auto first = fo4::nif::build_convex_hull_from_mesh(cube_vertices(), cube_triangles(), 0);
  const auto second = fo4::nif::build_convex_hull_from_mesh(cube_vertices(), cube_triangles(), 0);
  EXPECT_EQ(first.shape.vertices, second.shape.vertices);
  EXPECT_EQ(first.face_indices, second.face_indices);
}

TEST(CollisionBuild, AcceptsExactlyTwoHundredAndFiftyFiveHullVertices) {
  constexpr std::size_t k_ring = 127;
  std::vector<float> verts;
  for (const float z : {-1.0f, 1.0f}) {
    for (std::size_t i = 0; i < k_ring; ++i) {
      const auto angle = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(k_ring);
      verts.insert(verts.end(), {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)), z});
    }
  }
  verts.insert(verts.end(), {0.0f, 0.0f, 2.5f});
  const auto apex = static_cast<std::uint16_t>(2 * k_ring);
  std::vector<std::uint16_t> tris;
  for (std::size_t i = 0; i < k_ring; ++i) {
    const auto next = (i + 1) % k_ring;
    const auto bottom = static_cast<std::uint16_t>(i);
    const auto bottom_next = static_cast<std::uint16_t>(next);
    const auto top = static_cast<std::uint16_t>(k_ring + i);
    const auto top_next = static_cast<std::uint16_t>(k_ring + next);
    tris.insert(tris.end(), {bottom, bottom_next, top_next, bottom, top_next, top, top, top_next, apex});
  }
  const auto hull = fo4::nif::build_convex_hull_from_mesh(verts, tris, 0);
  EXPECT_EQ(hull.shape.vertices.size(), 255U);
}

TEST(CollisionBuild, RefusesMoreThanTwoHundredAndFiftyFiveDistinctPositions) {
  constexpr std::size_t k_ring = 128;
  std::vector<float> verts;
  for (const float z : {-1.0f, 1.0f}) {
    for (std::size_t i = 0; i < k_ring; ++i) {
      const auto angle = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(k_ring);
      verts.insert(verts.end(), {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)), z});
    }
  }
  std::vector<std::uint16_t> tris;
  for (std::size_t i = 0; i < k_ring; ++i) {
    const auto next = (i + 1) % k_ring;
    const auto bottom = static_cast<std::uint16_t>(i);
    const auto bottom_next = static_cast<std::uint16_t>(next);
    const auto top = static_cast<std::uint16_t>(k_ring + i);
    const auto top_next = static_cast<std::uint16_t>(k_ring + next);
    tris.insert(tris.end(), {bottom, bottom_next, top_next, bottom, top_next, top});
  }
  EXPECT_THROW(fo4::nif::build_convex_hull_from_mesh(verts, tris, 0), std::invalid_argument);
}
