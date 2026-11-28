#include "fo4/nif/uv.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

namespace uv = fo4::nif::uv;
using Vec = std::vector<float>;
using Ids = std::vector<std::uint32_t>;

const Vec k_hide_uvs{0.1f, 0.1f, 0.3f, 0.1f, 0.1f, 0.3f, 0.2f, 0.2f, 0.4f, 0.4f};
const Ids k_hide_tris{0, 1, 2, 2, 3, 4};
const Ids k_hidden1{1};
const uv::Box k_all_box{0, 0, 0.5, 0.5};

const Vec k_edge_uvs{0, 0, 1, 0, 0, 1, 0.4f, 0.12f, 0.6f, 0.12f, 0.5f, 0.2f};
const Ids k_edge_tris{0, 1, 2, 3, 4, 5};

const Vec k_overlap_uvs{0, 0, 1, 0, 0, 1, 0.1f, 0.1f, 0.9f, 0.1f, 0.1f, 0.9f};
const Ids k_overlap_tris{0, 1, 2, 3, 4, 5};

using Tri = std::array<std::uint32_t, 3>;

}  // namespace

TEST(UvOpsVisible, HidesVerticesThatOnlyAHiddenTriangleUsesButKeepsUnreferencedVertices) {
  EXPECT_EQ(uv::visible_verts(6, k_hide_tris, k_hidden1), (Ids{0, 1, 2, 5}));
}

TEST(UvOpsVisible, IgnoresATriangleThatPointsPastTheVertexCount) {
  EXPECT_EQ(uv::visible_verts(3, Ids{0, 1, 9}, Ids{}), (Ids{0, 1, 2}));
}

TEST(UvOpsPickEdge, PicksTheNearestEdgeAndSkipsAHiddenTriangleLyingOnTopOfIt) {
  EXPECT_EQ(uv::pick_edge(k_edge_uvs, k_edge_tris, k_hidden1, 0.5, 0.1, 0.5), (uv::IndexPair{0, 1}));
  EXPECT_EQ(uv::pick_edge(k_edge_uvs, k_edge_tris, Ids{}, 0.5, 0.1, 0.5), (uv::IndexPair{3, 4}));
}

TEST(UvOpsPickEdge, ReturnsNothingWhenEveryEdgeIsBeyondMaxDist) {
  EXPECT_FALSE(uv::pick_edge(k_edge_uvs, k_edge_tris, Ids{}, 0.5, 0.1, 0.01).has_value());
}

TEST(UvOpsPickFace, ReturnsTheTopmostNonHiddenTriangleContainingThePoint) {
  EXPECT_EQ(uv::pick_face(k_overlap_uvs, k_overlap_tris, Ids{}, 0.2, 0.2), (Tri{3, 4, 5}));
  EXPECT_EQ(uv::pick_face(k_overlap_uvs, k_overlap_tris, k_hidden1, 0.2, 0.2), (Tri{0, 1, 2}));
}

TEST(UvOpsPickFace, AcceptsClockwiseWindingAndPointsExactlyOnAnEdge) {
  EXPECT_EQ(uv::pick_face(k_overlap_uvs, Ids{0, 2, 1}, Ids{}, 0.2, 0.2), (Tri{0, 2, 1}));
  EXPECT_EQ(uv::pick_face(k_overlap_uvs, Ids{0, 1, 2}, Ids{}, 0.5, 0), (Tri{0, 1, 2}));
}

TEST(UvOpsPickFace, SkipsAZeroAreaTriangleSoACollapsedUvTriangleDoesNotSwallowEveryClick) {
  const Vec uvs{0, 0, 0.5f, 0.5f, 1, 1, 0, 0, 1, 0, 0, 1};
  EXPECT_EQ(uv::pick_face(uvs, Ids{3, 4, 5, 0, 1, 2}, Ids{}, 0.2, 0.2), (Tri{3, 4, 5}));
  EXPECT_FALSE(uv::pick_face(uvs, Ids{0, 1, 2}, Ids{}, 0.2, 0.2).has_value());
  EXPECT_FALSE(uv::pick_face(Vec(6, 0.5f), Ids{0, 1, 2}, Ids{}, 0.2, 0.2).has_value());
}

TEST(UvOpsBoxVerts, DropsHiddenOnlyVerticesInVertexModeAndHiddenTrianglesInFaceMode) {
  EXPECT_EQ(uv::box_verts(k_hide_uvs, k_hide_tris, k_hidden1, k_all_box, uv::BoxMode::vertex), (Ids{0, 1, 2}));
  EXPECT_EQ(uv::box_verts(k_hide_uvs, k_hide_tris, k_hidden1, k_all_box, uv::BoxMode::face), (Ids{0, 1, 2}));
}

