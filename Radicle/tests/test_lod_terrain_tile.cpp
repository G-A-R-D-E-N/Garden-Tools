#include "fo4/lod/terrain_tile.hpp"

#include "env_node_fixture.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::envnode;

std::vector<std::byte> xclc(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  return out.bytes;
}

std::vector<std::byte> flat_vhgt(float offset) {
  fo4::test::ByteWriter out;
  out.f32(offset);
  for (std::size_t i = 0; i < fo4::env::k_land_vertices; ++i) out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

std::vector<std::byte> world_plugin(std::int32_t cell_x, std::int32_t cell_y, float offset) {
  std::vector<Node> flat{
      rec("CELL", 0x802, {{"XCLC", xclc(cell_x, cell_y)}}),
      grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {{"VHGT", flat_vhgt(offset)}})})})};
  return make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                       grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(flat))})})})});
}

fo4::env::Environment load_env(const std::vector<std::byte>& bytes) {
  std::vector<fo4::Plugin> plugins;
  plugins.push_back(fo4::read_plugin(bytes, "Test.esp"));
  return fo4::env::Environment::from_plugins(std::move(plugins));
}

fo4::lod::TerrainTile small_tile() {
  fo4::lod::TerrainTile tile;
  tile.level = 4;
  tile.origin_cell_x = 0;
  tile.origin_cell_y = 0;
  tile.step = 1;
  tile.side = 2;
  fo4::lod::TileWaterShape water;
  for (std::uint32_t index = 0; index < 4; ++index) {
    const float x = static_cast<float>(index % 2) * 1024.0f;
    const float y = static_cast<float>(index / 2) * 1024.0f;
    tile.land_vertices.push_back(fo4::lod::TileVertex{x, y, 10.0f, x / 4096.0f, 1.0f - y / 4096.0f});
    water.vertices.push_back(fo4::lod::TileVertex{x, y, 20.0f, 0.0f, 0.0f});
  }
  tile.land_triangles = {0, 1, 2, 1, 3, 2};
  water.triangles = {0, 1, 2, 1, 3, 2};
  water.cells = {0};
  water.bound = {512.0f, 512.0f, 20.0f, 724.0f};
  tile.water_shapes.push_back(water);
  tile.land_bound = {512.0f, 512.0f, 10.0f, 724.0f};
  tile.land_aabb = {8192.0f, 8192.0f, 40.0f, 8192.0f, 8192.0f, 240.0f};
  tile.water_aabb = {8192.0f, 8192.0f, 80.0f, 8192.0f, 8192.0f, 0.0f};
  return tile;
}

std::uint64_t descriptor(const fo4::nif::NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  const auto base = info.offset;
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[base + 100 + i]) << (8 * i);
  return value;
}

std::vector<float> block_floats(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t count) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::vector<float> out;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t bits = 0;
    for (std::size_t k = 0; k < 4; ++k) bits |= static_cast<std::uint32_t>(bytes[info.offset + i * 4 + k]) << (8 * k);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    out.push_back(value);
  }
  return out;
}

std::uint32_t block_word(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t index) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::uint32_t value = 0;
  for (std::size_t k = 0; k < 4; ++k) value |= static_cast<std::uint32_t>(bytes[info.offset + index * 4 + k]) << (8 * k);
  return value;
}

std::size_t count_type(const fo4::nif::NifFile& nif, std::string_view type) {
  std::size_t count = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type == type) ++count;
  }
  return count;
}

fo4::lod::WorldTerrain terrain_with(
    int cells,
    const std::function<float(int cx, int cy, std::size_t row, std::size_t column)>& height,
    bool with_water, float water_height) {
  fo4::lod::WorldTerrain terrain;
  for (int cy = 0; cy < cells; ++cy) {
    for (int cx = 0; cx < cells; ++cx) {
      fo4::env::LandHeightGrid grid{};
      for (std::size_t row = 0; row < fo4::env::k_land_grid; ++row) {
        for (std::size_t column = 0; column < fo4::env::k_land_grid; ++column) {
          grid.heights[row * fo4::env::k_land_grid + column] = height(cx, cy, row, column);
        }
      }
      const std::pair<std::int32_t, std::int32_t> key{cx, cy};
      terrain.land.grids.emplace(key, grid);
      if (with_water) terrain.water_heights.emplace(key, water_height);
    }
  }
  terrain.has_water = with_water;
  terrain.default_water_height = water_height;
  return terrain;
}

