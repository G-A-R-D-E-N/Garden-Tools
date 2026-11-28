#include "fo4/c_api_nif_mesh.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using Vec = std::vector<float>;
using Ids = std::vector<uint32_t>;

struct Floats {
  float* data{nullptr};
  size_t count{0};
  Floats() = default;
  Floats(const Floats&) = delete;
  Floats& operator=(const Floats&) = delete;
  ~Floats() { radicle_free_floats(data); }
  Vec values() const { return Vec(data, data + count); }
};

struct Indices {
  uint32_t* data{nullptr};
  size_t count{0};
  Indices() = default;
  Indices(const Indices&) = delete;
  Indices& operator=(const Indices&) = delete;
  ~Indices() { radicle_free_node_refs(data); }
  Ids values() const { return Ids(data, data + count); }
};

const Ids k_fan_tris{0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};
const Vec k_fan_uvs{0.2f, 0.2f, 0, 0, 1, 0, 1, 1, 0, 1, 0.9f, 0.9f};
const Vec k_hide_uvs{0.1f, 0.1f, 0.3f, 0.1f, 0.1f, 0.3f, 0.2f, 0.2f, 0.4f, 0.4f};
const Ids k_hide_tris{0, 1, 2, 2, 3, 4};
const Ids k_hidden1{1};
const Vec k_edge_uvs{0, 0, 1, 0, 0, 1, 0.4f, 0.12f, 0.6f, 0.12f, 0.5f, 0.2f};
const Ids k_edge_tris{0, 1, 2, 3, 4, 5};
const Vec k_overlap_uvs{0, 0, 1, 0, 0, 1, 0.1f, 0.1f, 0.9f, 0.1f, 0.1f, 0.9f};
const Ids k_strip_tris{0, 1, 2, 0, 2, 3, 1, 4, 5, 1, 5, 2};

}  // namespace

