#include "fo4/lod/object_merge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace {

constexpr double k_cell_units = 4096.0;

bool level_usable(const LodSettings& settings, std::int32_t level) {
  if (level < settings.level_min || level > settings.level_max) return false;
  return level > 0 && (level & (level - 1)) == 0;
}

std::int64_t tile_cell_coordinate(double position, std::int16_t southwest_cell, std::int32_t level) {
  const auto cell = std::floor(position / k_cell_units);
  const auto delta = static_cast<std::int64_t>(cell) - static_cast<std::int64_t>(southwest_cell);
  const auto block = static_cast<std::int64_t>(level);
  auto index = delta / block;
  if ((delta % block) != 0 && delta < 0) --index;
  return static_cast<std::int64_t>(southwest_cell) + index * block;
}

bool tile_before(const LodObjectTile& left, const LodObjectTile& right) {
  if (left.y != right.y) return left.y < right.y;
  return left.x < right.x;
}

}  // namespace

LodObjectPlan plan_object_lod(std::span<const LodObject> objects,
                              const LodSettings& settings,
                              std::int32_t level,
                              std::size_t max_objects_per_tile) {
  if (!level_usable(settings, level)) {
    throw std::invalid_argument("object LOD level must be a power of two inside the settings range");
  }
  if (max_objects_per_tile == 0) {
    throw std::invalid_argument("object LOD batch cap must be at least one");
  }
  LodObjectPlan plan;
  std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::size_t>> grouped;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto& object = objects[index];
    if (!std::isfinite(object.x) || !std::isfinite(object.y)) {
      ++plan.dropped_objects;
      continue;
    }
    const auto x = tile_cell_coordinate(object.x, settings.southwest_x, level);
    const auto y = tile_cell_coordinate(object.y, settings.southwest_y, level);
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
      ++plan.dropped_objects;
      continue;
    }
    grouped[{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)}].push_back(index);
  }
  plan.tiles.reserve(grouped.size());
  for (auto& entry : grouped) {
    auto& indices = entry.second;
    for (std::size_t start = 0; start < indices.size(); start += max_objects_per_tile) {
      const auto count = std::min(max_objects_per_tile, indices.size() - start);
      LodObjectTile tile;
      tile.x = entry.first.first;
      tile.y = entry.first.second;
      tile.objects.assign(indices.begin() + static_cast<std::ptrdiff_t>(start),
                          indices.begin() + static_cast<std::ptrdiff_t>(start + count));
      plan.tiles.push_back(std::move(tile));
      if (start + count < indices.size()) ++plan.split_tiles;
    }
  }
  std::stable_sort(plan.tiles.begin(), plan.tiles.end(), tile_before);
  return plan;
}

}  // namespace lod
