#include "fo4/lod/tile_index.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace {

using GridKey = std::tuple<std::string, std::uint8_t, std::int32_t>;

std::uint8_t kind_order(LodTileKind kind) {
  return kind == LodTileKind::object ? 0u : 1u;
}

bool tile_before(const LodTile& left, const LodTile& right) {
  if (left.y != right.y) return left.y < right.y;
  return left.x < right.x;
}

}  // namespace

LodTileIndex build_lod_tile_index(std::span<const std::string> tile_paths) {
  LodTileIndex index;
  std::map<GridKey, LodTileGrid> grids;
  for (const auto& path : tile_paths) {
    const auto tile = parse_lod_tile_path(path);
    if (!tile.has_value()) {
      ++index.unparsed_paths;
      continue;
    }
    const auto key = GridKey{tile->worldspace, kind_order(tile->kind), tile->level};
    auto found = grids.find(key);
    if (found == grids.end()) {
      LodTileGrid grid;
      grid.worldspace = tile->worldspace;
      grid.kind = tile->kind;
      grid.level = tile->level;
      found = grids.emplace(key, std::move(grid)).first;
    }
    auto& tiles = found->second.tiles;
    if (std::any_of(tiles.begin(), tiles.end(), [&](const LodTile& existing) {
          return existing.x == tile->x && existing.y == tile->y;
        })) {
      ++index.duplicate_paths;
      continue;
    }
    tiles.push_back(*tile);
  }
  index.grids.reserve(grids.size());
  for (auto& entry : grids) {
    std::sort(entry.second.tiles.begin(), entry.second.tiles.end(), tile_before);
    index.grids.push_back(std::move(entry.second));
  }
  return index;
}

const LodTileGrid* find_lod_tile_grid(const LodTileIndex& index,
                                      std::string_view worldspace,
                                      LodTileKind kind,
                                      std::int32_t level) {
  for (const auto& grid : index.grids) {
    if (grid.worldspace == worldspace && grid.kind == kind && grid.level == level) {
      return &grid;
    }
  }
  return nullptr;
}

bool lod_tile_index_contains(const LodTileIndex& index, const LodTile& tile) {
  const auto* grid = find_lod_tile_grid(index, tile.worldspace, tile.kind, tile.level);
  if (grid == nullptr) return false;
  return std::any_of(grid->tiles.begin(), grid->tiles.end(), [&](const LodTile& existing) {
    return existing.x == tile.x && existing.y == tile.y;
  });
}

}  // namespace fo4::lod