std::size_t unreferenced_land_vertices(const fo4::lod::TerrainTile& tile) {
  std::vector<bool> referenced(tile.land_vertices.size(), false);
  for (const auto index : tile.land_triangles) {
    if (index < referenced.size()) referenced[index] = true;
  }
  std::size_t missing = 0;
  for (const auto used : referenced) {
    if (!used) ++missing;
  }
  return missing;
}

// Invariant: lattice x, y are distinct, so a skirt twin is a vertex repeating an earlier x, y, even on partial borders.
std::size_t skirt_vertex_count(const fo4::lod::TerrainTile& tile) {
  std::set<std::pair<float, float>> seen;
  std::size_t twins = 0;
  for (const auto& vertex : tile.land_vertices) {
    if (!seen.emplace(vertex.x, vertex.y).second) ++twins;
  }
  return twins;
}

}  // namespace

// Detects (invariant): a written tile that the LOD reader rejects or reads as fewer than two shapes, or a level-4
// water shape that is a plain BSTriShape instead of the vanilla BSSubIndexTriShape.
TEST(LodTerrainTile, WritesATileTheReaderAcceptsAsTerrainLod) {
  const auto bytes = fo4::lod::write_terrain_btr(small_tile(), "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain),
            fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.roots().size(), 1u);
  EXPECT_EQ(nif.blocks()[nif.roots().front()].type, "BSMultiBoundNode");
  EXPECT_EQ(count_type(nif, "BSTriShape"), 1u);
  EXPECT_EQ(count_type(nif, "BSSubIndexTriShape"), 1u);
  EXPECT_EQ(nif.blocks().at(5).type, "BSSubIndexTriShape");
  EXPECT_EQ(count_type(nif, "BSMultiBoundNode"), 2u);
  EXPECT_EQ(count_type(nif, "BSMultiBound"), 2u);
  EXPECT_EQ(count_type(nif, "BSMultiBoundAABB"), 2u);
  EXPECT_EQ(count_type(nif, "BSLightingShaderProperty"), 1u);
  EXPECT_EQ(count_type(nif, "BSEffectShaderProperty"), 1u);
  EXPECT_EQ(count_type(nif, "BSShaderTextureSet"), 1u);
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 2u);
  EXPECT_EQ(shapes[0].uvs.size(), 8u);
  EXPECT_TRUE(shapes[0].normals.empty());
  EXPECT_EQ(shapes[1].uvs.size(), 0u);
  EXPECT_EQ(shapes[0].tris.size(), 6u);
  EXPECT_EQ(shapes[1].tris.size(), 6u);
  EXPECT_EQ(descriptor(nif, 1) & 0xFU, 3u);
  EXPECT_EQ((descriptor(nif, 1) >> 44U) & 0xFU, 3u);
  EXPECT_EQ(descriptor(nif, 5) & 0xFU, 2u);
  EXPECT_EQ((descriptor(nif, 5) >> 44U) & 0xFU, 1u);
  const auto paths = shapes[0].textures;
  ASSERT_GE(paths.size(), 2u);
  EXPECT_EQ(paths[0], "Data\\Textures\\Terrain\\Commonwealth\\Commonwealth.4.0.0.DDS");
  EXPECT_EQ(paths[1], "Data\\Textures\\Terrain\\Commonwealth\\Commonwealth.4.0.0_msn.DDS");
  EXPECT_EQ(block_floats(nif, 8, 6), (std::vector<float>{8192.0f, 8192.0f, 80.0f, 8192.0f, 8192.0f, 0.0f}));
}

