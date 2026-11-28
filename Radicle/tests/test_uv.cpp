#include "fo4/nif/uv.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace {

namespace uv = fo4::nif::uv;
using Vec = std::vector<float>;
using Ids = std::vector<std::uint32_t>;
using Pins = std::vector<uv::Pin>;

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

Vec slice(const Vec& v, std::size_t from) { return slice(v, from, v.size()); }

void expect_near(const Vec& actual, const std::vector<double>& expected, double tol = 5e-6) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(actual[i], expected[i], tol) << "component " << i;
}

double dist(const Vec& uvs, std::size_t a, std::size_t b) {
  return std::hypot(static_cast<double>(uvs[2 * a]) - uvs[2 * b], static_cast<double>(uvs[2 * a + 1]) - uvs[2 * b + 1]);
}

double dist_3d(const Vec& verts, std::size_t a, std::size_t b) {
  return std::hypot(static_cast<double>(verts[3 * a]) - verts[3 * b], static_cast<double>(verts[3 * a + 1]) - verts[3 * b + 1],
                    static_cast<double>(verts[3 * a + 2]) - verts[3 * b + 2]);
}

std::array<double, 4> bbox(const Vec& uvs) {
  std::array<double, 4> out{1e30, -1e30, 1e30, -1e30};
  for (std::size_t i = 0; i < uvs.size(); i += 2) {
    out[0] = std::min<double>(out[0], uvs[i]);
    out[1] = std::max<double>(out[1], uvs[i]);
    out[2] = std::min<double>(out[2], uvs[i + 1]);
    out[3] = std::max<double>(out[3], uvs[i + 1]);
  }
  return out;
}

void expect_box(const std::array<double, 4>& actual, const std::array<double, 4>& expected, double tol) {
  for (std::size_t i = 0; i < 4; ++i) EXPECT_NEAR(actual[i], expected[i], tol) << "box value " << i;
}

double scale_of(const Vec& uvs, const Vec& verts, std::size_t a, std::size_t b) { return dist(uvs, a, b) / dist_3d(verts, a, b); }

const Vec k_tri_uvs{0.25f, 0.5f, 0.75f, 0.5f, 0.5f, 0.25f};

}  // namespace

TEST(UvMath, TwoDisjointTrianglesAreTwoIslands) {
  const Ids tris{0, 1, 2, 3, 4, 5};
  EXPECT_EQ(uv::islands(6, tris), (Ids{0, 0, 0, 1, 1, 1}));
}

TEST(UvMath, TrianglesSharingAVertexJoinIntoOneIsland) {
  const Ids tris{0, 1, 2, 2, 3, 4};
  EXPECT_EQ(uv::islands(5, tris), (Ids{0, 0, 0, 0, 0}));
}

TEST(UvMath, AnUnreferencedVertexHasItsOwnIsland) {
  const Ids tris{0, 1, 2};
  EXPECT_EQ(uv::islands(4, tris), (Ids{0, 0, 0, 1}));
}

TEST(UvMath, TransformMovesOnlyTheSelectedVertices) {
  const Ids sel{1};
  const auto out = uv::transform_sel(k_tri_uvs, sel, {0.25, -0.25, 1, 1, 0, {0, 0}});
  EXPECT_EQ(out, (Vec{0.25f, 0.5f, 1.0f, 0.25f, 0.5f, 0.25f}));
}

TEST(UvMath, TransformScalesAboutThePivot) {
  const auto out = uv::transform_sel(k_tri_uvs, all(3), {0, 0, 2, 1, 0, {0.5, 0.5}});
  EXPECT_EQ(out, (Vec{0.0f, 0.5f, 1.0f, 0.5f, 0.5f, 0.25f}));
}

TEST(UvMath, TransformRotatesAboutThePivot) {
  const Ids sel{0};
  const auto out = uv::transform_sel(k_tri_uvs, sel, {0, 0, 1, 1, std::numbers::pi / 2, {0.5, 0.5}});
  EXPECT_NEAR(out[0], 0.5, 0.005);
  EXPECT_NEAR(out[1], 0.25, 0.005);
  EXPECT_EQ(slice(out, 2), slice(k_tri_uvs, 2));
}

