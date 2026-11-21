#include "fo4/lod/object_tile.hpp"

#include "../nif/write_model.hpp"

#include "fo4/lod/land_height.hpp"
#include "fo4/lod/mesh_geometry.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/parallel.hpp"
#include "object_tile_group.hpp"
#include "fo4/material/material_file.hpp"

#include "fo4/constants.hpp"
#include "fo4/nif/nif.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace {

using namespace tile_group;

std::string lower_copy(std::string_view text) {
  std::string out{text};
  for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

using nif::write_detail::append_int;
using nif::write_detail::append_sized;
using nif::write_detail::Bytes;
using nif::write_detail::half_bits_even;

constexpr std::uint32_t k_node_flags = 14;
constexpr std::uint64_t k_vertex_flags = 0x1b;
constexpr std::uint64_t k_vertex_offsets = 0x430205;
constexpr std::uint64_t k_stride = 20;
constexpr std::uint32_t k_shader_flags1 = 0x80400001U;
constexpr std::uint32_t k_shader_flags2 = 0x5U;
constexpr std::size_t k_texture_slots = 10;
constexpr std::array<float, 13> k_identity{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f, 1.0f};
// Invariant: measured vanilla block order is root, node, shape, toggle, shader, texture set, alpha property, bound, AABB.
constexpr std::int32_t k_block_root = 0;
constexpr std::int32_t k_obj_name = 0;
constexpr std::string_view k_toggle_name = "ToggleRefID";

void put_float(Bytes& out, float value) { append_int(out, std::bit_cast<std::uint32_t>(value)); }

void append_av(Bytes& out, std::int32_t name, float scale, std::int32_t extra = -1,
               const std::array<float, 3>& translation = {0.0f, 0.0f, 0.0f}) {
  append_int(out, name);
  append_int(out, std::uint32_t{extra >= 0 ? 1U : 0U});
  if (extra >= 0) append_int(out, extra);
  append_int(out, std::int32_t{-1});
  append_int(out, k_node_flags);
  for (std::size_t i = 0; i < k_identity.size(); ++i) {
    put_float(out, i < 3 ? translation[i] : (i + 1 == k_identity.size() ? scale : k_identity[i]));
  }
  append_int(out, std::int32_t{-1});
}

std::int32_t floor_div(std::int32_t value, std::int32_t divisor) {
  const auto quotient = value / divisor;
  return (value % divisor != 0 && value < 0) ? quotient - 1 : quotient;
}

// Invariant: vanilla stores floor((v + 1) * 127.5), not the rounded byte the shared writer emits.
std::uint8_t unit_byte(float value) {
  return static_cast<std::uint8_t>(std::floor(std::clamp((value + 1.0f) * 127.5f, 0.0f, 255.0f)));
}

std::array<float, 3> bitangent_of(const ObjectTileVertex& vertex) {
  if (vertex.frame) return {vertex.bx, vertex.by, vertex.bz};
  float bx = vertex.ny * vertex.tz - vertex.nz * vertex.ty;
  float by = vertex.nz * vertex.tx - vertex.nx * vertex.tz;
  float bz = vertex.nx * vertex.ty - vertex.ny * vertex.tx;
  const float length = std::sqrt(bx * bx + by * by + bz * bz);
  if (length > 0.0f) {
    bx /= length;
    by /= length;
    bz /= length;
  }
  return {bx, by, bz};
}

// Invariant: a value exactly between two halves goes to the even one.
void append_vertex(Bytes& out, const ObjectTileVertex& vertex) {
  const auto bitangent = bitangent_of(vertex);
  append_int(out, half_bits_even(vertex.x));
  append_int(out, half_bits_even(vertex.y));
  append_int(out, half_bits_even(vertex.z));
  append_int(out, half_bits_even(bitangent[0]));
  append_int(out, half_bits_even(vertex.u));
  append_int(out, half_bits_even(vertex.v));
  append_int(out, unit_byte(vertex.nx));
  append_int(out, unit_byte(vertex.ny));
  append_int(out, unit_byte(vertex.nz));
  append_int(out, unit_byte(bitangent[1]));
  append_int(out, unit_byte(vertex.tx));
  append_int(out, unit_byte(vertex.ty));
  append_int(out, unit_byte(vertex.tz));
  append_int(out, unit_byte(bitangent[2]));
}

// Invariant: measured vanilla runs write triangle count, segment count, total segments, then start, primitives, -1, 0.
Bytes segment_tail(const ObjectTile& tile, const ObjectTileShape& shape) {
  const auto triangles = static_cast<std::uint32_t>(shape.triangles.size() / 3);
  std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
  // Constraint: only a level-4 tile groups by source cell; every measured level-8 and coarser tile has one segment.
  if (tile.level == 4) {
    for (const auto& segment : shape.segments) ranges.emplace_back(segment.start_index, segment.primitives);
  }
  if (ranges.empty()) ranges.emplace_back(0, triangles);
  Bytes out;
  append_int(out, triangles);
  append_int(out, static_cast<std::uint32_t>(ranges.size()));
  append_int(out, static_cast<std::uint32_t>(ranges.size()));
  for (const auto& [start, primitives] : ranges) {
    append_int(out, start);
    append_int(out, primitives);
    append_int(out, std::uint32_t{0xFFFFFFFFU});
    append_int(out, std::uint32_t{0});
  }
  return out;
}

// Invariant: measured 15 bytes are name index 1 (the empty string), 0, -1, flags 0x12EC, threshold 0x80.
Bytes alpha_property_block(std::int32_t name_index) {
  Bytes out;
  append_int(out, name_index);
  append_int(out, std::uint32_t{0});
  append_int(out, std::uint32_t{0xFFFFFFFFU});
  append_int(out, static_cast<std::uint16_t>(0x12EC));
  append_int(out, static_cast<std::uint8_t>(0x80));
  return out;
}

Bytes shape_block(const ObjectTile& tile, std::int32_t name, std::int32_t shader, const ObjectTileShape& shape,
                  std::int32_t alpha, std::int32_t extra, float scale) {
  Bytes out;
  // Invariant: measured vanilla puts the tile origin in the shape's translation, so the vertices stay tile-relative.
  append_av(out, name, scale, extra,
            {static_cast<float>(tile.origin_cell_x) * k_cell_world_units_f32,
             static_cast<float>(tile.origin_cell_y) * k_cell_world_units_f32, 0.0f});
  put_float(out, shape.bound[0]);
  put_float(out, shape.bound[1]);
  put_float(out, shape.bound[2]);
  put_float(out, shape.bound[3]);
  append_int(out, std::int32_t{-1});
  append_int(out, shader);
  append_int(out, alpha);
  const auto descriptor = static_cast<std::uint64_t>(k_vertex_flags << 44U) | k_vertex_offsets | (k_stride / 4U);
  append_int(out, descriptor);
  append_int(out, static_cast<std::uint32_t>(shape.triangles.size() / 3));
  append_int(out, static_cast<std::uint16_t>(shape.vertices.size()));
  append_int(out, static_cast<std::uint32_t>(shape.vertices.size() * k_stride + shape.triangles.size() * 2));
  for (const auto& vertex : shape.vertices) append_vertex(out, vertex);
  for (const auto index : shape.triangles) append_int(out, index);
  const auto tail = segment_tail(tile, shape);
  out.insert(out.end(), tail.begin(), tail.end());
  return out;
}

Bytes toggle_extra_block(std::int32_t name_index, std::uint32_t reference) {
  Bytes out;
  append_int(out, name_index);
  append_int(out, reference);
  return out;
}

// Invariant: the payload is the template all 1,560 shipped shader blocks share; only the texture-set reference varies.
Bytes shader_block(std::int32_t texture_set) {
  Bytes out;
  append_int(out, k_obj_name);                       // 0 name "obj"
  append_int(out, std::uint32_t{1});                 // 4 one extra-data entry
  append_int(out, k_block_root);                     // 8 the root node
  append_int(out, std::int32_t{-1});                 // 12 controller
  append_int(out, k_shader_flags1);                  // 16
  append_int(out, k_shader_flags2);                  // 20
  append_int(out, std::uint32_t{0});                 // 24 uv offset u
  append_int(out, std::uint32_t{0});                 // 28 uv offset v
  append_int(out, std::uint32_t{0x3F800000U});       // 32 uv scale u
  append_int(out, std::uint32_t{0x3F800000U});       // 36 uv scale v
  append_int(out, texture_set);                      // 40 texture set
  append_int(out, std::uint32_t{0});                 // 44
  append_int(out, std::uint32_t{0});                 // 48
  append_int(out, std::uint32_t{0});                 // 52
  append_int(out, std::uint32_t{0x3F800000U});       // 56
  append_int(out, std::uint32_t{1});                 // 60 measured bit pattern
  append_int(out, std::uint32_t{0});                 // 64
  append_int(out, std::uint32_t{0x3F800000U});       // 68
  append_int(out, std::uint32_t{0});                 // 72
  append_int(out, std::uint32_t{0x3F800000U});       // 76
  append_int(out, std::uint32_t{0x3F800000U});       // 80
  append_int(out, std::uint32_t{0x3F800000U});       // 84
  append_int(out, std::uint32_t{0x3F800000U});       // 88
  append_int(out, std::uint32_t{0x3F800000U});       // 92
  append_int(out, std::uint32_t{0});                 // 96
  append_int(out, std::uint32_t{0x7F7FFFFFU});       // 100 float max
  append_int(out, std::uint32_t{0});                 // 104
  append_int(out, std::uint32_t{0x3F800000U});       // 108
  append_int(out, std::uint32_t{0x40A00000U});       // 112 5.0
  for (int i = 0; i < 6; ++i) append_int(out, std::uint32_t{0xBF800000U});  // 116..140 -1
  return out;
}

// Invariant: texture names come from the atlas module, so a tile never names a file nothing writes.
std::string tile_texture_path(std::string_view worldspace, ObjectAtlasMap map) {
  std::string path = object_atlas_texture_path(worldspace, map);
  if (path.rfind("Data\\", 0) == 0) path.replace(0, 4, "data");
  return path;
}

// Invariant: measured vanilla Commonwealth tiles name their collision file without its first character, as a short string.
std::string export_path(const ObjectTile& tile, std::string_view worldspace) {
  std::string path{worldspace};
  path += "\\Objects\\";
  path += worldspace;
  path += '.' + std::to_string(tile.level) + '.' + std::to_string(tile.origin_cell_x) + '.' +
          std::to_string(tile.origin_cell_y) + ".hkt";
  path.erase(0, 1);
  if (path.size() > 254) path.erase(0, path.size() - 254);
  path.push_back('\0');
  return path;
}

Bytes texture_set_block(std::string_view worldspace) {
  Bytes out;
  append_int(out, static_cast<std::uint32_t>(k_texture_slots));
  for (std::size_t slot = 0; slot < k_texture_slots; ++slot) {
    if (slot == 0) {
      append_sized(out, tile_texture_path(worldspace, ObjectAtlasMap::diffuse));
    } else if (slot == 1) {
      append_sized(out, tile_texture_path(worldspace, ObjectAtlasMap::normal));
    } else if (slot == k_specular_slot) {
      append_sized(out, tile_texture_path(worldspace, ObjectAtlasMap::specular));
    } else {
      append_sized(out, std::string_view{});
    }
  }
  return out;
}

// Invariant: only BSMultiBoundNode carries a trailing bound reference, NiNode has children only.
Bytes node_block(std::int32_t name, const std::vector<std::int32_t>& children, std::int32_t bound) {
  Bytes out;
  append_av(out, name, 1.0f);
  append_int(out, std::uint32_t(children.size()));
  for (const auto child : children) append_int(out, child);
  if (bound >= 0) {
    append_int(out, bound);
    // Invariant: measured BSMultiBoundNode ends with a cull mode of 1 after its bound reference.
    append_int(out, std::uint32_t{1});
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

bool finite_vertex(const ObjectTileVertex& vertex) {
  return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z) &&
         std::isfinite(vertex.u) && std::isfinite(vertex.v) && std::isfinite(vertex.nx) &&
         std::isfinite(vertex.ny) && std::isfinite(vertex.nz);
}

// Invariant: each output is (x*m0 + y*m1) + z*m2 of one basis row, the engine's dot order (addition of the first pair commutes).
std::array<float, 3> engine_rotate(const float* basis, float x, float y, float z) {
  return {(y * basis[1] + x * basis[0]) + z * basis[2], (x * basis[3] + y * basis[4]) + z * basis[5],
          (x * basis[6] + y * basis[7]) + z * basis[8]};
}

void append_object(const fo4::nif::NifFile& nif, const fo4::nif::Shape& shape, const LodObjectCandidate& candidate,
                   const TileGroupKey& surface, std::int32_t level, std::int32_t tile_x, std::int32_t tile_y,
                   const LandGrids* terrain, ObjectTile& tile) {
  if (shape.verts.size() < 3 || shape.tris.empty()) return;
  const auto vertex_count = shape.verts.size() / 3;
  const auto window = uv_window_of(shape);
  std::size_t group = find_group(tile, surface, window);
  const auto global = fo4::nif::shape_to_world(nif, shape.block);
  const auto rotation = candidate.rotation_basis ? *candidate.rotation_basis : lod_placement_basis(candidate.rotation);
  const float scale = candidate.scale <= 0.0f ? 1.0f : candidate.scale;
  const float origin_x = static_cast<float>(tile_x) * k_cell_world_units_f32;
  const float origin_y = static_cast<float>(tile_y) * k_cell_world_units_f32;
  const float level_scale = static_cast<float>(level);
  std::vector<std::int32_t> placed(vertex_count, -1);
  std::vector<std::int8_t> below(terrain != nullptr ? vertex_count : 0, -1);
  std::size_t marked_group = std::numeric_limits<std::size_t>::max();
  for (std::size_t index = 0; index + 2 < shape.tris.size(); index += 3) {
    std::array<ObjectTileVertex, 3> corners;
    std::array<std::size_t, 3> sources{};
    std::array<bool, 3> under{};
    bool usable = true;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const auto source = static_cast<std::size_t>(shape.tris[index + corner]);
      if (source >= vertex_count) {
        usable = false;
        break;
      }
      sources[corner] = source;
      const float px = shape.verts[source * 3];
      const float py = shape.verts[source * 3 + 1];
      const float pz = shape.verts[source * 3 + 2];
      const auto moved = engine_rotate(global.r, px, py, pz);
      const float gx = global.t[0] + global.s * moved[0];
      const float gy = global.t[1] + global.s * moved[1];
      const float gz = global.t[2] + global.s * moved[2];
      const auto placed_offset = engine_rotate(rotation.data(), gx, gy, gz);
      const float offset_x = scale * placed_offset[0];
      const float offset_y = scale * placed_offset[1];
      const float offset_z = scale * placed_offset[2];
      const float world_x = candidate.position[0] + offset_x;
      const float world_y = candidate.position[1] + offset_y;
      const float world_z = candidate.position[2] + offset_z;
      ObjectTileVertex vertex;
      // Invariant: the tile origin is subtracted from the placement before the rotated offset is added.
      vertex.x = ((candidate.position[0] - origin_x) + offset_x) / level_scale;
      vertex.y = ((candidate.position[1] - origin_y) + offset_y) / level_scale;
      vertex.z = world_z / level_scale;
      if (terrain != nullptr) {
        auto& known = below[source];
        if (known < 0) {
          const auto ground = land_height_at(*terrain, world_x, world_y);
          const bool lower = ground.has_value() && static_cast<double>(world_z) < *ground - k_object_cull_depth;
          known = lower ? std::int8_t{1} : std::int8_t{0};
        }
        under[corner] = known == 1;
      }
      if (shape.uvs.size() >= (source + 1) * 2) {
        vertex.u = std::clamp(shape.uvs[source * 2] - window.origin[0], 0.0f, static_cast<float>(window.repeat[0]));
        vertex.v =
            std::clamp(shape.uvs[source * 2 + 1] - window.origin[1], 0.0f, static_cast<float>(window.repeat[1]));
      }
      const auto turned = [&](const std::vector<float>& channel) {
        // Invariant: a source byte frame is decoded as byte * (1 / 127.5) - 1 before it is turned.
        const auto decoded = [](float value) {
          const float byte = std::round((value + 1.0f) * 127.5f);
          const float exact = byte * (1.0f / 127.5f) - 1.0f;
          return std::fabs(exact - value) < 1e-6f ? exact : value;
        };
        const float cx = decoded(channel[source * 3]);
        const float cy = decoded(channel[source * 3 + 1]);
        const float cz = decoded(channel[source * 3 + 2]);
        const auto m = engine_rotate(global.r, cx, cy, cz);
        auto out = engine_rotate(rotation.data(), m[0], m[1], m[2]);
        const float length = std::sqrt((out[0] * out[0] + out[1] * out[1]) + out[2] * out[2]);
        // Invariant: a frame vector of length <= 1e-6 becomes zero, otherwise it is multiplied by 1 / length.
        if (length <= 1e-6f) {
          out = {0.0f, 0.0f, 0.0f};
        } else {
          const float inverse = 1.0f / length;
          out[0] *= inverse;
          out[1] *= inverse;
          out[2] *= inverse;
        }
        return out;
      };
      const bool has_normal = shape.normals.size() >= (source + 1) * 3;
      if (has_normal) {
        const auto normal = turned(shape.normals);
        vertex.nx = normal[0];
        vertex.ny = normal[1];
        vertex.nz = normal[2];
      }
      if (has_normal && shape.tangents.size() >= (source + 1) * 3 && shape.bitangents.size() >= (source + 1) * 3) {
        const auto tangent = turned(shape.tangents);
        const auto bitangent = turned(shape.bitangents);
        const bool finite = std::isfinite(tangent[0]) && std::isfinite(tangent[1]) && std::isfinite(tangent[2]) &&
                            std::isfinite(bitangent[0]) && std::isfinite(bitangent[1]) && std::isfinite(bitangent[2]);
        if (finite) {
          vertex.tx = tangent[0];
          vertex.ty = tangent[1];
          vertex.tz = tangent[2];
          vertex.bx = bitangent[0];
          vertex.by = bitangent[1];
          vertex.bz = bitangent[2];
          vertex.frame = true;
        }
      }
      if (!finite_vertex(vertex)) {
        ++tile.dropped_vertices;
        usable = false;
        continue;
      }
      corners[corner] = vertex;
    }
    if (!usable) {
      ++tile.dropped_triangles;
      continue;
    }
    if (under[0] && under[1] && under[2]) {
      ++tile.culled_triangles;
      continue;
    }
    // Invariant: the tangent follows the triangle's UV gradient, not a helper axis derived from the normal.
    {
      const float du1 = corners[1].u - corners[0].u;
      const float dv1 = corners[1].v - corners[0].v;
      const float du2 = corners[2].u - corners[0].u;
      const float dv2 = corners[2].v - corners[0].v;
      const float determinant = du1 * dv2 - du2 * dv1;
      std::array<float, 3> tangent{1.0f, 0.0f, 0.0f};
      if (std::fabs(determinant) > 1e-12f) {
        const float inverse = 1.0f / determinant;
        const float ex = corners[1].x - corners[0].x;
        const float ey = corners[1].y - corners[0].y;
        const float ez = corners[1].z - corners[0].z;
        const float fx = corners[2].x - corners[0].x;
        const float fy = corners[2].y - corners[0].y;
        const float fz = corners[2].z - corners[0].z;
        tangent = {(ex * dv2 - fx * dv1) * inverse, (ey * dv2 - fy * dv1) * inverse,
                   (ez * dv2 - fz * dv1) * inverse};
      }
      for (auto& corner : corners) {
        if (corner.frame) continue;
        float tx = tangent[0];
        float ty = tangent[1];
        float tz = tangent[2];
        const float along = tx * corner.nx + ty * corner.ny + tz * corner.nz;
        tx -= corner.nx * along;
        ty -= corner.ny * along;
        tz -= corner.nz * along;
        float length = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (!(length > 0.0f)) {
          const float helper_x = std::fabs(corner.nz) < 0.9f ? 0.0f : 1.0f;
          const float helper_z = std::fabs(corner.nz) < 0.9f ? 1.0f : 0.0f;
          tx = -helper_z * corner.ny;
          ty = helper_z * corner.nx - helper_x * corner.nz;
          tz = helper_x * corner.ny;
          length = std::sqrt(tx * tx + ty * ty + tz * tz);
        }
        if (length > 0.0f) {
          tx /= length;
          ty /= length;
          tz /= length;
        }
        corner.tx = tx;
        corner.ty = ty;
        corner.tz = tz;
      }
    }
    bool group_missing = group == tile.shapes.size();
    std::size_t needed = 0;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      if (placed[sources[corner]] >= 0) continue;
      bool repeated = false;
      for (std::size_t earlier = 0; earlier < corner; ++earlier) repeated = repeated || sources[earlier] == sources[corner];
      if (!repeated) ++needed;
    }
    // Invariant: a group that would pass 65535 vertices is closed and a new one opened, so no triangle is dropped.
    if (!group_missing && tile.shapes[group].vertices.size() + needed > k_max_vertices) {
      group_missing = true;
      std::fill(placed.begin(), placed.end(), -1);
    }
    if (group_missing) {
      ObjectTileShape created;
      created.name = surface.alpha ? "obj-at" : "obj";
      created.toggle_ref = surface.toggle_ref;
      created.material = surface.material;
      created.textures = surface.textures;
      created.uv_repeat = window.repeat;
      tile.shapes.push_back(std::move(created));
      group = tile.shapes.size() - 1;
    }
    auto& target = tile.shapes[group];
    target.uv_clamped = target.uv_clamped || window.clamped;
    if (marked_group != group) {
      target.block_starts.emplace_back(static_cast<std::uint32_t>(target.vertices.size()),
                                       static_cast<std::uint32_t>(target.triangles.size()));
      target.block_serials.push_back(tile.block_serial++);
      marked_group = group;
    }
    for (std::size_t corner = 0; corner < 3; ++corner) {
      auto& slot = placed[sources[corner]];
      if (slot < 0) {
        slot = static_cast<std::int32_t>(target.vertices.size());
        target.vertices.push_back(corners[corner]);
      }
      target.triangles.push_back(static_cast<std::uint16_t>(slot));
    }
    if (tile.level == 4) {
      // Invariant: the writer lays level-4 triangles out per cell, so every triangle remembers the cell it came from.
      const auto cell_column = candidate.cell_x - tile.origin_cell_x;
      const auto cell_row = candidate.cell_y - tile.origin_cell_y;
      const bool inside = cell_column >= 0 && cell_column < tile.level && cell_row >= 0 && cell_row < tile.level;
      target.triangle_cells.push_back(inside ? cell_column * tile.level + cell_row : -1);
    }
  }
}