// Detects (regression): shader blocks that drift from the one land and the one water shader every vanilla tile
// carries. Ours were type 0, name -1, flags 0x80400201/0x21, root material -1, clamp 3, smoothness 1 for the land,
// and a water block whose extra-data count read back as 0xFFFFFFFF. Pins the decoded fields, the name and root
// material indices (2, the empty string), single-sided flags, the block sizes, and the cull-mode word of both nodes.
TEST(LodTerrainTile, WritesTheVanillaLandAndWaterShaderBlocksAndBoundNodes) {
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(small_tile(), "Commonwealth"));
  EXPECT_EQ(nif.blocks().at(2).size, 140u);
  const auto land = fo4::nif::read_lighting_shader(nif, 2);
  EXPECT_EQ(land.shader_type, 18u);
  EXPECT_EQ(land.flags.word1, 0x80401000U);
  EXPECT_EQ(land.flags.word2, 0x3U);
  EXPECT_EQ(land.flags.word2 & 0x10U, 0u);
  EXPECT_EQ(block_word(nif, 2, 1), 2u);
  EXPECT_EQ(block_word(nif, 2, 15), 2u);
  EXPECT_TRUE(land.name.empty());
  EXPECT_TRUE(land.has_root_material);
  EXPECT_TRUE(land.root_material.empty());
  EXPECT_EQ(land.texture_set_ref, 3);
  EXPECT_EQ(land.texture_clamp, 0u);
  EXPECT_FLOAT_EQ(land.uv_offset[0], 0.0f);
  EXPECT_FLOAT_EQ(land.uv_offset[1], 0.0f);
  EXPECT_FLOAT_EQ(land.uv_scale[0], 1.0f);
  EXPECT_FLOAT_EQ(land.uv_scale[1], 1.0f);
  EXPECT_FLOAT_EQ(land.smoothness, 0.0f);
  EXPECT_FLOAT_EQ(land.alpha, 1.0f);
  EXPECT_FLOAT_EQ(land.specular_strength, 1.0f);
  EXPECT_FLOAT_EQ(land.rimlight_power, std::numeric_limits<float>::max());
  EXPECT_FLOAT_EQ(land.fresnel_power, 5.0f);
  EXPECT_FLOAT_EQ(land.wetness.metalness, -1.0f);

  EXPECT_EQ(nif.blocks().at(6).size, 104u);
  const auto water = fo4::nif::read_effect_shader(nif, 6);
  EXPECT_EQ(block_word(nif, 6, 0), 2u);
  EXPECT_EQ(block_word(nif, 6, 2), 0xFFFFFFFFU);
  EXPECT_TRUE(water.name.empty());
  EXPECT_EQ(water.flags.word1, 0x80000000U);
  EXPECT_EQ(water.flags.word2, 0x1U);
  EXPECT_TRUE(water.source_texture.empty());
  EXPECT_EQ(static_cast<unsigned>(water.texture_clamp), 3u);
  EXPECT_EQ(static_cast<unsigned>(water.lighting_influence), 0xFFu);
  EXPECT_FLOAT_EQ(water.uv_scale[0], 1.0f);
  EXPECT_FLOAT_EQ(water.uv_scale[1], 1.0f);
  for (const float value : water.base_color) EXPECT_FLOAT_EQ(value, 1.0f);
  EXPECT_FLOAT_EQ(water.base_color_scale, 1.0f);
  EXPECT_FLOAT_EQ(water.soft_falloff_depth, 100.0f);
  EXPECT_FLOAT_EQ(water.env_map_scale, 1.0f);
  EXPECT_EQ(block_word(nif, 5, 0), 2u);

  EXPECT_EQ(nif.blocks().at(0).size, 92u);
  EXPECT_EQ(nif.blocks().at(4).size, 88u);
  EXPECT_EQ(block_word(nif, 0, 22), 1u);
  EXPECT_EQ(block_word(nif, 4, 21), 1u);
}

// Detects (regression): a tile without water that still carries a WATER node with an empty zero-vertex shape and an
// unused effect shader. Vanilla has 2,562 such tiles, all 6 blocks: node, land shape, shader, texture set, bound, AABB.
TEST(LodTerrainTile, OmitsTheWaterBranchOfATileWithoutWater) {
  auto tile = small_tile();
  tile.water_shapes.clear();
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain), fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.blocks().size(), 6u);
  EXPECT_EQ(count_type(nif, "BSEffectShaderProperty"), 0u);
  EXPECT_EQ(count_type(nif, "BSMultiBoundNode"), 1u);
  EXPECT_EQ(nif.blocks().at(0).size, 88u);
  EXPECT_EQ(block_word(nif, 0, 18), 1u);
  EXPECT_EQ(block_word(nif, 0, 20), 4u);
  EXPECT_EQ(fo4::nif::read_shapes(nif).size(), 1u);
  EXPECT_EQ(block_floats(nif, 5, 6), (std::vector<float>{8192.0f, 8192.0f, 40.0f, 8192.0f, 8192.0f, 240.0f}));
}

