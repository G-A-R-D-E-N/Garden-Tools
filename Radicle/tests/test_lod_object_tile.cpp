#include "fo4/lod/object_tile.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/mesh_geometry.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "fo4/nif/shader.hpp"

#include "lod_bto_compare.hpp"
#include "lod_object_tile_fixture.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::lodtile;

fo4::lod::ObjectTileVertex vertex(float x, float y) {
  fo4::lod::ObjectTileVertex value;
  value.x = x;
  value.y = y;
  value.z = 0.0f;
  value.u = x / 512.0f;
  value.v = y / 512.0f;
  value.nx = 0.0f;
  value.ny = 0.0f;
  value.nz = 1.0f;
  value.tx = 1.0f;
  value.ty = 0.0f;
  value.tz = 0.0f;
  return value;
}

fo4::lod::ObjectTileShape shape_of(std::string name, std::string material) {
  fo4::lod::ObjectTileShape shape;
  shape.name = std::move(name);
  shape.material = std::move(material);
  shape.vertices = {vertex(0.0f, 0.0f), vertex(128.0f, 0.0f), vertex(0.0f, 128.0f)};
  shape.triangles = {0, 2, 1};
  shape.bound = {64.0f, 64.0f, 0.0f, 90.0f};
  shape.extents = {64.0f, 64.0f, 0.0f};
  return shape;
}

fo4::lod::ObjectTile small_tile() {
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  tile.origin_cell_x = 0;
  tile.origin_cell_y = 0;
  tile.shapes.push_back(shape_of("obj", "Materials\\Landscape\\Dirt.BGSM"));
  tile.objects = 1;
  tile.meshes = 1;
  return tile;
}

// Invariant: log-only; it counts post-cull triangles per source surface class so an alpha rule can be read off a real tile.
void log_surface_classes(const std::string& label, const fo4::archive::AssetResolver& assets,
                         const fo4::lod::LodSettings& settings, std::int32_t level, std::int32_t tile_x,
                         std::int32_t tile_y, const fo4::lod::LodCandidateCollection& candidates) {
  fo4::lod::MeshGeometryReader reader{assets};
  std::map<std::string, fo4::lod::ObjectTile> classes;
  for (const auto& candidate : candidates.candidates) {
    const auto origin = fo4::lod::object_tile_origin_of(settings, level, candidate);
    if (origin.first != tile_x || origin.second != tile_y) continue;
    const auto& shapes = reader.read(candidate.mesh);
    const auto* parsed = reader.nif(candidate.mesh);
    if (!shapes.has_value() || parsed == nullptr) continue;
    for (const auto& resolved : *shapes) {
      const auto& shape = resolved.shape;
      std::string key = shape.alpha_ref >= 0 ? "nif-alpha" : "no-nif-alpha";
      if (shape.alpha_ref >= 0 && static_cast<std::size_t>(shape.alpha_ref) < parsed->blocks().size() &&
          parsed->blocks()[static_cast<std::size_t>(shape.alpha_ref)].type == "NiAlphaProperty") {
        const auto alpha = fo4::nif::read_alpha(*parsed, static_cast<std::uint32_t>(shape.alpha_ref));
        key += std::string{" test="} + (alpha.test_enabled ? "1" : "0") + " blend=" + (alpha.blend_enabled ? "1" : "0");
      }
      const auto dot = shape.material.rfind('.');
      key += " material=" + (dot == std::string::npos ? std::string{"none"} : shape.material.substr(dot));
      key += std::string{" material-alpha-test="} + (resolved.material.alpha_test ? "1" : "0");
      auto& slot = classes[key];
      slot.level = level;
      slot.origin_cell_x = tile_x;
      slot.origin_cell_y = tile_y;
      fo4::lod::append_lod_object(slot, *parsed, shape, candidate, level, tile_x, tile_y, candidates.terrain.get());
    }
  }
  for (const auto& [key, slot] : classes) {
    std::size_t triangles = 0;
    for (const auto& shape : slot.shapes) triangles += shape.triangles.size() / 3;
    std::cout << "surface-class " << label << " [" << key << "] triangles " << triangles << std::endl;
  }
}

}  // namespace