// Invariant: a named material file's textures and alpha test win over the mesh's own texture set and alpha property.
void append_candidate(MeshGeometryReader& meshes, const LodObjectCandidate& candidate, std::int32_t level,
                      std::int32_t tile_x, std::int32_t tile_y, const LandGrids* terrain, ObjectTile& tile) {
  ++tile.objects;
  const auto& shapes = meshes.read(candidate.mesh);
  const auto* parsed_nif = meshes.nif(candidate.mesh);
  if (!shapes.has_value() || parsed_nif == nullptr) {
    ++tile.missing_meshes;
    return;
  }
  for (const auto& resolved : *shapes) {
    auto key = group_key_of(resolved, candidate);
    if (const auto swapped = meshes.swapped_material(resolved.shape.material, candidate.material_swap)) {
      key.material = swapped->first;
      key.textures = swapped->second.textures;
      key.alpha = resolved.shape.alpha_ref >= 0 || swapped->second.alpha_test;
    }
    append_object(*parsed_nif, resolved.shape, candidate, key, level, tile_x, tile_y, terrain, tile);
  }
  ++tile.meshes;
}

ObjectTile build_from_bucket(std::vector<const LodObjectCandidate*> bucket, std::int32_t level,
                             std::int32_t tile_x, std::int32_t tile_y, MeshGeometryReader& meshes,
                             const LandGrids* terrain) {
  ObjectTile tile;
  tile.level = level;
  tile.origin_cell_x = tile_x;
  tile.origin_cell_y = tile_y;
  // Invariant: objects are appended by cell column, then row; persistent references first, then the cell's references newest first.
  std::vector<const LodObjectCandidate*> ordered;
  ordered.reserve(bucket.size());
  for (const auto* candidate : bucket) {
    if (candidate->persistent) ordered.push_back(candidate);
  }
  for (auto end = bucket.size(); end > 0;) {
    auto begin = end - 1;
    while (begin > 0 && bucket[begin - 1]->reference == bucket[end - 1]->reference) --begin;
    for (auto at = begin; at < end; ++at) {
      if (!bucket[at]->persistent) ordered.push_back(bucket[at]);
    }
    end = begin;
  }
  bucket = std::move(ordered);
  std::stable_sort(bucket.begin(), bucket.end(), [](const LodObjectCandidate* left, const LodObjectCandidate* right) {
    if (left->cell_x != right->cell_x) return left->cell_x < right->cell_x;
    return left->cell_y < right->cell_y;
  });
  for (const auto* candidate : bucket) {
    append_candidate(meshes, *candidate, level, tile_x, tile_y, terrain, tile);
  }
  for (auto& shape : tile.shapes) measure_shape(shape);
  return tile;
}

}  // namespace

