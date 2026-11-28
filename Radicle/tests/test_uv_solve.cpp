#include "fo4/nif/uv.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace uv = fo4::nif::uv;
using Vec = std::vector<float>;
using Ids = std::vector<std::uint32_t>;
using Pairs = std::vector<uv::IndexPair>;

constexpr float k_nan = std::numeric_limits<float>::quiet_NaN();

Ids all(std::size_t n) {
  Ids out(n);
  for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint32_t>(i);
  return out;
}

Vec cat(const Vec& a, const Vec& b) {
  Vec out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

Vec slice(const Vec& v, std::size_t from, std::size_t to) { return Vec(v.begin() + from, v.begin() + to); }

void expect_near(const Vec& actual, const std::vector<double>& expected, double tol = 5e-7) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(actual[i], expected[i], tol) << "component " << i;
}

double dist(const Vec& uvs, std::size_t a, std::size_t b) {
  return std::hypot(static_cast<double>(uvs[2 * a]) - uvs[2 * b], static_cast<double>(uvs[2 * a + 1]) - uvs[2 * b + 1]);
}

const Ids k_fan_tris{0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};
const Vec k_fan_uvs{0.2f, 0.2f, 0, 0, 1, 0, 1, 1, 0, 1, 0.9f, 0.9f};

const Vec k_split_verts{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, -1, 0};
const Ids k_split_ids{0, 0, 0, 1, 1, 1};

const Vec k_square_uvs{0, 0, 1, 0, 1, 1, 0, 1, 2, 2, 2, 3, 1, 3, 1, 2, 5, 5, 6, 5, 5, 6};
const Vec k_square_verts{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, -1, 0, 0, -1, 0, 10, 10, 0, 11, 10, 0, 10, 11, 0};
const Ids k_square_ids{0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2};
const Ids k_square_tris{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10};

}  // namespace

TEST(UvSolveBoundary, KeepsTheFanCentreInteriorAndFlagsOnlyTheRim) {
  EXPECT_EQ(uv::boundary_verts(5, k_fan_tris), (Ids{1, 2, 3, 4}));
}

TEST(UvSolveBoundary, IgnoresATriangleThatPointsPastTheVertexCount) {
  const Ids tris{0, 1, 2, 0, 1, 9};
  EXPECT_EQ(uv::boundary_verts(3, tris), (Ids{0, 1, 2}));
}

TEST(UvSolveRelax, MovesOnlyTheSelectedInteriorVertexToTheMeanOfItsNeighbours) {
  const Ids sel{0, 1, 5};
  EXPECT_EQ(uv::relax_uvs(k_fan_uvs, k_fan_tris, sel, 1), (Vec{0.5f, 0.5f, 0, 0, 1, 0, 1, 1, 0, 1, 0.9f, 0.9f}));
}

TEST(UvSolveRelax, LeavesAnInteriorVertexStillWhenItIsNotSelected) {
  const Ids sel{1};
  EXPECT_EQ(uv::relax_uvs(k_fan_uvs, k_fan_tris, sel, 3), k_fan_uvs);
}

TEST(UvSolveRelax, IsANoOpForZeroIterations) {
  const Ids sel{0};
  EXPECT_EQ(uv::relax_uvs(k_fan_uvs, k_fan_tris, sel, 0), k_fan_uvs);
}

TEST(UvSolveStitchPairs, PairsSplitVerticesAcrossTwoIslandsInIndexOrder) {
  EXPECT_EQ(uv::find_stitch_pairs(k_split_verts, k_split_ids, all(6), 1e-6), (Pairs{{0, 4}, {1, 3}}));
}

TEST(UvSolveStitchPairs, SkipsUnselectedVerticesAndCoincidentVerticesOfTheSameIsland) {
  const Ids sel{0, 1, 2, 3, 5};
  EXPECT_EQ(uv::find_stitch_pairs(k_split_verts, k_split_ids, sel, 1e-6), (Pairs{{1, 3}}));
  EXPECT_EQ(uv::find_stitch_pairs(Vec{0, 0, 0, 0, 0, 0}, Ids{0, 0}, all(2), 1e-6), Pairs{});
}

