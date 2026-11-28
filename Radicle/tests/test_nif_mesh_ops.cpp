#include "fo4/nif/model_ops.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shape_join.hpp"
#include "fo4/nif/shape_model_ops.hpp"
#include "../src/nif/import_mesh.hpp"
#include "../src/nif/shape_ops_internal.hpp"
#include "../src/nif/write_model.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace nif_build;

namespace {

using fo4::nif::Transform;
using fo4::nif::read_nif;
using fo4::nif::read_shape;
using fo4::nif::read_shapes;

fo4::nif::import_detail::Mesh quad_mesh() {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
  mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
  mesh.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  mesh.tris = {0, 1, 2, 0, 2, 3};
  return mesh;
}

bool has_position(const fo4::nif::import_detail::Mesh& mesh, const std::array<float, 3>& want) {
  for (const auto& got : mesh.positions) {
    if (std::fabs(got[0] - want[0]) + std::fabs(got[1] - want[1]) + std::fabs(got[2] - want[2]) < 1e-5f) return true;
  }
  return false;
}

double surface_area(const fo4::nif::import_detail::Mesh& mesh) {
  double total = 0.0;
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    const auto& a = mesh.positions[mesh.tris[t]];
    const auto& b = mesh.positions[mesh.tris[t + 1]];
    const auto& c = mesh.positions[mesh.tris[t + 2]];
    const std::array<double, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const std::array<double, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const std::array<double, 3> cross{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                      ab[0] * ac[1] - ab[1] * ac[0]};
    total += 0.5 * std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
  }
  return total;
}

double signed_volume(const fo4::nif::import_detail::Mesh& mesh) {
  double total = 0.0;
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    const auto& a = mesh.positions[mesh.tris[t]];
    const auto& b = mesh.positions[mesh.tris[t + 1]];
    const auto& c = mesh.positions[mesh.tris[t + 2]];
    total += (static_cast<double>(a[0]) * (static_cast<double>(b[1]) * c[2] - static_cast<double>(b[2]) * c[1]) +
              static_cast<double>(a[1]) * (static_cast<double>(b[2]) * c[0] - static_cast<double>(b[0]) * c[2]) +
              static_cast<double>(a[2]) * (static_cast<double>(b[0]) * c[1] - static_cast<double>(b[1]) * c[0])) /
             6.0;
  }
  return total;
}

std::size_t edge_users(const fo4::nif::import_detail::Mesh& mesh, std::uint32_t a, std::uint32_t b) {
  std::size_t users = 0;
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    for (std::size_t k = 0; k < 3; ++k) {
      const auto x = mesh.tris[t + k];
      const auto y = mesh.tris[t + (k + 1) % 3];
      if ((x == a && y == b) || (x == b && y == a)) ++users;
    }
  }
  return users;
}

bool any_degenerate(const fo4::nif::import_detail::Mesh& mesh) {
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    const auto& a = mesh.positions[mesh.tris[t]];
    const auto& b = mesh.positions[mesh.tris[t + 1]];
    const auto& c = mesh.positions[mesh.tris[t + 2]];
    const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const std::array<float, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const std::array<float, 3> cross{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                     ab[0] * ac[1] - ab[1] * ac[0]};
    if (cross[0] == 0.0f && cross[1] == 0.0f && cross[2] == 0.0f) return true;
  }
  return false;
}

std::vector<std::byte> skinned_tri(float x0, std::uint8_t bone) {
  nif_build::Writer w;
  const float xs[3] = {x0, x0 + 1.0f, x0};
  const float ys[3] = {0.0f, 0.0f, 1.0f};
  for (int i = 0; i < 3; ++i) {
    w.f32(xs[i]);
    w.f32(ys[i]);
    w.f32(0.0f);
    w.f32(1.0f);
    w.u16(0x3C00);
    w.u16(0);
    w.u16(0);
    w.u16(0);
    w.u8(bone);
    w.u8(0);
    w.u8(0);
    w.u8(0);
  }
  return w.bytes();
}

}  // namespace

// Invariant: the cap rides the face normal at the full distance with one outward wall per boundary edge.
TEST(ModelOps, ExtrudeLiftsTheCapAlongTheFaceNormal) {
  auto mesh = quad_mesh();
  EXPECT_FALSE(fo4::nif::model_detail::extrude_faces(mesh, {0, 1}, 2.0f).has_value());
  ASSERT_EQ(mesh.positions.size(), 8U);
  EXPECT_EQ(mesh.tris.size(), 30U);
  std::size_t top = 0;
  std::size_t bottom = 0;
  for (const auto& position : mesh.positions) {
    if (position[2] == 2.0f) ++top;
    if (position[2] == 0.0f) ++bottom;
  }
  EXPECT_EQ(top, 4U);
  EXPECT_EQ(bottom, 4U);
  EXPECT_FALSE(any_degenerate(mesh));
  // Invariant: an outward-wound prism encloses its own volume, so inward walls would flip the sign.
  EXPECT_NEAR(signed_volume(mesh), 2.0, 1e-4);
}