TEST(UvOpsBoxVerts, FaceModeNeedsAllThreeCornersInTheBoxWhileEdgeModeTakesTheInsideEdge) {
  const Vec uvs{0.1f, 0.1f, 0.15f, 0.1f, 0.1f, 0.5f};
  const uv::Box box{0, 0, 0.2, 0.2};
  EXPECT_EQ(uv::box_verts(uvs, Ids{0, 1, 2}, Ids{}, box, uv::BoxMode::face), Ids{});
  EXPECT_EQ(uv::box_verts(uvs, Ids{0, 1, 2}, Ids{}, box, uv::BoxMode::edge), (Ids{0, 1}));
}

TEST(UvOpsInvert, NeverSelectsHiddenOnlyVerticesWhenInvertingAVisibleSelection) {
  const auto visible = uv::visible_verts(6, k_hide_tris, k_hidden1);
  EXPECT_EQ(uv::invert(visible, Ids{0}), (Ids{1, 2, 5}));
}

TEST(UvOpsLinked, FillsTheWholeIslandOfASelectedVertexAndNotASecondIsland) {
  EXPECT_EQ(uv::linked(Ids{0, 0, 0, 1, 1, 1}, Ids{1}), (Ids{0, 1, 2}));
}

TEST(UvOpsSelectedTris, ListsTrianglesWhoseThreeCornersAreAllSelectedAndNotHidden) {
  const Ids sel{0, 1, 2, 3};
  const Ids tris{0, 1, 2, 2, 3, 4};
  EXPECT_EQ(uv::selected_tris(tris, 5, sel, Ids{}), (Ids{0}));
  EXPECT_EQ(uv::selected_tris(tris, 5, sel, Ids{0}), Ids{});
}

TEST(UvOpsMergeAtCentre, MovesSelectedVertsToTheBoundingBoxCentreAndLeavesUnselectedOnes) {
  const Vec uvs{0.25f, 0.25f, 0.75f, 0.25f, 0.5f, 0.75f};
  EXPECT_EQ(uv::merge_at_centre(uvs, Ids{0, 1}), (Vec{0.5f, 0.25f, 0.5f, 0.25f, 0.5f, 0.75f}));
}

TEST(UvOpsMergeByDistance, WeldsNearVertsAtOrWithinDistToTheirMeanAndLeavesFarAndUnselectedVertsAlone) {
  const Vec uvs{0.5f, 0.5f, 0.75f, 0.5f, 0.9f, 0.9f, 0.625f, 0.5f};
  EXPECT_EQ(uv::merge_by_distance(uvs, Ids{0, 1, 2}, 0.25), (Vec{0.625f, 0.5f, 0.625f, 0.5f, 0.9f, 0.9f, 0.625f, 0.5f}));
}

TEST(UvOpsMergeByDistance, ClustersAChainTransitivelyWhenTheEndsAreFartherApartThanDist) {
  const Vec uvs{0, 0, 0.125f, 0, 0.25f, 0};
  EXPECT_EQ(uv::merge_by_distance(uvs, Ids{0, 1, 2}, 0.1875), (Vec{0.125f, 0, 0.125f, 0, 0.125f, 0}));
}

TEST(UvOpsMergeByDistance, WeldsVertsThatStraddleAGridCellBoundary) {
  const Vec uvs{0.1875f, 0.5f, 0.3125f, 0.5f};
  EXPECT_EQ(uv::merge_by_distance(uvs, Ids{0, 1}, 0.25), (Vec{0.25f, 0.5f, 0.25f, 0.5f}));
}

TEST(UvOpsMergeByDistance, ReturnsAnUnchangedCopyForATinyDistanceInsteadOfSpinningOnOverflowedGridCells) {
  const Vec uvs{0.5f, 0.5f, 0.75f, 0.5f};
  EXPECT_EQ(uv::merge_by_distance(uvs, Ids{0, 1}, 1e-17), uvs);
}

TEST(UvOpsMergeByDistance, CountsARepeatedSelectionEntryOnceInTheMean) {
  const Vec uvs{0.5f, 0.5f, 0.75f, 0.5f, 0.9f, 0.9f};
  EXPECT_EQ(uv::merge_by_distance(uvs, Ids{0, 0, 1}, 0.25), (Vec{0.625f, 0.5f, 0.625f, 0.5f, 0.9f, 0.9f}));
}

TEST(UvOpsAlign, AlignsTheChosenComponentOfSelectedVertsToMinCentreOrMax) {
  const Vec uvs{0.25f, 0.5f, 0.75f, 0.25f, 0.5f, 0.75f};
  const Ids sel{0, 2};
  EXPECT_EQ(uv::align_sel(uvs, sel, uv::Axis::u, uv::Align::min), (Vec{0.25f, 0.5f, 0.75f, 0.25f, 0.25f, 0.75f}));
  EXPECT_EQ(uv::align_sel(uvs, sel, uv::Axis::u, uv::Align::centre), (Vec{0.375f, 0.5f, 0.75f, 0.25f, 0.375f, 0.75f}));
  EXPECT_EQ(uv::align_sel(uvs, sel, uv::Axis::u, uv::Align::max), (Vec{0.5f, 0.5f, 0.75f, 0.25f, 0.5f, 0.75f}));
}

