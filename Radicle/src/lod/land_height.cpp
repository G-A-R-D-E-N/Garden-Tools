#include "fo4/lod/land_height.hpp"

#include "fo4/constants.hpp"
#include "fo4/environment/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace fo4::lod {
namespace {

constexpr double k_quad_units = 128.0;
constexpr std::size_t k_quads = env::k_land_grid - 1;

}  // namespace

// Invariant: the quad diagonal alternates with (row + column) % 2, the split that reproduced the vanilla cull counts.
std::optional<double> land_height_at(const LandGrids& land, double x, double y) {
  if (land.grids.empty() && !land.present.empty()) throw LodError("land height needs heights, land is presence-only");
  if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
  const double cell_x = std::floor(x / k_cell_world_units);
  const double cell_y = std::floor(y / k_cell_world_units);
  if (!(std::fabs(cell_x) < 1.0e9 && std::fabs(cell_y) < 1.0e9)) return std::nullopt;
  const std::pair<std::int32_t, std::int32_t> key{static_cast<std::int32_t>(cell_x), static_cast<std::int32_t>(cell_y)};
  const auto found = land.grids.find(key);
  if (found == land.grids.end()) return std::nullopt;
  const double u = std::clamp((x - cell_x * k_cell_world_units) / k_quad_units, 0.0, static_cast<double>(k_quads));
  const double v = std::clamp((y - cell_y * k_cell_world_units) / k_quad_units, 0.0, static_cast<double>(k_quads));
  const auto column = std::min(static_cast<std::size_t>(u), k_quads - 1);
  const auto row = std::min(static_cast<std::size_t>(v), k_quads - 1);
  const double fu = u - static_cast<double>(column);
  const double fv = v - static_cast<double>(row);
  const auto& heights = found->second.heights;
  const auto at = [&heights](std::size_t r, std::size_t c) {
    return static_cast<double>(heights[r * env::k_land_grid + c]);
  };
  const double h00 = at(row, column);
  const double h10 = at(row, column + 1);
  const double h01 = at(row + 1, column);
  const double h11 = at(row + 1, column + 1);
  if ((row + column) % 2 == 0) {
    if (fu >= fv) return h00 + (h10 - h00) * fu + (h11 - h10) * fv;
    return h00 + (h11 - h01) * fu + (h01 - h00) * fv;
  }
  if (fu + fv <= 1.0) return h00 + (h10 - h00) * fu + (h01 - h00) * fv;
  return h11 + (h01 - h11) * (1.0 - fu) + (h10 - h11) * (1.0 - fv);
}

}  // namespace fo4::lod