void append_lod_object(ObjectTile& tile, const fo4::nif::NifFile& nif, const fo4::nif::Shape& shape,
                       const LodObjectCandidate& candidate, std::int32_t level, std::int32_t tile_x,
                       std::int32_t tile_y, const LandGrids* terrain) {
  append_object(nif, shape, candidate, surface_of(shape, candidate), level, tile_x, tile_y, terrain, tile);
}

std::pair<std::int32_t, std::int32_t> object_tile_origin_of(const LodSettings& settings, std::int32_t level,
                                                            const LodObjectCandidate& candidate) {
  const auto cell_tile_x = settings.southwest_x + floor_div(candidate.cell_x - settings.southwest_x, level) * level;
  const auto cell_tile_y = settings.southwest_y + floor_div(candidate.cell_y - settings.southwest_y, level) * level;
  return {cell_tile_x, cell_tile_y};
}

std::vector<LodTile> object_tile_origins(const LodSettings& settings, std::int32_t level,
                                         const LodCandidateCollection& candidates) {
  std::set<std::pair<std::int32_t, std::int32_t>> origins;
  for (const auto& candidate : candidates.candidates) {
    origins.insert(object_tile_origin_of(settings, level, candidate));
  }
  std::vector<LodTile> tiles;
  tiles.reserve(origins.size());
  for (const auto& origin : origins) {
    const LodTile tile{std::string{}, level, origin.first, origin.second, LodTileKind::object};
    if (check_lod_tile_placement(settings, tile) != LodTilePlacement::ok) continue;
    tiles.push_back(tile);
  }
  return tiles;
}