// Constraint: an empty extrude selection is a refusal.
TEST(ModelOps, ExtrudeRefusesAnEmptySelection) {
  auto mesh = quad_mesh();
  EXPECT_TRUE(fo4::nif::model_detail::extrude_faces(mesh, {}, 1.0f).has_value());
  EXPECT_EQ(mesh.positions.size(), 4U);
  EXPECT_EQ(mesh.tris.size(), 6U);
}

// Invariant: the band follows the loop order and wraps around with no dropped quads.
TEST(ModelOps, BridgeJoinsTwoLoopsWithAWrappedBand) {
  auto mesh = quad_mesh();
  mesh.positions.insert(mesh.positions.end(),
                        {{3.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 0.0f}, {3.0f, 1.0f, 0.0f}});
  mesh.normals.insert(mesh.normals.end(), 4, {0.0f, 0.0f, 1.0f});
  mesh.uvs.insert(mesh.uvs.end(), 4, {0.0f, 0.0f});
  mesh.tris.insert(mesh.tris.end(), {4, 5, 6, 4, 6, 7});
  EXPECT_FALSE(fo4::nif::model_detail::bridge_loops(mesh, {0, 1, 2, 3}, {4, 5, 6, 7}).has_value());
  EXPECT_EQ(mesh.positions.size(), 8U);
  EXPECT_EQ(mesh.tris.size(), 36U);
}

// Constraint: loops of different lengths are a refusal.
TEST(ModelOps, BridgeRefusesMismatchedLoops) {
  auto mesh = quad_mesh();
  EXPECT_TRUE(fo4::nif::model_detail::bridge_loops(mesh, {0, 1, 2}, {0, 1, 2, 3}).has_value());
  EXPECT_EQ(mesh.tris.size(), 6U);
}

// Invariant: crossing triangles are split and no piece is dropped.
TEST(ModelOps, KnifeSplitsACrossingTriangle) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}};
  mesh.tris = {0, 1, 2};
  fo4::nif::model_detail::KnifeResult result;
  EXPECT_FALSE(fo4::nif::model_detail::knife_plane(mesh, {1.0f, 0.0f, 0.0f}, 1.0f, &result).has_value());
  EXPECT_GT(result.triangles_added, 0U);
  EXPECT_FALSE(mesh.tris.empty());
  for (const auto& position : mesh.positions) {
    EXPECT_GE(position[0], -1e-5f);
    EXPECT_LE(position[0], 2.0f + 1e-5f);
  }
}

// Regression: the knife was the one mesh op with no vertex ceiling, so cutting a dense mesh produced more
// vertices than a BSTriShape's 16-bit indices can name. It must refuse and leave the mesh as it was.
TEST(ModelOps, KnifeRefusesACutPastTheVertexLimitAndLeavesTheMeshUntouched) {
  fo4::nif::import_detail::Mesh mesh;
  constexpr std::uint32_t k_tris = 21845;
  for (std::uint32_t t = 0; t < k_tris; ++t) {
    const float y = static_cast<float>(t);
    mesh.positions.push_back({0.0f, y, 0.0f});
    mesh.positions.push_back({2.0f, y, 0.0f});
    mesh.positions.push_back({0.0f, y, 1.0f});
    mesh.tris.insert(mesh.tris.end(), {3 * t, 3 * t + 1, 3 * t + 2});
  }
  const auto before = mesh;
  const auto refusal = fo4::nif::model_detail::knife_plane(mesh, {1.0f, 0.0f, 0.0f}, 1.0f);
  ASSERT_TRUE(refusal.has_value());
  EXPECT_NE(refusal->find("65535"), std::string::npos);
  EXPECT_EQ(mesh.positions, before.positions);
  EXPECT_EQ(mesh.tris, before.tris);
}

// Constraint: a zero normal is a refusal.
TEST(ModelOps, KnifeRefusesAZeroNormal) {
  auto mesh = quad_mesh();
  EXPECT_TRUE(fo4::nif::model_detail::knife_plane(mesh, {0.0f, 0.0f, 0.0f}, 0.0f).has_value());
}

// Invariant: a split vertex carries each bone's blended weight, whatever slot order the ends use.
TEST(ModelOps, KnifeBlendsSkinWeightsByBone) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
  mesh.tris = {0, 1, 2};
  mesh.weights = {{0.75f, 0.25f, 0.0f, 0.0f}, {0.25f, 0.75f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}};
  mesh.bones = {{0, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}};
  EXPECT_FALSE(fo4::nif::model_detail::knife_plane(mesh, {1.0f, 0.0f, 0.0f}, 1.0f).has_value());
  ASSERT_EQ(mesh.weights.size(), 5U);
  std::size_t split = mesh.positions.size();
  for (std::size_t v = 3; v < mesh.positions.size(); ++v) {
    if (std::fabs(mesh.positions[v][0] - 1.0f) < 1e-5f && std::fabs(mesh.positions[v][1]) < 1e-5f) split = v;
  }
  ASSERT_LT(split, mesh.positions.size());
  EXPECT_EQ(mesh.bones[split][0], 0);
  EXPECT_FLOAT_EQ(mesh.weights[split][0], 0.75f);
  EXPECT_EQ(mesh.bones[split][1], 1);
  EXPECT_FLOAT_EQ(mesh.weights[split][1], 0.25f);
}

