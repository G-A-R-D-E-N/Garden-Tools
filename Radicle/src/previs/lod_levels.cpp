#include "fo4/previs/lod_levels.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fo4::previs {
namespace {

struct Cell {
  std::int64_t x{0};
  std::int64_t y{0};
  std::int64_t z{0};

  auto operator<=>(const Cell&) const = default;
};

// Constraint: a level welds every vertex that falls in one cell onto that cell's first vertex, so it owns fewer vertices than the level below.
std::vector<std::uint32_t> weld_representatives(std::span<const float> positions, float cell_size,
                                               std::vector<std::uint32_t>& representative_of_vertex) {
  std::map<Cell, std::uint32_t> first_of_cell;
  std::vector<std::uint32_t> kept;
  const std::size_t vertices = positions.size() / 3u;
  representative_of_vertex.assign(vertices, 0u);
  for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
    const Cell cell{
        static_cast<std::int64_t>(std::floor(positions[3 * vertex] / cell_size)),
        static_cast<std::int64_t>(std::floor(positions[3 * vertex + 1] / cell_size)),
        static_cast<std::int64_t>(std::floor(positions[3 * vertex + 2] / cell_size)),
    };
    const auto placed = first_of_cell.try_emplace(cell, static_cast<std::uint32_t>(vertex));
    if (placed.second) kept.push_back(static_cast<std::uint32_t>(vertex));
    representative_of_vertex[vertex] = placed.first->second;
  }
  return kept;
}

// Constraint: a welded level drops triangles that collapse onto one vertex and keeps one copy of each surviving triangle.
std::vector<std::uint16_t> remap_triangles(std::span<const std::uint16_t> indices,
                                           const std::vector<std::uint32_t>& kept,
                                           const std::vector<std::uint32_t>& representative_of_vertex) {
  std::map<std::uint32_t, std::uint16_t> local_of_vertex;
  for (std::size_t slot = 0; slot < kept.size(); ++slot) {
    local_of_vertex.emplace(kept[slot], static_cast<std::uint16_t>(slot));
  }
  std::vector<std::uint16_t> out;
  out.reserve(indices.size());
  std::map<std::array<std::uint16_t, 3>, bool> seen;
  for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3) {
    const std::array<std::uint16_t, 3> corner{
        local_of_vertex.at(representative_of_vertex.at(indices[triangle])),
        local_of_vertex.at(representative_of_vertex.at(indices[triangle + 1])),
        local_of_vertex.at(representative_of_vertex.at(indices[triangle + 2])),
    };
    if (corner[0] == corner[1] || corner[1] == corner[2] || corner[0] == corner[2]) continue;
    if (!seen.emplace(corner, true).second) continue;
    out.insert(out.end(), corner.begin(), corner.end());
  }
  return out;
}

float extent_of(std::span<const float> positions) {
  if (positions.empty()) return 0.0f;
  float low[3] = {positions[0], positions[1], positions[2]};
  float high[3] = {positions[0], positions[1], positions[2]};
  for (std::size_t i = 0; i + 2 < positions.size(); i += 3) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      low[axis] = std::min(low[axis], positions[i + axis]);
      high[axis] = std::max(high[axis], positions[i + axis]);
    }
  }
  return std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2]});
}

}  // namespace

// Constraint: the weld cell follows the mesh's own edge scale, so a dense mesh and a sparse one reduce by the same proportion.
float mean_edge_length(std::span<const float> positions, std::span<const std::uint16_t> indices) {
  double total = 0.0;
  std::size_t edges = 0;
  for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3) {
    for (std::size_t edge = 0; edge < 3; ++edge) {
      const auto from = 3u * static_cast<std::size_t>(indices[triangle + edge]);
      const auto to = 3u * static_cast<std::size_t>(indices[triangle + (edge + 1) % 3]);
      const double dx = static_cast<double>(positions[from]) - static_cast<double>(positions[to]);
      const double dy = static_cast<double>(positions[from + 1]) - static_cast<double>(positions[to + 1]);
      const double dz = static_cast<double>(positions[from + 2]) - static_cast<double>(positions[to + 2]);
      total += std::sqrt(dx * dx + dy * dy + dz * dz);
      ++edges;
    }
  }
  return edges == 0 ? 0.0f : static_cast<float>(total / static_cast<double>(edges));
}