TEST(UvMath, TransformScalesBeforeItRotatesAboutThePivot) {
  const Vec one{0.75f, 0.5f};
  const Ids sel{0};
  const auto out = uv::transform_sel(one, sel, {0, 0, 2, 1, std::numbers::pi / 2, {0.5, 0.5}});
  EXPECT_NEAR(out[0], 0.5, 0.005);
  EXPECT_NEAR(out[1], 1.0, 0.005);
}

TEST(UvMath, TransformReturnsANewVectorAndLeavesTheInputUntouched) {
  const Vec before = k_tri_uvs;
  const auto out = uv::transform_sel(k_tri_uvs, all(3), {1, 1, 1, 1, 0, {0, 0}});
  EXPECT_EQ(k_tri_uvs, before);
  EXPECT_NE(out, k_tri_uvs);
}

TEST(UvMath, BoundsOfTheSelectionAndNulloptForAnEmptyOne) {
  const Ids sel{0, 2};
  const auto box = uv::bounds(k_tri_uvs, sel);
  ASSERT_TRUE(box.has_value());
  EXPECT_EQ(box->min_u, 0.25);
  EXPECT_EQ(box->min_v, 0.25);
  EXPECT_EQ(box->max_u, 0.5);
  EXPECT_EQ(box->max_v, 0.5);
  EXPECT_FALSE(uv::bounds(k_tri_uvs, Ids{}).has_value());
}

TEST(UvMath, MedianTakesThePerAxisMedian) {
  const auto m = uv::median(k_tri_uvs, all(3));
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->u, 0.5);
  EXPECT_EQ(m->v, 0.5);
}

TEST(UvMath, MedianCountsARepeatedSelectionIndexOnce) {
  const Vec uvs{0, 0, 10, 0, 20, 0};
  const auto m = uv::median(uvs, Ids{0, 0, 1});
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->u, 5);
}

TEST(UvMath, MedianOfManyVerticesWithANanSortsTheNanLast) {
  Vec uvs;
  for (int i = 0; i < 20; ++i) {
    uvs.push_back(i == 7 ? k_nan : static_cast<float>(i));
    uvs.push_back(0);
  }
  const auto m = uv::median(uvs, all(20));
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->u, 10.5);
  EXPECT_EQ(m->v, 0);
}

TEST(UvMath, PickNearestReturnsTheNearestVertexInsideTheRadius) {
  EXPECT_EQ(uv::pick_nearest(k_tri_uvs, 0.74, 0.5, 0.05), 1);
}

TEST(UvMath, PickNearestReturnsMinusOneWhenNothingIsInTheRadius) {
  EXPECT_EQ(uv::pick_nearest(k_tri_uvs, 0, 0, 0.05), -1);
}

TEST(UvMath, PointInBoxIsInclusive) {
  const uv::Box box{0, 0, 1, 1};
  EXPECT_TRUE(uv::point_in_box(0.5, 0.5, box));
  EXPECT_TRUE(uv::point_in_box(1.0, 0.0, box));
  EXPECT_FALSE(uv::point_in_box(1.5, 0.5, box));
}

TEST(UvMath, FrameViewFitsTheTileAndTheShapeInsideTheCanvas) {
  const Vec uvs{0.25f, 0.25f, 0.75f, 0.75f};
  const auto v = uv::frame_view(uvs, 800, 600);
  EXPECT_NEAR(v.scale, 540, 0.005);
  EXPECT_NEAR(v.px, 130, 0.005);
  EXPECT_NEAR(v.py, 30, 0.005);
}

TEST(UvMath, ResolvePivotFollowsTheMode) {
  const Ids sel{0, 2};
  EXPECT_EQ(uv::resolve_pivot(k_tri_uvs, sel, uv::PivotMode::centre).u, 0.375);
  EXPECT_EQ(uv::resolve_pivot(k_tri_uvs, sel, uv::PivotMode::median).v, 0.375);
  EXPECT_EQ(uv::resolve_pivot(k_tri_uvs, sel, uv::PivotMode::tile).u, 0.5);
  EXPECT_EQ(uv::resolve_pivot(k_tri_uvs, Ids{}, uv::PivotMode::centre).v, 0.5);
}