// Detects (invariant): a written object tile the reader rejects, or more than the one tile texture set.
TEST(LodObjectTile, WritesAnObjectTileTheReaderAcceptsAsObjectLod) {
  const auto bytes = fo4::lod::write_object_bto(small_tile(), "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.roots().size(), 1u);
  EXPECT_EQ(nif.roots().front(), static_cast<std::uint32_t>(k_block_root));
  EXPECT_EQ(nif.blocks().at(nif.roots().front()).type, "NiNode");
  EXPECT_EQ(nif.blocks().size(), 7u);
  EXPECT_EQ(nif.blocks().at(k_block_node).type, "BSMultiBoundNode");
  EXPECT_EQ(nif.blocks().at(k_block_texture_set).type, "BSShaderTextureSet");
  EXPECT_EQ(read_u32(nif, static_cast<std::uint32_t>(k_block_root), 72), 1u);
  EXPECT_EQ(count_type(nif, "BSSubIndexTriShape"), 1u);
  EXPECT_EQ(count_type(nif, "BSMultiBoundNode"), 1u);
  EXPECT_EQ(count_type(nif, "BSMultiBound"), 1u);
  EXPECT_EQ(count_type(nif, "BSMultiBoundAABB"), 1u);
  EXPECT_EQ(count_type(nif, "BSLightingShaderProperty"), 1u);
  EXPECT_EQ(count_type(nif, "NiAlphaProperty"), 0u);
  expect_group_references(nif);
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  EXPECT_EQ(shapes[0].tris.size(), 3u);
  EXPECT_EQ(shapes[0].verts.size(), 9u);
  EXPECT_EQ(shapes[0].normals.size(), 9u);
  EXPECT_EQ(shapes[0].uvs.size(), 6u);
  EXPECT_EQ(read_u64(nif, k_block_shape, 100), 0x1b00000430205ULL);
  // Invariant: measured vanilla slots are diffuse, normal, five empty slots, then specular at slot 7.
  ASSERT_EQ(shapes[0].textures.size(), 10u);
  EXPECT_EQ(shapes[0].textures[0], "data\\Textures\\Terrain\\Commonwealth\\Objects\\Commonwealth.Objects.DDS");
  EXPECT_EQ(shapes[0].textures[1], "data\\Textures\\Terrain\\Commonwealth\\Objects\\Commonwealth.Objects_n.DDS");
  EXPECT_EQ(shapes[0].textures[2], "");
  EXPECT_EQ(shapes[0].textures[7], "data\\Textures\\Terrain\\Commonwealth\\Objects\\Commonwealth.Objects_s.DDS");
}

// Detects (invariant): a vertex value exactly between two halves written away from zero instead of to the even half.
TEST(LodObjectTile, RoundsAnExactHalfwayVertexValueToTheEvenHalf) {
  auto tile = small_tile();
  auto& vertices = tile.shapes[0].vertices;
  vertices[0].x = 573.25f;
  vertices[0].y = -573.25f;
  vertices[0].z = 573.75f;
  vertices[0].u = 0.5f + 1.0f / 4096.0f;
  vertices[0].v = 0.5f + 3.0f / 4096.0f;
  const auto nif = fo4::nif::read_nif(fo4::lod::write_object_bto(tile, "Commonwealth"));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  ASSERT_GE(shapes[0].verts.size(), 3u);
  EXPECT_EQ(shapes[0].verts[0], 573.0f);
  EXPECT_EQ(shapes[0].verts[1], -573.0f);
  EXPECT_EQ(shapes[0].verts[2], 574.0f);
  ASSERT_GE(shapes[0].uvs.size(), 2u);
  EXPECT_EQ(shapes[0].uvs[0], 0.5f);
  EXPECT_EQ(shapes[0].uvs[1], 0.5f + 4.0f / 4096.0f);
}

// Detects (invariant): a changed vertex scale, rotation, UV or normal, or a dropped attribute, between the decoded mesh and the written tile.
// Invariant: vertices are checked against the placement maths, so the expected values follow the importer and only the tile size and write determinism are pinned.
TEST(LodObjectTile, PinsADecodedMeshTileVertexByVertex) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  const auto& shape = shapes[0];
  ASSERT_EQ(shape.tris.size(), 3u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::LodObjectCandidate candidate;
  candidate.cell_x = 1;
  candidate.cell_y = -1;
  candidate.position = {96.0f, -32.0f, 8.0f};
  candidate.rotation = {0.25f, -0.5f, 1.25f};
  candidate.scale = 2.0f;
  fo4::lod::append_lod_object(tile, nif, shape, candidate, 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  const auto& vertices = tile.shapes[0].vertices;
  ASSERT_EQ(vertices.size(), 3u);

  const auto global = fo4::nif::shape_to_world(nif, shape.block);
  const auto basis = fo4::lod::lod_placement_basis(candidate.rotation);
  const auto rotate = [](const float* matrix, const std::array<float, 3>& value) {
    return std::array<float, 3>{matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
                                matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
                                matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2]};
  };
  for (std::size_t corner = 0; corner < 3; ++corner) {
    const auto source = static_cast<std::size_t>(shape.tris[corner]);
    const std::array<float, 3> local{shape.verts[source * 3] * global.s, shape.verts[source * 3 + 1] * global.s,
                                     shape.verts[source * 3 + 2] * global.s};
    auto inner = rotate(global.r, local);
    for (std::size_t axis = 0; axis < 3; ++axis) inner[axis] += global.t[axis];
    const auto placed = rotate(basis.data(), inner);
    const std::array<float, 3> expected{(candidate.position[0] + placed[0] * candidate.scale) / 8.0f,
                                        (candidate.position[1] + placed[1] * candidate.scale) / 8.0f,
                                        (candidate.position[2] + placed[2] * candidate.scale) / 8.0f};
    const auto& vertex = vertices[corner];
    EXPECT_NEAR(vertex.x, expected[0], 1e-3f) << corner;
    EXPECT_NEAR(vertex.y, expected[1], 1e-3f) << corner;
    EXPECT_NEAR(vertex.z, expected[2], 1e-3f) << corner;
    EXPECT_FLOAT_EQ(vertex.u, shape.uvs[source * 2]) << corner;
    EXPECT_FLOAT_EQ(vertex.v, shape.uvs[source * 2 + 1]) << corner;
    auto normal = rotate(basis.data(), rotate(global.r, {shape.normals[source * 3], shape.normals[source * 3 + 1],
                                                          shape.normals[source * 3 + 2]}));
    const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    ASSERT_GT(length, 0.0f);
    EXPECT_NEAR(vertex.nx, normal[0] / length, 1e-4f) << corner;
    EXPECT_NEAR(vertex.ny, normal[1] / length, 1e-4f) << corner;
    EXPECT_NEAR(vertex.nz, normal[2] / length, 1e-4f) << corner;
  }

  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  EXPECT_EQ(bytes.size(), 1116u);
  EXPECT_EQ(fo4::lod::write_object_bto(tile, "Commonwealth"), bytes);
}

// Detects (invariant): a wrong sine or cosine coefficient, swapped matrix lane or flipped sign in lod_placement_basis.
TEST(LodObjectTile, PlacementBasisMatchesTheClosedFormAcrossFullTurnsAndRightAngles) {
  const float half_pi = 1.57079632679f;
  std::vector<float> angles;
  for (int step = -16; step <= 16; ++step) angles.push_back(static_cast<float>(step) * half_pi / 2.0f);
  angles.push_back(0.3f);
  angles.push_back(-2.7f);
  angles.push_back(5.9f);
  for (const float ax : angles) {
    for (const float ay : angles) {
      for (const float az : angles) {
        const float cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
        const float cz = std::cos(az), sz = std::sin(az);
        const std::array<float, 9> expected{cz * cy,                sz * cy,                -sy,
                                            cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx,
                                            cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx};
        const auto basis = fo4::lod::lod_placement_basis({ax, ay, az});
        for (std::size_t k = 0; k < 9; ++k) ASSERT_NEAR(basis[k], expected[k], 1e-5f) << ax << " " << ay << " " << az << " " << k;
      }
    }
  }
}

// Detects (invariant): a zero-length source normal turning into NaN or infinity through the reciprocal-length multiply.
TEST(LodObjectTile, AZeroLengthSourceNormalStaysZeroInsteadOfNonFinite) {
  const auto nif = synthetic_mesh();
  auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  auto& shape = shapes[0];
  for (auto& value : shape.normals) value = 0.0f;
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::LodObjectCandidate candidate;
  candidate.position = {96.0f, -32.0f, 8.0f};
  candidate.rotation = {0.25f, -0.5f, 1.25f};
  fo4::lod::append_lod_object(tile, nif, shape, candidate, 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  for (const auto& vertex : tile.shapes[0].vertices) {
    EXPECT_TRUE(std::isfinite(vertex.nx) && std::isfinite(vertex.ny) && std::isfinite(vertex.nz));
  }
}

TEST(LodObjectTile, WritesAnAlphaGroupWithTheMeasuredPropertyAndTail) {
  auto tile = small_tile();
  tile.shapes.push_back(shape_of("obj-at", "Materials\\Landscape\\Grass.BGSM"));
  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.blocks().size(), 13u);
  EXPECT_EQ(count_type(nif, "BSShaderTextureSet"), 1u);
  EXPECT_EQ(count_type(nif, "NiAlphaProperty"), 1u);
  EXPECT_EQ(count_type(nif, "BSSubIndexTriShape"), 2u);
  const auto groups = group_blocks(nif);
  ASSERT_EQ(groups.size(), 2u);
  const auto alpha_block = static_cast<std::uint32_t>(read_i32(nif, groups[1].shape, 96));
  ASSERT_LT(alpha_block, nif.blocks().size());
  EXPECT_EQ(nif.blocks().at(alpha_block).type, "NiAlphaProperty");
  EXPECT_EQ(hex_of(nif, alpha_block, 0, 15), "0100000000000000ffffffffec1280");
  const auto alpha_name = read_i32(nif, alpha_block, 0);
  ASSERT_GE(alpha_name, 0);
  ASSERT_LT(static_cast<std::size_t>(alpha_name), nif.strings().size());
  EXPECT_EQ(nif.strings()[static_cast<std::size_t>(alpha_name)], "");
  const auto alpha_shape = groups[1].shape;
  EXPECT_EQ(read_i32(nif, alpha_shape, 96), static_cast<std::int32_t>(alpha_block));
  expect_group_references(nif);
  const auto opaque_shape = groups[0].shape;
  EXPECT_EQ(hex_of(nif, opaque_shape, nif.blocks().at(opaque_shape).size - 28, 28),
            "0100000001000000010000000000000001000000ffffffff00000000");
  EXPECT_EQ(hex_of(nif, alpha_shape, nif.blocks().at(alpha_shape).size - 28, 28),
            "0100000001000000010000000000000001000000ffffffff00000000");
}

// Detects (invariant): an empty tile written with groups or a texture set instead of a bare root.
TEST(LodObjectTile, WritesAnEmptyTileAsARootWithoutShapes) {
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  EXPECT_EQ(nif.roots().size(), 1u);
  EXPECT_EQ(nif.blocks().at(nif.roots().front()).type, "NiNode");
  EXPECT_EQ(nif.blocks().size(), 1u);
  EXPECT_EQ(count_type(nif, "BSShaderTextureSet"), 0u);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::no_shapes);
}