float edge_percentile(std::span<const float> positions, std::span<const std::uint16_t> indices, float percentile) {
  std::vector<float> lengths;
  lengths.reserve(indices.size());
  for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3) {
    for (std::size_t edge = 0; edge < 3; ++edge) {
      const auto from = 3u * static_cast<std::size_t>(indices[triangle + edge]);
      const auto to = 3u * static_cast<std::size_t>(indices[triangle + (edge + 1) % 3]);
      const double dx = static_cast<double>(positions[from]) - static_cast<double>(positions[to]);
      const double dy = static_cast<double>(positions[from + 1]) - static_cast<double>(positions[to + 1]);
      const double dz = static_cast<double>(positions[from + 2]) - static_cast<double>(positions[to + 2]);
      lengths.push_back(static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz)));
    }
  }
  if (lengths.empty()) return 0.0f;
  std::sort(lengths.begin(), lengths.end());
  const auto slot = static_cast<std::size_t>(std::clamp(percentile, 0.0f, 1.0f) * static_cast<float>(lengths.size() - 1));
  return lengths[slot];
}


LodLevels reduce_mesh_levels_with_cells(std::span<const float> positions,
                                                 std::span<const std::uint16_t> indices,
                                                 const LodWeldCells& cells) {
  if (positions.size() % 3u != 0) throw std::invalid_argument("a LOD level needs xyz positions");
  if (indices.size() % 3u != 0 || indices.empty()) {
    throw std::invalid_argument("a LOD level needs whole triangles");
  }
  const std::size_t vertices = positions.size() / 3u;
  if (vertices == 0 || vertices > 65535u) throw std::invalid_argument("a LOD level needs u16 vertices");
  for (const auto index : indices) {
    if (index >= vertices) throw std::invalid_argument("a triangle index runs past the vertex list");
  }

  LodLevels levels;
  levels[0].kept_vertices.resize(vertices);
  for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
    levels[0].kept_vertices[vertex] = static_cast<std::uint32_t>(vertex);
  }
  levels[0].triangles.assign(indices.begin(), indices.end());

  const std::array<float, k_previs_lod_levels> cell{cells.level1, cells.level2};
  for (std::size_t level = 1; level < k_previs_lod_levels; ++level) {
    if (!(cell[level - 1] > 0.0f)) continue;
    std::vector<std::uint32_t> representative_of_vertex;
    auto kept = weld_representatives(positions, cell[level - 1], representative_of_vertex);
    if (kept.size() >= vertices || kept.size() < 3) continue;
    auto triangles = remap_triangles(indices, kept, representative_of_vertex);
    if (triangles.empty()) continue;
    levels[level].kept_vertices = std::move(kept);
    levels[level].triangles = std::move(triangles);
  }
  return levels;
}

LodLevels reduce_mesh_levels(std::span<const float> positions, std::span<const std::uint16_t> indices) {
  const auto edge = mean_edge_length(positions, indices);
  const LodWeldCells cells = edge > 0.0f ? LodWeldCells{k_previs_lod1_edge_factor * edge, k_previs_lod2_edge_factor * edge}
                                        : LodWeldCells{};
  return reduce_mesh_levels_with_cells(positions, indices, cells);
}