namespace {

const Vec k_squares{0, 0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 0.6f, 0.6f, 0.9f, 0.6f, 0.9f, 0.9f, 0.6f, 0.9f};
const Ids k_square_ids{0, 0, 0, 0, 1, 1, 1, 1};
const Ids k_both_squares{0, 4};

Vec nine_uvs() {
  Vec out;
  for (int k = 0; k < 9; ++k) {
    const float a = 0.1f * static_cast<float>(k), b = a + 0.05f;
    for (const float x : {a, a, b, a, b, b, a, b}) out.push_back(x);
  }
  return out;
}

Ids nine_ids() {
  Ids out(36);
  for (std::size_t i = 0; i < 36; ++i) out[i] = static_cast<std::uint32_t>(i / 4);
  return out;
}

const Vec k_scale_verts{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 2, 2, 0, 2, 2, 2, 2, 0, 2, 2};
const Vec k_scale_uvs{0, 0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 0.5f, 0.5f, 1, 0.5f, 1, 1, 0.5f, 1};
const Ids k_scale_tris{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
const Ids k_scale_ids{0, 0, 0, 0, 1, 1, 1, 1};

}  // namespace

TEST(UvPack, PacksALargeAndASmallSquareWithTheMarginAndTheirSizeRatioKept) {
  const auto out = uv::pack_islands(k_squares, k_square_ids, k_both_squares, 0.1);
  ASSERT_TRUE(out.has_value());
  expect_near(*out, {0.1, 0.1, 0.5375, 0.1, 0.5375, 0.5375, 0.1, 0.5375, 0.6375, 0.1, 0.9, 0.1, 0.9, 0.3625, 0.6375, 0.3625});
}

TEST(UvPack, MovesAWholeIslandWhenOnlyOneOfItsVerticesIsSelected) {
  const Ids sel{0};
  const auto out = uv::pack_islands(k_squares, k_square_ids, sel, 0.1);
  ASSERT_TRUE(out.has_value());
  expect_near(slice(*out, 0, 8), {0.1, 0.1, 0.9, 0.1, 0.9, 0.9, 0.1, 0.9});
  EXPECT_EQ(slice(*out, 8), slice(k_squares, 8));
}

TEST(UvPack, LeavesAnIslandWithNoSelectedVertexExactlyAsItWas) {
  const Vec with_third = cat(k_squares, {0.2f, 0.7f, 0.3f, 0.7f, 0.25f, 0.8f});
  Ids ids = k_square_ids;
  ids.insert(ids.end(), {2, 2, 2});
  const auto out = uv::pack_islands(with_third, ids, k_both_squares, 0.1);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(slice(*out, 16), (Vec{0.2f, 0.7f, 0.3f, 0.7f, 0.25f, 0.8f}));
  EXPECT_NEAR((*out)[0], 0.1, 5e-6);
}

TEST(UvPack, ReturnsAnEqualCopyWhenNothingIsSelected) {
  const auto out = uv::pack_islands(k_squares, k_square_ids, Ids{}, 0.1);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, k_squares);
}

TEST(UvPack, ReturnsNulloptWhenNineIslandsCannotFitTheTileAtMarginQuarter) {
  const Vec uvs = nine_uvs();
  const Ids ids = nine_ids();
  EXPECT_FALSE(uv::pack_islands(uvs, ids, all(36), 0.25).has_value());
  EXPECT_TRUE(uv::pack_islands(uvs, ids, all(36), 0.01).has_value());
}

TEST(UvPack, AverageScaleScalesTwoIslandsToTheAreaWeightedMeanStretch) {
  const auto out = uv::average_island_scale(k_scale_uvs, k_scale_verts, k_scale_tris, k_scale_ids, k_both_squares);
  expect_near(out, {0.1, 0.1, 0.4, 0.1, 0.4, 0.4, 0.1, 0.4, 0.45, 0.45, 1.05, 0.45, 1.05, 1.05, 0.45, 1.05});
}

