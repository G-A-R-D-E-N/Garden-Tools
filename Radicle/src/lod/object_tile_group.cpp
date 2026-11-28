#include "object_tile_group.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace tile_group {
namespace {

constexpr float k_uv_slack = 1e-3f;

std::string texture_slot(const std::vector<std::string>& textures, std::size_t index) {
  return index < textures.size() ? textures[index] : std::string{};
}

}  // namespace

// Invariant: the group key is the reader's resolved material, so a .bgsm stays exact.
TileGroupKey group_key_of(const ResolvedMeshShape& resolved, const LodObjectCandidate& candidate) {
  TileGroupKey key;
  key.alpha = resolved.shape.alpha_ref >= 0 || resolved.material.alpha_test;
  key.toggle_ref = candidate.toggle_ref;
  key.material = resolved.shape.material;
  key.textures = resolved.material.textures;
  return key;
}

UvWindow uv_window_of(const fo4::nif::Shape& shape) {
  UvWindow window;
  for (std::size_t axis = 0; axis < 2; ++axis) {
    bool any = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    for (std::size_t index = axis; index < shape.uvs.size(); index += 2) {
      const float value = shape.uvs[index];
      if (!std::isfinite(value)) continue;
      minimum = any ? std::min(minimum, value) : value;
      maximum = any ? std::max(maximum, value) : value;
      any = true;
    }
    if (!any) continue;
    const auto low = std::clamp(std::floor(static_cast<double>(minimum) + k_uv_slack), -1.0e6, 1.0e6);
    const auto high = std::clamp(std::ceil(static_cast<double>(maximum) - k_uv_slack), -1.0e6, 1.0e6);
    const auto span = std::max(1.0, high - low);
    window.origin[axis] = static_cast<float>(low);
    if (span > static_cast<double>(k_object_uv_max_repeat)) {
      window.clamped = true;
      window.repeat[axis] = k_object_uv_max_repeat;
    } else {
      window.repeat[axis] = static_cast<std::int32_t>(span);
    }
  }
  return window;
}

// Invariant: without a material file the mesh's own texture set names the diffuse, normal and specular sources.
TileGroupKey surface_of(const fo4::nif::Shape& shape, const LodObjectCandidate& candidate) {
  TileGroupKey surface;
  surface.alpha = shape.alpha_ref >= 0;
  surface.toggle_ref = candidate.toggle_ref;
  surface.material = shape.material;
  surface.textures = {texture_slot(shape.textures, 0), texture_slot(shape.textures, 1),
                      texture_slot(shape.textures, k_specular_slot)};
  return surface;
}

// Invariant: the newest matching group is returned, so a group that filled up is never appended to again.
std::size_t find_group(const ObjectTile& tile, const TileGroupKey& surface, const UvWindow& window) {
  for (std::size_t index = tile.shapes.size(); index > 0; --index) {
    const auto& shape = tile.shapes[index - 1];
    if (shape.name == (surface.alpha ? "obj-at" : "obj") && shape.toggle_ref == surface.toggle_ref &&
        shape.material == surface.material &&
        shape.textures == surface.textures && shape.uv_repeat == window.repeat) {
      return index - 1;
    }
  }
  return tile.shapes.size();
}

