#include "fo4/lod/terrain_tile.hpp"

#include "../nif/write_model.hpp"

#include "fo4/constants.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/lod/terrain_heightfield.hpp"
#include "fo4/lod/terrain_texture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace fo4::lod {
namespace {

using nif::write_detail::append_int;
using nif::write_detail::append_sized;
using nif::write_detail::Bytes;
using nif::write_detail::half_bits;

constexpr std::size_t k_max_side = 129;
constexpr std::size_t k_stored_span = 4096;
constexpr std::size_t k_cell_span = env::k_land_grid - 1;
constexpr std::uint32_t k_node_flags = 14;
constexpr std::uint64_t k_land_vertex_flags = 0x3;
constexpr std::uint64_t k_water_vertex_flags = 0x1;
constexpr std::uint32_t k_land_shader_type = 18;
constexpr std::uint32_t k_shader_flags1 = 0x80401000U;
constexpr std::uint32_t k_shader_flags2 = 0x3U;
constexpr std::uint32_t k_water_flags1 = 0x80000000U;
constexpr std::uint32_t k_water_flags2 = 0x1U;
constexpr std::uint32_t k_cull_mode = 1;
constexpr std::size_t k_texture_slots = 10;
constexpr std::int32_t k_chunk_name = 0;
constexpr std::int32_t k_land_name = 1;
constexpr std::int32_t k_empty_name = 2;
constexpr std::int32_t k_water_name = 3;
constexpr std::int32_t k_land_shape_block = 1;
constexpr std::int32_t k_land_shader_block = 2;
constexpr std::int32_t k_texture_set_block = 3;
constexpr std::int32_t k_water_node_block = 4;
constexpr std::int32_t k_first_water_shape_block = 5;
constexpr std::int32_t k_bare_land_bound_block = 4;
constexpr std::int32_t k_segment_level = 4;
constexpr std::size_t k_segment_count = 16;
constexpr float k_infinity = std::numeric_limits<float>::infinity();
constexpr std::array<float, 6> k_empty_box{k_infinity, k_infinity, k_infinity, -k_infinity, -k_infinity, -k_infinity};
constexpr std::array<float, 13> k_identity{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f, 1.0f};

void put_float(Bytes& out, float value) { append_int(out, std::bit_cast<std::uint32_t>(value)); }

void append_av(Bytes& out, std::int32_t name, float scale) {
  append_int(out, name);
  append_int(out, std::uint32_t{0});
  append_int(out, std::int32_t{-1});
  append_int(out, k_node_flags);
  for (std::size_t i = 0; i < k_identity.size(); ++i) put_float(out, i + 1 == k_identity.size() ? scale : k_identity[i]);
  append_int(out, std::int32_t{-1});
}

std::array<float, 4> bound_from(const std::vector<TileVertex>& vertices, const std::array<float, 3>& fallback) {
  if (vertices.empty()) return {fallback[0], fallback[1], fallback[2], 0.0f};
  std::array<float, 3> center{};
  const auto count = static_cast<float>(vertices.size());
  for (const auto& vertex : vertices) {
    center[0] += vertex.x / count;
    center[1] += vertex.y / count;
    center[2] += vertex.z / count;
  }
  float radius_sq = 0.0f;
  for (const auto& vertex : vertices) {
    const float dx = vertex.x - center[0];
    const float dy = vertex.y - center[1];
    const float dz = vertex.z - center[2];
    radius_sq = std::max(radius_sq, dx * dx + dy * dy + dz * dz);
  }
  return {center[0], center[1], center[2], std::sqrt(radius_sq)};
}

void grow_box(std::array<float, 6>& box, const std::vector<TileVertex>& vertices) {
  for (const auto& vertex : vertices) {
    box[0] = std::min(box[0], vertex.x);
    box[1] = std::min(box[1], vertex.y);
    box[2] = std::min(box[2], vertex.z);
    box[3] = std::max(box[3], vertex.x);
    box[4] = std::max(box[4], vertex.y);
    box[5] = std::max(box[5], vertex.z);
  }
}

// Invariant: a vanilla water sphere is centred on its vertex box, the radius reaching the farthest vertex.
std::array<float, 4> box_bound(const std::array<float, 6>& box, const std::vector<TileVertex>& vertices) {
  const std::array<float, 3> center{(box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f};
  float radius_sq = 0.0f;
  for (const auto& vertex : vertices) {
    const float dx = vertex.x - center[0];
    const float dy = vertex.y - center[1];
    const float dz = vertex.z - center[2];
    radius_sq = std::max(radius_sq, dx * dx + dy * dy + dz * dz);
  }
  return {center[0], center[1], center[2], std::sqrt(radius_sq)};
}

// Invariant: the vanilla water AABB is the box of every water vertex in world units, centre then half extents.
std::array<float, 6> box_aabb(const std::array<float, 6>& box, std::int32_t level) {
  const auto scale = static_cast<float>(level);
  const float hx = (box[3] - box[0]) * 0.5f;
  const float hy = (box[4] - box[1]) * 0.5f;
  const float hz = (box[5] - box[2]) * 0.5f;
  return {(box[0] + hx) * scale, (box[1] + hy) * scale, (box[2] + hz) * scale, hx * scale, hy * scale, hz * scale};
}

void append_vertex(Bytes& out, const TileVertex& vertex, bool water) {
  append_int(out, half_bits(vertex.x));
  append_int(out, half_bits(vertex.y));
  append_int(out, half_bits(vertex.z));
  append_int(out, half_bits(0.0f));
  if (water) return;
  append_int(out, half_bits(vertex.u));
  append_int(out, half_bits(vertex.v));
}

Bytes shape_block(std::int32_t name, std::int32_t shader, const std::vector<TileVertex>& vertices,
                  const std::vector<std::uint16_t>& triangles, const std::array<float, 4>& bound, bool water,
                  float scale) {
  Bytes out;
  append_av(out, name, scale);
  put_float(out, bound[0]);
  put_float(out, bound[1]);
  put_float(out, bound[2]);
  put_float(out, bound[3]);
  append_int(out, std::int32_t{-1});
  append_int(out, shader);
  append_int(out, std::int32_t{-1});
  const auto flags = water ? k_water_vertex_flags : k_land_vertex_flags;
  const std::uint64_t stride = water ? 8 : 12;
  const std::uint64_t offset_bits = water ? 0x2 : 0x203;
  const auto descriptor = static_cast<std::uint64_t>(flags << 44U) | offset_bits | (stride / 4U);
  append_int(out, descriptor);
  append_int(out, static_cast<std::uint32_t>(triangles.size() / 3));
  append_int(out, static_cast<std::uint16_t>(vertices.size()));
  append_int(out, static_cast<std::uint32_t>(vertices.size() * stride + triangles.size() * 2));
  for (const auto& vertex : vertices) append_vertex(out, vertex, water);
  for (const auto index : triangles) append_int(out, index);
  return out;
}

// Invariant: byte-identical to the one 140-byte land shader all 8,271 vanilla tiles carry, single-sided.
Bytes shader_block(std::int32_t texture_set) {
  Bytes out;
  append_int(out, k_land_shader_type);
  append_int(out, k_empty_name);
  append_int(out, std::uint32_t{0});
  append_int(out, std::int32_t{-1});
  append_int(out, k_shader_flags1);
  append_int(out, k_shader_flags2);
  for (const float value : {0.0f, 0.0f, 1.0f, 1.0f}) put_float(out, value);
  append_int(out, texture_set);
  for (const float value : {0.0f, 0.0f, 0.0f, 1.0f}) put_float(out, value);
  append_int(out, k_empty_name);
  append_int(out, std::uint32_t{0});
  for (const float value : {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, std::numeric_limits<float>::max(),
                            0.0f, 1.0f, 5.0f}) {
    put_float(out, value);
  }
  for (int i = 0; i < 6; ++i) put_float(out, -1.0f);
  return out;
}

Bytes texture_set_block(std::string_view diffuse, std::string_view normal) {
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(k_texture_slots));
  append_sized(out, diffuse);
  append_sized(out, normal);
  for (std::size_t slot = 2; slot < k_texture_slots; ++slot) append_sized(out, std::string_view{});
  return out;
}

// Invariant: byte-identical to the one 104-byte water shader all 5,783 vanilla water shapes carry, source texture empty.
Bytes effect_shader_block() {
  Bytes out;
  append_int(out, k_empty_name);
  append_int(out, std::uint32_t{0});
  append_int(out, std::int32_t{-1});
  append_int(out, k_water_flags1);
  append_int(out, k_water_flags2);
  for (const float value : {0.0f, 0.0f, 1.0f, 1.0f}) put_float(out, value);
  append_int(out, std::uint32_t{0});
  append_int(out, std::uint32_t{0xFF03U});
  for (int i = 0; i < 4; ++i) put_float(out, 0.0f);
  for (int i = 0; i < 5; ++i) put_float(out, 1.0f);
  put_float(out, 100.0f);
  for (int i = 0; i < 4; ++i) append_int(out, std::uint32_t{0});
  put_float(out, 1.0f);
  return out;
}

// Invariant: segment k = cx * 4 + cy holds that cell's quad; an empty segment is start 0, primitives 0.
Bytes segment_tail(const TileWaterShape& shape) {
  if (shape.cells.size() * 6 != shape.triangles.size()) {
    throw std::invalid_argument("water shape needs one cell per quad");
  }
  std::array<std::pair<std::uint32_t, std::uint32_t>, k_segment_count> ranges{};
  for (std::size_t quad = 0; quad < shape.cells.size(); ++quad) {
    auto& range = ranges.at(shape.cells[quad]);
    if (range.second != 0) throw std::invalid_argument("water shape has two quads in one cell");
    range.first = static_cast<std::uint32_t>(quad * 6);
    range.second = 2;
  }
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(shape.triangles.size() / 3));
  append_int(out, static_cast<std::uint32_t>(k_segment_count));
  append_int(out, static_cast<std::uint32_t>(k_segment_count));
  for (const auto& [start, primitives] : ranges) {
    append_int(out, start);
    append_int(out, primitives);
    append_int(out, std::uint32_t{0xFFFFFFFFU});
    append_int(out, std::uint32_t{0});
  }
  return out;
}