TEST(CApiNifUvEdit, BoundaryVertsKeepsTheFanCentreInterior) {
  Indices out;
  ASSERT_EQ(radicle_uv_boundary_verts(5, k_fan_tris.data(), k_fan_tris.size(), &out.data, &out.count), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(out.values(), (Ids{1, 2, 3, 4}));
}

TEST(CApiNifUvEdit, RelaxMovesOnlyTheSelectedInteriorVertex) {
  const Ids sel{0, 1, 5};
  Floats out;
  ASSERT_EQ(radicle_uv_relax(k_fan_uvs.data(), 6, k_fan_tris.data(), k_fan_tris.size(), sel.data(), sel.size(), 1, &out.data, &out.count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(out.values(), (Vec{0.5f, 0.5f, 0, 0, 1, 0, 1, 1, 0, 1, 0.9f, 0.9f}));
}

TEST(CApiNifUvEdit, FindStitchPairsAnswersFlatPairsCountedInPairs) {
  const Vec verts{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, -1, 0};
  const Ids ids{0, 0, 0, 1, 1, 1};
  const Ids sel{0, 1, 2, 3, 4, 5};
  Indices out;
  ASSERT_EQ(radicle_uv_find_stitch_pairs(verts.data(), 6, ids.data(), sel.data(), sel.size(), 1e-6, &out.data, &out.count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(out.count, 2u);
  EXPECT_EQ(std::vector<uint32_t>(out.data, out.data + 2 * out.count), (Ids{0, 4, 1, 3}));
}

TEST(CApiNifUvEdit, StitchMovesTheMobileSquareOntoItsNeighbour) {
  const Vec uvs{0, 0, 1, 0, 1, 1, 0, 1, 2, 2, 2, 3, 1, 3, 1, 2, 5, 5, 6, 5, 5, 6};
  const Vec verts{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, -1, 0, 0, -1, 0, 10, 10, 0, 11, 10, 0, 10, 11, 0};
  const Ids ids{0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2};
  Ids sel(11);
  for (uint32_t i = 0; i < 11; ++i) sel[i] = i;
  Floats out;
  ASSERT_EQ(radicle_uv_stitch(uvs.data(), verts.data(), 11, ids.data(), sel.data(), sel.size(), 1e-6, &out.data, &out.count), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::vector<double> expected{0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 5, 5, 6, 5, 5, 6};
  ASSERT_EQ(out.count, expected.size());
  for (size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(out.data[i], expected[i], 5e-7) << "component " << i;
}

TEST(CApiNifUvEdit, OverlappingFacesReportsTwoStackedTrianglesAndHonoursHidden) {
  const Vec uvs{0, 0, 1, 0, 0, 1};
  const Ids tris{0, 1, 2, 0, 1, 2};
  Indices out;
  ASSERT_EQ(radicle_uv_overlapping_faces(uvs.data(), 3, tris.data(), tris.size(), nullptr, 0, &out.data, &out.count), RADICLE_STATUS_OK);
  EXPECT_EQ(out.values(), (Ids{0, 1}));
  Indices none;
  const Ids hidden{1};
  ASSERT_EQ(radicle_uv_overlapping_faces(uvs.data(), 3, tris.data(), tris.size(), hidden.data(), hidden.size(), &none.data, &none.count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(none.count, 0u);
  EXPECT_EQ(none.data, nullptr);
}

TEST(CApiNifUvEdit, VisibleVertsDropsVerticesOnlyAHiddenTriangleUses) {
  Indices out;
  ASSERT_EQ(radicle_uv_visible_verts(6, k_hide_tris.data(), k_hide_tris.size(), k_hidden1.data(), k_hidden1.size(), &out.data, &out.count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(out.values(), (Ids{0, 1, 2, 5}));
}

TEST(CApiNifUvEdit, PickEdgeAnswersTheNearestEdgeOrNotFound) {
  uint32_t a = 9, b = 9;
  uint8_t found = 7;
  ASSERT_EQ(radicle_uv_pick_edge(k_edge_uvs.data(), 6, k_edge_tris.data(), k_edge_tris.size(), nullptr, 0, 0.5, 0.1, 0.5, &a, &b, &found),
            RADICLE_STATUS_OK);
  EXPECT_EQ(found, 1);
  EXPECT_EQ(a, 3u);
  EXPECT_EQ(b, 4u);
  ASSERT_EQ(radicle_uv_pick_edge(k_edge_uvs.data(), 6, k_edge_tris.data(), k_edge_tris.size(), nullptr, 0, 0.5, 0.1, 0.01, &a, &b, &found),
            RADICLE_STATUS_OK);
  EXPECT_EQ(found, 0);
  EXPECT_EQ(a, 0u);
  EXPECT_EQ(b, 0u);
}

TEST(CApiNifUvEdit, PickFaceAnswersTheTopmostVisibleTriangle) {
  uint32_t corners[3]{};
  uint8_t found = 0;
  ASSERT_EQ(radicle_uv_pick_face(k_overlap_uvs.data(), 6, k_edge_tris.data(), k_edge_tris.size(), nullptr, 0, 0.2, 0.2, corners, &found),
            RADICLE_STATUS_OK);
  EXPECT_EQ(found, 1);
  EXPECT_EQ((Ids{corners[0], corners[1], corners[2]}), (Ids{3, 4, 5}));
  ASSERT_EQ(radicle_uv_pick_face(k_overlap_uvs.data(), 6, k_edge_tris.data(), k_edge_tris.size(), k_hidden1.data(), k_hidden1.size(), 0.2, 0.2,
                                 corners, &found),
            RADICLE_STATUS_OK);
  EXPECT_EQ((Ids{corners[0], corners[1], corners[2]}), (Ids{0, 1, 2}));
  ASSERT_EQ(radicle_uv_pick_face(k_overlap_uvs.data(), 6, k_edge_tris.data(), k_edge_tris.size(), nullptr, 0, 5, 5, corners, &found),
            RADICLE_STATUS_OK);
  EXPECT_EQ(found, 0);
}

TEST(CApiNifUvEdit, BoxVertsFollowsTheModeAndRefusesAnUnknownOne) {
  Indices out;
  ASSERT_EQ(radicle_uv_box_verts(k_hide_uvs.data(), 5, k_hide_tris.data(), k_hide_tris.size(), k_hidden1.data(), k_hidden1.size(), 0, 0, 0.5, 0.5, 0,
                                 &out.data, &out.count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(out.values(), (Ids{0, 1, 2}));
  const Vec uvs{0.1f, 0.1f, 0.15f, 0.1f, 0.1f, 0.5f};
  const Ids tri{0, 1, 2};
  Indices edge;
  ASSERT_EQ(radicle_uv_box_verts(uvs.data(), 3, tri.data(), tri.size(), nullptr, 0, 0, 0, 0.2, 0.2, 1, &edge.data, &edge.count), RADICLE_STATUS_OK);
  EXPECT_EQ(edge.values(), (Ids{0, 1}));
  Indices face;
  ASSERT_EQ(radicle_uv_box_verts(uvs.data(), 3, tri.data(), tri.size(), nullptr, 0, 0, 0, 0.2, 0.2, 2, &face.data, &face.count), RADICLE_STATUS_OK);
  EXPECT_EQ(face.count, 0u);
  Indices bad;
  EXPECT_EQ(radicle_uv_box_verts(uvs.data(), 3, tri.data(), tri.size(), nullptr, 0, 0, 0, 0.2, 0.2, 3, &bad.data, &bad.count),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiNifUvEdit, InvertLinkedAndSelectedTrisAnswerVertexAndFaceLists) {
  const Ids visible{0, 1, 2, 5};
  const Ids one{0};
  Indices inverted;
  ASSERT_EQ(radicle_uv_invert(visible.data(), visible.size(), one.data(), one.size(), &inverted.data, &inverted.count), RADICLE_STATUS_OK);
  EXPECT_EQ(inverted.values(), (Ids{1, 2, 5}));

  const Ids ids{0, 0, 0, 1, 1, 1};
  const Ids picked{1};
  Indices linked;
  ASSERT_EQ(radicle_uv_linked(ids.data(), ids.size(), picked.data(), picked.size(), &linked.data, &linked.count), RADICLE_STATUS_OK);
  EXPECT_EQ(linked.values(), (Ids{0, 1, 2}));

  const Ids sel{0, 1, 2, 3};
  const Ids tris{0, 1, 2, 2, 3, 4};
  Indices faces;
  ASSERT_EQ(radicle_uv_selected_tris(tris.data(), tris.size(), 5, sel.data(), sel.size(), nullptr, 0, &faces.data, &faces.count), RADICLE_STATUS_OK);
  EXPECT_EQ(faces.values(), (Ids{0}));
}

TEST(CApiNifUvEdit, MergeAtCentreAndByDistanceWeldSelectedVertices) {
  const Vec centre{0.25f, 0.25f, 0.75f, 0.25f, 0.5f, 0.75f};
  const Ids two{0, 1};
  Floats at_centre;
  ASSERT_EQ(radicle_uv_merge_at_centre(centre.data(), 3, two.data(), two.size(), &at_centre.data, &at_centre.count), RADICLE_STATUS_OK);
  EXPECT_EQ(at_centre.values(), (Vec{0.5f, 0.25f, 0.5f, 0.25f, 0.5f, 0.75f}));

  const Vec near{0.5f, 0.5f, 0.75f, 0.5f, 0.9f, 0.9f, 0.625f, 0.5f};
  const Ids three{0, 1, 2};
  Floats by_distance;
  ASSERT_EQ(radicle_uv_merge_by_distance(near.data(), 4, three.data(), three.size(), 0.25, &by_distance.data, &by_distance.count), RADICLE_STATUS_OK);
  EXPECT_EQ(by_distance.values(), (Vec{0.625f, 0.5f, 0.625f, 0.5f, 0.9f, 0.9f, 0.625f, 0.5f}));
}

TEST(CApiNifUvEdit, AlignFollowsAxisAndTargetAndRefusesUnknownCodes) {
  const Vec uvs{0.25f, 0.5f, 0.75f, 0.25f, 0.5f, 0.75f};
  const Ids sel{0, 2};
  Floats centre;
  ASSERT_EQ(radicle_uv_align(uvs.data(), 3, sel.data(), sel.size(), 0, 1, &centre.data, &centre.count), RADICLE_STATUS_OK);
  EXPECT_EQ(centre.values(), (Vec{0.375f, 0.5f, 0.75f, 0.25f, 0.375f, 0.75f}));
  Floats bad;
  EXPECT_EQ(radicle_uv_align(uvs.data(), 3, sel.data(), sel.size(), 2, 1, &bad.data, &bad.count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_uv_align(uvs.data(), 3, sel.data(), sel.size(), 0, 3, &bad.data, &bad.count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bad.count, 0u);
}

TEST(CApiNifUvEdit, SnapGridSnapPixelsAndClampTileRewriteOnlySelectedVertices) {
  const Vec uvs{0.3f, 0.6f, 0.3f, 0.6f};
  const Ids one{0};
  Floats grid;
  ASSERT_EQ(radicle_uv_snap_grid(uvs.data(), 2, one.data(), one.size(), 0.25, &grid.data, &grid.count), RADICLE_STATUS_OK);
  EXPECT_EQ(grid.values(), (Vec{0.25f, 0.5f, 0.3f, 0.6f}));
  Floats pixels;
  ASSERT_EQ(radicle_uv_snap_pixels(uvs.data(), 2, one.data(), one.size(), 4, 4, 1, &pixels.data, &pixels.count), RADICLE_STATUS_OK);
  EXPECT_EQ(pixels.values(), (Vec{0.375f, 0.625f, 0.3f, 0.6f}));

  const Vec wide{-0.2f, 0.5f, 1.4f, 1.2f, 0.3f, 0.7f, 1.5f, -0.1f};
  const Ids three{0, 1, 2};
  Floats clamped;
  ASSERT_EQ(radicle_uv_clamp_tile(wide.data(), 4, three.data(), three.size(), &clamped.data, &clamped.count), RADICLE_STATUS_OK);
  EXPECT_EQ(clamped.values(), (Vec{0, 0.5f, 1, 1, 0.3f, 0.7f, 1.5f, -0.1f}));
}

TEST(CApiNifUvEdit, SeamsInSelectionAnswersFlatPairsAndSplitPlanDuplicatesTheSeamVertices) {
  const Ids sel{1, 2};
  Indices seams;
  ASSERT_EQ(radicle_uv_seams_in_selection(k_strip_tris.data(), k_strip_tris.size(), sel.data(), sel.size(), nullptr, 0, &seams.data, &seams.count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(seams.count, 1u);
  EXPECT_EQ(std::vector<uint32_t>(seams.data, seams.data + 2), (Ids{1, 2}));

  Indices sources, tris;
  ASSERT_EQ(radicle_uv_split_plan(6, k_strip_tris.data(), k_strip_tris.size(), seams.data, seams.count, &sources.data, &sources.count, &tris.data,
                                  &tris.count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(sources.values(), (Ids{1, 2}));
  EXPECT_EQ(tris.values(), (Ids{0, 1, 2, 0, 2, 3, 6, 4, 5, 6, 5, 7}));
}

TEST(CApiNifUvEdit, SplitPlanWithNoSeamsLeavesTheTrianglesAlone) {
  Indices sources, tris;
  ASSERT_EQ(radicle_uv_split_plan(6, k_strip_tris.data(), k_strip_tris.size(), nullptr, 0, &sources.data, &sources.count, &tris.data, &tris.count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(sources.count, 0u);
  EXPECT_EQ(tris.values(), k_strip_tris);
}

TEST(CApiNifUvEdit, MissingArraysAndOutputsAreRefused) {
  Indices out;
  EXPECT_EQ(radicle_uv_boundary_verts(5, nullptr, 3, &out.data, &out.count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_uv_boundary_verts(5, k_fan_tris.data(), k_fan_tris.size(), nullptr, &out.count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_uv_split_plan(6, k_strip_tris.data(), k_strip_tris.size(), nullptr, 1, &out.data, &out.count, &out.data, &out.count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  uint8_t found = 7;
  uint32_t a = 1, b = 1;
  EXPECT_EQ(radicle_uv_pick_edge(nullptr, 3, k_edge_tris.data(), k_edge_tris.size(), nullptr, 0, 0, 0, 1, &a, &b, &found),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(found, 0);
}