namespace {

// Constraint: a wider cell can only merge more vertices, so the kept triangle count never rises with the cell.
std::size_t welded_triangles(std::span<const float> positions, std::span<const std::uint16_t> indices, float cell_size) {
  std::vector<std::uint32_t> representative_of_vertex;
  const auto kept = weld_representatives(positions, cell_size, representative_of_vertex);
  return remap_triangles(indices, kept, representative_of_vertex).size() / 3u;
}

// Constraint: remap can drop duplicate triangles, so a kept count is not monotone in the cell and the solve scans then brackets.
float solve_cell_for_triangles(std::span<const float> positions, std::span<const std::uint16_t> indices,
                               std::size_t wanted_triangles) {
  const auto extent = extent_of(positions);
  if (!(extent > 0.0f) || wanted_triangles == 0) return 0.0f;
  const float fine = extent * 1.0e-4f;
  constexpr int k_samples = 96;
  const double span = static_cast<double>(extent) / static_cast<double>(fine);
  float best_cell = fine;
  std::size_t best_error = SIZE_MAX;
  bool best_at_least = false;
  float bracket_fine = -1.0f;
  float bracket_coarse = -1.0f;
  bool previous_kept_more = false;
  const auto consider = [&](float cell) {
    const auto count = welded_triangles(positions, indices, cell);
    const auto error = count > wanted_triangles ? count - wanted_triangles : wanted_triangles - count;
    // Constraint: an equal miss is settled toward keeping more, so a solver never coarsens a level past its object's ratio.
    if (error < best_error || (error == best_error && count >= wanted_triangles && !best_at_least)) {
      best_error = error;
      best_cell = cell;
      best_at_least = count >= wanted_triangles;
    }
  };
  for (int sample = 0; sample <= k_samples; ++sample) {
    const auto cell = static_cast<float>(static_cast<double>(fine) * std::pow(span, static_cast<double>(sample) / k_samples));
    const auto count = welded_triangles(positions, indices, cell);
    consider(cell);
    if (count >= wanted_triangles) {
      bracket_fine = cell;
      previous_kept_more = true;
    } else if (previous_kept_more) {
      bracket_coarse = cell;
      previous_kept_more = false;
      float low = bracket_fine;
      float high = bracket_coarse;
      for (int step = 0; step < 32; ++step) {
        const float mid = 0.5f * (low + high);
        consider(mid);
        if (welded_triangles(positions, indices, mid) >= wanted_triangles) {
          low = mid;
        } else {
          high = mid;
        }
      }
      // Constraint: the count is not monotone in the cell, so the bracketing interval is swept densely before it is trusted.
      constexpr int k_dense = 256;
      for (int step = 0; step <= k_dense; ++step) {
        consider(bracket_fine + (bracket_coarse - bracket_fine) * (static_cast<float>(step) / k_dense));
      }
    }
  }
  // Constraint: the ratio band a shipped object lives in is narrow, so it is swept at a resolution the geometric scan cannot reach.
  const auto edge = mean_edge_length(positions, indices);
  if (edge > 0.0f) {
    constexpr int k_band = 512;
    for (int step = 0; step <= k_band; ++step) {
      consider(edge * (0.5f + 2.5f * (static_cast<float>(step) / k_band)));
    }
  }
  // Constraint: every sweep above lands on its own step, so the winning cell is refined before it is returned.
  const float centre = best_cell;
  constexpr int k_refine = 64;
  for (int step = 0; step <= k_refine; ++step) {
    consider(centre * (0.9f + 0.2f * (static_cast<float>(step) / k_refine)));
  }
  return best_cell;
}

// Constraint: a target is a fraction of level 0, so a fraction outside (0,1) names no reduction at that level.
bool target_names_reduction(float ratio) { return ratio > 0.0f && ratio < 1.0f; }

// Constraint: a requested fraction rounds to a triangle count held between one triangle and level 0's count.
std::size_t target_triangle_count(std::size_t triangles, float ratio) {
  const auto count = static_cast<std::size_t>(std::lround(static_cast<double>(ratio) * static_cast<double>(triangles)));
  return std::min<std::size_t>(std::max<std::size_t>(count, 1u), triangles);
}

}  // namespace

std::array<float, k_previs_lod_levels> lod_triangle_ratios(const LodLevels& levels) {
  std::array<float, k_previs_lod_levels> ratios{};
  const auto base = levels[0].triangles.size() / 3u;
  if (base == 0) return ratios;
  for (std::size_t level = 0; level < ratios.size(); ++level) {
    ratios[level] = static_cast<float>(levels[level].triangles.size() / 3u) / static_cast<float>(base);
  }
  return ratios;
}