// Detects (invariant): a tile the writer cannot represent must be refused instead of written broken.
TEST(LodObjectTile, RefusesATileThatWouldNotReadBack) {
  auto zero_level = small_tile();
  zero_level.level = 0;
  EXPECT_THROW(fo4::lod::write_object_bto(zero_level, "Commonwealth"), fo4::lod::LodError);
  auto partial = small_tile();
  partial.shapes[0].triangles = {0, 1};
  EXPECT_THROW(fo4::lod::write_object_bto(partial, "Commonwealth"), fo4::lod::LodError);
  auto out_of_range = small_tile();
  out_of_range.shapes[0].triangles = {0, 1, 7};
  EXPECT_THROW(fo4::lod::write_object_bto(out_of_range, "Commonwealth"), fo4::lod::LodError);
  auto empty = small_tile();
  empty.shapes[0].vertices.clear();
  EXPECT_THROW(fo4::lod::write_object_bto(empty, "Commonwealth"), fo4::lod::LodError);
}

// Detects (invariant): a mesh whose every triangle is dropped must not create an empty group.
TEST(LodObjectTile, DropsAnAllNonFiniteMeshWithoutCreatingAGroup) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::LodObjectCandidate candidate;
  candidate.position = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
  candidate.rotation = {0.0f, 0.0f, 0.0f};
  candidate.scale = 1.0f;
  fo4::lod::append_lod_object(tile, nif, shapes[0], candidate, 8, 0, 0);
  const auto triangles = shapes[0].tris.size() / 3;
  EXPECT_TRUE(tile.shapes.empty());
  EXPECT_EQ(tile.dropped_triangles, triangles);
  EXPECT_EQ(tile.dropped_vertices, triangles * 3);
}

// Detects (invariant): a valid mesh must create exactly one group with its triangles and no drops.
TEST(LodObjectTile, AppendsAValidMeshIntoOneGroup) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::LodObjectCandidate candidate;
  candidate.position = {0.0f, 0.0f, 0.0f};
  candidate.rotation = {0.0f, 0.0f, 0.0f};
  candidate.scale = 1.0f;
  fo4::lod::append_lod_object(tile, nif, shapes[0], candidate, 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  EXPECT_EQ(tile.shapes[0].triangles.size(), shapes[0].tris.size());
  EXPECT_EQ(tile.shapes[0].vertices.size(), shapes[0].verts.size() / 3);
  EXPECT_EQ(tile.dropped_triangles, 0u);
  EXPECT_EQ(tile.dropped_vertices, 0u);
}

// Detects (invariant): a level-4 tile must list one segment per source cell in column-major order (segment k is column
// k / 4, row k % 4); the earlier row-major order put every cell below the first row into the wrong segment.
TEST(LodObjectTile, SegmentsALevelFourTilePerSourceCell) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 4;
  for (const std::int32_t cell : {0, 2}) {
    fo4::lod::LodObjectCandidate candidate;
    candidate.cell_x = 0;
    candidate.cell_y = cell;
    candidate.position = {0.0f, static_cast<float>(cell) * 4096.0f, 0.0f};
    candidate.rotation = {0.0f, 0.0f, 0.0f};
    candidate.scale = 1.0f;
    fo4::lod::append_lod_object(tile, nif, shapes[0], candidate, 4, 0, 0);
  }
  ASSERT_EQ(tile.shapes.size(), 1u);
  // Invariant: the skipped cell 1 stays in the list as a zero-primitive segment, as vanilla level-4 shapes do.
  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  const auto written = fo4::nif::read_nif(bytes);
  const auto written_groups = group_blocks(written);
  ASSERT_EQ(written_groups.size(), 1u);
  const auto segments = fo4::nif::read_segments(written, written_groups[0].shape);
  ASSERT_EQ(segments.segments.size(), 3u);
  EXPECT_EQ(segments.total_segments, 3u);
  EXPECT_EQ(segments.primitives, 2u);
  EXPECT_EQ(segments.segments[0].start_index, 0u);
  EXPECT_EQ(segments.segments[0].primitives, 1u);
  EXPECT_EQ(segments.segments[1].start_index, 3u);
  EXPECT_EQ(segments.segments[1].primitives, 0u);
  EXPECT_EQ(segments.segments[2].start_index, 3u);
  EXPECT_EQ(segments.segments[2].primitives, 1u);
  EXPECT_TRUE(segments.segments[0].sub_segments.empty());
}