TEST(UvSolveStitchPairs, FindsAPairWhoseVerticesSitInAdjacentGridCells) {
  EXPECT_EQ(uv::find_stitch_pairs(Vec{0.99f, 0, 0, 1.01f, 0, 0, 1.2f, 0, 0}, Ids{0, 1, 1}, all(3), 0.05), (Pairs{{0, 1}}));
}

TEST(UvSolveStitchPairs, ReturnsNoPairsForATinyOrNanToleranceWithoutSpinning) {
  EXPECT_EQ(uv::find_stitch_pairs(k_split_verts, k_split_ids, all(6), 1e-17), Pairs{});
  EXPECT_EQ(uv::find_stitch_pairs(k_split_verts, k_split_ids, all(6), std::numeric_limits<double>::quiet_NaN()), Pairs{});
}

TEST(UvSolveStitch, RotatesAndOffsetsTheMobileSquareOntoItsNeighbourSoPairedUvsCoincide) {
  const auto out = uv::stitch_selection(k_square_uvs, k_square_verts, k_square_ids, all(11), 1e-6);
  expect_near(out, {0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 5, 5, 6, 5, 5, 6});
  EXPECT_NEAR(dist(out, 0, 4), 0, 5e-7);
  EXPECT_NEAR(dist(out, 1, 5), 0, 5e-7);
}

TEST(UvSolveStitch, KeepsTheMobileIslandEdgeLengthsUnchanged) {
  const auto out = uv::stitch_selection(k_square_uvs, k_square_verts, k_square_ids, all(11), 1e-6);
  for (const auto& edge : std::vector<std::pair<std::size_t, std::size_t>>{{4, 5}, {5, 6}, {6, 7}, {7, 4}}) {
    EXPECT_NEAR(dist(out, edge.first, edge.second), dist(k_square_uvs, edge.first, edge.second), 5e-7);
  }
}

TEST(UvSolveStitch, LeavesAnIslandWithNoPairToTheAnchorUntouched) {
  const auto out = uv::stitch_selection(k_square_uvs, k_square_verts, k_square_ids, all(11), 1e-6);
  EXPECT_EQ(slice(out, 16, out.size()), (Vec{5, 5, 6, 5, 5, 6}));
}

TEST(UvSolveStitch, ReconcilesACoincidentVertexClosureSoEveryPairedVertexLandsOnOnePoint) {
  const auto out = uv::stitch_selection(Vec{0.5f, 0.5f, 0.1f, 0.1f, 0.9f, 0.2f}, Vec(9, 0.0f), Ids{0, 1, 1}, all(3), 1e-6);
  EXPECT_NEAR(dist(out, 0, 1), 0, 5e-7);
  EXPECT_NEAR(dist(out, 0, 2), 0, 5e-7);
}

TEST(UvSolveStitch, ClosesAThreeIslandSeamCycleSoEveryPairedVertexCoincides) {
  const Vec verts{0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0};
  const Vec uvs{0, 0, 0, 1, 1, 0, 0.5f, 0.5f, 0.9f, 0.2f, 0.2f, 0.9f};
  const auto out = uv::stitch_selection(uvs, verts, Ids{0, 0, 1, 1, 2, 2}, all(6), 1e-6);
  EXPECT_NEAR(dist(out, 0, 2), 0, 5e-7);
  EXPECT_NEAR(dist(out, 1, 5), 0, 5e-7);
  EXPECT_NEAR(dist(out, 3, 4), 0, 5e-7);
}

TEST(UvSolveOverlap, ReportsTwoIdenticalStackedTriangles) {
  EXPECT_EQ(uv::overlapping_faces(Vec{0, 0, 1, 0, 0, 1}, Ids{0, 1, 2, 0, 1, 2}, Ids{}), (Ids{0, 1}));
}