TEST(UvPack, AverageScaleLeavesASingleIslandUnchanged) {
  const Ids tris{0, 1, 2, 0, 2, 3};
  const auto out = uv::average_island_scale(slice(k_scale_uvs, 0, 8), slice(k_scale_verts, 0, 12), tris, Ids{0, 0, 0, 0}, Ids{0});
  EXPECT_EQ(out, slice(k_scale_uvs, 0, 8));
}

TEST(UvPack, TriStretchScoresAUniformlyScaledMeshZeroOnEveryTriangle) {
  const Ids tris{0, 1, 2, 0, 2, 3};
  expect_near(uv::tri_stretch(Vec{0, 0, 2, 0, 2, 2, 0, 2}, Vec{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0}, tris), {0, 0});
}

TEST(UvPack, TriStretchScoresARatioOfTwoAndOfOneHalfAroundAMeanOfOneAsHalfEach) {
  const Vec verts{0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 5, 2, 0, 5, 0, 2, 5};
  const Vec uvs{0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 0, 2};
  expect_near(uv::tri_stretch(uvs, verts, Ids{0, 1, 2, 3, 4, 5}), {0.5, 0.5});
}

TEST(UvPack, TriStretchClampsAnExtremeStretchToOne) {
  const Vec verts{0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 5, 4, 0, 5, 0, 2, 5};
  const Vec uvs{0, 0, 4, 0, 0, 2, 0, 0, 1, 0, 0, 1};
  expect_near(uv::tri_stretch(uvs, verts, Ids{0, 1, 2, 3, 4, 5}), {1, 1});
}

TEST(UvPack, TriStretchGivesOneValuePerWholeTriangleAndZeroToOnePastTheVertexCount) {
  const Vec verts{0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 5, 4, 0, 5, 0, 2, 5};
  const Vec uvs{0, 0, 4, 0, 0, 2, 0, 0, 1, 0, 0, 1};
  expect_near(uv::tri_stretch(uvs, verts, Ids{0, 1, 2, 3, 4, 5, 0, 1, 99, 7}), {1, 1, 0});
}

TEST(UvPack, ANanMarginIsTreatedAsZeroAndNeverWritesNanUvs) {
  const auto nan_margin = uv::pack_islands(k_squares, k_square_ids, k_both_squares, std::numeric_limits<double>::quiet_NaN());
  ASSERT_TRUE(nan_margin.has_value());
  EXPECT_EQ(*nan_margin, *uv::pack_islands(k_squares, k_square_ids, k_both_squares, 0));
  for (const float x : *nan_margin) EXPECT_TRUE(std::isfinite(x));
}

TEST(UvPack, AverageScaleClampsTheFactorSoATinyIslandCannotLeaveTheHalfFloatRange) {
  const Vec verts = cat(slice(k_scale_verts, 0, 12), {0, 2, 0, 1, 2, 0, 1, 3, 0, 0, 3, 0});
  const Vec uvs = cat(slice(k_scale_uvs, 0, 8), {0, 0, 1e-4f, 0, 1e-4f, 1e-4f, 0, 1e-4f});
  const auto out = uv::average_island_scale(uvs, verts, k_scale_tris, k_scale_ids, k_both_squares);
  for (const float x : out) {
    EXPECT_TRUE(std::isfinite(x));
    EXPECT_LE(std::abs(x), 65504.0f);
  }
  const Vec small = slice(out, 8);
  EXPECT_NEAR(*std::max_element(small.begin(), small.end()) - *std::min_element(small.begin(), small.end()), 1e-1, 5e-6);
}