// Detects (invariant): block mix, texture-set count, group references and AABB must match the measured tile.
TEST(LodObjectTileReal, WritesATileThatMatchesTheVanillaLevelEightTileStructure) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path data{dir};
  const std::filesystem::path master = data / "Fallout4.esm";
  const std::filesystem::path meshes = data / "Fallout4 - Meshes.ba2";
  if (!std::filesystem::exists(master) || !std::filesystem::exists(meshes)) {
    GTEST_SKIP() << "no Fallout4.esm or Fallout4 - Meshes.ba2 under " << dir;
  }
  const std::vector<std::filesystem::path> plugins{master};
  const auto env = fo4::env::Environment::load(plugins);
  const std::vector<std::filesystem::path> archives{meshes};
  const auto assets = fo4::archive::AssetResolver::open(data, archives);

  // Invariant: the measured reference tile's 24 blocks by type are 1 NiNode, 4 nodes, 4 shapes, 4 shaders, 1 texture set, 4 bounds, 4 AABBs, 1 alpha property and 1 integer extra data.
  constexpr std::size_t k_measured_vertices = 101897;
  constexpr std::size_t k_measured_triangles = 52085;
  constexpr std::size_t k_measured_blocks = 24;
  constexpr std::size_t k_measured_shapes = 4;
  constexpr std::size_t k_measured_alpha = 1;
  constexpr std::size_t k_measured_extra = 1;
  constexpr std::uint64_t k_measured_descriptor = 0x1b00000430205ULL;

  const fo4::lod::LodSettings commonwealth{-96, -96, 256, 4, 32};
  const auto worldspace = fo4::FormKey::parse("00003C:Fallout4.esm");
  const auto candidates = fo4::lod::collect_object_lod_candidates(env, worldspace, 8);
  const auto tile = fo4::lod::build_object_tile(assets, commonwealth, 8, 0, 0, candidates);
  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  // Invariant: the written tile merges surfaces into one shape per alpha class, so the NIF is what counts.
  const auto written_shapes = count_type(nif, "BSSubIndexTriShape");
  const auto alpha_groups = count_type(nif, "NiAlphaProperty");
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  for (const auto& shape : tile.shapes) {
    vertices += shape.vertices.size();
    triangles += shape.triangles.size() / 3;
  }
  const auto groups = group_blocks(nif);
  const auto aabb = groups.empty() ? std::vector<float>{} : block_floats(nif, groups.front().aabb, 6);
  std::cout << "object tile level 8 (0,0): objects " << tile.objects << ", meshes " << tile.meshes
            << ", missing meshes " << tile.missing_meshes << ", dropped vertices " << tile.dropped_vertices
            << ", dropped triangles " << tile.dropped_triangles << ", culled " << tile.culled_triangles
            << ", shapes " << tile.shapes.size() << " (vanilla " << k_measured_shapes << "), alpha groups "
            << alpha_groups << " (vanilla " << k_measured_alpha << "), blocks " << nif.blocks().size()
            << " (vanilla " << k_measured_blocks << ": 2 + 5*" << k_measured_shapes << " + alpha "
            << k_measured_alpha << " + extra " << k_measured_extra << "), vertices " << vertices
            << ", triangles " << triangles << ", bytes " << bytes.size() << '\n';
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.blocks().at(nif.roots().front()).type, "NiNode");
  EXPECT_GT(tile.meshes, 0u);
  EXPECT_GT(written_shapes, 0u);
  // Invariant: the vanilla tile is two opaque shapes (index-cap split), one alpha shape and one toggled shape, with the
  // vanilla vertex and triangle totals once the below-terrain cull and the MultirefLOD rule have run.
  EXPECT_EQ(written_shapes, k_measured_shapes);
  EXPECT_EQ(count_type(nif, "NiAlphaProperty"), k_measured_alpha);
  EXPECT_EQ(count_type(nif, "NiIntegerExtraData"), k_measured_extra);
  EXPECT_EQ(nif.blocks().size(), k_measured_blocks);
  EXPECT_EQ(vertices, k_measured_vertices);
  EXPECT_EQ(triangles, k_measured_triangles);
  EXPECT_GT(tile.culled_triangles, 0u);
  std::size_t widest = 0;
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != "BSSubIndexTriShape") continue;
    const auto mine = fo4::nif::read_segments(nif, block);
    EXPECT_LE(static_cast<std::size_t>(mine.primitives) * 3, 65535u);
    widest = std::max(widest, static_cast<std::size_t>(mine.primitives));
  }
  EXPECT_LE(widest, 21845u);
  EXPECT_EQ(k_measured_blocks, 1u + 5u * k_measured_shapes + 1u + k_measured_alpha + k_measured_extra);
  // Invariant: our tile has one root, five blocks per group, one shared texture set, one alpha property per alpha group and one extra data per toggled group.
  EXPECT_EQ(nif.blocks().size(), 1u + 5u * written_shapes + 1u + alpha_groups + count_type(nif, "NiIntegerExtraData"));
  EXPECT_EQ(count_type(nif, "BSShaderTextureSet"), 1u);
  EXPECT_EQ(count_type(nif, "BSMultiBoundNode"), written_shapes);
  EXPECT_EQ(count_type(nif, "BSLightingShaderProperty"), written_shapes);
  EXPECT_EQ(count_type(nif, "BSMultiBound"), written_shapes);
  EXPECT_EQ(count_type(nif, "BSMultiBoundAABB"), written_shapes);
  EXPECT_EQ(read_u32(nif, static_cast<std::uint32_t>(k_block_root), 72), static_cast<std::uint32_t>(written_shapes));
  expect_group_references(nif);
  ASSERT_FALSE(groups.empty());
  EXPECT_EQ(read_u64(nif, groups.front().shape, 100), k_measured_descriptor);
  ASSERT_EQ(aabb.size(), 6u);
  EXPECT_GT(aabb[3], 0.0f);
  EXPECT_GT(aabb[4], 0.0f);
  EXPECT_GT(aabb[5], 0.0f);

  // Invariant: the vanilla tile at this level and coordinate is the block-by-block reference for segments.
  const auto reference = assets.locate("Meshes\\Terrain\\Commonwealth\\Objects\\Commonwealth.8.0.0.BTO");
  ASSERT_TRUE(reference.has_value());
  const auto vanilla = fo4::nif::read_nif(assets.read(*reference));
  std::vector<std::uint32_t> vanilla_shapes;
  for (std::uint32_t block = 0; block < vanilla.blocks().size(); ++block) {
    if (vanilla.blocks()[block].type == "BSSubIndexTriShape") vanilla_shapes.push_back(block);
  }
  ASSERT_EQ(vanilla_shapes.size(), 4u);
  std::size_t matched = 0;
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != "BSSubIndexTriShape") continue;
    if (matched == vanilla_shapes.size()) break;
    const auto mine = fo4::nif::read_segments(nif, block);
    const auto theirs = fo4::nif::read_segments(vanilla, vanilla_shapes[matched]);
    // Invariant: a level-8 tile writes one segment covering every triangle, with no sub-index composites.
    EXPECT_EQ(mine.total_segments, 1u);
    EXPECT_EQ(theirs.total_segments, 1u);
    ASSERT_EQ(mine.segments.size(), 1u);
    ASSERT_EQ(theirs.segments.size(), 1u);
    EXPECT_EQ(mine.segments.front().start_index, theirs.segments.front().start_index);
    EXPECT_EQ(mine.segments.front().primitives, mine.primitives);
    EXPECT_EQ(theirs.segments.front().primitives, theirs.primitives);
    EXPECT_TRUE(mine.segments.front().sub_segments.empty());
    EXPECT_TRUE(theirs.segments.front().sub_segments.empty());
    ++matched;
  }
  EXPECT_EQ(matched, std::min(written_shapes, vanilla_shapes.size()));
}