void measure_shape(ObjectTileShape& shape) {
  if (shape.vertices.empty()) {
    shape.bound = {0.0f, 0.0f, 0.0f, 0.0f};
    shape.extents = {0.0f, 0.0f, 0.0f};
    return;
  }
  std::array<float, 3> min{shape.vertices.front().x, shape.vertices.front().y, shape.vertices.front().z};
  std::array<float, 3> max = min;
  for (const auto& vertex : shape.vertices) {
    min[0] = std::min(min[0], vertex.x);
    min[1] = std::min(min[1], vertex.y);
    min[2] = std::min(min[2], vertex.z);
    max[0] = std::max(max[0], vertex.x);
    max[1] = std::max(max[1], vertex.y);
    max[2] = std::max(max[2], vertex.z);
  }
  const std::array<float, 3> center{(min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f,
                                    (min[2] + max[2]) * 0.5f};
  float radius_sq = 0.0f;
  for (const auto& vertex : shape.vertices) {
    const float dx = vertex.x - center[0];
    const float dy = vertex.y - center[1];
    const float dz = vertex.z - center[2];
    radius_sq = std::max(radius_sq, dx * dx + dy * dy + dz * dz);
  }
  shape.bound = {center[0], center[1], center[2], std::sqrt(radius_sq)};
  shape.extents = {(max[0] - min[0]) * 0.5f, (max[1] - min[1]) * 0.5f, (max[2] - min[2]) * 0.5f};
}

// Invariant: one shape per toggle parent and alpha class, split only at the 65,535 index or vertex cap, after the atlas bake.
std::vector<ObjectTileShape> render_shapes(const ObjectTile& tile) {
  constexpr std::size_t k_index_cap = 65535;
  const auto cells = static_cast<std::size_t>(tile.level > 0 ? tile.level : 1);
  // Invariant: a level-4 tile lists one segment per cell, so its triangles are laid out in cell order first.
  const bool per_cell = tile.level == 4;
  std::vector<ObjectTileShape> out;
  std::vector<std::vector<std::vector<std::uint16_t>>> cell_triangles;
  // Invariant: level-4 triangles wait in cell buckets, so their index count is tracked here for the 65,535 split.
  std::vector<std::size_t> pending_indices;
  std::map<std::pair<std::uint32_t, bool>, std::size_t> open_shape;
  struct Block {
    const ObjectTileShape* source{nullptr};
    std::size_t first_vertex{0};
    std::size_t first_index{0};
    std::size_t end_vertex{0};
    std::size_t end_index{0};
    std::uint32_t serial{0};
  };
  std::vector<Block> blocks;
  // Invariant: a group splits only between the objects it was built from, so a capped shape ends full.
  for (const auto& source : tile.shapes) {
    auto starts = source.block_starts;
    auto serials = source.block_serials;
    serials.resize(starts.size(), 0);
    if (starts.empty() || starts.front() != std::pair<std::uint32_t, std::uint32_t>{0, 0}) {
      starts.insert(starts.begin(), std::pair<std::uint32_t, std::uint32_t>{0, 0});
      serials.insert(serials.begin(), 0);
    }
    for (std::size_t block = 0; block < starts.size(); ++block) {
      blocks.push_back(Block{&source, starts[block].first, starts[block].second,
                             block + 1 < starts.size() ? starts[block + 1].first : source.vertices.size(),
                             block + 1 < starts.size() ? starts[block + 1].second : source.triangles.size(),
                             serials[block]});
    }
  }
  // Invariant: blocks run in the order the tile appended them across every group, not group by group, as vanilla pools do.
  std::stable_sort(blocks.begin(), blocks.end(),
                   [](const Block& left, const Block& right) { return left.serial < right.serial; });
  for (const auto& entry : blocks) {
    const auto& source = *entry.source;
    const bool alpha = source.name == "obj-at";
    const std::pair<std::uint32_t, bool> key{source.toggle_ref, alpha};
    const std::size_t first_vertex = entry.first_vertex;
    const std::size_t first_index = entry.first_index;
    const std::size_t end_vertex = entry.end_vertex;
    const std::size_t end_index = entry.end_index;
    const auto found = open_shape.find(key);
    const bool has = found != open_shape.end();
    const auto held = has ? out[found->second].triangles.size() + pending_indices[found->second] : 0;
    const bool fits = has && held + (end_index - first_index) <= k_index_cap &&
                      out[found->second].vertices.size() + (end_vertex - first_vertex) <= k_max_vertices;
    if (!fits) {
      ObjectTileShape created;
      created.name = alpha ? "obj-at" : "obj";
      created.toggle_ref = source.toggle_ref;
      out.push_back(std::move(created));
      cell_triangles.emplace_back(cells * cells);
      pending_indices.push_back(0);
      open_shape[key] = out.size() - 1;
    }
    const auto open = open_shape.at(key);
    auto& target = out[open];
    const auto base = target.vertices.size();
    target.vertices.insert(target.vertices.end(), source.vertices.begin() + static_cast<std::ptrdiff_t>(first_vertex),
                           source.vertices.begin() + static_cast<std::ptrdiff_t>(end_vertex));
    target.uv_clamped = target.uv_clamped || source.uv_clamped;
    if (!per_cell) {
      for (std::size_t index = first_index; index < end_index; ++index) {
        target.triangles.push_back(static_cast<std::uint16_t>(base + source.triangles[index] - first_vertex));
      }
      continue;
    }
    for (std::size_t triangle = first_index / 3; triangle * 3 + 2 < end_index; ++triangle) {
      const auto recorded = triangle < source.triangle_cells.size() ? source.triangle_cells[triangle] : 0;
      const auto in_tile = recorded >= 0 && static_cast<std::size_t>(recorded) < cells * cells;
      auto& bucket = cell_triangles[open][in_tile ? static_cast<std::size_t>(recorded) : std::size_t{0}];
      for (std::size_t corner = 0; corner < 3; ++corner) {
        bucket.push_back(static_cast<std::uint16_t>(base + source.triangles[triangle * 3 + corner] - first_vertex));
      }
      pending_indices[open] += 3;
    }
  }
  if (per_cell) {
    for (std::size_t index = 0; index < out.size(); ++index) {
      auto& shape = out[index];
      // Invariant: segments run through the highest occupied cell, in column-major order, trailing empty cells go.
      std::size_t highest = 0;
      for (std::size_t cell = 0; cell < cell_triangles[index].size(); ++cell) {
        if (!cell_triangles[index][cell].empty()) highest = cell;
      }
      for (std::size_t cell = 0; cell <= highest; ++cell) {
        const auto start = static_cast<std::uint32_t>(shape.triangles.size());
        const auto& bucket = cell_triangles[index][cell];
        shape.triangles.insert(shape.triangles.end(), bucket.begin(), bucket.end());
        shape.segments.push_back(ObjectTileSegment{tile.origin_cell_x + static_cast<std::int32_t>(cell / cells),
                                                   tile.origin_cell_y + static_cast<std::int32_t>(cell % cells),
                                                   start, static_cast<std::uint32_t>(bucket.size() / 3)});
      }
    }
  }
  for (auto& shape : out) measure_shape(shape);
  // Constraint: plain shapes first, then toggled ones by descending parent, opaque before alpha.
  std::stable_sort(out.begin(), out.end(), [](const ObjectTileShape& left, const ObjectTileShape& right) {
    if ((left.toggle_ref != 0) != (right.toggle_ref != 0)) return left.toggle_ref == 0;
    if (left.toggle_ref != right.toggle_ref) return left.toggle_ref > right.toggle_ref;
    return (left.name == "obj-at") < (right.name == "obj-at");
  });
  return out;
}

}  // namespace tile_group

}  // namespace fo4::lod
