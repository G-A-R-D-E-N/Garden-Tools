#include "fo4/lod/terrain_texture.hpp"

#include "fo4/environment/terrain.hpp"
#include "fo4/texture/dds.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace {

constexpr std::size_t k_pixel_bytes = 4;
constexpr std::size_t k_cell_span = env::k_land_grid - 1;

std::uint8_t clamp_byte(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<std::uint8_t>(value);
}

void fill_block(TerrainImage& image, int left, int top, int size, int red, int green, int blue) {
  for (int row = top; row < top + size; ++row) {
    if (row < 0 || row >= image.height) continue;
    for (int column = left; column < left + size; ++column) {
      if (column < 0 || column >= image.width) continue;
      const auto index = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                          static_cast<std::size_t>(column)) *
                         k_pixel_bytes;
      image.rgba[index] = clamp_byte(red);
      image.rgba[index + 1] = clamp_byte(green);
      image.rgba[index + 2] = clamp_byte(blue);
      image.rgba[index + 3] = 255;
    }
  }
}

void fill_all(TerrainImage& image, int red, int green, int blue) {
  for (auto& value : image.rgba) value = 0;
  fill_block(image, 0, 0, image.width, red, green, blue);
}

}  // namespace

TerrainCellIndex build_terrain_cell_index(const env::Environment& environment, const FormKey& worldspace) {
  TerrainCellIndex by_grid;
  for (const auto* cell : environment.worldspace_cells(worldspace)) {
    try {
      const auto data = env::decode_cell(environment, *cell);
      if (!data.grid) continue;
      by_grid[{data.grid->x, data.grid->y}] = cell;
    } catch (const env::WorldDecodeError&) {
      continue;
    }
  }
  return by_grid;
}

namespace {

const env::RecordRef* live_land(const env::Environment& environment, const env::RecordRef& cell) {
  const auto* land = environment.cell_landscape(cell.record->form_key);
  // Invariant: a deleted LAND means the cell has no terrain, matching the heightfield builder.
  if (land != nullptr && land->record->header.flags.is_deleted()) return nullptr;
  return land;
}

}  // namespace

std::string_view terrain_texture_map_name(TerrainTextureMap map) {
  return map == TerrainTextureMap::diffuse ? "terrain-diffuse" : "terrain-normal";
}

std::string terrain_texture_path(std::string_view worldspace, std::int32_t level, std::int32_t x, std::int32_t y,
                                 TerrainTextureMap map) {
  std::string path{"Data\\Textures\\Terrain\\"};
  path += worldspace;
  path += '\\';
  path += worldspace;
  path += '.' + std::to_string(level) + '.' + std::to_string(x) + '.' + std::to_string(y);
  path += map == TerrainTextureMap::diffuse ? ".DDS" : "_msn.DDS";
  return path;
}

std::string terrain_texture_ledger_key(std::string_view worldspace, std::int32_t level, std::int32_t x,
                                       std::int32_t y, TerrainTextureMap map) {
  std::string key;
  for (const char raw : worldspace) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw))));
  key += "|texture|terrain.";
  key += std::to_string(level) + '.' + std::to_string(x) + '.' + std::to_string(y);
  key += map == TerrainTextureMap::diffuse ? ".diffuse" : ".normal";
  return key;
}

std::filesystem::path terrain_texture_local_path(const std::filesystem::path& data_dir, std::string_view worldspace,
                                                 std::int32_t level, std::int32_t x, std::int32_t y,
                                                 TerrainTextureMap map) {
  std::filesystem::path path = data_dir;
  path /= "Textures";
  path /= "Terrain";
  path /= std::string{worldspace};
  std::string name{worldspace};
  name += '.' + std::to_string(level) + '.' + std::to_string(x) + '.' + std::to_string(y);
  name += map == TerrainTextureMap::diffuse ? ".DDS" : "_msn.DDS";
  path /= name;
  return path;
}