// Invariant: every ring edge splits at its midpoint and every crossed triangle keeps its area.
TEST(ModelOps, CutLoopSplitsARingOfEdgesAtTheirMidpoints) {
  auto mesh = quad_mesh();
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> ring{{0, 1}, {1, 2}, {2, 3}, {3, 0}};
  EXPECT_FALSE(fo4::nif::model_detail::cut_loop(mesh, ring).has_value());
  ASSERT_EQ(mesh.positions.size(), 8U);
  EXPECT_EQ(mesh.tris.size(), 18U);
  EXPECT_TRUE(has_position(mesh, {0.5f, 0.0f, 0.0f}));
  EXPECT_TRUE(has_position(mesh, {1.0f, 0.5f, 0.0f}));
  EXPECT_TRUE(has_position(mesh, {0.5f, 1.0f, 0.0f}));
  EXPECT_TRUE(has_position(mesh, {0.0f, 0.5f, 0.0f}));
  EXPECT_FALSE(any_degenerate(mesh));
  EXPECT_NEAR(surface_area(mesh), 1.0, 1e-5);
  // Invariant: the shared diagonal still joins both split halves, so the surface stays closed.
  EXPECT_EQ(edge_users(mesh, 0, 2), 2U);
}

// Constraint: an open ring or a non-edge is a refusal.
TEST(ModelOps, CutLoopRefusesAnOpenRing) {
  auto mesh = quad_mesh();
  EXPECT_TRUE(fo4::nif::model_detail::cut_loop(mesh, {{0, 1}, {1, 2}}).has_value());
  EXPECT_TRUE(fo4::nif::model_detail::cut_loop(mesh, {{0, 1}, {1, 3}, {3, 0}}).has_value());
  EXPECT_EQ(mesh.positions.size(), 4U);
  EXPECT_EQ(mesh.tris.size(), 6U);
}

// Invariant: a ring the cut cannot triangulate leaves the mesh exactly as it was.
TEST(ModelOps, CutLoopLeavesTheMeshUntouchedWhenItRefuses) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}};
  mesh.tris = {0, 1, 2};
  const std::vector<std::pair<std::uint32_t, std::uint32_t>> ring{{0, 1}, {1, 2}, {2, 0}};
  EXPECT_TRUE(fo4::nif::model_detail::cut_loop(mesh, ring).has_value());
  EXPECT_EQ(mesh.positions.size(), 3U);
  EXPECT_EQ(mesh.tris.size(), 3U);
}

// Invariant: the chamfer replaces the edge with a strip and keeps the surface area.
TEST(ModelOps, BevelChamfersASelectedEdge) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}};
  mesh.tris = {0, 1, 2};
  const auto before = surface_area(mesh);
  EXPECT_FALSE(fo4::nif::model_detail::bevel_edges(mesh, {{0, 1}}, 0.1f).has_value());
  EXPECT_EQ(mesh.positions.size(), 5U);
  EXPECT_EQ(mesh.tris.size(), 9U);
  EXPECT_FALSE(any_degenerate(mesh));
  // Invariant: the strip and the trimmed face share the inset edge, so the chamfer leaves no crack.
  EXPECT_EQ(edge_users(mesh, 3, 4), 2U);
  EXPECT_NEAR(surface_area(mesh), before, 1e-5);
}

// Invariant: a two-faced edge gains the chamfer quad between both faces.
TEST(ModelOps, BevelClosesATwoFacedEdgeWithAChamferQuad) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}};
  mesh.tris = {0, 1, 2, 1, 0, 3};
  const auto before = surface_area(mesh);
  EXPECT_FALSE(fo4::nif::model_detail::bevel_edges(mesh, {{0, 1}}, 0.1f).has_value());
  EXPECT_EQ(mesh.positions.size(), 8U);
  EXPECT_EQ(mesh.tris.size(), 18U);
  EXPECT_FALSE(any_degenerate(mesh));
  EXPECT_NEAR(surface_area(mesh), before, 1e-5);
  // Invariant: the inset edge is shared by the trimmed face and the chamfer quad.
  EXPECT_EQ(edge_users(mesh, 4, 5), 2U);
  EXPECT_EQ(edge_users(mesh, 6, 7), 2U);
}

// Constraint: a zero bevel width is a refusal.
TEST(ModelOps, BevelRefusesAZeroWidth) {
  auto mesh = quad_mesh();
  EXPECT_TRUE(fo4::nif::model_detail::bevel_edges(mesh, {{0, 1}}, 0.0f).has_value());
}

// Constraint: edges that share a vertex are refused, and the mesh is left untouched.
TEST(ModelOps, BevelRefusesEdgesSharingAVertex) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}};
  mesh.tris = {0, 1, 2};
  EXPECT_TRUE(fo4::nif::model_detail::bevel_edges(mesh, {{0, 1}, {1, 2}}, 0.1f).has_value());
  EXPECT_EQ(mesh.positions.size(), 3U);
  EXPECT_EQ(mesh.tris.size(), 3U);
}

