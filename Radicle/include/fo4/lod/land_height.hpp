#pragma once

#include "fo4/lod/terrain_heightfield.hpp"

#include <optional>

namespace fo4::lod {

// Invariant: the height of the LAND quad triangle under a world point; nullopt where that cell has no LAND.
std::optional<double> land_height_at(const LandGrids& land, double x, double y);

}  // namespace fo4::lod