// Detects (invariant): a level-4 tile keeps vanilla's one-shape-per-alpha-class split, per-cell segments and shared
// texture set, so a whole-world level-4 generate stays structurally comparable to the shipped tiles.
TEST(LodObjectTileReal, WritesATileThatMatchesTheVanillaLevelFourTileStructure) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path data{dir};
  const std::filesystem::path master = data / "Fallout4.esm";
  const std::filesystem::path meshes = data / "Fallout4 - Meshes.ba2";
  if (!std::filesystem::exists(master) || !std::filesystem::exists(meshes)) {
    GTEST_SKIP() << "no Fallout4.esm or Fallout4 - Meshes.ba2 under " << dir;
  }
  const std::vector<std::filesystem::path> plugins{master};
  const auto env = fo4::env::Environment::load(plugins);
  const std::vector<std::filesystem::path> archives{meshes};
  const auto assets = fo4::archive::AssetResolver::open(data, archives);
  const fo4::lod::LodSettings commonwealth{-96, -96, 256, 4, 32};
  const auto worldspace = fo4::FormKey::parse("00003C:Fallout4.esm");
  const auto candidates = fo4::lod::collect_object_lod_candidates(env, worldspace, 4);
  const auto tile = fo4::lod::build_object_tile(assets, commonwealth, 4, 0, 0, candidates);
  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  const auto written_shapes = count_type(nif, "BSSubIndexTriShape");
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  for (const auto& shape : tile.shapes) {
    vertices += shape.vertices.size();
    triangles += shape.triangles.size() / 3;
  }
  // Invariant: the vanilla level-4 tile (0,0) holds these vertex and triangle totals after the cull and the MultirefLOD rule.
  EXPECT_EQ(vertices, 56352u);
  EXPECT_EQ(triangles, 29908u);
  std::array<std::uint32_t, 16> mine_cells{};
  std::array<std::uint32_t, 16> vanilla_cells{};
  std::cout << "object tile level 4 (0,0): objects " << tile.objects << ", meshes " << tile.meshes
            << ", written shapes " << written_shapes << ", blocks " << nif.blocks().size()
            << ", alpha properties " << count_type(nif, "NiAlphaProperty") << '\n';
  const auto reference = assets.locate("Meshes\\Terrain\\Commonwealth\\Objects\\Commonwealth.4.0.0.BTO");
  ASSERT_TRUE(reference.has_value());
  const auto vanilla = fo4::nif::read_nif(assets.read(*reference));
  std::vector<std::uint32_t> vanilla_shapes;
  for (std::uint32_t block = 0; block < vanilla.blocks().size(); ++block) {
    if (vanilla.blocks()[block].type == "BSSubIndexTriShape") vanilla_shapes.push_back(block);
  }
  ASSERT_EQ(vanilla_shapes.size(), 3u);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::ok);
  // Invariant: vanilla's level-4 tile is one shape per alpha class plus the 65,535-index cap split.
  EXPECT_EQ(written_shapes, 3u);
  EXPECT_EQ(nif.blocks().size(), 1u + 5u * written_shapes + 1u + count_type(nif, "NiAlphaProperty"));
  EXPECT_EQ(count_type(nif, "BSShaderTextureSet"), 1u);
  EXPECT_EQ(count_type(nif, "NiAlphaProperty"), 1u);
  std::size_t matched = 0;
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type != "BSSubIndexTriShape") continue;
    ASSERT_LT(matched, vanilla_shapes.size());
    const auto mine = fo4::nif::read_segments(nif, block);
    const auto theirs = fo4::nif::read_segments(vanilla, vanilla_shapes[matched]);
    std::cout << "  shape " << matched << ": ours segments " << mine.segments.size() << " primitives "
              << mine.primitives << ", vanilla segments " << theirs.segments.size() << " primitives "
              << theirs.primitives << '\n';
    // Invariant: a level-4 tile lists one segment per occupied cell, never more than the tile's cell count.
    EXPECT_GT(mine.segments.size(), 0u);
    EXPECT_LE(mine.segments.size(), 16u);
    EXPECT_EQ(mine.segments.front().start_index, 0u);
    for (std::size_t cell = 0; cell < mine.segments.size() && cell < mine_cells.size(); ++cell) {
      mine_cells[cell] += mine.segments[cell].primitives;
    }
    std::size_t covered = 0;
    for (const auto& segment : mine.segments) covered += segment.primitives;
    EXPECT_EQ(covered, mine.primitives);
    EXPECT_TRUE(mine.segments.front().sub_segments.empty());
    ++matched;
  }
  EXPECT_EQ(matched, 3u);
  for (const auto block : vanilla_shapes) {
    const auto theirs = fo4::nif::read_segments(vanilla, block);
    for (std::size_t cell = 0; cell < theirs.segments.size() && cell < vanilla_cells.size(); ++cell) {
      vanilla_cells[cell] += theirs.segments[cell].primitives;
    }
  }
  // Invariant: segment k is cell column k / 4 and row k % 4, so the triangles per segment equal the vanilla tile's.
  EXPECT_EQ(mine_cells, vanilla_cells);
}