// Detects (regression): a cell with no LAND left as a hole where vanilla meshes it at the worldspace default land
// height (Diamond City -2048, Far Harbor 0). Only cell (1,1) has LAND at 800; the rest must be -2048 / 4 = -512 in
// stored units, and the same terrain without a default must still leave the hole.
TEST(LodTerrainTile, MeshesCellsWithoutLandAtTheWorldspaceDefaultLandHeight) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 800.0f; }, false, 0.0f);
  for (int cy = 0; cy < 4; ++cy) {
    for (int cx = 0; cx < 4; ++cx) {
      if (cx == 1 && cy == 1) continue;
      terrain.land.grids.erase(std::pair<std::int32_t, std::int32_t>{cx, cy});
    }
  }
  const auto in_first_cell = [](const fo4::lod::TileVertex& vertex) {
    return vertex.x > 0.0f && vertex.x < 1024.0f && vertex.y > 0.0f && vertex.y < 1024.0f;
  };
  const auto holed = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(std::count_if(holed.land_vertices.begin(), holed.land_vertices.end(), in_first_cell), 0);
  terrain.default_land_height = -2048.0f;
  const auto filled = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(filled.land_cells, 1u);
  EXPECT_GT(std::count_if(filled.land_vertices.begin(), filled.land_vertices.end(), in_first_cell), 0);
  for (const auto& vertex : filled.land_vertices) {
    if (in_first_cell(vertex)) {
      EXPECT_FLOAT_EQ(vertex.z, -512.0f) << vertex.x << ',' << vertex.y;
    }
  }
  EXPECT_EQ(unreferenced_land_vertices(filled), 0u);
}

// Detects (regression): a tile whose cells all lack LAND but have water (Far Harbor: 1,511 of 1,511 such tiles are
// flat land at the default height 0) coming out with no terrain, instead of the flat 3x3 lattice of 17 vertices and
// 24 triangles.
TEST(LodTerrainTile, GivesAWaterOnlyTileFlatLandAtTheDefaultLandHeight) {
  fo4::lod::WorldTerrain terrain;
  terrain.has_water = true;
  terrain.default_water_height = 112.0f;
  terrain.default_land_height = 0.0f;
  for (int cy = 0; cy < 4; ++cy) {
    for (int cx = 0; cx < 4; ++cx) {
      terrain.water_heights.emplace(std::pair<std::int32_t, std::int32_t>{cx, cy}, 112.0f);
    }
  }
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(tile.land_cells, 0u);
  EXPECT_EQ(tile.water_cells, 16u);
  EXPECT_EQ(tile.side, 3u);
  ASSERT_EQ(tile.land_vertices.size(), 17u);
  EXPECT_EQ(tile.land_triangles.size() / 3, 24u);
  for (std::size_t index = 0; index < 9; ++index) EXPECT_FLOAT_EQ(tile.land_vertices[index].z, 0.0f) << index;
}

// Detects (invariant): a simplification step that leaves more than 129 samples per side, which no u16 index can hold.
TEST(LodTerrainTile, SimplifiesEveryLevelToAtMostOneHundredAndTwentyNineSamples) {
  const auto env = load_env(world_plugin(0, 0, 10.0f));
  const auto worldspace = fo4::FormKey::parse("000800:Test.esp");
  const fo4::lod::LodSettings settings{};
  for (const auto& [level, step] : {std::pair<std::int32_t, std::size_t>{4, 1}, {8, 2}, {16, 4}, {32, 8}}) {
    const auto tile = fo4::lod::build_terrain_tile(env, worldspace, settings, level, 0, 0);
    EXPECT_LE(tile.side, 129u) << level;
    EXPECT_GE(tile.side, 3u) << level;
    EXPECT_GE(tile.step, step) << level;
    EXPECT_LE(tile.land_vertices.size(), 65535u) << level;
    EXPECT_TRUE(tile.water_shapes.empty()) << level;
  }
}