TEST(UvPack, NeverWritesToItsInputs) {
  const Vec uvs = k_scale_uvs, verts = k_scale_verts;
  const Ids tris = k_scale_tris, ids = k_scale_ids;
  (void)uv::pack_islands(uvs, ids, k_both_squares, 0.1);
  (void)uv::average_island_scale(uvs, verts, tris, ids, k_both_squares);
  (void)uv::tri_stretch(uvs, verts, tris);
  EXPECT_EQ(uvs, k_scale_uvs);
  EXPECT_EQ(verts, k_scale_verts);
  EXPECT_EQ(tris, k_scale_tris);
  EXPECT_EQ(ids, k_scale_ids);
}

namespace {

const Vec k_quad_verts{0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0};
const Ids k_quad_tris{0, 1, 2, 0, 2, 3};
const Ids k_quad_ids{0, 0, 0, 0};
const Vec k_quad_uvs(8, 0.0f);
const float k_s2 = std::numbers::sqrt2_v<float>;
const Vec k_tilted_verts{0, 0, 0, 2, 0, 0, 2, k_s2, k_s2, 0, k_s2, k_s2};
const Vec k_two_verts = cat(k_quad_verts, {5, 5, 0, 6, 5, 0, 6, 6, 0, 5, 6, 0});
const Ids k_two_tris{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
const Ids k_two_ids{0, 0, 0, 0, 1, 1, 1, 1};
const std::vector<std::array<std::size_t, 2>> k_quad_edges{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}, {1, 3}};

}  // namespace

TEST(UvProject, ProjectsAFlatQuadIntoTheTileWithOneUniformScaleForEveryEdge) {
  const auto out = uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4)).uvs;
  expect_box(bbox(out), {0.01, 0.99, 0.01, 0.99}, 5e-6);
  const double s = scale_of(out, k_quad_verts, 0, 1);
  for (const auto& e : k_quad_edges) EXPECT_NEAR(scale_of(out, k_quad_verts, e[0], e[1]), s, 5e-5);
}

TEST(UvProject, ProjectsAQuadInATiltedPlaneWithTheSameUniformScale) {
  const auto out = uv::smart_project(k_quad_uvs, k_tilted_verts, k_quad_tris, k_quad_ids, all(4)).uvs;
  const double s = scale_of(out, k_tilted_verts, 0, 1);
  for (const auto& e : std::vector<std::array<std::size_t, 2>>{{1, 2}, {2, 3}, {3, 0}, {0, 2}}) {
    EXPECT_NEAR(scale_of(out, k_tilted_verts, e[0], e[1]), s, 5e-5);
  }
}

TEST(UvProject, KeepsTexelDensityEqualAcrossTwoDifferentlySizedIslands) {
  const auto out = uv::smart_project(cat(k_quad_uvs, k_quad_uvs), k_two_verts, k_two_tris, k_two_ids, all(8)).uvs;
  const auto big = bbox(slice(out, 0, 8)), small = bbox(slice(out, 8));
  EXPECT_NEAR((big[1] - big[0]) / (small[1] - small[0]), 2, 5e-4);
  for (const float x : out) {
    EXPECT_TRUE(std::isfinite(x));
    EXPECT_GE(x, -1e-6f);
    EXPECT_LE(x, 1 + 1e-6f);
  }
}

TEST(UvProject, PassesThePackMarginThrough) {
  expect_box(bbox(uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4), 0.1).uvs), {0.1, 0.9, 0.1, 0.9}, 5e-6);
}

TEST(UvProject, ProjectsTheWholeIslandWhenOnlyOneOfItsVerticesIsSelected) {
  const auto info = uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, Ids{0});
  EXPECT_EQ(info.projected, 4u);
  EXPECT_EQ(info.skipped, 0u);
  const auto want = bbox(uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4)).uvs);
  expect_box(bbox(info.uvs), want, 5e-6);
}

TEST(UvProject, LeavesAnIslandWithNoSelectedVertexExactlyAsItWas) {
  const Vec tail{0.4f, 0.4f, 0.5f, 0.4f, 0.5f, 0.5f, 0.4f, 0.5f};
  const auto out = uv::smart_project(cat(k_quad_uvs, tail), k_two_verts, k_two_tris, k_two_ids, all(4)).uvs;
  EXPECT_EQ(slice(out, 8), tail);
}