// Detects (invariant): the BTO compare accepting a two-byte frame difference, a moved vertex or a systematic one-byte bias.
TEST(LodObjectTile, BtoCompareAcceptsOneByteFrameNoiseOnlyWithinTolerance) {
  using fo4::test::lodcompare::CanonicalTriangle;
  const auto triangle = [](float z, std::uint8_t normal_z, std::uint64_t key = 0) {
    CanonicalTriangle value;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      value.key[corner] = corner + 1 + key;
      value.pos[corner] = {static_cast<float>(corner), 0.0f, z};
      value.frame[corner] = {128, 128, normal_z, 128, 128, 128};
    }
    return value;
  };
  const auto only_ours = [&](const CanonicalTriangle& ours, const CanonicalTriangle& vanilla) {
    std::vector<CanonicalTriangle> left;
    std::vector<CanonicalTriangle> right;
    fo4::test::lodcompare::PairingSlack slack;
    fo4::test::lodcompare::diff_triangles({ours}, {vanilla}, left, right, slack);
    EXPECT_EQ(left.size(), right.size());
    return left.size();
  };
  EXPECT_EQ(only_ours(triangle(10.0f, 128), triangle(10.0f, 129)), 0u);
  EXPECT_EQ(only_ours(triangle(10.0f, 128), triangle(10.0f, 130)), 1u);
  EXPECT_EQ(only_ours(triangle(10.0f, 128, 0), triangle(10.5f, 128, 10)), 1u);
  EXPECT_EQ(only_ours(triangle(10.0f, 128, 0), triangle(10.0004f, 128, 10)), 0u);
  EXPECT_EQ(only_ours(triangle(1000.0f, 128, 0), triangle(1000.5f, 128, 10)), 0u);
  EXPECT_EQ(only_ours(triangle(1000.0f, 128, 0), triangle(1001.0f, 128, 10)), 1u);
  std::vector<CanonicalTriangle> ours;
  std::vector<CanonicalTriangle> vanilla;
  for (std::uint64_t index = 0; index < 200; ++index) {
    ours.push_back(triangle(1.0f, 128, index * 10));
    vanilla.push_back(triangle(1.0f, 129, index * 10));
  }
  std::vector<CanonicalTriangle> left;
  std::vector<CanonicalTriangle> right;
  fo4::test::lodcompare::PairingSlack slack;
  fo4::test::lodcompare::diff_triangles(ours, vanilla, left, right, slack);
  EXPECT_TRUE(left.empty());
  EXPECT_EQ(slack.frame, 200u);
  EXPECT_TRUE(fo4::test::lodcompare::slack_over_budget(slack, ours.size()));
  slack.frame = 2;
  EXPECT_FALSE(fo4::test::lodcompare::slack_over_budget(slack, ours.size()));
}

// Detects (invariant): a generated Commonwealth tile departing from the vanilla .bto in any tier, see the code note.
TEST(LodObjectTileReal, FixedCommonwealthTilesMatchTheVanillaBtoBlockByBlockAndTriangleByTriangle) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to the Fallout 4 Data folder to run this");
  const std::filesystem::path data{dir};
  const std::filesystem::path master = data / "Fallout4.esm";
  const std::filesystem::path meshes = data / "Fallout4 - Meshes.ba2";
  if (!std::filesystem::exists(master) || !std::filesystem::exists(meshes)) {
    GTEST_SKIP() << "no Fallout4.esm or Fallout4 - Meshes.ba2 under " << dir;
  }
  const std::vector<std::filesystem::path> plugins{master};
  const auto env = fo4::env::Environment::load(plugins);
  const std::filesystem::path materials = data / "Fallout4 - Materials.ba2";
  std::vector<std::filesystem::path> archives{meshes};
  // Invariant: a generate run names every archive, so the .bgsm alpha test that classifies a shape must be readable here.
  if (std::filesystem::exists(materials)) archives.push_back(materials);
  const auto assets = fo4::archive::AssetResolver::open(data, archives);
  const fo4::lod::LodSettings commonwealth{-96, -96, 256, 4, 32};
  const auto worldspace = fo4::FormKey::parse("00003C:Fallout4.esm");

  struct Wanted {
    std::int32_t level;
    std::int32_t x;
    std::int32_t y;
  };
  const std::vector<Wanted> wanted{{4, 0, 0},  {4, -8, -8}, {8, 0, 0},   {8, -8, -8},
                                   {16, 0, 0}, {16, 16, 16}, {32, 0, 0}, {32, -32, -32}};
  std::map<std::int32_t, fo4::lod::LodCandidateCollection> by_level;
  fo4::test::lodcompare::Sweep sweep;
  for (const auto& tile_at : wanted) {
    const std::string label = "Commonwealth." + std::to_string(tile_at.level) + "." + std::to_string(tile_at.x) +
                              "." + std::to_string(tile_at.y);
    auto found = by_level.find(tile_at.level);
    if (found == by_level.end()) {
      found = by_level
                  .emplace(tile_at.level, fo4::lod::collect_object_lod_candidates(env, worldspace, tile_at.level))
                  .first;
    }
    const auto reference = assets.locate("Meshes\\Terrain\\Commonwealth\\Objects\\" + label + ".BTO");
    if (!reference) {
      ADD_FAILURE() << label << ": the vanilla tile is not in the meshes archive";
      continue;
    }
    const auto tile = fo4::lod::build_object_tile(assets, commonwealth, tile_at.level, tile_at.x, tile_at.y,
                                                  found->second);
    if (tile.shapes.empty()) {
      ADD_FAILURE() << label << ": the generator built no shape for a tile vanilla ships";
      continue;
    }
    log_surface_classes(label, assets, commonwealth, tile_at.level, tile_at.x, tile_at.y, found->second);
    const auto vanilla_bytes = assets.read(*reference);
    const auto ours_bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
    fo4::test::lodcompare::TileComparison compared;
    std::string whole_text = "not compared";
    try {
      const auto vanilla = fo4::nif::read_nif(vanilla_bytes);
      const auto ours = fo4::nif::read_nif(ours_bytes);
      compared = fo4::test::lodcompare::compare_object_tile(ours, vanilla, true);
      fo4::test::lodcompare::log_vertex_agreement(label, ours, vanilla);
      const auto whole = fo4::test::lodcompare::first_difference(ours_bytes, vanilla_bytes);
      whole_text = whole ? "first difference at " + fo4::test::lodcompare::place_of(ours, *whole) : "identical";
    } catch (const std::exception& failure) {
      compared.note("layout", std::string{"a NIF reader threw: "} + failure.what());
    }
    std::cout << "bto-manifest " << label << " ours " << ours_bytes.size() << ' '
              << fo4::test::lodcompare::hex64(fo4::fnv1a64_bytes(ours_bytes)) << " vanilla " << vanilla_bytes.size()
              << ' ' << fo4::test::lodcompare::hex64(fo4::fnv1a64_bytes(vanilla_bytes)) << " whole-file "
              << whole_text;
    for (const auto& tier : fo4::test::lodcompare::k_tiers) {
      const auto at = compared.diffs.find(tier);
      std::cout << ' ' << tier << '=' << (at == compared.diffs.end() ? 0 : at->second.size());
    }
    std::cout << " geometry-exact-diffs=" << compared.exact_leftovers << " geometry-tolerated=" << compared.tolerated
              << '\n';
    sweep.add(label, compared);
  }
  EXPECT_EQ(sweep.tiles, wanted.size());
  sweep.expect_clean("fixed Commonwealth object tiles");
}