Bytes bound_block(std::int32_t data) {
  Bytes out;
  append_int(out, data);
  return out;
}

Bytes aabb_block(const std::array<float, 6>& aabb) {
  Bytes out;
  for (const float value : aabb) put_float(out, value);
  return out;
}

Bytes node_block(std::int32_t name, const std::vector<std::int32_t>& children, std::int32_t bound) {
  Bytes out;
  append_av(out, name, 1.0f);
  append_int(out, std::uint32_t(children.size()));
  for (const auto child : children) append_int(out, child);
  append_int(out, bound);
  append_int(out, k_cull_mode);
  return out;
}

constexpr float k_land_decimation_tolerance = 16.0f;
constexpr std::size_t k_max_land_triangles = 2048;
constexpr std::size_t k_min_land_samples = 3;

enum class QuadCover : std::uint8_t { none, partial, full };

// Invariant: a lattice quad is land only when every cell under it is present.
QuadCover quad_cover(const TerrainChunk& chunk, std::size_t step, std::size_t row, std::size_t column) {
  const auto cells = static_cast<std::size_t>(chunk.level);
  const auto last_row = std::min((row + step - 1) / k_cell_span, cells - 1);
  const auto last_column = std::min((column + step - 1) / k_cell_span, cells - 1);
  std::size_t present = 0;
  std::size_t total = 0;
  for (auto cy = row / k_cell_span; cy <= last_row; ++cy) {
    for (auto cx = column / k_cell_span; cx <= last_column; ++cx) {
      ++total;
      if (chunk.cell_present[cy * cells + cx] != 0) ++present;
    }
  }
  if (present == 0) return QuadCover::none;
  return present == total ? QuadCover::full : QuadCover::partial;
}