TEST(UvProject, ReturnsAnEqualCopyWhenNothingIsSelected) {
  EXPECT_EQ(uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, Ids{}).uvs, k_quad_uvs);
}

TEST(UvProject, ReportsFitsFalseAndKeepsTheInputUvsWhenNineIslandsCannotPackAtMarginQuarter) {
  Vec verts;
  Ids tris;
  for (std::uint32_t k = 0; k < 9; ++k) {
    const float x = 3.0f * static_cast<float>(k);
    for (const float c : {x, 0.0f, 0.0f, x + 2, 0.0f, 0.0f, x + 2, 2.0f, 0.0f, x, 2.0f, 0.0f}) verts.push_back(c);
    for (const std::uint32_t c : {4 * k, 4 * k + 1, 4 * k + 2, 4 * k, 4 * k + 2, 4 * k + 3}) tris.push_back(c);
  }
  const Ids ids = nine_ids();
  const Vec uvs(72, 0.0f);
  const auto info = uv::smart_project(uvs, verts, tris, ids, all(36), 0.25);
  EXPECT_FALSE(info.fits);
  EXPECT_EQ(info.uvs, uvs);
  EXPECT_TRUE(uv::smart_project(uvs, verts, tris, ids, all(36), 0.01).fits);
}

TEST(UvProject, SkipsAClosedShellIslandWhoseNormalsCancelAndNeverWritesNan) {
  const Vec verts{0, 0, 0, 1, 0, 0, 0, 1, 0};
  const Ids tris{0, 1, 2, 0, 2, 1};
  const Ids ids{0, 0, 0};
  const Vec uvs(6, 0.0f);
  const auto info = uv::smart_project(uvs, verts, tris, ids, all(3));
  EXPECT_EQ(info.projected, 0u);
  EXPECT_EQ(info.skipped, 1u);
  EXPECT_EQ(info.uvs, uvs);
  EXPECT_TRUE(uv::island_planes(verts, tris, ids, all(3)).empty());
}

TEST(UvProject, SkipsAnIslandWithNonFiniteVerticesAndStillProjectsTheFiniteOne) {
  const Vec verts{k_nan, 0, 0, 1, 0, 0, 0, 1, 0, 5, 5, 0, 6, 5, 0, 6, 6, 0, 5, 6, 0};
  const Ids tris{0, 1, 2, 4, 5, 6, 4, 6, 7};
  const auto out = uv::smart_project(Vec(8, 0.0f), verts, tris, Ids{0, 0, 0, 1, 1, 1, 1}, all(7)).uvs;
  EXPECT_EQ(slice(out, 0, 6), Vec(6, 0.0f));
  for (const float x : slice(out, 6)) EXPECT_TRUE(std::isfinite(x));
}

TEST(UvProject, IsDeterministicAndIdempotentOnItsOwnOutput) {
  const auto first = uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4)).uvs;
  EXPECT_EQ(uv::smart_project(k_quad_uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4)).uvs, first);
  EXPECT_EQ(uv::smart_project(first, k_quad_verts, k_quad_tris, k_quad_ids, all(4)).uvs, first);
}

TEST(UvProject, NeverWritesToItsInputs) {
  const Vec uvs = k_quad_uvs, verts = k_quad_verts;
  const Ids tris = k_quad_tris, ids = k_quad_ids;
  (void)uv::smart_project(uvs, verts, tris, ids, all(4));
  EXPECT_EQ(uvs, k_quad_uvs);
  EXPECT_EQ(verts, k_quad_verts);
  EXPECT_EQ(tris, k_quad_tris);
  EXPECT_EQ(ids, k_quad_ids);
}

TEST(UvProject, IslandPlanesUseTheAreaWeightedNormalOfTwoNonCoplanarFaces) {
  const Vec verts{0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 1};
  const Ids tris{0, 1, 2, 0, 3, 4};
  const auto planes = uv::island_planes(verts, tris, Ids{0, 0, 0, 0, 0}, all(5));
  ASSERT_EQ(planes.count(0), 1u);
  const auto& u = planes.at(0).u;
  const auto& v = planes.at(0).v;
  const std::array<double, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
  EXPECT_NEAR(n[1], 0, 5e-7);
  EXPECT_NEAR(n[0], 0.124, 5e-4);
  EXPECT_NEAR(n[2], 0.992, 5e-4);
}