// Detects (invariant): a collection's composed placement matrix being ignored for the candidate Euler triple.
TEST(LodObjectTile, AppliesAComposedBasisInsteadOfTheCandidateEulerTriple) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::LodObjectCandidate candidate;
  candidate.scale = 1.0f;
  candidate.rotation = {0.0f, 0.0f, 0.0f};
  candidate.rotation_basis = std::array<float, 9>{0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  fo4::lod::append_lod_object(tile, nif, shapes[0], candidate, 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  float max_x = -1e9f;
  float max_y = -1e9f;
  for (const auto& vertex : tile.shapes[0].vertices) {
    if (vertex.x > max_x) max_x = vertex.x;
    if (vertex.y > max_y) max_y = vertex.y;
  }
  // Invariant: a composed basis turns the mesh corner to (0, 128) before the level divide.
  EXPECT_NEAR(max_x, 0.0f, 1e-3f);
  EXPECT_NEAR(max_y, 16.0f, 1e-3f);
}

// Detects (invariant): a placed reference turning counter-clockwise, which mirrors every rotated LOD object.
// A heading of 90 degrees faces east in the game, so the mesh corner on +Y must land on +X.
TEST(LodObjectTile, TurnsAPlacedReferenceClockwiseForAPositiveHeading) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::LodObjectCandidate candidate;
  candidate.scale = 1.0f;
  candidate.rotation = {0.0f, 0.0f, 1.5707963267948966f};
  fo4::lod::append_lod_object(tile, nif, shapes[0], candidate, 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  float max_x = -1e9f;
  float min_y = 1e9f;
  float max_y = -1e9f;
  for (const auto& vertex : tile.shapes[0].vertices) {
    max_x = std::max(max_x, vertex.x);
    min_y = std::min(min_y, vertex.y);
    max_y = std::max(max_y, vertex.y);
  }
  // Invariant: (0, 128) turns to (128, 0) and (128, 0) turns to (0, -128), before the level divide by 8.
  EXPECT_NEAR(max_x, 16.0f, 1e-3f);
  EXPECT_NEAR(min_y, -16.0f, 1e-3f);
  EXPECT_NEAR(max_y, 0.0f, 1e-3f);
}

// Detects (invariant): triangles silently dropped once a group reaches the 16-bit vertex limit, which would punch
// holes into every dense level-16 and level-32 tile. The group must be closed and a second one opened.
TEST(LodObjectTile, OpensASecondGroupAtTheVertexLimitInsteadOfDroppingTriangles) {
  const auto nif = synthetic_mesh();
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 32;
  constexpr std::size_t k_objects = 21846;
  for (std::size_t index = 0; index < k_objects; ++index) {
    fo4::lod::LodObjectCandidate candidate;
    candidate.scale = 1.0f;
    candidate.position = {static_cast<float>(index % 256) * 200.0f, static_cast<float>(index / 256) * 200.0f, 0.0f};
    fo4::lod::append_lod_object(tile, nif, shapes[0], candidate, 32, 0, 0);
  }
  EXPECT_EQ(tile.dropped_triangles, 0u);
  EXPECT_EQ(tile.dropped_vertices, 0u);
  ASSERT_EQ(tile.shapes.size(), 2u);
  EXPECT_EQ(tile.shapes[0].vertices.size(), 65535u);
  EXPECT_EQ(tile.shapes[1].vertices.size(), 3u);
  EXPECT_EQ(tile.shapes[0].triangles.size() + tile.shapes[1].triangles.size(), k_objects * 3);
  // Invariant: the split tile still writes and reads back as an object tile with both shapes.
  const auto written = fo4::nif::read_nif(fo4::lod::write_object_bto(tile, "Commonwealth"));
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(written, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(count_type(written, "BSSubIndexTriShape"), 2u);
}

// Detects (invariant): the UV window taken over a merged group instead of per mesh, which collapses a mesh whose
// UVs sit in another whole-number square onto one texel column, or thins a plain mesh across a tiling one's entry.
TEST(LodObjectTile, ShiftsEachMeshIntoItsOwnUvWindowBeforeMerging) {
  const auto mesh_with = [](const char* uvs) {
    const std::string obj = std::string{"v 0 0 0\nv 128 0 0\nv 0 128 0\n"} + uvs + "vn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
    fo4::nif::ImportOptions options;
    return fo4::nif::read_nif(fo4::nif::import_obj(obj, options));
  };
  const auto plain = mesh_with("vt 0.1 0.1\nvt 0.9 0.1\nvt 0.1 0.9\n");
  const auto shifted = mesh_with("vt 5.1 -2.9\nvt 5.9 -2.9\nvt 5.1 -2.1\n");
  const auto tiling = mesh_with("vt 0.1 0.1\nvt 2.9 0.1\nvt 0.1 0.9\n");
  const auto far_tiling = mesh_with("vt 0.1 0.1\nvt 9.9 0.1\nvt 0.1 0.9\n");
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  float offset = 0.0f;
  for (const auto* nif : {&plain, &shifted, &tiling, &far_tiling}) {
    const auto shapes = fo4::nif::read_shapes(*nif);
    ASSERT_EQ(shapes.size(), 1u);
    fo4::lod::LodObjectCandidate candidate;
    candidate.scale = 1.0f;
    candidate.position = {offset, 0.0f, 0.0f};
    offset += 1000.0f;
    fo4::lod::append_lod_object(tile, *nif, shapes[0], candidate, 8, 0, 0);
  }
  // Invariant: the plain and the shifted mesh share one group; each tiling mesh gets a group of its own repeat.
  ASSERT_EQ(tile.shapes.size(), 3u);
  EXPECT_EQ(tile.shapes[0].vertices.size(), 6u);
  EXPECT_EQ(tile.shapes[0].uv_repeat, (std::array<std::int32_t, 2>{1, 1}));
  EXPECT_FALSE(tile.shapes[0].uv_clamped);
  float span_u = 0.0f;
  for (const auto& vertex : tile.shapes[0].vertices) {
    EXPECT_GE(vertex.u, 0.0f);
    EXPECT_LE(vertex.u, 1.0f);
    EXPECT_GE(vertex.v, 0.0f);
    EXPECT_LE(vertex.v, 1.0f);
  }
  // Invariant: the shifted mesh keeps its own 0.8-wide UV extent instead of collapsing onto an edge.
  for (std::size_t index = 3; index < 6; ++index) {
    span_u = std::max(span_u, std::fabs(tile.shapes[0].vertices[index].u - tile.shapes[0].vertices[3].u));
  }
  // Constraint: the source stores half-float UVs, which step by 1/256 between 4 and 8.
  EXPECT_NEAR(span_u, 0.8f, 1e-2f);
  EXPECT_EQ(tile.shapes[1].uv_repeat[0], 3);
  EXPECT_FALSE(tile.shapes[1].uv_clamped);
  EXPECT_EQ(tile.shapes[2].uv_repeat[0], fo4::lod::k_object_uv_max_repeat);
  EXPECT_TRUE(tile.shapes[2].uv_clamped);
}

// Detects (invariant): a vertex frame rebuilt from the UV gradient, or a non-finite source channel reaching the quantiser.
TEST(LodObjectTile, CarriesTheSourceTangentFrameTurnedByThePlacement) {
  const auto nif = synthetic_mesh();
  auto shape = fo4::nif::read_shapes(nif).at(0);
  const auto count = shape.verts.size();
  shape.tangents.assign(count, 0.0f);
  shape.bitangents.assign(count, 0.0f);
  for (std::size_t vertex = 0; vertex < count / 3; ++vertex) {
    shape.tangents[vertex * 3] = 1.0f;
    shape.bitangents[vertex * 3 + 1] = 1.0f;
  }
  fo4::lod::LodObjectCandidate candidate;
  candidate.rotation = {0.0f, 0.0f, 0.5f};
  candidate.scale = 1.0f;
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::append_lod_object(tile, nif, shape, candidate, 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  const auto global = fo4::nif::shape_to_world(nif, shape.block);
  const auto basis = fo4::lod::lod_placement_basis(candidate.rotation);
  const auto turn = [&](const std::array<float, 3>& value) {
    const std::array<float, 3> inner{global.r[0] * value[0] + global.r[1] * value[1] + global.r[2] * value[2],
                                     global.r[3] * value[0] + global.r[4] * value[1] + global.r[5] * value[2],
                                     global.r[6] * value[0] + global.r[7] * value[1] + global.r[8] * value[2]};
    std::array<float, 3> out{basis[0] * inner[0] + basis[1] * inner[1] + basis[2] * inner[2],
                             basis[3] * inner[0] + basis[4] * inner[1] + basis[5] * inner[2],
                             basis[6] * inner[0] + basis[7] * inner[1] + basis[8] * inner[2]};
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    for (auto& axis : out) axis /= length;
    return out;
  };
  const auto tangent = turn({1.0f, 0.0f, 0.0f});
  const auto bitangent = turn({0.0f, 1.0f, 0.0f});
  ASSERT_FALSE(tile.shapes[0].vertices.empty());
  for (const auto& vertex : tile.shapes[0].vertices) {
    EXPECT_TRUE(vertex.frame);
    EXPECT_NEAR(vertex.tx, tangent[0], 1e-4f);
    EXPECT_NEAR(vertex.ty, tangent[1], 1e-4f);
    EXPECT_NEAR(vertex.tz, tangent[2], 1e-4f);
    EXPECT_NEAR(vertex.bx, bitangent[0], 1e-4f);
    EXPECT_NEAR(vertex.by, bitangent[1], 1e-4f);
    EXPECT_NEAR(vertex.bz, bitangent[2], 1e-4f);
  }

  auto bare = shape;
  bare.tangents.clear();
  bare.bitangents.clear();
  fo4::lod::ObjectTile fallback;
  fallback.level = 8;
  fo4::lod::append_lod_object(fallback, nif, bare, candidate, 8, 0, 0);
  ASSERT_EQ(fallback.shapes.size(), 1u);
  for (const auto& vertex : fallback.shapes[0].vertices) {
    EXPECT_FALSE(vertex.frame);
    EXPECT_NEAR(vertex.tx * vertex.tx + vertex.ty * vertex.ty + vertex.tz * vertex.tz, 1.0f, 1e-4f);
  }

  auto broken = shape;
  broken.tangents[0] = std::numeric_limits<float>::quiet_NaN();
  fo4::lod::ObjectTile survivor;
  survivor.level = 8;
  fo4::lod::append_lod_object(survivor, nif, broken, candidate, 8, 0, 0);
  ASSERT_EQ(survivor.shapes.size(), 1u);
  EXPECT_EQ(survivor.shapes[0].vertices.size(), tile.shapes[0].vertices.size());
  std::size_t without_frame = 0;
  for (const auto& vertex : survivor.shapes[0].vertices) without_frame += vertex.frame ? 0 : 1;
  EXPECT_EQ(without_frame, 1u);
}