// Detects (regression): a cell without its own XCLW must still be water at the worldspace default
// height; dropping that rule must empty every ocean tile again (real-data level 4: 0 vs 64 verts).
TEST(LodTerrainTile, UsesTheWorldspaceDefaultWaterHeightForCellsWithoutTheirOwn) {
  fo4::test::ByteWriter dnam;
  dnam.f32(-2048.0f);
  dnam.f32(450.0f);
  std::vector<Node> cells{
      rec("CELL", 0x802, {{"XCLC", xclc(0, 0)}}),
      grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {{"VHGT", flat_vhgt(10.0f)}})})})};
  const auto bytes = make_plugin(
      {}, {top("WATR", {rec("WATR", 0x18, {})}),
           top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")},
                                              {"NAM2", bytes_of({0x18})},
                                              {"DNAM", dnam.bytes}}),
                         grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(cells))})})})});
  const auto env = load_env(bytes);
  const auto worldspace = fo4::FormKey::parse("000800:Test.esp");
  const auto terrain = fo4::lod::collect_world_terrain(env, worldspace);
  EXPECT_TRUE(terrain.has_water);
  ASSERT_EQ(terrain.water_heights.size(), 1u);
  EXPECT_FLOAT_EQ(terrain.default_water_height, 450.0f);
  ASSERT_TRUE(terrain.default_land_height.has_value());
  EXPECT_FLOAT_EQ(*terrain.default_land_height, -2048.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(tile.land_cells, 1u);
  EXPECT_EQ(tile.water_cells, 1u);
  ASSERT_EQ(tile.water_shapes.size(), 1u);
  EXPECT_EQ(tile.water_shapes.front().vertices.size(), 4u);
  EXPECT_EQ(tile.water_shapes.front().triangles.size(), 6u);
  EXPECT_GT(tile.land_vertices.size(), 0u);
}

// Detects (invariant): a fully submerged tile that loses its whole land shape.
TEST(LodTerrainTile, KeepsTheTerrainOfATileWhoseWholeSurfaceIsUnderWater) {
  fo4::test::ByteWriter dnam;
  dnam.f32(-2048.0f);
  dnam.f32(450.0f);
  std::vector<Node> cells;
  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      const std::uint32_t cell_id = 0x802 + y * 4 + x;
      cells.push_back(rec("CELL", cell_id, {{"XCLC", xclc(static_cast<std::int32_t>(x),
                                                            static_cast<std::int32_t>(y))}}));
      cells.push_back(grp(cell_id, 6, {grp(cell_id, 9, {rec("LAND", 0x900 + cell_id,
                                                              {{"VHGT", flat_vhgt(10.0f)}})})}));
    }
  }
  const auto bytes = make_plugin(
      {}, {top("WATR", {rec("WATR", 0x18, {})}),
           top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")},
                                              {"NAM2", bytes_of({0x18})},
                                              {"DNAM", dnam.bytes}}),
                         grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(cells))})})})});
  const auto env = load_env(bytes);
  const auto worldspace = fo4::FormKey::parse("000800:Test.esp");
  const auto terrain = fo4::lod::collect_world_terrain(env, worldspace);
  ASSERT_EQ(terrain.water_heights.size(), 16u);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(tile.water_cells, 16u);
  EXPECT_FALSE(tile.land_vertices.empty());
  EXPECT_FALSE(tile.land_triangles.empty());
}