// Invariant: the error is measured against the emitted triangles, never a bilinear patch.
float lattice_deviation(const TerrainChunk& chunk, std::size_t step) {
  constexpr auto rejected = std::numeric_limits<float>::infinity();
  const auto span = chunk.side - 1;
  const auto at = [&chunk](std::size_t row, std::size_t column) { return chunk.heights[row * chunk.side + column]; };
  const auto extent = static_cast<float>(step);
  float worst = 0.0f;
  for (std::size_t row = 0; row + step <= span; row += step) {
    for (std::size_t column = 0; column + step <= span; column += step) {
      const auto cover = quad_cover(chunk, step, row, column);
      if (cover == QuadCover::none) continue;
      if (cover == QuadCover::partial) return rejected;
      const float south_west = at(row, column);
      const float south_east = at(row, column + step);
      const float north_west = at(row + step, column);
      const float north_east = at(row + step, column + step);
      if (!std::isfinite(south_west) || !std::isfinite(south_east) || !std::isfinite(north_west) ||
          !std::isfinite(north_east)) {
        return rejected;
      }
      for (std::size_t dy = 0; dy <= step; ++dy) {
        for (std::size_t dx = 0; dx <= step; ++dx) {
          const float value = at(row + dy, column + dx);
          if (!std::isfinite(value)) continue;
          const float tx = static_cast<float>(dx) / extent;
          const float ty = static_cast<float>(dy) / extent;
          const float meshed = dx + dy <= step
                                   ? south_west + (south_east - south_west) * tx + (north_west - south_west) * ty
                                   : north_east + (north_west - north_east) * (1.0f - tx) +
                                         (south_east - north_east) * (1.0f - ty);
          worst = std::max(worst, std::fabs(value - meshed));
        }
      }
    }
  }
  return worst;
}