bool terrain_tile_has_land(const TerrainCellIndex& by_grid, const env::Environment& environment, std::int32_t level,
                           std::int32_t tile_x, std::int32_t tile_y) {
  for (int cy = 0; cy < level; ++cy) {
    for (int cx = 0; cx < level; ++cx) {
      const auto found = by_grid.find({tile_x + cx, tile_y + cy});
      if (found != by_grid.end() && live_land(environment, *found->second) != nullptr) return true;
    }
  }
  return false;
}

TerrainTextures bake_terrain_textures(const env::Environment& environment, const FormKey& worldspace,
                                      const LodSettings& settings, std::int32_t level, std::int32_t tile_x,
                                      std::int32_t tile_y) {
  return bake_terrain_textures(build_terrain_cell_index(environment, worldspace), environment, settings, level,
                               tile_x, tile_y);
}

TerrainTextures bake_terrain_textures(const TerrainCellIndex& by_grid, const env::Environment& environment,
                                      const LodSettings& settings, std::int32_t level, std::int32_t tile_x,
                                      std::int32_t tile_y) {
  const LodTile tile{std::string{}, level, tile_x, tile_y, LodTileKind::terrain};
  const auto placement = check_lod_tile_placement(settings, tile);
  if (placement != LodTilePlacement::ok) {
    throw LodError(std::string{"terrain texture placement is "} +
                   std::string{lod_tile_placement_name(placement)});
  }
  TerrainTextures textures;
  textures.diffuse.width = k_terrain_texture_side;
  textures.diffuse.height = k_terrain_texture_side;
  textures.normal.width = k_terrain_texture_side;
  textures.normal.height = k_terrain_texture_side;
  const auto pixels_total = static_cast<std::size_t>(k_terrain_texture_side) * k_terrain_texture_side;
  textures.diffuse.rgba.assign(pixels_total * k_pixel_bytes, 0);
  textures.normal.rgba.assign(pixels_total * k_pixel_bytes, 0);
  fill_all(textures.normal, 127, 127, 255);
  const int cells = level;
  const int pixels = k_terrain_texture_side / cells;
  std::size_t land_cells = 0;
  for (int cy = 0; cy < cells; ++cy) {
    for (int cx = 0; cx < cells; ++cx) {
      int layers = 0;
      int water = 0;
      const auto found = by_grid.find({tile_x + cx, tile_y + cy});
      if (found != by_grid.end()) {
        const auto* land = live_land(environment, *found->second);
        env::LandscapeData landscape;
        bool decoded = false;
        if (land != nullptr) {
          ++land_cells;
          try {
            landscape = env::decode_landscape(environment, *land);
            decoded = true;
          } catch (const env::WorldDecodeError&) {
            decoded = false;
          }
          try {
            if (env::resolve_water(environment, *found->second).height.has_value()) water = 1;
          } catch (const env::WorldDecodeError&) {
            water = 0;
          }
        }
        if (decoded) {
          for (const auto& layer : landscape.layers) {
            if (layer.base) ++layers;
          }
          if (landscape.normals.size() == env::k_land_vertices * 3) {
            for (int row = 0; row < pixels; ++row) {
              for (int column = 0; column < pixels; ++column) {
                // Invariant: the image top row is v = 1 and the tile's v axis is 1 - y, so rows flip.
                const auto cell_row = static_cast<std::size_t>(pixels - 1 - row) * k_cell_span /
                                      static_cast<std::size_t>(pixels);
                const auto cell_column =
                    static_cast<std::size_t>(column) * k_cell_span / static_cast<std::size_t>(pixels);
                const auto source = (cell_row * env::k_land_grid + cell_column) * 3;
                // Constraint: VNML bytes go straight into RGB in the diffuse row order; flat is 127/127/255.
                fill_block(textures.normal, cx * pixels + column, cy * pixels + row, 1,
                           std::to_integer<std::uint8_t>(landscape.normals[source]),
                           std::to_integer<std::uint8_t>(landscape.normals[source + 1]),
                           std::to_integer<std::uint8_t>(landscape.normals[source + 2]));
              }
            }
          }
        }
      }
      fill_block(textures.diffuse, cx * pixels, cy * pixels, pixels, 40 + layers * 40, 40 + cy * 8 + cx * 3,
                 water != 0 ? 120 : 60 + cx * 5);
    }
  }
  textures.diffuse.land_cells = land_cells;
  textures.normal.land_cells = land_cells;
  return textures;
}