void collect_object_loose_inputs(const archive::AssetResolver& assets, const LodSettings& settings,
                                 std::string_view world, std::int32_t level,
                                 const LodCandidateCollection& candidates,
                                 std::map<std::string, std::vector<LodInput>>& out, ObjectLooseMeshCache& mesh_cache) {
  MeshGeometryReader meshes{assets};
  // Constraint: a mesh's material and textures are read once, because they are the same for every tile it lands on.
  const auto mesh_inputs = [&](const std::string& mesh) -> const std::vector<LodInput>& {
    const auto cached = mesh_cache.find(mesh);
    if (cached != mesh_cache.end()) return cached->second;
    std::vector<LodInput> inputs;
    if (auto file = loose_file_input(assets, mesh_asset_path(mesh))) inputs.push_back(std::move(*file));
    const auto read = meshes.read(mesh);
    if (read.has_value()) {
      for (const auto& resolved : *read) {
        if (!resolved.shape.material.empty()) {
          if (auto file = loose_file_input(assets, lod_data_folder_path(resolved.shape.material, "Materials"))) {
            inputs.push_back(std::move(*file));
          }
        }
        for (const auto& texture : resolved.material.textures) {
          if (texture.empty()) continue;
          if (auto file = loose_file_input(assets, lod_data_folder_path(texture, "Textures"))) {
            inputs.push_back(std::move(*file));
          }
        }
      }
    }
    return mesh_cache.emplace(mesh, std::move(inputs)).first->second;
  };
  for (const auto& candidate : candidates.candidates) {
    const auto origin = object_tile_origin_of(settings, level, candidate);
    const LodTile tile{std::string{world}, level, origin.first, origin.second, LodTileKind::object};
    // Constraint: the origin rule must match the builder, so an input never lands on a tile that drops it.
    if (check_lod_tile_placement(settings, tile) != LodTilePlacement::ok) continue;
    auto& inputs = out[lod_ledger_key(tile)];
    const auto& shared = mesh_inputs(candidate.mesh);
    inputs.insert(inputs.end(), shared.begin(), shared.end());
  }
  for (auto& entry : out) {
    auto& inputs = entry.second;
    std::sort(inputs.begin(), inputs.end(),
              [](const LodInput& left, const LodInput& right) { return left.path < right.path; });
    inputs.erase(std::unique(inputs.begin(), inputs.end(),
                             [](const LodInput& left, const LodInput& right) { return left.path == right.path; }),
                 inputs.end());
  }
}