// Invariant: rotation applies about the pivot, not the origin.
TEST(ModelOps, ModalTransformRotatesAboutThePivot) {
  fo4::nif::import_detail::Mesh mesh;
  mesh.positions = {{2.0f, 0.0f, 0.0f}};
  fo4::nif::model_detail::ModalTransform xf;
  xf.rotation = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
  xf.pivot = {1.0f, 0.0f, 0.0f};
  EXPECT_FALSE(fo4::nif::model_detail::transform_about_pivot(mesh, xf).has_value());
  ASSERT_EQ(mesh.positions.size(), 1U);
  EXPECT_NEAR(mesh.positions[0][0], 1.0f, 1e-5f);
  EXPECT_NEAR(mesh.positions[0][1], 1.0f, 1e-5f);
}

// Constraint: a zero scale axis is a refusal.
TEST(ModelOps, ModalTransformRefusesAZeroScale) {
  auto mesh = quad_mesh();
  fo4::nif::model_detail::ModalTransform xf;
  xf.scale = {0.0f, 1.0f, 1.0f};
  EXPECT_TRUE(fo4::nif::model_detail::transform_about_pivot(mesh, xf).has_value());
  EXPECT_EQ(mesh.positions.size(), 4U);
}

// Invariant: donor triangles append in order and the donor block is removed.
TEST(ShapeJoin, JoinConcatenatesTrianglesIntoTheFirstBlock) {
  const auto verts = triangle_vertices();
  const auto file =
      build_nif({"node", "mat"},
                {node_block("NiNode", Transform{}, {1, 2}),
                 shape_with_refs("BSTriShape", 0, Transform{}, nif_build::k_full, 16, -1, 3, -1, verts, {0, 1, 2}),
                 shape_with_refs("BSTriShape", 0, Transform{}, nif_build::k_full, 16, -1, 3, -1, verts, {0, 1, 2}),
                 lighting_shader_block(1, -1)},
                {0});
  fo4::nif::JoinRequest request;
  request.blocks = {1, 2};
  fo4::nif::JoinReport report;
  const auto joined = fo4::nif::join_shapes(read_nif(file), request, &report);
  EXPECT_EQ(report.target, 1U);
  const auto check = read_nif(joined);
  const auto shapes = read_shapes(check);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].tris, (std::vector<std::uint16_t>{0, 1, 2, 3, 4, 5}));
  EXPECT_EQ(shapes[0].verts.size(), 18U);
  EXPECT_EQ(report.vertices_before, 3U);
  EXPECT_EQ(report.triangles_before, 1U);
  EXPECT_EQ(report.vertices_after, 6U);
  EXPECT_EQ(report.triangles_after, 2U);
}

// Constraint: incompatible vertex layouts are a refusal.
TEST(ShapeJoin, JoinRefusesDifferentVertexLayouts) {
  const std::uint32_t uv_flags = nif_build::k_full | nif_build::k_has_uv;
  auto uv_verts = nif_build::encode_vertex(true, uv_flags, nif_build::k_verts[0]);
  const auto second = nif_build::encode_vertex(true, uv_flags, nif_build::k_verts[1]);
  uv_verts.insert(uv_verts.end(), second.begin(), second.end());
  const auto third = nif_build::encode_vertex(true, uv_flags, nif_build::k_verts[0]);
  uv_verts.insert(uv_verts.end(), third.begin(), third.end());
  const auto file = nif_build::build_nif(
      {"node"},
      {nif_build::node_block("NiNode", Transform{}, {1, 2}),
       nif_build::shape_block("BSTriShape", 0, Transform{}, nif_build::k_full, 16, triangle_vertices(), {0, 1, 2}),
       nif_build::shape_block("BSTriShape", 0, Transform{}, uv_flags, 20, uv_verts, {0, 1, 2})},
      {0});
  fo4::nif::JoinRequest request;
  request.blocks = {1, 2};
  EXPECT_THROW(fo4::nif::join_shapes(read_nif(file), request), std::invalid_argument);
}

// Invariant: copies keep disjoint verts and full face coverage.
TEST(ShapeJoin, SeparateSplitsFacesIntoAnAppendedCopy) {
  const auto one = triangle_vertices();
  auto verts = one;
  verts.insert(verts.end(), one.begin(), one.end());
  const auto file = nif_build::build_nif(
      {"node"},
      {nif_build::node_block("NiNode", Transform{}, {1}),
       nif_build::shape_block("BSTriShape", 0, Transform{}, nif_build::k_full, 16, verts, {0, 1, 2, 3, 4, 5})},
      {0});
  fo4::nif::SeparateRequest request;
  request.block = 1;
  request.groups = {{0}, {1}};
  fo4::nif::SeparateReport report;
  const auto split = fo4::nif::separate_shape(read_nif(file), request, &report);
  ASSERT_EQ(report.blocks.size(), 2U);
  EXPECT_EQ(report.blocks[0], 1U);
  const auto check = read_nif(split);
  const auto first = read_shape(check, 1);
  const auto second = read_shape(check, report.blocks[1]);
  EXPECT_EQ(first.tris.size(), 3U);
  EXPECT_EQ(second.tris.size(), 3U);
  // Invariant: each copy keeps only the vertices its own triangles use, so the halves share nothing.
  EXPECT_EQ(first.verts.size(), 9U);
  EXPECT_EQ(second.verts.size(), 9U);
  EXPECT_EQ(first.tris, (std::vector<std::uint16_t>{0, 1, 2}));
  EXPECT_EQ(second.tris, (std::vector<std::uint16_t>{0, 1, 2}));
}