// Invariant: a coarser lattice is used only while it still tracks the full-resolution heightfield.
std::size_t decimation_step(const TerrainChunk& chunk, std::size_t first) {
  const auto span = chunk.side - 1;
  const auto count_for = [span](std::size_t candidate) { return span / candidate + 1; };
  const auto triangles_for = [&](std::size_t candidate) {
    const auto count = count_for(candidate);
    return 2 * (count - 1) * (count - 1);
  };
  std::size_t step = first;
  while (step * 2 <= span && count_for(step * 2) >= k_min_land_samples) {
    // Invariant: a lattice over the triangle budget is coarsened whatever the error, so a tile stays bounded.
    const auto within_budget = triangles_for(step) <= k_max_land_triangles;
    if (within_budget && lattice_deviation(chunk, step * 2) > k_land_decimation_tolerance) break;
    step *= 2;
  }
  return step;
}

// Invariant: a cell with no LAND takes the worldspace default land height and never overwrites a neighbour's shared sample.
void fill_default_land(TerrainChunk& chunk, float height) {
  const auto cells = static_cast<std::size_t>(chunk.level);
  for (std::size_t cy = 0; cy < cells; ++cy) {
    for (std::size_t cx = 0; cx < cells; ++cx) {
      if (chunk.cell_present[cy * cells + cx] != 0) continue;
      chunk.cell_present[cy * cells + cx] = 1;
      for (std::size_t row = 0; row < env::k_land_grid; ++row) {
        for (std::size_t column = 0; column < env::k_land_grid; ++column) {
          float& slot = chunk.heights[(cy * k_cell_span + row) * chunk.side + cx * k_cell_span + column];
          if (std::isnan(slot)) slot = height;
        }
      }
    }
  }
}