// Detects (invariant): a submerged seabed kept, and the one dry sample referenced by a triangle.
TEST(LodTerrainTile, KeepsEverySubmergedVertexAndTheOneDrySampleAmongThem) {
  constexpr float k_water = 450.0f;
  const auto terrain = terrain_with(
      4,
      [](int cx, int cy, std::size_t row, std::size_t column) {
        if (cx == 0 && cy == 0 && row == 16 && column == 16) return 900.0f;
        return -50.0f;
      },
      true, k_water);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_FALSE(tile.land_vertices.empty());
  std::size_t below = 0;
  std::size_t dry_index = tile.land_vertices.size();
  for (std::size_t index = 0; index < tile.land_vertices.size(); ++index) {
    const float world = tile.land_vertices[index].z * 4.0f;
    if (world < k_water) ++below;
    if (std::fabs(world - 900.0f) < 0.5f) dry_index = index;
  }
  EXPECT_GT(below, 100u);
  ASSERT_LT(dry_index, tile.land_vertices.size());
  const auto referenced = std::find(tile.land_triangles.begin(), tile.land_triangles.end(),
                                    static_cast<std::uint16_t>(dry_index));
  EXPECT_NE(referenced, tile.land_triangles.end());
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
}

// Detects (invariant): interior heightfield relief carried into the triangle mesh, not one quad per cell.
TEST(LodTerrainTile, CarriesInteriorReliefIntoTheTriangleMesh) {
  const auto terrain = terrain_with(
      4,
      [](int cx, int cy, std::size_t row, std::size_t column) {
        const auto x = static_cast<float>(cx * 32) + static_cast<float>(column);
        const auto y = static_cast<float>(cy * 32) + static_cast<float>(row);
        return 200.0f * std::sin(x * 0.15f) + 150.0f * std::cos(y * 0.11f);
      },
      false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  const auto land_count = tile.land_vertices.size() - skirt_vertex_count(tile);
  float lowest = tile.land_vertices.front().z;
  float highest = lowest;
  for (std::size_t index = 0; index < land_count; ++index) {
    lowest = std::min(lowest, tile.land_vertices[index].z);
    highest = std::max(highest, tile.land_vertices[index].z);
  }
  EXPECT_GT(tile.land_triangles.size() / 3 - 2 * skirt_vertex_count(tile), 1000u);
  EXPECT_GT((highest - lowest) * 4.0f, 300.0f);
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
  for (const auto index : tile.land_triangles) EXPECT_LT(index, tile.land_vertices.size());
}

// Detects (invariant): no vertex emitted that a triangle does not reference.
TEST(LodTerrainTile, EmitsNoLandVertexThatNoTriangleReferences) {
  const auto terrain = terrain_with(
      4,
      [](int cx, int cy, std::size_t row, std::size_t column) {
        return 40.0f * static_cast<float>((row * 7 + column * 3 + cx + cy) % 11);
      },
      false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_FALSE(tile.land_vertices.empty());
  ASSERT_FALSE(tile.land_triangles.empty());
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
  const auto highest = *std::max_element(tile.land_triangles.begin(), tile.land_triangles.end());
  EXPECT_EQ(static_cast<std::size_t>(highest) + 1, tile.land_vertices.size());
}

// Detects (regression): a flat tile decimated short of the vanilla 3x3 lattice (a minimum sample count above 3 gave
// 5x5, 41 vertices and 64 triangles), or one that ships every sample. Vanilla flat tiles: 17 vertices, 24 triangles.
TEST(LodTerrainTile, DecimatesAFlatTileToTheThreeByThreeVanillaLattice) {
  const auto terrain =
      terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(tile.side, 3u);
  EXPECT_EQ(skirt_vertex_count(tile), 8u);
  EXPECT_EQ(tile.land_vertices.size(), 17u);
  EXPECT_EQ(tile.land_triangles.size() / 3, 24u);
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
}

// Detects (invariant): the east and north tile edge samples survive decimation.
TEST(LodTerrainTile, KeepsTheEastAndNorthEdgeSamples) {
  const auto terrain = terrain_with(
      4,
      [](int cx, int cy, std::size_t row, std::size_t column) {
        return static_cast<float>(cx * 32 + column) * 0.5f + static_cast<float>(cy * 32 + row);
      },
      false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  float max_x = 0.0f;
  float max_y = 0.0f;
  const fo4::lod::TileVertex* corner = nullptr;
  for (const auto& vertex : tile.land_vertices) {
    max_x = std::max(max_x, vertex.x);
    max_y = std::max(max_y, vertex.y);
    if (vertex.x > 4095.0f && vertex.y > 4095.0f && corner == nullptr) corner = &vertex;
  }
  EXPECT_FLOAT_EQ(max_x, 4096.0f);
  EXPECT_FLOAT_EQ(max_y, 4096.0f);
  ASSERT_NE(corner, nullptr);
  EXPECT_FLOAT_EQ(corner->z * 4.0f, 32.0f * 4.0f * 0.5f + 32.0f * 4.0f);
}

// Detects (invariant): a missing cell stays unmeshed instead of being bridged.
TEST(LodTerrainTile, LeavesAMissingCellUnmeshed) {
  auto terrain = terrain_with(
      4,
      [](int cx, int cy, std::size_t row, std::size_t column) {
        return 30.0f * static_cast<float>((row + column + cx * 3 + cy * 5) % 9);
      },
      false, 0.0f);
  terrain.land.grids.erase({1, 1});
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_FALSE(tile.land_triangles.empty());
  for (const auto& vertex : tile.land_vertices) {
    const bool inside_hole =
        vertex.x > 1024.0f && vertex.x < 2048.0f && vertex.y > 1024.0f && vertex.y < 2048.0f;
    EXPECT_FALSE(inside_hole) << vertex.x << ',' << vertex.y;
  }
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
}

// Detects (regression): a flat tile with one missing cell that either decimates on to the 3x3 minimum and bridges
// the gap with the corner samples its neighbours share, or stops above the one-quad-per-cell 5x5 lattice.
TEST(LodTerrainTile, DecimatesAroundAMissingCellWithoutBridgingIt) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  terrain.land.grids.erase({1, 1});
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(tile.land_cells, 15u);
  EXPECT_EQ(tile.side, 5u);
  const auto skirt = skirt_vertex_count(tile);
  EXPECT_EQ(skirt, 16u);
  EXPECT_EQ(tile.land_vertices.size(), 25u + skirt);
  EXPECT_EQ(tile.land_triangles.size() / 3, 30u + 2 * skirt);
  for (std::size_t first = 0; first + 2 < tile.land_triangles.size(); first += 3) {
    float x = 0.0f;
    float y = 0.0f;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const auto& vertex = tile.land_vertices.at(tile.land_triangles[first + corner]);
      x += vertex.x / 3.0f;
      y += vertex.y / 3.0f;
    }
    const bool inside_hole = x > 1024.0f && x < 2048.0f && y > 1024.0f && y < 2048.0f;
    EXPECT_FALSE(inside_hole) << x << ',' << y;
  }
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
}

// Detects (regression): decimation that scores a lattice against a bilinear patch. A twisted surface is
// exactly bilinear on every quad, so that score is zero at any step while the emitted triangles cut 100
// units through the saddle of a 5x5 lattice (400 through a 3x3).
TEST(LodTerrainTile, KeepsTheEmittedTrianglesWithinToleranceOnATwistedSurface) {
  const auto surface = [](float x, float y) { return 6400.0f * (x / 128.0f) * (y / 128.0f); };
  const auto terrain = terrain_with(
      4,
      [&surface](int cx, int cy, std::size_t row, std::size_t column) {
        return surface(static_cast<float>(cx * 32) + static_cast<float>(column),
                       static_cast<float>(cy * 32) + static_cast<float>(row));
      },
      false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_FALSE(tile.land_triangles.empty());
  float worst = 0.0f;
  const auto land_count = tile.land_vertices.size() - skirt_vertex_count(tile);
  for (std::size_t first = 0; first + 2 < tile.land_triangles.size(); first += 3) {
    if (*std::max_element(tile.land_triangles.begin() + static_cast<std::ptrdiff_t>(first),
                          tile.land_triangles.begin() + static_cast<std::ptrdiff_t>(first + 3)) >= land_count) {
      continue;
    }
    for (std::size_t edge = 0; edge < 3; ++edge) {
      const auto& from = tile.land_vertices.at(tile.land_triangles[first + edge]);
      const auto& to = tile.land_vertices.at(tile.land_triangles[first + (edge + 1) % 3]);
      const float x = (from.x + to.x) * 0.5f / 32.0f;
      const float y = (from.y + to.y) * 0.5f / 32.0f;
      worst = std::max(worst, std::fabs((from.z + to.z) * 0.5f * 4.0f - surface(x, y)));
    }
  }
  EXPECT_LE(worst, 16.0f);
  EXPECT_GT(worst, 0.0f);
  // Invariant: the tolerance still buys decimation here; the budget lattice alone would be 33 a side, and a bilinear
  // score would run on to the 3x3 minimum.
  EXPECT_EQ(tile.side, 17u);
}

// Detects (invariant): a level-4 tile whose terrain is clipped away by water instead of kept as land.
TEST(LodTerrainTileReal, WritesATileThatMatchesTheVanillaLevelFourTileStructure) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);
  const auto worldspace = fo4::FormKey::parse("00003C:Fallout4.esm");

  // Invariant: measured on Meshes\Terrain\Commonwealth\Commonwealth.4.-96.-96.BTR (2,594 bytes, 11 blocks).
  constexpr std::size_t k_vanilla_land_vertices = 17;
  constexpr std::size_t k_vanilla_land_triangles = 24;
  constexpr std::size_t k_vanilla_water_vertices = 64;
  constexpr std::size_t k_vanilla_water_triangles = 32;
  constexpr std::uint64_t k_vanilla_land_descriptor = 0x300000000203ULL;
  constexpr std::uint64_t k_vanilla_water_descriptor = 0x100000000002ULL;

  const fo4::lod::LodSettings commonwealth{-96, -96, 256, 4, 32};
  const auto tile = fo4::lod::build_terrain_tile(env, worldspace, commonwealth, 4, -96, -96);
  ASSERT_EQ(tile.water_shapes.size(), 1u);
  const auto bytes = fo4::lod::write_terrain_btr(tile, "Commonwealth");
  const auto nif = fo4::nif::read_nif(bytes);
  std::cout << "tile level 4 (-96,-96): bytes " << bytes.size() << " vs vanilla 2594, blocks "
            << nif.blocks().size() << " vs vanilla 11, land verts " << tile.land_vertices.size() << " vs "
            << k_vanilla_land_vertices << ", land tris " << tile.land_triangles.size() / 3 << " vs "
            << k_vanilla_land_triangles << ", water verts " << tile.water_shapes.front().vertices.size() << " vs "
            << k_vanilla_water_vertices << ", water tris " << tile.water_shapes.front().triangles.size() / 3 << " vs "
            << k_vanilla_water_triangles << '\n';
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain),
            fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.blocks().size(), 11u);
  EXPECT_EQ(descriptor(nif, 1), k_vanilla_land_descriptor);
  EXPECT_EQ(descriptor(nif, 5), k_vanilla_water_descriptor);
  // Invariant: vanilla keeps the seabed below the water plane, so a submerged tile still carries land.
  std::size_t below_water = 0;
  for (const auto& vertex : tile.land_vertices) {
    if (vertex.z * 4.0f < 112.5f) ++below_water;
  }
  EXPECT_GT(below_water, 0u);
  const auto skirt = skirt_vertex_count(tile);
  EXPECT_GE(tile.land_vertices.size() - skirt, 9u);
  EXPECT_LE(tile.land_vertices.size() - skirt, 48u);
  EXPECT_LE(tile.land_triangles.size() / 3 - 2 * skirt, 64u);
  EXPECT_EQ(tile.land_triangles.size() % 6, 0u);
  EXPECT_EQ(nif.blocks().at(5).type, "BSSubIndexTriShape");
  EXPECT_EQ(tile.water_shapes.front().vertices.size(), 64u);
  EXPECT_EQ(tile.water_shapes.front().triangles.size() / 3, tile.water_shapes.front().vertices.size() / 2);
  const auto land_aabb = block_floats(nif, 10, 6);
  const auto water_aabb = block_floats(nif, 8, 6);
  EXPECT_FLOAT_EQ(land_aabb[0], 8192.0f);
  EXPECT_FLOAT_EQ(land_aabb[1], 8192.0f);
  EXPECT_LT(bytes.size(), 168000u);
  EXPECT_FLOAT_EQ(water_aabb[0], 8192.0f);
  EXPECT_FLOAT_EQ(water_aabb[1], 8192.0f);
  EXPECT_FLOAT_EQ(water_aabb[5], 0.0f);
}