// Invariant: donor bone indices remap to the merged palette with donor binds carried over.
TEST(ShapeJoin, JoinMergesSkinnedDonorsIntoOneBoneList) {
  const auto file = build_nif(
      {"root", "A", "B", "mat"},
      {node_block("NiNode", Transform{}, {1, 2, 4, 5}),
       shape_with_refs("BSTriShape", 0, Transform{}, nif_build::k_full | nif_build::k_has_skin, 28, 3, 9, -1, skinned_tri(0.0f, 0), {0, 1, 2}),
       shape_with_refs("BSTriShape", 0, Transform{}, nif_build::k_full | nif_build::k_has_skin, 28, 6, 9, -1, skinned_tri(5.0f, 0), {0, 1, 2}),
       skin_instance_block(7, {4}), named_node_block("NiNode", 1, Transform{}, {}),
       named_node_block("NiNode", 2, Transform{}, {}), skin_instance_block(8, {5}), bone_data_block(1),
       bone_data_block(1), lighting_shader_block(3, -1)},
      {0});
  fo4::nif::JoinRequest request;
  request.blocks = {1, 2};
  const auto joined = fo4::nif::join_shapes(read_nif(file), request);
  const auto shape = read_shape(read_nif(joined), 1);
  ASSERT_EQ(shape.bones.size(), 2U);
  EXPECT_EQ(shape.bones[0].name, "A");
  EXPECT_EQ(shape.bones[1].name, "B");
  ASSERT_EQ(shape.bone_indices.size(), 24U);
  EXPECT_EQ(shape.bone_indices[0], 0);
  EXPECT_EQ(shape.bone_indices[12], 1);
  EXPECT_EQ(shape.tris.size(), 6U);
}

// Invariant: each triangle lands in its dominant-bone copy with no group dropped.
TEST(ShapeJoin, SeparateByBonePartitionsOneDominantBonePerTriangle) {
  auto verts = skinned_tri(0.0f, 0);
  const auto more = skinned_tri(5.0f, 1);
  verts.insert(verts.end(), more.begin(), more.end());
  const auto file = build_nif(
      {"root", "A", "B", "mat"},
      {node_block("NiNode", Transform{}, {1, 3, 4}),
       shape_with_refs("BSTriShape", 0, Transform{}, nif_build::k_full | nif_build::k_has_skin, 28, 2, 5, -1, verts, {0, 1, 2, 3, 4, 5}),
       skin_instance_block(6, {3, 4}), named_node_block("NiNode", 1, Transform{}, {}),
       named_node_block("NiNode", 2, Transform{}, {}), lighting_shader_block(3, -1), bone_data_block(2)},
      {0});
  fo4::nif::SeparateReport report;
  const auto split = fo4::nif::separate_by_bone(read_nif(file), 1, &report);
  ASSERT_EQ(report.blocks.size(), 2U);
  const auto check = read_nif(split);
  EXPECT_EQ(read_shape(check, report.blocks[0]).tris.size(), 3U);
  EXPECT_EQ(read_shape(check, report.blocks[1]).tris.size(), 3U);
}

// Constraint: a partial partition is a refusal.
TEST(ShapeJoin, SeparateRefusesAnIncompletePartition) {
  const auto one = triangle_vertices();
  auto verts = one;
  verts.insert(verts.end(), one.begin(), one.end());
  const auto file = nif_build::build_nif(
      {"node"},
      {nif_build::node_block("NiNode", Transform{}, {1}),
       nif_build::shape_block("BSTriShape", 0, Transform{}, nif_build::k_full, 16, verts, {0, 1, 2, 3, 4, 5})},
      {0});
  fo4::nif::SeparateRequest request;
  request.block = 1;
  request.groups = {{0}};
  EXPECT_THROW(fo4::nif::separate_shape(read_nif(file), request), std::invalid_argument);
  request.groups = {{0}, {}};
  EXPECT_THROW(fo4::nif::separate_shape(read_nif(file), request), std::invalid_argument);
  request.groups = {{0}, {0}};
  EXPECT_THROW(fo4::nif::separate_shape(read_nif(file), request), std::invalid_argument);
}

