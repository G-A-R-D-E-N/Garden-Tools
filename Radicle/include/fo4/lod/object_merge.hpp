#pragma once

#include "fo4/lod/lod.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::lod {

inline constexpr std::size_t k_shipped_max_shapes_per_object_tile = 12;

struct LodObject {
  double x{0.0};
  double y{0.0};
  std::string mesh;
};

struct LodObjectTile {
  std::int32_t x{0};
  std::int32_t y{0};
  std::vector<std::size_t> objects;
};

struct LodObjectPlan {
  std::vector<LodObjectTile> tiles;
  std::size_t split_tiles{0};
  std::size_t dropped_objects{0};
};

LodObjectPlan plan_object_lod(std::span<const LodObject> objects,
                              const LodSettings& settings,
                              std::int32_t level,
                              std::size_t max_objects_per_tile = k_shipped_max_shapes_per_object_tile);

}  // namespace fo4::lod