TEST(UvUnwrap, MapsAPlanarQuadIsometricallyWithThePinsExactAndTheMeshOrientation) {
  const Pins pins{{0, 0, 0}, {1, 2, 0}};
  const auto out = uv::lscm_unwrap(Vec(8, 0.0f), k_quad_verts, k_quad_tris, k_quad_ids, all(4), pins).uvs;
  EXPECT_NEAR(out[0], 0, 5e-7);
  EXPECT_NEAR(out[1], 0, 5e-7);
  EXPECT_NEAR(out[2], 2, 5e-7);
  EXPECT_NEAR(out[3], 0, 5e-7);
  EXPECT_NEAR(out[4], 2, 5e-5);
  EXPECT_NEAR(out[5], 2, 5e-5);
  EXPECT_NEAR(out[6], 0, 5e-5);
  EXPECT_NEAR(out[7], 2, 5e-5);
  const double cross = (static_cast<double>(out[2]) - out[0]) * (static_cast<double>(out[5]) - out[1]) -
                       (static_cast<double>(out[3]) - out[1]) * (static_cast<double>(out[4]) - out[0]);
  EXPECT_GT(cross, 0);
}

TEST(UvUnwrap, LeavesAnIslandWithFewerThanTwoPinsUntouched) {
  const Vec uvs{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
  const Pins pins{{0, 0, 0}};
  EXPECT_EQ(uv::lscm_unwrap(uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4), pins).uvs, uvs);
}

TEST(UvUnwrap, GivesTheSameResultFromTwoDifferentStartingUvs) {
  const Pins pins{{0, 0, 0}, {3, 0, 2}};
  const auto a = uv::lscm_unwrap(Vec(8, 0.0f), k_quad_verts, k_quad_tris, k_quad_ids, all(4), pins).uvs;
  const auto b = uv::lscm_unwrap(Vec{0.9f, 0.9f, 0.2f, 0.1f, 0.1f, 0.8f, 0.1f, 0.1f}, k_quad_verts, k_quad_tris, k_quad_ids, all(4), pins).uvs;
  for (std::size_t i = 0; i < a.size(); ++i) EXPECT_NEAR(b[i], a[i], 5e-7);
  EXPECT_NEAR(b[0], 0, 5e-7);
  EXPECT_NEAR(b[1], 0, 5e-7);
  EXPECT_NEAR(b[6], 0, 5e-7);
  EXPECT_NEAR(b[7], 2, 5e-7);
}

TEST(UvUnwrap, UnrollsABentDevelopableStripWithEveryEdgeLengthKept) {
  const Vec verts{0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2};
  const Pins pins{{0, 0, 0}, {1, 2, 0}};
  const auto out = uv::lscm_unwrap(Vec(8, 0.0f), verts, Ids{0, 1, 2, 0, 1, 3}, Ids{0, 0, 0, 0}, all(4), pins).uvs;
  for (const auto& e : std::vector<std::array<std::size_t, 2>>{{0, 1}, {1, 2}, {0, 2}, {1, 3}, {0, 3}}) {
    EXPECT_NEAR(dist(out, e[0], e[1]), dist_3d(verts, e[0], e[1]), 5e-4);
  }
}

TEST(UvUnwrap, UnwrapsOnlyTheIslandThatHasPins) {
  const Vec verts = cat(k_quad_verts, {5, 5, 0, 6, 5, 0, 6, 6, 0, 5, 6, 0});
  const Pins pins{{0, 0, 0}, {1, 2, 0}};
  const Vec tail{0.4f, 0.4f, 0.5f, 0.4f, 0.5f, 0.5f, 0.4f, 0.5f};
  const auto info = uv::lscm_unwrap(cat(Vec(8, 0.0f), tail), verts, k_two_tris, k_two_ids, all(8), pins);
  EXPECT_EQ(info.unwrapped, 1u);
  EXPECT_EQ(info.skipped, 1u);
  EXPECT_EQ(slice(info.uvs, 8), tail);
}