std::vector<ObjectTile> build_object_tiles(const archive::AssetResolver& assets, const LodSettings& settings,
                                           std::int32_t level, const std::vector<LodTile>& tiles,
                                           const LodCandidateCollection& candidates) {
  std::map<std::pair<std::int32_t, std::int32_t>, std::vector<const LodObjectCandidate*>> buckets;
  for (const auto& candidate : candidates.candidates) {
    buckets[object_tile_origin_of(settings, level, candidate)].push_back(&candidate);
  }
  const std::vector<const LodObjectCandidate*> none;
  std::vector<ObjectTile> built(tiles.size());
  // Invariant: each chunk owns its tile range and its mesh cache, so results land in tile order.
  const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
  const auto chunks = std::min<std::size_t>(tiles.size(), std::max<std::size_t>(1, hardware));
  parallel_for(chunks, [&](std::size_t chunk) {
    MeshGeometryReader meshes(assets);
    const auto begin = tiles.size() * chunk / chunks;
    const auto end = tiles.size() * (chunk + 1) / chunks;
    for (auto index = begin; index < end; ++index) {
      const auto& tile = tiles[index];
      const auto found = buckets.find({tile.x, tile.y});
      built[index] = build_from_bucket(found == buckets.end() ? none : found->second, level, tile.x, tile.y, meshes,
                                       candidates.terrain.get());
    }
  });
  return built;
}