// Invariant: a vertex no triangle references must never reach the file, or the tile carries orphans.
void compact_land(TerrainTile& tile) {
  if (tile.land_triangles.empty()) {
    tile.land_vertices.clear();
    return;
  }
  std::vector<std::uint8_t> used(tile.land_vertices.size(), 0);
  for (const auto index : tile.land_triangles) {
    if (index < used.size()) used[index] = 1;
  }
  std::vector<std::int32_t> remap(tile.land_vertices.size(), -1);
  std::vector<TileVertex> vertices;
  vertices.reserve(tile.land_vertices.size());
  for (std::size_t index = 0; index < tile.land_vertices.size(); ++index) {
    if (used[index] == 0) continue;
    remap[index] = static_cast<std::int32_t>(vertices.size());
    vertices.push_back(tile.land_vertices[index]);
  }
  for (auto& index : tile.land_triangles) {
    const auto mapped = remap[index];
    index = static_cast<std::uint16_t>(mapped < 0 ? 0 : mapped);
  }
  tile.land_vertices = std::move(vertices);
}

// Constraint: the vanilla skirt drops 1000/2000/3000/4000 world units at levels 4/8/16/32, constant per level.
float skirt_drop(std::int32_t level) {
  const auto doublings = std::max(1, std::bit_width(static_cast<std::uint32_t>(level)) - 2);
  return 1000.0f * static_cast<float>(doublings) / static_cast<float>(level);
}

struct RingEdge {
  std::size_t from;
  std::size_t quad;
};

// Invariant: the ring runs over the bounding box of the land quads, counter-clockwise from its south-west corner.
std::vector<RingEdge> ring_edges(std::size_t side, const std::vector<std::uint8_t>& quad_land) {
  const auto last = side - 1;
  const auto point = [side](std::size_t row, std::size_t column) { return row * side + column; };
  const auto quad = [last](std::size_t row, std::size_t column) { return row * last + column; };
  std::size_t row0 = last;
  std::size_t row1 = 0;
  std::size_t col0 = last;
  std::size_t col1 = 0;
  bool any = false;
  for (std::size_t row = 0; row < last; ++row) {
    for (std::size_t column = 0; column < last; ++column) {
      if (quad_land[quad(row, column)] == 0) continue;
      any = true;
      row0 = std::min(row0, row);
      row1 = std::max(row1, row);
      col0 = std::min(col0, column);
      col1 = std::max(col1, column);
    }
  }
  std::vector<RingEdge> ring;
  if (!any) return ring;
  ring.reserve(2 * (row1 - row0 + col1 - col0 + 2));
  for (std::size_t i = col0; i <= col1; ++i) ring.push_back({point(row0, i), quad(row0, i)});
  for (std::size_t i = row0; i <= row1; ++i) ring.push_back({point(i, col1 + 1), quad(i, col1)});
  for (std::size_t i = col1 + 1; i > col0; --i) ring.push_back({point(row1 + 1, i), quad(row1, i - 1)});
  for (std::size_t i = row1 + 1; i > row0; --i) ring.push_back({point(i, col0), quad(i - 1, col0)});
  return ring;
}

// Invariant: a ring edge is skirted only where the quad inside it is land; each touched ring vertex gets one twin.
void append_skirt(TerrainTile& tile, const std::vector<std::int32_t>& lattice,
                  const std::vector<std::uint8_t>& quad_land) {
  const auto ring = ring_edges(tile.side, quad_land);
  const auto count = ring.size();
  std::vector<std::uint8_t> skirted(count, 0);
  std::vector<std::uint8_t> needed(count, 0);
  for (std::size_t k = 0; k < count; ++k) {
    if (quad_land[ring[k].quad] == 0) continue;
    skirted[k] = 1;
    needed[k] = 1;
    needed[(k + 1) % count] = 1;
  }
  const float drop = skirt_drop(tile.level);
  std::vector<std::int32_t> bottom(count, -1);
  for (std::size_t k = 0; k < count; ++k) {
    if (needed[k] == 0) continue;
    TileVertex twin = tile.land_vertices[static_cast<std::size_t>(lattice[ring[k].from])];
    twin.z -= drop;
    bottom[k] = static_cast<std::int32_t>(tile.land_vertices.size());
    tile.land_vertices.push_back(twin);
  }
  for (std::size_t k = 0; k < count; ++k) {
    if (skirted[k] == 0) continue;
    const auto next = (k + 1) % count;
    const auto top0 = static_cast<std::uint16_t>(lattice[ring[k].from]);
    const auto top1 = static_cast<std::uint16_t>(lattice[ring[next].from]);
    const auto bottom0 = static_cast<std::uint16_t>(bottom[k]);
    const auto bottom1 = static_cast<std::uint16_t>(bottom[next]);
    for (const auto index : {top1, top0, bottom0, top1, bottom0, bottom1}) tile.land_triangles.push_back(index);
  }
}

}  // namespace