TEST(UvUnwrap, RefusesPinsThatSitAtTheSameUvInsteadOfCollapsingTheIsland) {
  const Vec uvs(8, 0.5f);
  const Pins pins{{0, 0.5, 0.5}, {2, 0.5, 0.5}};
  const auto info = uv::lscm_unwrap(uvs, k_quad_verts, k_quad_tris, k_quad_ids, all(4), pins);
  EXPECT_EQ(info.unwrapped, 0u);
  EXPECT_EQ(info.skipped, 1u);
  EXPECT_EQ(info.uvs, uvs);
}

TEST(UvUnwrap, IsDeterministicAndNeverWritesToItsInputs) {
  const Vec uvs(8, 0.0f), verts = k_quad_verts;
  const Ids tris = k_quad_tris, ids = k_quad_ids;
  const Pins pins{{0, 0, 0}, {2, 1, 1}};
  const auto first = uv::lscm_unwrap(uvs, verts, tris, ids, all(4), pins).uvs;
  EXPECT_EQ(uv::lscm_unwrap(uvs, verts, tris, ids, all(4), pins).uvs, first);
  EXPECT_EQ(uvs, Vec(8, 0.0f));
  EXPECT_EQ(verts, k_quad_verts);
  EXPECT_EQ(tris, k_quad_tris);
  EXPECT_EQ(ids, k_quad_ids);
}

TEST(UvUnwrap, AcceptsAWellFormedPinnedIslandAsConvergedInsteadOfSkippingIt) {
  const Pins pins{{0, 0, 0}, {1, 2, 0}};
  const auto info = uv::lscm_unwrap(Vec(8, 0.0f), k_quad_verts, k_quad_tris, k_quad_ids, all(4), pins);
  EXPECT_EQ(info.skipped, 0u);
  EXPECT_EQ(info.unwrapped, 1u);
  EXPECT_NEAR(info.uvs[2], 2, 5e-6);
  EXPECT_NEAR(info.uvs[3], 0, 5e-6);
}

TEST(UvUnwrap, AcceptsTheAllZeroSolutionWhenThePinsPutTheFreeUvAtTheOrigin) {
  const Vec verts{0, 0, 0, 1, 0, 0, 0, 1, 0};
  const Pins pins{{0, 0, -1}, {1, 1, -1}};
  const auto info = uv::lscm_unwrap(Vec{0, 0, 0, 0, 0.5f, 0.5f}, verts, Ids{0, 1, 2}, Ids{0, 0, 0}, all(3), pins);
  EXPECT_EQ(info.unwrapped, 1u);
  EXPECT_EQ(info.skipped, 0u);
  expect_near(info.uvs, {0, -1, 1, -1, 0, 0}, 5e-7);
}

TEST(UvUnwrap, LeavesAPatchUnchangedWhenItJoinsThePinnedPatchOnlyThroughACollinearFace) {
  const Vec verts{0, 0, 0, 2, 0, 0, 0, 2, 0, 4, 0, 0, 6, 0, 0, 4, 2, 0, 3, 0, 0};
  const Ids tris{0, 1, 2, 3, 4, 5, 1, 6, 3};
  const Pins pins{{0, 0, 0}, {1, 2, 0}};
  const Vec start{0, 0, 0, 0, 0, 0, 0.4f, 0.4f, 0.5f, 0.4f, 0.4f, 0.5f, 0.6f, 0.6f};
  const auto info = uv::lscm_unwrap(start, verts, tris, uv::islands(7, tris), all(7), pins);
  EXPECT_EQ(info.unwrapped, 1u);
  EXPECT_EQ(info.skipped, 1u);
  EXPECT_EQ(slice(info.uvs, 6), slice(start, 6));
  EXPECT_NEAR(dist(info.uvs, 0, 2), dist_3d(verts, 0, 2), 5e-5);
}