ObjectTile build_object_tile(const env::Environment& environment, const archive::AssetResolver& assets,
                             const FormKey& worldspace, const LodSettings& settings, std::int32_t level,
                             std::int32_t tile_x, std::int32_t tile_y) {
  const auto candidates = collect_object_lod_candidates(environment, worldspace, level);
  const std::vector<LodTile> wanted{LodTile{std::string{}, level, tile_x, tile_y, LodTileKind::object}};
  const auto built = build_object_tiles(assets, settings, level, wanted, candidates);
  return built.empty() ? ObjectTile{} : built.front();
}

ObjectTile build_object_tile(const archive::AssetResolver& assets, const LodSettings& settings,
                             std::int32_t level, std::int32_t tile_x, std::int32_t tile_y,
                             const LodCandidateCollection& candidates) {
  const std::vector<LodTile> wanted{LodTile{std::string{}, level, tile_x, tile_y, LodTileKind::object}};
  const auto built = build_object_tiles(assets, settings, level, wanted, candidates);
  return built.empty() ? ObjectTile{} : built.front();
}

std::string object_tile_empty_ledger_key(const LodTile& tile) {
  std::string key = lower_copy(tile.worldspace);
  key += "|empty|object.";
  key += std::to_string(tile.level) + '.' + std::to_string(tile.x) + '.' + std::to_string(tile.y);
  return key;
}