namespace {

using fo4::nif::ModelOp;
using fo4::nif::ModelOpReport;
using fo4::nif::ModelOpRequest;

constexpr std::size_t k_descriptor_at = 100;

std::uint64_t packed_descriptor(std::uint32_t flags, std::size_t stride) {
  struct Field {
    std::uint32_t flag;
    std::size_t width;
    unsigned shift;
  };
  std::size_t at = (flags & nif_build::k_full) != 0 ? 16 : 8;
  std::uint64_t word = (static_cast<std::uint64_t>(flags) << 44U) | (stride / 4);
  for (const Field field : {Field{nif_build::k_has_uv, 4, 8}, Field{nif_build::k_has_normal, 4, 16},
                            Field{nif_build::k_has_tangent, 4, 20}, Field{nif_build::k_has_colour, 4, 24},
                            Field{nif_build::k_has_skin, 12, 28}}) {
    if ((flags & field.flag) == 0) continue;
    word |= static_cast<std::uint64_t>(at / 4) << field.shift;
    at += field.width;
  }
  return word;
}

nif_build::Block with_packed_descriptor(nif_build::Block block, std::uint32_t flags, std::size_t stride) {
  const auto word = packed_descriptor(flags, stride);
  for (std::size_t i = 0; i < 8; ++i) block.data[k_descriptor_at + i] = static_cast<std::byte>(word >> (8 * i));
  return block;
}

std::uint32_t rich_flags(bool full) {
  return (full ? nif_build::k_full : 0U) | nif_build::k_has_uv | nif_build::k_has_normal | nif_build::k_has_tangent |
         nif_build::k_has_colour | nif_build::k_has_skin;
}

std::size_t rich_stride(bool full) { return full ? 44 : 36; }

const Transform k_placed{{1.0f, 2.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};

// Invariant: blocks are 0 root, 1 shape (skin 2, shader 9, alpha 10), 2 skin instance, 3-7 bones, 8 bone data, 9 shader, 10 alpha.
std::vector<std::byte> rich_file(bool full, const std::string& skin_type = "BSSkin::Instance") {
  const auto flags = rich_flags(full);
  std::vector<std::byte> verts;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto one = nif_build::encode_vertex(full, flags, nif_build::k_verts[i % 2]);
    verts.insert(verts.end(), one.begin(), one.end());
  }
  auto skin = skin_instance_block(8, {3, 4, 5, 6, 7});
  skin.type = skin_type;
  return build_nif(
      {"root", "b1", "b2", "b3", "b4", "b5", "mat"},
      {node_block("NiNode", Transform{}, {1, 3, 4, 5, 6, 7}),
       with_packed_descriptor(
           shape_with_refs("BSTriShape", 0, k_placed, flags, rich_stride(full), 2, 9, 10, verts, {0, 1, 2}), flags,
           rich_stride(full)),
       skin, named_node_block("NiNode", 1, Transform{}, {}), named_node_block("NiNode", 2, Transform{}, {}),
       named_node_block("NiNode", 3, Transform{}, {}), named_node_block("NiNode", 4, Transform{}, {}),
       named_node_block("NiNode", 5, Transform{}, {}), bone_data_block(5), lighting_shader_block(6, -1),
       alpha_block(236, 128)},
      {0});
}

std::vector<std::byte> plain_triangle_file() {
  return build_nif({"node"},
                   {node_block("NiNode", Transform{}, {1}),
                    shape_block("BSTriShape", 0, Transform{}, nif_build::k_full, 16, triangle_vertices(), {0, 1, 2})},
                   {0});
}

ModelOpRequest bridge_request() {
  ModelOpRequest request;
  request.op = ModelOp::bridge_loops;
  request.block = 1;
  request.loop_a = {0, 1};
  request.loop_b = {2, 3};
  return request;
}

std::string refusal_text(const std::vector<std::byte>& file, const ModelOpRequest& request) {
  try {
    fo4::nif::apply_model_op(read_nif(file), request);
  } catch (const std::invalid_argument& error) {
    return error.what();
  }
  return "no refusal";
}

bool mentions(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void expect_sphere_refreshed(const fo4::nif::NifFile& nif, const fo4::nif::Shape& shape) {
  const auto layout = fo4::nif::shape_detail::read_layout(nif, 1);
  const auto bytes = fo4::nif::write_detail::load_model(nif).blocks[1];
  const auto want = fo4::nif::import_detail::bounding_sphere(fo4::nif::shape_detail::to_vec3(shape.verts));
  for (std::size_t k = 0; k < 4; ++k) {
    EXPECT_EQ(fo4::nif::write_detail::read_int<std::uint32_t>(bytes, layout.sphere_at + 4 * k),
              std::bit_cast<std::uint32_t>(want[k]));
  }
  EXPECT_GT(want[3], 0.0f);
}

}  // namespace

// Detects: a writer that re-encodes a full or half precision shape in another layout or loses a channel (UV, normal, tangent, bitangent, colour, skin weight or bone index), because every untouched vertex must keep its exact bytes.
TEST(ShapeModelOps, BridgeKeepsEveryVertexByteForFullAndHalfPrecision) {
  for (const bool full : {true, false}) {
    SCOPED_TRACE(full ? "full precision" : "half precision");
    const auto source = read_nif(rich_file(full));
    ModelOpReport report;
    const auto result = read_nif(fo4::nif::apply_model_op(source, bridge_request(), &report));
    EXPECT_EQ(report.vertices_before, 4U);
    EXPECT_EQ(report.vertices_after, 4U);
    EXPECT_EQ(report.triangles_before, 1U);
    EXPECT_EQ(report.triangles_after, 5U);
    const auto layout = fo4::nif::shape_detail::read_layout(source, 1);
    const auto before = fo4::nif::write_detail::load_model(source).blocks[1];
    const auto after = fo4::nif::write_detail::load_model(result).blocks[1];
    ASSERT_EQ(layout.stride, rich_stride(full));
    ASSERT_GE(before.size(), layout.vertices_at + 4 * layout.stride);
    ASSERT_GE(after.size(), layout.vertices_at + 4 * layout.stride);
    EXPECT_TRUE(std::equal(before.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at),
                           before.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at + 4 * layout.stride),
                           after.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at)));
    const auto old_shape = read_shape(source, 1);
    const auto new_shape = read_shape(result, 1);
    EXPECT_EQ(new_shape.bone_indices, old_shape.bone_indices);
    EXPECT_EQ(new_shape.weights, old_shape.weights);
    EXPECT_EQ(new_shape.colors, old_shape.colors);
    EXPECT_EQ(new_shape.tris.size(), 15U);
  }
}