TerrainTile build_terrain_tile(const env::Environment& environment, const FormKey& worldspace,
                               const LodSettings& settings, std::int32_t level, std::int32_t cell_x,
                               std::int32_t cell_y) {
  return build_terrain_tile(collect_world_terrain(environment, worldspace), settings, level, cell_x, cell_y);
}

TerrainTile build_terrain_tile(const WorldTerrain& terrain, const LodSettings& settings, std::int32_t level,
                               std::int32_t cell_x, std::int32_t cell_y) {
  auto chunk = assemble_terrain_chunk(terrain.land, settings, level, cell_x, cell_y);
  TerrainTile tile;
  tile.level = level;
  tile.origin_cell_x = cell_x;
  tile.origin_cell_y = cell_y;
  tile.land_cells = static_cast<std::size_t>(std::count(chunk.cell_present.begin(), chunk.cell_present.end(), 1));
  if (terrain.default_land_height && std::isfinite(*terrain.default_land_height)) {
    fill_default_land(chunk, *terrain.default_land_height);
  }
  std::size_t step = 1;
  while ((chunk.side - 1) / step + 1 > k_max_side) step *= 2;
  step = decimation_step(chunk, step);
  tile.step = step;
  tile.side = (chunk.side - 1) / step + 1;
  const auto stored_spacing = static_cast<float>(k_stored_span) / static_cast<float>(tile.side - 1);
  const auto stored_height = [level](float world) { return world / static_cast<float>(level); };

  const auto& water_heights = terrain.water_heights;
  const auto cells = static_cast<std::size_t>(level);

  // Invariant: water never removes terrain, so a seabed sample below the water plane stays in the mesh.
  std::vector<std::int32_t> lattice(tile.side * tile.side, -1);
  for (std::size_t row = 0; row < tile.side; ++row) {
    for (std::size_t column = 0; column < tile.side; ++column) {
      const float height = chunk.heights[(row * step) * chunk.side + column * step];
      if (!std::isfinite(height)) continue;
      TileVertex vertex;
      vertex.x = static_cast<float>(column) * stored_spacing;
      vertex.y = static_cast<float>(row) * stored_spacing;
      vertex.z = stored_height(height);
      vertex.u = vertex.x / static_cast<float>(k_stored_span);
      vertex.v = 1.0f - vertex.y / static_cast<float>(k_stored_span);
      lattice[row * tile.side + column] = static_cast<std::int32_t>(tile.land_vertices.size());
      tile.land_vertices.push_back(vertex);
    }
  }
  const auto quads = tile.side - 1;
  std::vector<std::uint8_t> quad_land(quads * quads, 0);
  for (std::size_t row = 0; row < quads; ++row) {
    for (std::size_t column = 0; column < quads; ++column) {
      const auto a = lattice[row * tile.side + column];
      const auto b = lattice[row * tile.side + column + 1];
      const auto c = lattice[(row + 1) * tile.side + column];
      const auto d = lattice[(row + 1) * tile.side + column + 1];
      if (a < 0 || b < 0 || c < 0 || d < 0) continue;
      if (quad_cover(chunk, step, row * step, column * step) != QuadCover::full) continue;
      quad_land[row * quads + column] = 1;
      // Invariant: land triangles are counter-clockwise seen from above, the winding the skirt triangles share.
      for (const auto index : {a, b, c, b, d, c}) tile.land_triangles.push_back(static_cast<std::uint16_t>(index));
    }
  }
  append_skirt(tile, lattice, quad_land);
  compact_land(tile);

  struct WetCell {
    float height;
    std::size_t cx;
    std::size_t cy;
  };
  std::vector<WetCell> wet;
  for (std::size_t cx = 0; cx < cells; ++cx) {
    for (std::size_t cy = 0; cy < cells; ++cy) {
      const auto found = water_heights.find({cell_x + static_cast<std::int32_t>(cx),
                                             cell_y + static_cast<std::int32_t>(cy)});
      if (found != water_heights.end() && std::isfinite(found->second)) wet.push_back({found->second, cx, cy});
    }
  }
  // Invariant: vanilla orders water quads by height, then column, then row.
  std::sort(wet.begin(), wet.end(), [](const WetCell& a, const WetCell& b) {
    if (a.height != b.height) return a.height < b.height;
    return a.cx != b.cx ? a.cx < b.cx : a.cy < b.cy;
  });
  const auto cell_stored = static_cast<float>(k_stored_span) / static_cast<float>(level);
  float shape_height = 0.0f;
  for (const auto& cell : wet) {
    if (tile.water_shapes.empty() || (level == k_segment_level && cell.height != shape_height)) {
      tile.water_shapes.emplace_back();
    }
    shape_height = cell.height;
    auto& shape = tile.water_shapes.back();
    ++tile.water_cells;
    const float x0 = static_cast<float>(cell.cx) * cell_stored;
    const float y0 = static_cast<float>(cell.cy) * cell_stored;
    const float z = cell.height / static_cast<float>(level);
    const auto base = static_cast<std::uint16_t>(shape.vertices.size());
    shape.vertices.push_back(TileVertex{x0, y0, z, 0.0f, 0.0f});
    shape.vertices.push_back(TileVertex{x0 + cell_stored, y0, z, 0.0f, 0.0f});
    shape.vertices.push_back(TileVertex{x0, y0 + cell_stored, z, 0.0f, 0.0f});
    shape.vertices.push_back(TileVertex{x0 + cell_stored, y0 + cell_stored, z, 0.0f, 0.0f});
    // Invariant: water triangles wind counter-clockwise seen from above, like the land.
    for (const std::uint16_t offset : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2},
                                       std::uint16_t{1}, std::uint16_t{3}, std::uint16_t{2}}) {
      shape.triangles.push_back(static_cast<std::uint16_t>(base + offset));
    }
    shape.cells.push_back(static_cast<std::uint16_t>(cell.cx * cells + cell.cy));
  }

  const float block_world = static_cast<float>(k_stored_span) * static_cast<float>(level);
  tile.land_bound = bound_from(tile.land_vertices, {block_world / (2.0f * level), block_world / (2.0f * level), 0.0f});
  const float half_world = block_world / 2.0f;
  tile.land_aabb = {half_world, half_world, tile.land_bound[2] * level, half_world, half_world,
                    tile.land_bound[3] * level};
  auto water_box = k_empty_box;
  for (auto& shape : tile.water_shapes) {
    auto box = k_empty_box;
    grow_box(box, shape.vertices);
    grow_box(water_box, shape.vertices);
    shape.bound = box_bound(box, shape.vertices);
  }
  if (!tile.water_shapes.empty()) tile.water_aabb = box_aabb(water_box, level);
  return tile;
}