std::vector<std::byte> write_object_bto(const ObjectTile& tile, std::string_view worldspace) {
  if (tile.level <= 0) throw LodError("object tile level must be positive");
  const auto shapes = render_shapes(tile);
  for (const auto& shape : shapes) {
    if (shape.vertices.empty() || shape.triangles.empty()) throw LodError("object tile shape is empty");
    if (shape.vertices.size() > k_max_vertices) throw LodError("object tile shape exceeds 65535 vertices");
    if (shape.triangles.size() % 3 != 0) throw LodError("object tile shape has a partial triangle");
    for (const auto index : shape.triangles) {
      if (index >= shape.vertices.size()) throw LodError("object tile shape has an out-of-range index");
    }
  }
  auto model = nif::write_detail::Model{};
  model.banner = "Gamebryo File Format, Version 20.2.0.7";
  model.version = k_nif_version;
  model.user_version = k_nif_user_version;
  model.bs_version = k_nif_bs_version;
  model.export_strings.assign(3, std::string(1, '\0'));
  model.export_strings.push_back(export_path(tile, worldspace));
  // Invariant: measured vanilla lists a block type or a string once, in the order the blocks first use it.
  const auto type_index = [&](std::string_view type) {
    const std::string key{type};
    auto found = std::find(model.block_types.begin(), model.block_types.end(), key);
    if (found == model.block_types.end()) found = model.block_types.insert(found, key);
    return static_cast<std::uint16_t>(found - model.block_types.begin());
  };
  const auto string_index = [&](std::string_view text) {
    const std::string key{text};
    auto found = std::find(model.strings.begin(), model.strings.end(), key);
    if (found == model.strings.end()) found = model.strings.insert(found, key);
    return static_cast<std::int32_t>(found - model.strings.begin());
  };
  const auto add_block = [&](std::vector<Bytes>& blocks, Bytes block, std::string_view type) {
    blocks.push_back(std::move(block));
    model.block_type_index.push_back(type_index(type));
  };
  struct GroupLayout {
    std::int32_t node{0};
    std::int32_t shape{0};
    std::int32_t extra{-1};
    std::int32_t shader{0};
    std::int32_t alpha{-1};
    std::int32_t bound{0};
    std::int32_t aabb{0};
  };
  std::vector<GroupLayout> layout(shapes.size());
  std::int32_t texture_set_index = -1;
  std::int32_t cursor = k_block_root + 1;
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    auto& group = layout[index];
    group.node = cursor++;
    group.shape = cursor++;
    if (shapes[index].toggle_ref != 0) group.extra = cursor++;
    group.shader = cursor++;
    if (index == 0) texture_set_index = cursor++;
    if (shapes[index].name == "obj-at") group.alpha = cursor++;
    group.bound = cursor++;
    group.aabb = cursor++;
  }
  std::vector<std::int32_t> node_indices;
  for (const auto& group : layout) node_indices.push_back(group.node);
  const float level_scale = static_cast<float>(tile.level);
  std::vector<Bytes> blocks;
  add_block(blocks, node_block(string_index("obj"), node_indices, -1), "NiNode");
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    const auto& shape = shapes[index];
    const auto& group = layout[index];
    const bool alpha = shape.name == "obj-at";
    add_block(blocks, node_block(string_index(""), {group.shape}, group.bound), "BSMultiBoundNode");
    add_block(blocks,
              shape_block(tile, string_index(alpha ? "obj-at" : "obj"), group.shader, shape, group.alpha, group.extra,
                          level_scale),
              "BSSubIndexTriShape");
    if (group.extra >= 0) {
      add_block(blocks, toggle_extra_block(string_index(k_toggle_name), shape.toggle_ref), "NiIntegerExtraData");
    }
    add_block(blocks, shader_block(texture_set_index), "BSLightingShaderProperty");
    if (index == 0) add_block(blocks, texture_set_block(worldspace), "BSShaderTextureSet");
    if (alpha) add_block(blocks, alpha_property_block(string_index("")), "NiAlphaProperty");
    add_block(blocks, bound_block(group.aabb), "BSMultiBound");
    const std::array<float, 6> aabb{
        static_cast<float>(tile.origin_cell_x) * k_cell_world_units_f32 + shape.bound[0] * level_scale,
        static_cast<float>(tile.origin_cell_y) * k_cell_world_units_f32 + shape.bound[1] * level_scale,
        shape.bound[2] * level_scale, shape.extents[0] * level_scale, shape.extents[1] * level_scale,
        shape.extents[2] * level_scale};
    add_block(blocks, aabb_block(aabb), "BSMultiBoundAABB");
  }
  model.blocks = std::move(blocks);
  model.roots = {k_block_root};
  for (const auto& text : model.strings) {
    model.max_string_length = std::max(model.max_string_length, static_cast<std::uint32_t>(text.size()));
  }
  return nif::write_detail::serialize(model);
}

}  // namespace fo4::lod