TEST(UvOpsSnapGrid, RoundsSelectedVertsToTheStepAndReturnsACopyForAZeroStep) {
  const Vec uvs{0.3f, 0.6f, 0.3f, 0.6f};
  EXPECT_EQ(uv::snap_grid(uvs, Ids{0}, 0.25), (Vec{0.25f, 0.5f, 0.3f, 0.6f}));
  EXPECT_EQ(uv::snap_grid(uvs, Ids{0}, 0), uvs);
}

TEST(UvOpsSnapGrid, RoundsAHalfTowardPositiveInfinityLikeMathRound) {
  const Vec uvs{-0.125f, 0.375f};
  EXPECT_EQ(uv::snap_grid(uvs, Ids{0}, 0.25), (Vec{0.0f, 0.5f}));
}

TEST(UvOpsSnapPixels, SnapsToTexelCornersOrTexelCentresAndReturnsACopyForAZeroSize) {
  const Vec uvs{0.3f, 0.6f, 0.3f, 0.6f};
  EXPECT_EQ(uv::snap_pixels(uvs, Ids{0}, 4, 4, false), (Vec{0.25f, 0.5f, 0.3f, 0.6f}));
  EXPECT_EQ(uv::snap_pixels(uvs, Ids{0}, 4, 4, true), (Vec{0.375f, 0.625f, 0.3f, 0.6f}));
  EXPECT_EQ(uv::snap_pixels(uvs, Ids{0}, 0, 4, false), uvs);
  EXPECT_EQ(uv::snap_pixels(uvs, Ids{0}, std::numeric_limits<double>::quiet_NaN(), 4, false), uvs);
}

TEST(UvOpsSnapPixels, KeepsACentreSnappedUvOfExactlyOneInsideTheTile) {
  EXPECT_EQ(uv::snap_pixels(Vec{1, 1, 0.3f, 0.6f}, Ids{0}, 4, 4, true), (Vec{0.875f, 0.875f, 0.3f, 0.6f}));
}

TEST(UvOpsSnapPixels, KeepsTheIntegerTileOfATiledUvWhileSnappingToItsTexelCentre) {
  EXPECT_EQ(uv::snap_pixels(Vec{1.5f, 0.6f, -0.3f, -0.6f}, Ids{0, 1}, 4, 4, true), (Vec{1.625f, 0.625f, -0.375f, -0.625f}));
}

TEST(UvOpsSnapPixels, SnapsAnIntegerBoundaryToTheTileNearerZeroSoTheZeroEdgeStaysInTileZero) {
  EXPECT_EQ(uv::snap_pixels(Vec{0, 1, 2, -1}, Ids{0, 1}, 4, 4, true), (Vec{0.125f, 0.875f, 1.875f, -0.875f}));
}

TEST(UvOpsClampTile, ClampsSelectedVertsIntoZeroOneAndLeavesInRangeAndUnselectedValuesUnchanged) {
  const Vec uvs{-0.2f, 0.5f, 1.4f, 1.2f, 0.3f, 0.7f, 1.5f, -0.1f};
  EXPECT_EQ(uv::clamp_tile(uvs, Ids{0, 1, 2}), (Vec{0, 0.5f, 1, 1, 0.3f, 0.7f, 1.5f, -0.1f}));
}

TEST(UvOpsClampTile, IgnoresASelectionEntryPastTheVertexCount) {
  const Vec uvs{-0.2f, 1.4f};
  EXPECT_EQ(uv::clamp_tile(uvs, Ids{5}), uvs);
}

TEST(UvOpsImmutability, UvReturningOperatorsLeaveTheirUvInputUntouched) {
  const Vec before{0.3f, 0.6f, 1.4f, -0.2f, 0.5f, 0.5f};
  const Vec uvs = before;
  const Ids sel{0, 1};
  uv::snap_grid(uvs, sel, 0.25);
  uv::snap_pixels(uvs, sel, 4, 4, true);
  uv::clamp_tile(uvs, sel);
  uv::merge_at_centre(uvs, sel);
  uv::merge_by_distance(uvs, sel, 1);
  uv::align_sel(uvs, sel, uv::Axis::v, uv::Align::max);
  EXPECT_EQ(uvs, before);
}

TEST(UvOpsImmutability, TriangleQueriesLeaveTheirTriangleAndUvInputsUntouched) {
  const Vec uvs = k_overlap_uvs;
  const Ids tris = k_overlap_tris;
  uv::visible_verts(6, tris, Ids{});
  uv::pick_edge(uvs, tris, Ids{}, 0.2, 0.2, 1);
  uv::pick_face(uvs, tris, Ids{}, 0.2, 0.2);
  uv::box_verts(uvs, tris, Ids{}, k_all_box, uv::BoxMode::face);
  uv::selected_tris(tris, 6, Ids{0, 1, 2}, Ids{});
  EXPECT_EQ(uvs, k_overlap_uvs);
  EXPECT_EQ(tris, k_overlap_tris);
}