// Detects: an edit that rebuilds the shape block from scratch and drops its name, skin, shader, alpha or transform, or leaves the bounding sphere unrefreshed; each of them must survive the edit.
TEST(ShapeModelOps, ExtrudeKeepsReferencesAndTransformAndRefreshesTheSphere) {
  const auto source = read_nif(rich_file(true));
  ModelOpRequest request;
  request.op = ModelOp::extrude_faces;
  request.block = 1;
  request.tris = {0};
  request.distance = 0.5f;
  ModelOpReport report;
  const auto result = read_nif(fo4::nif::apply_model_op(source, request, &report));
  EXPECT_EQ(report.vertices_after, 7U);
  EXPECT_EQ(report.triangles_after, 7U);
  const auto old_shape = read_shape(source, 1);
  const auto shape = read_shape(result, 1);
  EXPECT_EQ(shape.name, old_shape.name);
  EXPECT_EQ(shape.skin_ref, 2);
  EXPECT_EQ(shape.shader_ref, 9);
  EXPECT_EQ(shape.alpha_ref, 10);
  EXPECT_EQ(shape.material, "mat");
  EXPECT_EQ(shape.bones.size(), 5U);
  const auto placed = fo4::nif::block_transform(result, 1);
  for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(placed.t[i], k_placed.t[i]);
  for (std::size_t i = 0; i < 9; ++i) EXPECT_EQ(placed.r[i], k_placed.r[i]);
  EXPECT_EQ(placed.s, k_placed.s);
  ASSERT_EQ(shape.verts.size(), 21U);
  // Invariant: the three selected vertices are duplicated after the four source vertices, so source vertex 0 gains copy 4.
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(shape.verts[12 + k], old_shape.verts[k] + old_shape.normals[k] * 0.5f, 1e-5f);
  }
  for (std::size_t k = 0; k < 4; ++k) {
    EXPECT_EQ(shape.bone_indices[16 + k], old_shape.bone_indices[k]);
    EXPECT_EQ(shape.weights[16 + k], old_shape.weights[k]);
  }
  expect_sphere_refreshed(result, shape);
  const auto old_blocks = fo4::nif::write_detail::load_model(source).blocks;
  const auto new_blocks = fo4::nif::write_detail::load_model(result).blocks;
  for (const std::size_t other : {2U, 8U, 9U, 10U}) EXPECT_EQ(new_blocks[other], old_blocks[other]);
}

// Detects: a knife on a real NIF that writes the wrong vertex or triangle counts or cut points, or leaves the sphere at its old value; the cut must land on the plane.
TEST(ShapeModelOps, KnifeCutsAPlainShapeAndRefreshesTheSphere) {
  const auto source = read_nif(plain_triangle_file());
  ModelOpRequest request;
  request.op = ModelOp::knife_plane;
  request.block = 1;
  request.normal = {1.0f, 0.0f, 0.0f};
  request.offset = 0.5f;
  ModelOpReport report;
  const auto result = read_nif(fo4::nif::apply_model_op(source, request, &report));
  EXPECT_EQ(report.vertices_before, 3U);
  EXPECT_EQ(report.vertices_after, 5U);
  EXPECT_EQ(report.triangles_after, 3U);
  EXPECT_EQ(report.triangles_added, 3U);
  const auto shape = read_shape(result, 1);
  ASSERT_EQ(shape.verts.size(), 15U);
  bool has_edge_point = false;
  bool has_hypotenuse_point = false;
  for (std::size_t v = 3; v < 5; ++v) {
    if (std::fabs(shape.verts[3 * v] - 0.5f) > 1e-5f) continue;
    has_edge_point = has_edge_point || std::fabs(shape.verts[3 * v + 1]) < 1e-5f;
    has_hypotenuse_point = has_hypotenuse_point || std::fabs(shape.verts[3 * v + 1] - 0.5f) < 1e-5f;
  }
  EXPECT_TRUE(has_edge_point);
  EXPECT_TRUE(has_hypotenuse_point);
  expect_sphere_refreshed(result, shape);
}