LodWeldCells solve_lod_weld_cells(std::span<const float> positions, std::span<const std::uint16_t> indices,
                                  const LodLevelTarget& target) {
  const auto triangles = indices.size() / 3u;
  LodWeldCells cells{};
  if (target_names_reduction(target.level1)) {
    cells.level1 = solve_cell_for_triangles(positions, indices, target_triangle_count(triangles, target.level1));
  }
  if (target_names_reduction(target.level2)) {
    cells.level2 = solve_cell_for_triangles(positions, indices, target_triangle_count(triangles, target.level2));
  }
  return cells;
}

LodLevels reduce_mesh_levels_to_target(std::span<const float> positions, std::span<const std::uint16_t> indices,
                                       const LodLevelTarget& target) {
  // Constraint: a pair that names no reduction on either level leaves the measured edge rule standing.
  if (!target_names_reduction(target.level1) && !target_names_reduction(target.level2)) {
    return reduce_mesh_levels(positions, indices);
  }
  // Constraint: each level is solved on its own ratio, so a pinned pair is honored even when level 1 keeps more triangles than level 2.
  return reduce_mesh_levels_with_cells(positions, indices, solve_lod_weld_cells(positions, indices, target));
}

std::optional<PackedLodLevels> pack_lod_levels(const LodLevels& levels, std::span<const std::byte> vertices,
                                               std::size_t stride) {
  if (stride == 0 || vertices.empty() || vertices.size() % stride != 0) return std::nullopt;
  const auto available = vertices.size() / stride;
  if (available == 0 || available > 65535u) return std::nullopt;
  std::array<std::uint32_t, k_previs_lod_levels> counts{};
  std::uint32_t total = 0;
  for (std::size_t level = 0; level < counts.size(); ++level) {
    counts[level] = static_cast<std::uint32_t>(levels[level].kept_vertices.size());
    total += counts[level];
  }
  while (total > 65535u) {
    std::size_t dropped = counts.size();
    for (std::size_t level = counts.size(); level-- > 1;) {
      if (counts[level] != 0) {
        dropped = level;
        break;
      }
    }
    if (dropped == counts.size()) return std::nullopt;
    total -= counts[dropped];
    counts[dropped] = 0;
  }
  if (total == 0) return std::nullopt;
  PackedLodLevels packed;
  packed.vertices.reserve(static_cast<std::size_t>(total) * stride);
  for (std::size_t level = 0; level < counts.size(); ++level) {
    if (counts[level] == 0) continue;
    for (const auto vertex : levels[level].kept_vertices) {
      if (vertex >= available) return std::nullopt;
      const auto begin = vertices.begin() +
                         static_cast<std::ptrdiff_t>(static_cast<std::size_t>(vertex) * stride);
      packed.vertices.insert(packed.vertices.end(), begin, begin + static_cast<std::ptrdiff_t>(stride));
    }
  }
  std::array<std::uint32_t, k_previs_lod_levels> offsets{};
  std::uint32_t next = 0;
  for (std::size_t level = 0; level < offsets.size(); ++level) {
    offsets[level] = next;
    next += counts[level];
  }
  const auto shifted = [](const std::vector<std::uint16_t>& triangles, std::uint32_t base) {
    std::vector<std::uint16_t> out;
    out.reserve(triangles.size());
    for (const auto index : triangles) out.push_back(static_cast<std::uint16_t>(index + base));
    return out;
  };
  packed.indices = shifted(levels[0].triangles, offsets[0]);
  for (std::size_t level = 1; level < counts.size(); ++level) {
    if (counts[level] == 0) continue;
    packed.higher_lod_indices[level - 1] = shifted(levels[level].triangles, offsets[level]);
  }
  packed.vertex_count = total;
  packed.level_vertex_count = counts;
  return packed;
}

}  // namespace fo4::previs
