#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::lod {

// Invariant: RGBA8, row-major, width and height are the measured vanilla terrain texture size.
inline constexpr int k_terrain_texture_side = 512;

enum class TerrainDdsFormat : std::uint8_t {
  bgra8 = 87,
  bc3 = 77,
};

enum class TerrainTextureMap : std::uint8_t { diffuse, normal };

inline constexpr std::size_t k_terrain_texture_maps = 2;

std::string_view terrain_texture_map_name(TerrainTextureMap map);

// Invariant: the .btr texture set and the terrain atlas writer both call this, so their names cannot drift.
std::string terrain_texture_path(std::string_view worldspace, std::int32_t level, std::int32_t x, std::int32_t y,
                                 TerrainTextureMap map);

// Invariant: a terrain atlas ledger key has three parts, so it can never parse as a five-part tile key.
std::string terrain_texture_ledger_key(std::string_view worldspace, std::int32_t level, std::int32_t x,
                                       std::int32_t y, TerrainTextureMap map);

// Invariant: the local file is the tile's Data-less path, so both platforms agree on one layout.
std::filesystem::path terrain_texture_local_path(const std::filesystem::path& data_dir, std::string_view worldspace,
                                                 std::int32_t level, std::int32_t x, std::int32_t y,
                                                 TerrainTextureMap map);

struct TerrainImage {
  int width{0};
  int height{0};
  // Design choice, unmeasured: land_cells must stand in for the 462 textureless vanilla tiles.
  std::size_t land_cells{0};
  std::vector<std::uint8_t> rgba;
};

struct TerrainTextures {
  TerrainImage diffuse;
  TerrainImage normal;
};

// Invariant: a world's CELL grid is decoded once and reused, as decoding every cell per tile was the slow path.
using TerrainCellIndex = std::map<std::pair<std::int32_t, std::int32_t>, const env::RecordRef*>;
TerrainCellIndex build_terrain_cell_index(const env::Environment& environment, const FormKey& worldspace);

// Invariant: an atlas is asked for only for a tile the writer counts a land cell for, so both use this predicate.
bool terrain_tile_has_land(const TerrainCellIndex& by_grid, const env::Environment& environment, std::int32_t level,
                           std::int32_t tile_x, std::int32_t tile_y);

// Invariant: one cell pass serves both bakes, so diffuse and normal share land_cells.
TerrainTextures bake_terrain_textures(const env::Environment& environment, const FormKey& worldspace,
                                      const LodSettings& settings, std::int32_t level, std::int32_t tile_x,
                                      std::int32_t tile_y);

// Invariant: the indexed overload reads the same cells as the one above, with byte-identical output.
TerrainTextures bake_terrain_textures(const TerrainCellIndex& index, const env::Environment& environment,
                                      const LodSettings& settings, std::int32_t level, std::int32_t tile_x,
                                      std::int32_t tile_y);

bool terrain_tile_needs_texture(const TerrainImage& image);

enum class TerrainDdsAlpha : std::uint8_t { opaque, keep };

// Invariant: the DDS has one mip per halving down to 1x1; alpha is forced opaque unless the caller keeps it.
using TerrainMipClamp = void (*)(std::vector<std::uint8_t>& level, int width, int height, int mip,
                                 const void* context);
std::vector<std::byte> write_terrain_dds(const TerrainImage& image, TerrainDdsFormat format = TerrainDdsFormat::bc3,
                                         TerrainDdsAlpha alpha = TerrainDdsAlpha::opaque,
                                         TerrainMipClamp clamp = nullptr, const void* clamp_context = nullptr);

}  // namespace fo4::lod