bool terrain_tile_needs_texture(const TerrainImage& image) { return image.land_cells != 0; }

std::vector<std::byte> write_terrain_dds(const TerrainImage& image, TerrainDdsFormat format, TerrainDdsAlpha alpha,
                                         TerrainMipClamp clamp, const void* clamp_context) {
  if (image.width <= 0 || image.height <= 0 ||
      image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) *
                               k_pixel_bytes) {
    throw texture::dds_error{"InvalidImage", "terrain image size does not match its pixel buffer"};
  }
  texture::DdsInfo info;
  info.width = image.width;
  info.height = image.height;
  info.array_size = 1;
  info.depth = 1;
  info.is_cube_map = false;
  info.mip_count = 1;
  for (int side = std::max(image.width, image.height); side > 1; side >>= 1) ++info.mip_count;
  const auto dxgi = static_cast<std::uint8_t>(format);
  const auto found = texture::lookup_format(dxgi);
  if (!found.has_value()) throw texture::dds_error{"UnsupportedFormat", "terrain format has no DDS layout"};
  info.format = *found;
  auto header = texture::build_dds_header(info);
  std::vector<std::uint8_t> level(image.rgba);
  if (alpha == TerrainDdsAlpha::opaque) {
    for (std::size_t index = 0; index < level.size() / k_pixel_bytes; ++index) level[index * k_pixel_bytes + 3] = 255;
  }
  int width = image.width;
  int height = image.height;
  for (int mip = 0; mip < info.mip_count; ++mip) {
    if (mip > 0) {
      const int next_width = std::max(1, width >> 1);
      const int next_height = std::max(1, height >> 1);
      std::vector<std::uint8_t> next(static_cast<std::size_t>(next_width) * static_cast<std::size_t>(next_height) *
                                     k_pixel_bytes);
      for (int row = 0; row < next_height; ++row) {
        for (int column = 0; column < next_width; ++column) {
          for (std::size_t channel = 0; channel < k_pixel_bytes; ++channel) {
            int sum = 0;
            for (int dy = 0; dy < 2; ++dy) {
              for (int dx = 0; dx < 2; ++dx) {
                const int source_x = std::min(width - 1, column * 2 + dx);
                const int source_y = std::min(height - 1, row * 2 + dy);
                sum += level[(static_cast<std::size_t>(source_y) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(source_x)) *
                                 k_pixel_bytes +
                             channel];
              }
            }
            next[(static_cast<std::size_t>(row) * static_cast<std::size_t>(next_width) +
                  static_cast<std::size_t>(column)) *
                     k_pixel_bytes +
                 channel] = static_cast<std::uint8_t>((sum + 2) / 4);
          }
        }
      }
      level = std::move(next);
      width = next_width;
      height = next_height;
    }
    if (clamp != nullptr) clamp(level, width, height, mip, clamp_context);
    if (format == TerrainDdsFormat::bc3) {
      const auto encoded = texture::encode_bc3(level, width, height);
      header.insert(header.end(), encoded.begin(), encoded.end());
    } else {
      for (std::size_t index = 0; index < level.size() / k_pixel_bytes; ++index) {
        header.push_back(level[index * k_pixel_bytes + 2]);
        header.push_back(level[index * k_pixel_bytes + 1]);
        header.push_back(level[index * k_pixel_bytes]);
        header.push_back(level[index * k_pixel_bytes + 3]);
      }
    }
  }
  std::vector<std::byte> out;
  out.reserve(header.size());
  for (const auto value : header) out.push_back(static_cast<std::byte>(value));
  return out;
}

}  // namespace fo4::lod
