#include "fo4/lod/object_tile.hpp"

#include "fo4/archive/assets.hpp"
#include "fo4/lod/land_height.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/terrain_heightfield.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"

#include "lod_bto_compare.hpp"
#include "lod_object_tile_fixture.hpp"
#include "material_file_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::lodtile;

fo4::nif::NifFile obj_mesh(const char* text) {
  fo4::nif::ImportOptions options;
  return fo4::nif::read_nif(fo4::nif::import_obj(text, options));
}

fo4::lod::LandGrids flat_land(float height) {
  fo4::env::LandHeightGrid grid;
  grid.heights.fill(height);
  fo4::lod::LandGrids land;
  land.grids.emplace(std::pair<std::int32_t, std::int32_t>{0, 0}, grid);
  return land;
}

constexpr const char* k_quad_obj =
    "v 0 0 0\nv 128 0 0\nv 128 128 0\nv 0 128 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";

fo4::lod::LodObjectCandidate candidate_at(float x, float y, float z) {
  fo4::lod::LodObjectCandidate candidate;
  candidate.position = {x, y, z};
  candidate.scale = 1.0f;
  return candidate;
}

}  // namespace

// Detects (invariant): the quad diagonal not alternating with (row + column) % 2, which shifts every cull near a slope.
TEST(LodObjectTile, ReadsTheLandHeightOnAnAlternatingQuadDiagonal) {
  fo4::env::LandHeightGrid grid;
  grid.heights.fill(0.0f);
  grid.heights[1 * fo4::env::k_land_grid + 1] = 100.0f;
  fo4::lod::LandGrids land;
  land.grids.emplace(std::pair<std::int32_t, std::int32_t>{0, 0}, grid);
  fo4::env::LandHeightGrid flat;
  flat.heights.fill(7.0f);
  land.grids.emplace(std::pair<std::int32_t, std::int32_t>{-1, -1}, flat);
  // Invariant: quad (row 0, column 0) splits (0,0)-(1,1), so (0.75, 0.25) lies on the plane through the raised corner.
  EXPECT_NEAR(fo4::lod::land_height_at(land, 96.0, 32.0).value(), 25.0, 1e-9);
  // Invariant: quad (row 0, column 1) splits the other way, so both of these points read 25.
  EXPECT_NEAR(fo4::lod::land_height_at(land, 160.0, 32.0).value(), 25.0, 1e-9);
  EXPECT_NEAR(fo4::lod::land_height_at(land, 224.0, 96.0).value(), 25.0, 1e-9);
  EXPECT_NEAR(fo4::lod::land_height_at(land, -10.0, -10.0).value(), 7.0, 1e-9);
  EXPECT_FALSE(fo4::lod::land_height_at(land, 4106.0, 10.0).has_value());
  EXPECT_FALSE(fo4::lod::land_height_at(land, std::numeric_limits<double>::quiet_NaN(), 0.0).has_value());
}