std::vector<std::byte> write_terrain_btr(const TerrainTile& tile, std::string_view worldspace) {
  const bool has_water = !tile.water_shapes.empty();
  const bool segmented = has_water && tile.level == k_segment_level;
  const auto water_shapes = static_cast<std::int32_t>(tile.water_shapes.size());
  auto model = nif::write_detail::Model{};
  model.banner = "Gamebryo File Format, Version 20.2.0.7";
  model.version = k_nif_version;
  model.user_version = k_nif_user_version;
  model.bs_version = k_nif_bs_version;
  model.export_strings.assign(4, std::string{});
  model.block_types = {"BSMultiBoundNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"};
  model.block_type_index = {0, 1, 2, 3};
  if (has_water) {
    // Invariant: the vanilla type table lists BSSubIndexTriShape ahead of the effect shader only at level 4.
    const auto shape_type = static_cast<std::uint16_t>(segmented ? 4 : 1);
    const auto effect_type = static_cast<std::uint16_t>(segmented ? 5 : 4);
    const auto bound_type = static_cast<std::uint16_t>(effect_type + 1);
    const auto aabb_type = static_cast<std::uint16_t>(effect_type + 2);
    if (segmented) model.block_types.push_back("BSSubIndexTriShape");
    model.block_types.insert(model.block_types.end(), {"BSEffectShaderProperty", "BSMultiBound", "BSMultiBoundAABB"});
    model.block_type_index.push_back(0);
    for (std::int32_t shape = 0; shape < water_shapes; ++shape) {
      model.block_type_index.insert(model.block_type_index.end(), {shape_type, effect_type});
    }
    model.block_type_index.insert(model.block_type_index.end(), {bound_type, aabb_type, bound_type, aabb_type});
  } else {
    model.block_types.insert(model.block_types.end(), {"BSMultiBound", "BSMultiBoundAABB"});
    model.block_type_index = {0, 1, 2, 3, 4, 5};
  }
  model.roots = {0};

  // Invariant: the vanilla string table is chunk, Land, an empty string, then WATER only when the tile has water.
  model.strings = {"chunk", "Land", ""};
  if (has_water) model.strings.push_back("WATER");
  const auto diffuse = terrain_texture_path(worldspace, tile.level, tile.origin_cell_x, tile.origin_cell_y,
                                            TerrainTextureMap::diffuse);
  const auto normal = terrain_texture_path(worldspace, tile.level, tile.origin_cell_x, tile.origin_cell_y,
                                           TerrainTextureMap::normal);

  // Invariant: vanilla lists each water shape then its shader, then the water bound pair, then the land bound pair.
  const auto water_bound = k_first_water_shape_block + 2 * water_shapes;
  const auto land_bound = has_water ? water_bound + 2 : k_bare_land_bound_block;
  std::vector<std::int32_t> water_children;
  for (std::int32_t shape = 0; shape < water_shapes; ++shape) {
    water_children.push_back(k_first_water_shape_block + 2 * shape);
  }
  model.blocks = {
      node_block(k_chunk_name, has_water ? std::vector<std::int32_t>{k_land_shape_block, k_water_node_block}
                                         : std::vector<std::int32_t>{k_land_shape_block},
                 land_bound),
      shape_block(k_land_name, k_land_shader_block, tile.land_vertices, tile.land_triangles, tile.land_bound, false,
                  static_cast<float>(tile.level)),
      shader_block(k_texture_set_block),
      texture_set_block(diffuse, normal)};
  if (has_water) {
    model.blocks.push_back(node_block(k_water_name, water_children, water_bound));
    for (const auto& shape : tile.water_shapes) {
      const auto shader = static_cast<std::int32_t>(model.blocks.size()) + 1;
      auto block = shape_block(k_empty_name, shader, shape.vertices, shape.triangles, shape.bound, true,
                               static_cast<float>(tile.level));
      if (segmented) {
        const auto tail = segment_tail(shape);
        block.insert(block.end(), tail.begin(), tail.end());
      }
      model.blocks.push_back(std::move(block));
      model.blocks.push_back(effect_shader_block());
    }
    model.blocks.push_back(bound_block(water_bound + 1));
    model.blocks.push_back(aabb_block(tile.water_aabb));
  }
  model.blocks.push_back(bound_block(land_bound + 1));
  model.blocks.push_back(aabb_block(tile.land_aabb));
  for (const auto& text : model.strings) {
    model.max_string_length = std::max(model.max_string_length, static_cast<std::uint32_t>(text.size()));
  }
  return nif::write_detail::serialize(model);
}

}  // namespace fo4::lod
