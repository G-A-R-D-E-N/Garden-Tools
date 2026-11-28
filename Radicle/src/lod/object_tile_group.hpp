#pragma once

#include "fo4/lod/mesh_geometry.hpp"
#include "fo4/lod/object_tile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::lod::tile_group {

inline constexpr std::size_t k_max_vertices = 65535;

// Invariant: measured vanilla texture set lists the specular atlas at slot 7, never at slot 2.
inline constexpr std::size_t k_specular_slot = 7;

// Invariant: the surface is what decides a shape's group: the alpha lane, the material and its source textures.
struct TileGroupKey {
  bool alpha{false};
  std::uint32_t toggle_ref{0};
  std::string material;
  std::array<std::string, 3> textures;
};

// Invariant: the window is the whole-number UV square one source mesh's coordinates fall in.
struct UvWindow {
  std::array<float, 2> origin{0.0f, 0.0f};
  std::array<std::int32_t, 2> repeat{1, 1};
  bool clamped{false};
};

TileGroupKey group_key_of(const ResolvedMeshShape& resolved, const LodObjectCandidate& candidate);

UvWindow uv_window_of(const fo4::nif::Shape& shape);

TileGroupKey surface_of(const fo4::nif::Shape& shape, const LodObjectCandidate& candidate);

std::size_t find_group(const ObjectTile& tile, const TileGroupKey& surface, const UvWindow& window);

void measure_shape(ObjectTileShape& shape);

// Invariant: this runs after the atlas bake, so the per-surface shapes have already had their UVs remapped.
std::vector<ObjectTileShape> render_shapes(const ObjectTile& tile);

}  // namespace fo4::lod::tile_group