TEST(UvSolveOverlap, DoesNotReportTrianglesThatShareAnEdgeOrOnlyACorner) {
  EXPECT_EQ(uv::overlapping_faces(Vec{0, 0, 1, 0, 0, 1, 1, 1}, Ids{0, 1, 2, 1, 2, 3}, Ids{}), Ids{});
  EXPECT_EQ(uv::overlapping_faces(Vec{0, 0, 1, 0, 0, 1, 2, 0, 2, -1}, Ids{0, 1, 2, 1, 3, 4}, Ids{}), Ids{});
}

TEST(UvSolveOverlap, IgnoresHiddenTriangles) {
  EXPECT_EQ(uv::overlapping_faces(Vec{0, 0, 1, 0, 0, 1}, Ids{0, 1, 2, 0, 1, 2}, Ids{1}), Ids{});
}

TEST(UvSolveOverlap, IgnoresDegenerateAndNanTriangles) {
  const Vec uvs{0, 0, 1, 0, 0, 1, 0.2f, 0.2f, 0.4f, 0.4f, 0.6f, 0.6f};
  EXPECT_EQ(uv::overlapping_faces(uvs, Ids{0, 1, 2, 3, 4, 5}, Ids{}), Ids{});
  EXPECT_EQ(uv::overlapping_faces(uvs, Ids{0, 1, 2, 0, 1, 2, 3, 4, 5}, Ids{}), (Ids{0, 1}));
  const Vec nan_uvs{0, 0, 1, 0, 0, 1, k_nan, k_nan, k_nan, k_nan, k_nan, k_nan};
  EXPECT_EQ(uv::overlapping_faces(nan_uvs, Ids{0, 1, 2, 3, 4, 5, 0, 1, 2}, Ids{}), (Ids{0, 2}));
}

TEST(UvSolveStitchGroups, StitchesASecondIndependentSeamGroupThatSharesNoIslandWithTheFirst) {
  const Vec verts = cat(k_split_verts, Vec{5, 0, 0, 6, 0, 0, 5, 1, 0, 5, 0, 0, 6, 0, 0, 5, -1, 0});
  const Ids ids{0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};
  const Vec uvs = cat(slice(k_square_uvs, 0, 12), Vec{0.6f, 0.6f, 0.8f, 0.6f, 0.6f, 0.8f, 0.1f, 0.1f, 0.3f, 0.1f, 0.1f, 0.3f});
  const auto out = uv::stitch_selection(uvs, verts, ids, all(12), 1e-6);
  EXPECT_NEAR(dist(out, 0, 4), 0, 5e-7);
  EXPECT_NEAR(dist(out, 6, 9), 0, 5e-7);
  EXPECT_NEAR(dist(out, 7, 10), 0, 5e-7);
  expect_near(slice(out, 18, 24), {0.6, 0.6, 0.8, 0.6, 0.6, 0.8});
}

TEST(UvSolveOverlapStray, ReportsOnlyTheOverlappingFacesWhenAThirdTriangleSitsFarOutsideTheTile) {
  const Vec uvs{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1000, 1000, 1001, 1000, 1000, 1001};
  EXPECT_EQ(uv::overlapping_faces(uvs, Ids{0, 1, 2, 3, 4, 5, 6, 7, 8}, Ids{}), (Ids{0, 1}));
}

TEST(UvSolveImmutability, NeverWritesToItsUvVertexIdOrTriangleInputs) {
  const Vec uvs = k_square_uvs, verts = k_square_verts;
  const Ids ids = k_square_ids, tris = k_square_tris, sel = all(11);
  uv::boundary_verts(11, tris);
  uv::relax_uvs(uvs, tris, sel, 5);
  uv::find_stitch_pairs(verts, ids, sel, 1e-6);
  uv::stitch_selection(uvs, verts, ids, sel, 1e-6);
  uv::overlapping_faces(uvs, tris, Ids{});
  EXPECT_EQ(uvs, k_square_uvs);
  EXPECT_EQ(verts, k_square_verts);
  EXPECT_EQ(ids, k_square_ids);
  EXPECT_EQ(tris, k_square_tris);
}
