#pragma once

#include "fo4/lod/lod.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::lod {

struct LodTileGrid {
  std::string worldspace;
  LodTileKind kind{LodTileKind::object};
  std::int32_t level{0};
  std::vector<LodTile> tiles;

  friend bool operator==(const LodTileGrid&, const LodTileGrid&) = default;
};

struct LodTileIndex {
  std::vector<LodTileGrid> grids;
  std::size_t unparsed_paths{0};
  std::size_t duplicate_paths{0};

  friend bool operator==(const LodTileIndex&, const LodTileIndex&) = default;
};

LodTileIndex build_lod_tile_index(std::span<const std::string> tile_paths);

const LodTileGrid* find_lod_tile_grid(const LodTileIndex& index,
                                      std::string_view worldspace,
                                      LodTileKind kind,
                                      std::int32_t level);

bool lod_tile_index_contains(const LodTileIndex& index, const LodTile& tile);

}  // namespace fo4::lod