// Detects (invariant): a triangle culled by a terrain-only test (any vertex, or a non-strict 64 unit margin), a vertex
// kept although no kept triangle uses it, or a cull in a cell that has no LAND.
TEST(LodObjectTile, CullsATriangleOnlyWhenAllThreeVerticesAreMoreThan64UnitsBelowTheLand) {
  const auto mesh = obj_mesh(
      "v 0 0 0\nv 128 0 0\nv 128 128 0\nv 0 128 500\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
      "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
  const auto shapes = fo4::nif::read_shapes(mesh);
  ASSERT_EQ(shapes.size(), 1u);
  const auto build = [&](const fo4::lod::LandGrids* terrain, float x, float z) {
    fo4::lod::ObjectTile tile;
    tile.level = 8;
    fo4::lod::append_lod_object(tile, mesh, shapes[0], candidate_at(x, 0.0f, z), 8, 0, 0, terrain);
    return tile;
  };
  const auto land = flat_land(100.0f);
  const auto open = build(nullptr, 0.0f, 0.0f);
  ASSERT_EQ(open.shapes.size(), 1u);
  EXPECT_EQ(open.shapes[0].triangles.size(), 6u);
  EXPECT_EQ(open.culled_triangles, 0u);
  // Invariant: the first triangle is wholly under the land (z 0 < 100 - 64), the second has a vertex 400 above it.
  const auto cut = build(&land, 0.0f, 0.0f);
  ASSERT_EQ(cut.shapes.size(), 1u);
  EXPECT_EQ(cut.culled_triangles, 1u);
  EXPECT_EQ(cut.dropped_triangles, 0u);
  EXPECT_EQ(cut.shapes[0].triangles.size(), 3u);
  EXPECT_EQ(cut.shapes[0].vertices.size(), 3u);
  // Constraint: z 36 is not below 100 - 64, so the comparison is strict and exact.
  const auto edge = build(&land, 0.0f, 36.0f);
  ASSERT_EQ(edge.shapes.size(), 1u);
  EXPECT_EQ(edge.culled_triangles, 0u);
  EXPECT_EQ(edge.shapes[0].triangles.size(), 6u);
  const auto under_edge = build(&land, 0.0f, 35.0f);
  EXPECT_EQ(under_edge.culled_triangles, 1u);
  // Invariant: a cell without LAND is never culled.
  const auto elsewhere = build(&land, 4200.0f, -4000.0f);
  ASSERT_EQ(elsewhere.shapes.size(), 1u);
  EXPECT_EQ(elsewhere.culled_triangles, 0u);
  EXPECT_EQ(elsewhere.shapes[0].triangles.size(), 6u);
}

// Detects (regression): the mesh root node transform ignored when an object is placed, which put every mesh with a
// rotated or offset root (vanilla has them) at the wrong spot in the tile.
TEST(LodObjectTile, AppliesTheMeshRootNodeTransform) {
  auto mesh = obj_mesh(k_quad_obj);
  ASSERT_EQ(mesh.roots().size(), 1u);
  const auto root = mesh.roots()[0];
  const auto shapes = fo4::nif::read_shapes(mesh);
  ASSERT_EQ(shapes.size(), 1u);
  ASSERT_NE(shapes[0].block, root);
  auto bytes = mesh.bytes();
  const auto transform_at = mesh.blocks().at(root).offset + 16 + 4 * static_cast<std::size_t>(read_u32(mesh, root, 4));
  const float patch[13] = {1000.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
  std::memcpy(bytes.data() + transform_at, patch, sizeof(patch));
  const auto rooted = fo4::nif::read_nif(bytes);
  ASSERT_NEAR(fo4::nif::block_transform(rooted, root).t[0], 1000.0f, 1e-3);
  const auto placed = fo4::nif::read_shapes(rooted);
  ASSERT_EQ(placed.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::append_lod_object(tile, rooted, placed[0], candidate_at(0.0f, 0.0f, 0.0f), 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  std::vector<std::uint16_t> order;
  for (const auto index : placed[0].tris) {
    if (std::find(order.begin(), order.end(), index) == order.end()) order.push_back(index);
  }
  ASSERT_EQ(tile.shapes[0].vertices.size(), order.size());
  for (std::size_t slot = 0; slot < order.size(); ++slot) {
    const float px = placed[0].verts[order[slot] * 3];
    const float py = placed[0].verts[order[slot] * 3 + 1];
    // Invariant: the root rotates a quarter turn about z, (x, y) becomes (-y, x), then moves 1000 along x.
    EXPECT_NEAR(tile.shapes[0].vertices[slot].x, (1000.0f - py) / 8.0f, 1e-3) << slot;
    EXPECT_NEAR(tile.shapes[0].vertices[slot].y, px / 8.0f, 1e-3) << slot;
  }
}

// Detects (invariant): a mesh wholly below the land creating an empty group that the writer then refuses.
TEST(LodObjectTile, CreatesNoGroupForAMeshWhollyBelowTheLand) {
  const auto mesh = obj_mesh(k_quad_obj);
  const auto shapes = fo4::nif::read_shapes(mesh);
  ASSERT_EQ(shapes.size(), 1u);
  const auto land = flat_land(500.0f);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::append_lod_object(tile, mesh, shapes[0], candidate_at(0.0f, 0.0f, 0.0f), 8, 0, 0, &land);
  EXPECT_TRUE(tile.shapes.empty());
  EXPECT_EQ(tile.culled_triangles, 2u);
  EXPECT_EQ(tile.dropped_triangles, 0u);
}

// Detects (invariant): vertices merged by value inside one object; vanilla keeps every source vertex, so a mesh that
// repeats a position, UV and normal keeps both copies.
TEST(LodObjectTile, KeepsDuplicateSourceVertices) {
  const auto mesh = obj_mesh(
      "v 0 0 0\nv 128 0 0\nv 0 128 0\nv 0 0 0\nv 128 0 0\nv 0 128 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\n"
      "f 1/1/1 2/2/1 3/3/1\nf 4/1/1 5/2/1 6/3/1\n");
  const auto shapes = fo4::nif::read_shapes(mesh);
  ASSERT_EQ(shapes.size(), 1u);
  ASSERT_EQ(shapes[0].verts.size() / 3, 6u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::append_lod_object(tile, mesh, shapes[0], candidate_at(0.0f, 0.0f, 0.0f), 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 1u);
  EXPECT_EQ(tile.shapes[0].vertices.size(), 6u);
  EXPECT_EQ(tile.shapes[0].triangles, (std::vector<std::uint16_t>{0, 1, 2, 3, 4, 5}));
}

// Detects (invariant): enable-parent children merged into the always-shown shapes, a missing toggle extra data block, or
// toggled shapes out of vanilla order (plain first, then descending parent, opaque before alpha).
TEST(LodObjectTile, WritesAToggledShapePerEnableParentAfterThePlainShapes) {
  const auto mesh = obj_mesh(k_quad_obj);
  const auto shapes = fo4::nif::read_shapes(mesh);
  ASSERT_EQ(shapes.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  float offset = 0.0f;
  for (const std::uint32_t toggle : {0x820u, 0u, 0x900u, 0x820u}) {
    auto candidate = candidate_at(offset, 0.0f, 0.0f);
    candidate.toggle_ref = toggle;
    offset += 500.0f;
    fo4::lod::append_lod_object(tile, mesh, shapes[0], candidate, 8, 0, 0);
  }
  ASSERT_EQ(tile.shapes.size(), 3u);
  const auto bytes = fo4::lod::write_object_bto(tile, "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object), fo4::lod::LodTileStructure::ok);
  expect_group_references(nif);
  const auto groups = group_blocks(nif);
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_EQ(count_type(nif, "NiIntegerExtraData"), 2u);
  EXPECT_EQ(nif.blocks().size(), 1u + 5u * 3u + 1u + 2u);
  EXPECT_EQ(groups[0].shift, 0u);
  ASSERT_EQ(groups[1].shift, 4u);
  ASSERT_EQ(groups[2].shift, 4u);
  const std::uint32_t expected_toggle[] = {0u, 0x900u, 0x820u};
  for (std::size_t index = 1; index < 3; ++index) {
    const auto extra = static_cast<std::uint32_t>(read_i32(nif, groups[index].shape, 8));
    EXPECT_EQ(extra, groups[index].shape + 1);
    EXPECT_EQ(nif.blocks().at(extra).type, "NiIntegerExtraData");
    EXPECT_EQ(read_u32(nif, extra, 4), expected_toggle[index]);
    const auto name = read_i32(nif, extra, 0);
    ASSERT_GE(name, 0);
    ASSERT_LT(static_cast<std::size_t>(name), nif.strings().size());
    EXPECT_EQ(nif.strings()[static_cast<std::size_t>(name)], "ToggleRefID");
  }
  const auto written = fo4::nif::read_shapes(nif);
  ASSERT_EQ(written.size(), 3u);
  EXPECT_EQ(written[0].tris.size(), 6u);
  EXPECT_EQ(written[1].tris.size(), 6u);
  EXPECT_EQ(written[2].tris.size(), 12u);
}

// Detects (invariant): a capped shape split between groups instead of between objects, which leaves it over the 65,535
// index limit (one group of 10,923 quads holds 65,538 indices) or short of it; at level 4 also the triangles waiting in
// per-cell buckets not counting toward the cap, which leaves one shape of 65,538 indices.
TEST(LodObjectTile, SplitsACappedShapeBetweenObjectsAtTheIndexLimit) {
  const auto mesh = obj_mesh(k_quad_obj);
  const auto shapes = fo4::nif::read_shapes(mesh);
  ASSERT_EQ(shapes.size(), 1u);
  for (const std::int32_t level : {8, 4}) {
    fo4::lod::ObjectTile tile;
    tile.level = level;
    constexpr std::size_t k_quads = 10923;
    for (std::size_t index = 0; index < k_quads; ++index) {
      auto candidate = candidate_at(static_cast<float>(index % 100), 0.0f, 0.0f);
      candidate.cell_x = static_cast<std::int32_t>(index % 4);
      fo4::lod::append_lod_object(tile, mesh, shapes[0], candidate, level, 0, 0);
    }
    ASSERT_EQ(tile.shapes.size(), 1u) << level;
    EXPECT_EQ(tile.shapes[0].block_starts.size(), k_quads) << level;
    const auto nif = fo4::nif::read_nif(fo4::lod::write_object_bto(tile, "Commonwealth"));
    std::vector<std::uint32_t> primitives;
    for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
      if (nif.blocks()[block].type == "BSSubIndexTriShape") primitives.push_back(fo4::nif::read_segments(nif, block).primitives);
    }
    ASSERT_EQ(primitives.size(), 2u) << level;
    EXPECT_EQ(primitives[0], 21844u) << level;
    EXPECT_EQ(primitives[1], 2u) << level;
  }
}

// Detects (invariant): the collection's LAND dropped between collection and build (the tile builder given no terrain),
// which keeps every below-terrain object in a generated tile although append_lod_object culls when called directly.
TEST(LodObjectTile, CullsThroughTheTerrainTheCandidateCollectionCarries) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_cull_flow";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  fo4::nif::ImportOptions options;
  const auto bytes = fo4::nif::import_obj(k_quad_obj, options);
  {
    std::ofstream out(folder / "Meshes" / "quad.nif", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  fo4::lod::LodCandidateCollection collection;
  for (const float z : {0.0f, 500.0f}) {
    auto candidate = candidate_at(0.0f, 0.0f, z);
    candidate.mesh = "quad.nif";
    collection.candidates.push_back(candidate);
  }
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  const std::vector<fo4::lod::LodTile> tiles{
      fo4::lod::LodTile{std::string{}, 8, 0, 0, fo4::lod::LodTileKind::object}};
  const auto open = fo4::lod::build_object_tiles(assets, settings, 8, tiles, collection);
  collection.terrain = std::make_shared<const fo4::lod::LandGrids>(flat_land(500.0f));
  const auto cut = fo4::lod::build_object_tiles(assets, settings, 8, tiles, collection);
  std::filesystem::remove_all(folder);
  ASSERT_EQ(open.size(), 1u);
  ASSERT_EQ(open[0].shapes.size(), 1u);
  EXPECT_EQ(open[0].shapes[0].triangles.size(), 12u);
  EXPECT_EQ(open[0].culled_triangles, 0u);
  ASSERT_EQ(cut.size(), 1u);
  ASSERT_EQ(cut[0].shapes.size(), 1u);
  EXPECT_EQ(cut[0].shapes[0].triangles.size(), 6u);
  EXPECT_EQ(cut[0].culled_triangles, 2u);
}

// Detects (invariant): a multi-tile build that mixes up, drops or recounts tiles against one-tile builds.
TEST(LodObjectTile, BuildsManyTilesInTileOrderLikeOneTileBuilds) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_many_tiles";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  fo4::nif::ImportOptions options;
  const auto bytes = fo4::nif::import_obj(k_quad_obj, options);
  {
    std::ofstream out(folder / "Meshes" / "quad.nif", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  fo4::lod::LodCandidateCollection collection;
  std::vector<fo4::lod::LodTile> tiles;
  constexpr std::int32_t k_tiles = 37;
  for (std::int32_t index = 0; index < k_tiles; ++index) {
    tiles.push_back(fo4::lod::LodTile{std::string{}, 8, index * 8, 0, fo4::lod::LodTileKind::object});
    for (std::int32_t copy = 0; copy <= index % 3; ++copy) {
      auto candidate = candidate_at(static_cast<float>(copy), 0.0f, 0.0f);
      candidate.mesh = "quad.nif";
      candidate.cell_x = index * 8;
      candidate.cell_y = 0;
      collection.candidates.push_back(candidate);
    }
  }
  const auto many = fo4::lod::build_object_tiles(assets, settings, 8, tiles, collection);
  ASSERT_EQ(many.size(), tiles.size());
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    const auto one = fo4::lod::build_object_tile(assets, settings, 8, tiles[index].x, tiles[index].y, collection);
    EXPECT_EQ(many[index].origin_cell_x, tiles[index].x) << index;
    EXPECT_EQ(many[index].objects, index % 3 + 1) << index;
    EXPECT_EQ(many[index].objects, one.objects) << index;
    EXPECT_EQ(many[index].meshes, one.meshes) << index;
    ASSERT_EQ(many[index].shapes.size(), one.shapes.size()) << index;
    ASSERT_FALSE(many[index].shapes.empty()) << index;
    EXPECT_EQ(many[index].shapes[0].triangles, one.shapes[0].triangles) << index;
  }
  std::filesystem::remove_all(folder);
}

// Detects (invariant): objects appended by cell row instead of by cell column.
TEST(LodObjectTile, AppendsObjectsInColumnMajorCellOrder) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_cell_order";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  fo4::nif::ImportOptions options;
  const auto bytes = fo4::nif::import_obj(k_quad_obj, options);
  {
    std::ofstream out(folder / "Meshes" / "quad.nif", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  fo4::lod::LodCandidateCollection collection;
  for (const auto& [cell_x, cell_y] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{0, 0}}) {
    auto candidate = candidate_at(static_cast<float>(cell_x) * 4096.0f, static_cast<float>(cell_y) * 4096.0f, 0.0f);
    candidate.mesh = "quad.nif";
    candidate.cell_x = cell_x;
    candidate.cell_y = cell_y;
    collection.candidates.push_back(candidate);
  }
  const auto tile = fo4::lod::build_object_tile(assets, settings, 8, 0, 0, collection);
  std::filesystem::remove_all(folder);
  ASSERT_EQ(tile.shapes.size(), 1u);
  const auto& vertices = tile.shapes[0].vertices;
  ASSERT_EQ(vertices.size(), 12u);
  EXPECT_LT(vertices[0].x, 512.0f);
  EXPECT_LT(vertices[0].y, 512.0f);
  EXPECT_LT(vertices[4].x, 512.0f);
  EXPECT_GE(vertices[4].y, 512.0f);
  EXPECT_GE(vertices[8].x, 512.0f);
  EXPECT_LT(vertices[8].y, 512.0f);
}

// Detects (invariant): objects appended oldest reference first or persistent last, shifting the 65,535 index cap split.
TEST(LodObjectTile, AppendsPersistentReferencesFirstThenAReferencesNewestFirst) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_reference_order";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  fo4::nif::ImportOptions options;
  const auto bytes = fo4::nif::import_obj(k_quad_obj, options);
  {
    std::ofstream out(folder / "Meshes" / "quad.nif", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  const auto first = fo4::FormKey::parse("000001:Fallout4.esm");
  const auto second = fo4::FormKey::parse("000002:Fallout4.esm");
  const auto third = fo4::FormKey::parse("000003:Fallout4.esm");
  fo4::lod::LodCandidateCollection collection;
  const auto add = [&](const fo4::FormKey& reference, float x, bool persistent) {
    auto candidate = candidate_at(x, 0.0f, 0.0f);
    candidate.mesh = "quad.nif";
    candidate.reference = reference;
    candidate.persistent = persistent;
    collection.candidates.push_back(candidate);
  };
  add(first, 0.0f, false);
  add(second, 1024.0f, false);
  add(second, 1280.0f, false);
  add(third, 2048.0f, true);
  const auto tile = fo4::lod::build_object_tile(assets, settings, 8, 0, 0, collection);
  std::filesystem::remove_all(folder);
  ASSERT_EQ(tile.shapes.size(), 1u);
  const auto& vertices = tile.shapes[0].vertices;
  ASSERT_EQ(vertices.size(), 16u);
  EXPECT_FLOAT_EQ(vertices[0].x, 256.0f);
  EXPECT_FLOAT_EQ(vertices[4].x, 128.0f);
  EXPECT_FLOAT_EQ(vertices[8].x, 160.0f);
  EXPECT_FLOAT_EQ(vertices[12].x, 0.0f);
}

// Detects (invariant): a written pool listing its objects group by group instead of in the order they were appended.
TEST(LodObjectTile, WritesObjectsOfDifferentGroupsInAppendOrder) {
  const auto mesh_with = [](const char* material) {
    fo4::nif::ImportOptions options;
    options.material = material;
    return fo4::nif::read_nif(fo4::nif::import_obj(k_quad_obj, options));
  };
  const auto mesh_a = mesh_with("Materials\\LOD\\A.BGSM");
  const auto mesh_b = mesh_with("Materials\\LOD\\B.BGSM");
  const auto shapes_a = fo4::nif::read_shapes(mesh_a);
  const auto shapes_b = fo4::nif::read_shapes(mesh_b);
  ASSERT_EQ(shapes_a.size(), 1u);
  ASSERT_EQ(shapes_b.size(), 1u);
  fo4::lod::ObjectTile tile;
  tile.level = 8;
  fo4::lod::append_lod_object(tile, mesh_a, shapes_a[0], candidate_at(0.0f, 0.0f, 0.0f), 8, 0, 0);
  fo4::lod::append_lod_object(tile, mesh_b, shapes_b[0], candidate_at(1024.0f, 0.0f, 0.0f), 8, 0, 0);
  fo4::lod::append_lod_object(tile, mesh_a, shapes_a[0], candidate_at(2048.0f, 0.0f, 0.0f), 8, 0, 0);
  ASSERT_EQ(tile.shapes.size(), 2u);
  const auto written = fo4::nif::read_shapes(fo4::nif::read_nif(fo4::lod::write_object_bto(tile, "Commonwealth")));
  ASSERT_EQ(written.size(), 1u);
  ASSERT_EQ(written[0].verts.size(), 36u);
  EXPECT_LT(written[0].verts[0], 64.0f);
  EXPECT_GE(written[0].verts[12], 128.0f);
  EXPECT_LT(written[0].verts[12], 192.0f);
  EXPECT_GE(written[0].verts[24], 256.0f);
}

// Detects (invariant): a reference's material swap ignored at build, leaving an alpha-test replacement in the opaque group.
TEST(LodObjectTile, AppliesAReferenceMaterialSwapToTheLodShapeAlphaClass) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_material_swap";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  std::filesystem::create_directories(folder / "Materials" / "LOD");
  fo4::nif::ImportOptions options;
  options.material = "Materials\\LOD\\Plain.BGSM";
  const auto bytes = fo4::nif::import_obj(k_quad_obj, options);
  {
    std::ofstream out(folder / "Meshes" / "quad.nif", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  auto material = fo4::material::read_material_file(fo4::test::material_bgsm({.version = 2}));
  ASSERT_TRUE(fo4::material::set_field(material, "AlphaTest", true));
  const auto alpha = fo4::material::write_material_file(material);
  {
    std::ofstream out(folder / "Materials" / "LOD" / "Alpha.BGSM", std::ios::binary);
    out.write(reinterpret_cast<const char*>(alpha.data()), static_cast<std::streamsize>(alpha.size()));
  }
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  const std::vector<fo4::lod::LodTile> tiles{
      fo4::lod::LodTile{std::string{}, 8, 0, 0, fo4::lod::LodTileKind::object}};
  const auto build = [&](bool swapped, const char* original) {
    fo4::lod::LodObjectCandidate candidate = candidate_at(0.0f, 0.0f, 0.0f);
    candidate.mesh = "quad.nif";
    if (swapped) candidate.material_swap.emplace_back(original, "lod/Alpha.bgsm");
    fo4::lod::LodCandidateCollection collection;
    collection.candidates.push_back(candidate);
    return fo4::lod::build_object_tiles(assets, settings, 8, tiles, collection);
  };
  const auto plain = build(false, "");
  const auto swapped = build(true, "LOD\\plain.bgsm");
  const auto unrelated = build(true, "LOD\\Other.BGSM");
  std::filesystem::remove_all(folder);
  ASSERT_EQ(plain.size(), 1u);
  ASSERT_EQ(plain[0].shapes.size(), 1u);
  EXPECT_EQ(plain[0].shapes[0].name, "obj");
  ASSERT_EQ(swapped.size(), 1u);
  ASSERT_EQ(swapped[0].shapes.size(), 1u);
  EXPECT_EQ(swapped[0].shapes[0].name, "obj-at");
  ASSERT_EQ(unrelated.size(), 1u);
  ASSERT_EQ(unrelated[0].shapes.size(), 1u);
  EXPECT_EQ(unrelated[0].shapes[0].name, "obj");
}

namespace {

void write_quad_with_material(const std::filesystem::path& folder, const char* name, const char* material) {
  fo4::nif::ImportOptions options;
  options.material = material;
  const auto bytes = fo4::nif::import_obj(k_quad_obj, options);
  std::ofstream out(folder / "Meshes" / name, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

struct AtlasLevel {
  std::int32_t level{0};
  fo4::lod::LodCandidateCollection collection;
  std::vector<fo4::lod::LodTile> tiles;
};

}  // namespace

// Detects (invariant): the streamed atlas must equal the all-at-once bake in digest, texels and BTO bytes.
TEST(LodObjectTile, StreamedAtlasPlanWritesTheSameBytesAsTheAllAtOnceBake) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_streamed_atlas";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  write_quad_with_material(folder, "a.nif", "Materials\\LOD\\A.BGSM");
  write_quad_with_material(folder, "b.nif", "Materials\\LOD\\B.BGSM");
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  std::vector<AtlasLevel> levels;
  for (const std::int32_t level : {8, 16}) {
    AtlasLevel entry;
    entry.level = level;
    for (std::int32_t index = 0; index < 5 - level / 8; ++index) {
      entry.tiles.push_back(fo4::lod::LodTile{std::string{}, level, index * level, 0, fo4::lod::LodTileKind::object});
      for (std::int32_t copy = 0; copy <= index % 2; ++copy) {
        auto candidate = candidate_at(static_cast<float>(index * level * 4096 + 100 + copy), 100.0f, 0.0f);
        candidate.mesh = (index + copy) % 2 == 0 ? "a.nif" : "b.nif";
        candidate.cell_x = index * level;
        candidate.cell_y = 0;
        entry.collection.candidates.push_back(candidate);
      }
    }
    levels.push_back(std::move(entry));
  }

  std::vector<fo4::lod::ObjectTile> all;
  for (const auto& entry : levels) {
    for (auto& tile : fo4::lod::build_object_tiles(assets, settings, entry.level, entry.tiles, entry.collection)) {
      all.push_back(std::move(tile));
    }
  }
  const auto raw_first = fo4::lod::write_object_bto(all.front(), "TestWorld");
  const auto reference_atlas = fo4::lod::bake_object_atlas(assets, all);
  std::vector<std::vector<std::byte>> reference;
  for (const auto& tile : all) reference.push_back(fo4::lod::write_object_bto(tile, "TestWorld"));

  fo4::lod::ObjectAtlasPlan plan;
  std::size_t tile_count = 0;
  for (const auto& entry : levels) {
    const auto built = fo4::lod::build_object_tiles(assets, settings, entry.level, entry.tiles, entry.collection);
    for (const auto& tile : built) plan.add(tile);
    tile_count += built.size();
  }
  ASSERT_EQ(tile_count, all.size());
  const auto atlas = plan.bake(assets);
  EXPECT_GE(atlas.entries, 2u);
  EXPECT_EQ(atlas.entries, reference_atlas.entries);
  EXPECT_EQ(atlas.shapes, reference_atlas.shapes);
  EXPECT_EQ(atlas.side, reference_atlas.side);
  EXPECT_EQ(atlas.content_hash, reference_atlas.content_hash);
  for (std::size_t map = 0; map < fo4::lod::k_object_atlas_maps; ++map) {
    EXPECT_EQ(atlas.maps[map].rgba, reference_atlas.maps[map].rgba) << map;
  }

  std::size_t flat = all.size();
  for (std::size_t at = levels.size(); at-- > 0;) {
    const auto& entry = levels[at];
    for (std::size_t index = entry.tiles.size(); index-- > 0;) {
      --flat;
      auto tile = fo4::lod::build_object_tile(assets, settings, entry.level, entry.tiles[index].x,
                                              entry.tiles[index].y, entry.collection);
      ASSERT_TRUE(plan.remap(tile)) << flat;
      const auto bytes = fo4::lod::write_object_bto(tile, "TestWorld");
      EXPECT_EQ(bytes, reference[flat]) << "level " << entry.level << " tile " << index;
    }
  }
  EXPECT_NE(raw_first, reference.front());
  std::filesystem::remove_all(folder);
}

// Detects (invariant): a tile with a shape the atlas never packed must be refused, never written with foreign UVs.
TEST(LodObjectTile, AtlasPlanRefusesATileWhoseShapeItNeverSaw) {
  const auto folder = std::filesystem::temp_directory_path() / "radicle_lod_atlas_refusal";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "Meshes");
  write_quad_with_material(folder, "a.nif", "Materials\\LOD\\A.BGSM");
  write_quad_with_material(folder, "b.nif", "Materials\\LOD\\B.BGSM");
  const auto assets = fo4::archive::AssetResolver::open(folder, {});
  const fo4::lod::LodSettings settings{0, 0, 256, 4, 32};
  const auto tile_of = [&](const char* mesh) {
    fo4::lod::LodCandidateCollection collection;
    auto candidate = candidate_at(0.0f, 0.0f, 0.0f);
    candidate.mesh = mesh;
    collection.candidates.push_back(candidate);
    return fo4::lod::build_object_tile(assets, settings, 8, 0, 0, collection);
  };
  auto seen = tile_of("a.nif");
  auto unseen = tile_of("b.nif");
  std::filesystem::remove_all(folder);
  ASSERT_FALSE(seen.shapes.empty());
  ASSERT_FALSE(unseen.shapes.empty());
  fo4::lod::ObjectAtlasPlan plan;
  plan.add(seen);
  plan.bake(assets);
  const auto before = unseen.shapes.front().vertices;
  EXPECT_FALSE(plan.remap(unseen));
  ASSERT_EQ(unseen.shapes.front().vertices.size(), before.size());
  for (std::size_t index = 0; index < before.size(); ++index) {
    EXPECT_EQ(unseen.shapes.front().vertices[index].u, before[index].u) << index;
    EXPECT_EQ(unseen.shapes.front().vertices[index].v, before[index].v) << index;
  }
  EXPECT_TRUE(plan.remap(seen));
}