// Detects: a shape the writer cannot reproduce being written anyway, whether the Mesh cannot carry it (second UV set, normal without tangent, other tri-shape types, a partitioned skin) or its bytes would change on rewrite (a non-zero spare bitangent slot, descriptor offsets out of order); each must be refused.
TEST(ShapeModelOps, RefusesShapesTheWriterWouldChange) {
  auto request = bridge_request();
  EXPECT_TRUE(mentions(refusal_text(rich_file(true, "BSDismemberSkinInstance"), request), "BSDismemberSkinInstance"));

  const auto lod = build_nif(
      {"node"},
      {node_block("NiNode", Transform{}, {1}),
       shape_block("BSMeshLODTriShape", 0, Transform{}, nif_build::k_full, 16, triangle_vertices(), {0, 1, 2})},
      {0});
  EXPECT_TRUE(mentions(refusal_text(lod, request), "only BSTriShape"));

  const std::uint32_t uv2_flags = nif_build::k_full | nif_build::k_has_uv | nif_build::k_has_uv2;
  const auto uv2_vertex = nif_build::encode_vertex(true, uv2_flags, nif_build::k_verts[0]);
  const auto uv2 = build_nif(
      {"node"},
      {node_block("NiNode", Transform{}, {1}),
       with_packed_descriptor(shape_block("BSTriShape", 0, Transform{}, uv2_flags, 24, uv2_vertex, {}), uv2_flags, 24)},
      {0});
  EXPECT_TRUE(mentions(refusal_text(uv2, request), "second UV set"));

  const std::uint32_t normal_only = nif_build::k_full | nif_build::k_has_normal;
  const auto normal_vertex = nif_build::encode_vertex(true, normal_only, nif_build::k_verts[0]);
  const auto lonely_normal = build_nif(
      {"node"},
      {node_block("NiNode", Transform{}, {1}),
       with_packed_descriptor(shape_block("BSTriShape", 0, Transform{}, normal_only, 20, normal_vertex, {}),
                              normal_only, 20)},
      {0});
  EXPECT_TRUE(mentions(refusal_text(lonely_normal, request), "bitangent"));

  const std::uint32_t uv_only = nif_build::k_full | nif_build::k_has_uv;
  const auto uv_vertex = nif_build::encode_vertex(true, uv_only, nif_build::k_verts[0]);
  const auto unpacked = build_nif(
      {"node"},
      {node_block("NiNode", Transform{}, {1}), shape_block("BSTriShape", 0, Transform{}, uv_only, 20, uv_vertex, {})},
      {0});
  EXPECT_TRUE(mentions(refusal_text(unpacked, request), "stores UV0"));

  nif_build::Writer slot;
  for (int i = 0; i < 3; ++i) {
    slot.f32(static_cast<float>(i));
    slot.f32(0.0f);
    slot.f32(0.0f);
    slot.f32(1.0f);
  }
  const auto spare = build_nif(
      {"node"},
      {node_block("NiNode", Transform{}, {1}),
       shape_block("BSTriShape", 0, Transform{}, nif_build::k_full, 16, slot.bytes(), {0, 1, 2})},
      {0});
  EXPECT_TRUE(mentions(refusal_text(spare, request), "would change on rewrite"));

  request.block = 0;
  EXPECT_TRUE(mentions(refusal_text(rich_file(true), request), "only BSTriShape"));
  request.block = 99;
  EXPECT_TRUE(mentions(refusal_text(rich_file(true), request), "past the file"));
}

// Detects: an op refusal (missing normals, a bad loop, a knife plane that misses the shape, a half-precision position past 65504) written as a file instead of thrown with the op's reason, which must reach the caller.
TEST(ShapeModelOps, ForwardsTheOpRefusalAsInvalidArgument) {
  ModelOpRequest extrude;
  extrude.op = ModelOp::extrude_faces;
  extrude.block = 1;
  extrude.tris = {0};
  extrude.distance = 1.0f;
  EXPECT_TRUE(mentions(refusal_text(plain_triangle_file(), extrude), "normals"));
  auto request = bridge_request();
  request.loop_b = {2};
  EXPECT_TRUE(mentions(refusal_text(rich_file(true), request), "different lengths"));
  request = bridge_request();
  request.loop_b = {2, 9};
  EXPECT_TRUE(mentions(refusal_text(rich_file(true), request), "past the mesh"));
  ModelOpRequest miss;
  miss.op = ModelOp::knife_plane;
  miss.block = 1;
  miss.normal = {1.0f, 0.0f, 0.0f};
  miss.offset = -4.0f;
  EXPECT_TRUE(mentions(refusal_text(plain_triangle_file(), miss), "change nothing"));
  ModelOpRequest overflow;
  overflow.op = ModelOp::extrude_faces;
  overflow.block = 1;
  overflow.tris = {0};
  overflow.distance = 1.0e6f;
  EXPECT_TRUE(mentions(refusal_text(rich_file(false), overflow), "cannot hold"));
}
