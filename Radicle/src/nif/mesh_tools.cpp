#include "mesh_tools.hpp"

#include "fo4/hash.hpp"
#include "customization_remap_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::nif::mesh_detail {
namespace {

using write_detail::append_int;
using write_detail::half_bits;
using write_detail::read_int;
using write_detail::unit_byte;
using write_detail::write_int;

constexpr std::size_t k_triangle_bytes = 6;
constexpr std::size_t k_axes = 3;
constexpr std::size_t k_data_size_back = 4;
constexpr std::size_t k_vertex_count_back = 6;
constexpr std::size_t k_triangle_count_back = 10;
constexpr std::uint32_t k_max_vertices = 0xFFFFU;
constexpr std::uint64_t k_max_data_size = 0xFFFFFFFFULL;
constexpr std::string_view k_plain_type = "BSTriShape";

// Invariant: the three count fields sit at fixed negative offsets from the vertex data, exactly as read_layout reads them.
struct Counts {
  std::size_t triangle_count_at{0};
  std::size_t vertex_count_at{0};
  std::size_t data_size_at{0};
};

Counts counts_of(const Layout& layout) {
  if (layout.vertices_at < k_triangle_count_back) {
    throw std::runtime_error("internal error: the vertex data starts before its count fields");
  }
  Counts counts;
  counts.triangle_count_at = layout.vertices_at - k_triangle_count_back;
  counts.vertex_count_at = layout.vertices_at - k_vertex_count_back;
  counts.data_size_at = layout.vertices_at - k_data_size_back;
  return counts;
}

std::optional<std::string> read_geometry(const Bytes& bytes, const Layout& layout, Geometry* geometry) {
  geometry->vertices.clear();
  geometry->vertices.reserve(layout.vertex_count);
  for (std::size_t v = 0; v < layout.vertex_count; ++v) {
    const auto at = layout.vertices_at + v * layout.stride;
    if (at > bytes.size() || layout.stride > bytes.size() - at) return "a vertex runs past the end of its block";
    geometry->vertices.emplace_back(bytes.data() + at, bytes.data() + at + layout.stride);
  }
  geometry->triangles.clear();
  geometry->triangles.reserve(layout.triangle_count * k_axes);
  for (std::size_t t = 0; t < layout.triangle_count; ++t) {
    const auto at = layout.triangles_at + t * k_triangle_bytes;
    if (at > bytes.size() || k_triangle_bytes > bytes.size() - at) {
      return "a triangle runs past the end of its block";
    }
    for (std::size_t corner = 0; corner < k_axes; ++corner) {
      const auto index = static_cast<std::uint32_t>(read_int<std::uint16_t>(bytes, at + 2 * corner));
      if (index >= geometry->vertices.size()) return "a triangle indexes a vertex that is not in the shape";
      geometry->triangles.push_back(index);
    }
  }
  return std::nullopt;
}

std::vector<Vec3> positions_of(const Geometry& geometry) {
  std::vector<Vec3> out;
  out.reserve(geometry.vertices.size());
  for (const auto& vertex : geometry.vertices) {
    Vec3 point{};
    for (std::size_t axis = 0; axis < k_axes; ++axis) {
      point[axis] = std::bit_cast<float>(read_int<std::uint32_t>(vertex, 4 * axis));
    }
    out.push_back(point);
  }
  return out;
}

void write_geometry(Bytes& bytes, const Layout& layout, const Geometry& geometry) {
  if (layout.stride == 0) throw std::runtime_error("internal error: the vertex stride is zero");
  if (geometry.triangles.size() % k_axes != 0) {
    throw std::runtime_error("internal error: the triangle list is not whole faces");
  }
  for (const auto& vertex : geometry.vertices) {
    if (vertex.size() != layout.stride) throw std::runtime_error("internal error: a vertex is not one stride long");
  }
  if (geometry.vertices.size() > k_max_vertices) throw std::runtime_error("the vertex count does not fit a tri-shape");
  const auto data_size = geometry.vertices.size() * layout.stride + geometry.triangles.size() * 2;
  if (data_size > k_max_data_size) throw std::runtime_error("the vertex data does not fit a tri-shape");
  const auto counts = counts_of(layout);
  const auto old_end = std::min(bytes.size(), layout.triangles_at + layout.triangle_count * k_triangle_bytes);
  Bytes out;
  out.reserve(bytes.size() + data_size);
  out.insert(out.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(layout.vertices_at));
  for (const auto& vertex : geometry.vertices) out.insert(out.end(), vertex.begin(), vertex.end());
  for (const auto index : geometry.triangles) append_int(out, static_cast<std::uint16_t>(index));
  out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(old_end), bytes.end());
  write_int(out, counts.triangle_count_at, static_cast<std::uint32_t>(geometry.triangles.size() / k_axes));
  write_int(out, counts.vertex_count_at, static_cast<std::uint16_t>(geometry.vertices.size()));
  write_int(out, counts.data_size_at, static_cast<std::uint32_t>(data_size));
  bytes = std::move(out);
}

// Invariant: the hash and the equality read the same stride of bytes, so equal keys are byte-identical vertices.
struct VertexHash {
  std::size_t operator()(const Vertex& vertex) const noexcept {
    return static_cast<std::size_t>(fnv1a64_bytes(vertex));
  }
};

struct VertexEqual {
  bool operator()(const Vertex& left, const Vertex& right) const noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
      if (left[i] != right[i]) return false;
    }
    return true;
  }
};

std::optional<std::string> weld_duplicate_vertices(Geometry& geometry) {
  std::unordered_map<Vertex, std::uint32_t, VertexHash, VertexEqual> index_of;
  index_of.reserve(geometry.vertices.size());
  std::vector<std::uint32_t> remap(geometry.vertices.size(), 0);
  std::vector<Vertex> unique;
  unique.reserve(geometry.vertices.size());
  for (std::size_t v = 0; v < geometry.vertices.size(); ++v) {
    const auto [found, inserted] =
        index_of.try_emplace(geometry.vertices[v], static_cast<std::uint32_t>(unique.size()));
    if (inserted) unique.push_back(geometry.vertices[v]);
    remap[v] = found->second;
  }
  if (unique.size() == geometry.vertices.size()) return "no duplicate vertices";
  geometry.vertices = std::move(unique);
  for (auto& index : geometry.triangles) index = remap[index];
  std::vector<std::uint32_t> kept;
  kept.reserve(geometry.triangles.size());
  for (std::size_t face = 0; face + k_axes <= geometry.triangles.size(); face += k_axes) {
    const auto first = geometry.triangles[face];
    const auto second = geometry.triangles[face + 1];
    const auto third = geometry.triangles[face + 2];
    if (first == second || second == third || first == third) continue;
    kept.push_back(first);
    kept.push_back(second);
    kept.push_back(third);
  }
  geometry.triangles = std::move(kept);
  return std::nullopt;
}

std::optional<std::string> drop_degenerate_triangles(Geometry& geometry, const std::vector<Vec3>* positions) {
  std::vector<std::uint32_t> kept;
  kept.reserve(geometry.triangles.size());
  for (std::size_t face = 0; face + k_axes <= geometry.triangles.size(); face += k_axes) {
    const auto first = geometry.triangles[face];
    const auto second = geometry.triangles[face + 1];
    const auto third = geometry.triangles[face + 2];
    if (first == second || second == third || first == third) continue;
    if (positions != nullptr) {
      const auto& a = (*positions)[first];
      const auto& b = (*positions)[second];
      const auto& c = (*positions)[third];
      const Vec3 area = shape_detail::cross(shape_detail::sub(b, a), shape_detail::sub(c, a));
      if (shape_detail::dot(area, area) <= 0.0f) continue;
    }
    kept.push_back(first);
    kept.push_back(second);
    kept.push_back(third);
  }
  if (kept.size() == geometry.triangles.size()) {
    return positions == nullptr ? "no repeated-index faces; zero-area faces need full precision"
                                : "no degenerate triangles";
  }
  geometry.triangles = std::move(kept);
  return std::nullopt;
}

std::optional<std::string> drop_unused_vertices(Geometry& geometry) {
  std::vector<bool> used(geometry.vertices.size(), false);
  for (const auto index : geometry.triangles) used[index] = true;
  std::vector<std::uint32_t> remap(geometry.vertices.size(), 0);
  std::vector<Vertex> kept;
  kept.reserve(geometry.vertices.size());
  for (std::size_t v = 0; v < geometry.vertices.size(); ++v) {
    if (!used[v]) continue;
    remap[v] = static_cast<std::uint32_t>(kept.size());
    kept.push_back(geometry.vertices[v]);
  }
  if (kept.size() == geometry.vertices.size()) return "every vertex is used by a triangle";
  for (auto& index : geometry.triangles) index = remap[index];
  geometry.vertices = std::move(kept);
  return std::nullopt;
}

constexpr std::size_t k_max_grid = 64;
constexpr float k_min_unit_length = 0.5f;
// Invariant: a quantized unit vector decodes to a length near one, so anything longer than this is invalid data.
constexpr float k_max_unit_length = 2.0f;
constexpr float k_max_frame_dot = 0.05f;
constexpr float k_min_sine_squared = 1e-3f;

using CellKey = std::array<std::uint32_t, 3>;
using ClusterKey = std::pair<CellKey, std::uint32_t>;

std::uint32_t cell_index(float value, float low, float span, std::size_t grid) {
  if (!(span > 0.0f)) return 0;
  const auto scaled = (value - low) / span * static_cast<float>(grid);
  if (!(scaled > 0.0f)) return 0;
  const auto cell = static_cast<std::uint32_t>(scaled);
  const auto last = static_cast<std::uint32_t>(grid - 1);
  return cell > last ? last : cell;
}

CellKey cell_of_point(const Vec3& point, const Vec3& low, const Vec3& span, std::size_t grid) {
  return CellKey{cell_index(point[0], low[0], span[0], grid), cell_index(point[1], low[1], span[1], grid),
                 cell_index(point[2], low[2], span[2], grid)};
}

std::uint32_t find_island(std::vector<std::uint32_t>& parent, std::uint32_t v) {
  while (parent[v] != v) {
    parent[v] = parent[parent[v]];
    v = parent[v];
  }
  return v;
}

std::vector<std::uint32_t> islands_of(const Geometry& geometry, std::size_t count) {
  std::vector<std::uint32_t> parent(count);
  for (std::size_t v = 0; v < count; ++v) parent[v] = static_cast<std::uint32_t>(v);
  for (std::size_t corner = 0; corner + k_axes <= geometry.triangles.size(); corner += k_axes) {
    const auto first = find_island(parent, geometry.triangles[corner]);
    const auto second = find_island(parent, geometry.triangles[corner + 1]);
    const auto third = find_island(parent, geometry.triangles[corner + 2]);
    parent[second] = first;
    parent[third] = first;
  }
  std::vector<std::uint32_t> island(count);
  for (std::size_t v = 0; v < count; ++v) island[v] = find_island(parent, static_cast<std::uint32_t>(v));
  return island;
}

// Invariant: two vertices share a wedge only if an edge path inside their cluster joins them, so seam copies stay apart.
std::vector<std::uint32_t> wedges_of(const Geometry& geometry, const std::vector<std::uint32_t>& cluster_of) {
  std::vector<std::uint32_t> parent(cluster_of.size());
  for (std::size_t v = 0; v < parent.size(); ++v) parent[v] = static_cast<std::uint32_t>(v);
  for (std::size_t corner = 0; corner + k_axes <= geometry.triangles.size(); corner += k_axes) {
    for (std::size_t edge = 0; edge < k_axes; ++edge) {
      const auto a = geometry.triangles[corner + edge];
      const auto b = geometry.triangles[corner + (edge + 1) % k_axes];
      if (cluster_of[a] == cluster_of[b]) parent[find_island(parent, b)] = find_island(parent, a);
    }
  }
  std::vector<std::uint32_t> wedge(parent.size());
  for (std::size_t v = 0; v < wedge.size(); ++v) wedge[v] = find_island(parent, static_cast<std::uint32_t>(v));
  return wedge;
}

void write_position(Vertex& vertex, const Vec3& point) {
  for (std::size_t axis = 0; axis < k_axes; ++axis) {
    write_int(vertex, 4 * axis, std::bit_cast<std::uint32_t>(point[axis]));
  }
}

void write_unit(Vertex& vertex, std::size_t at, const Vec3& value) {
  for (std::size_t axis = 0; axis < k_axes; ++axis) {
    const auto encoded = std::lround(std::clamp((value[axis] + 1.0f) * 127.5f, 0.0f, 255.0f));
    vertex[at + axis] = static_cast<std::byte>(static_cast<std::uint8_t>(encoded));
  }
}

// Invariant: the accepted grid is the finest one with at least one surviving face and at most ceil(faces * ratio) faces.
std::optional<std::string> simplify_geometry(Geometry& geometry, float ratio, float* error) {
  const auto positions = positions_of(geometry);
  if (positions.empty()) return "no vertex data";
  for (const auto& point : positions) {
    for (std::size_t axis = 0; axis < k_axes; ++axis) {
      if (!std::isfinite(point[axis])) return "a vertex position is not finite";
    }
  }
  const auto faces = geometry.triangles.size() / k_axes;
  const auto target =
      static_cast<std::size_t>(std::ceil(static_cast<double>(faces) * static_cast<double>(ratio)));
  Vec3 low = positions.front();
  Vec3 high = positions.front();
  for (const auto& point : positions) {
    for (std::size_t axis = 0; axis < k_axes; ++axis) {
      low[axis] = std::min(low[axis], point[axis]);
      high[axis] = std::max(high[axis], point[axis]);
    }
  }
  Vec3 span{};
  for (std::size_t axis = 0; axis < k_axes; ++axis) span[axis] = high[axis] - low[axis];
  const auto island = islands_of(geometry, positions.size());
  std::vector<Vertex> best_vertices;
  std::vector<std::uint32_t> best_triangles;
  float best_error = 0.0f;
  for (std::size_t grid = k_max_grid; grid > 0; --grid) {
    std::map<ClusterKey, std::uint32_t> cluster_of_cell;
    std::vector<Vec3> sums;
    std::vector<float> counts;
    std::vector<std::uint32_t> cluster_of(positions.size(), 0);
    for (std::size_t v = 0; v < positions.size(); ++v) {
      ClusterKey key{cell_of_point(positions[v], low, span, grid), island[v]};
      const auto [found, inserted] =
          cluster_of_cell.try_emplace(std::move(key), static_cast<std::uint32_t>(sums.size()));
      if (inserted) {
        sums.push_back(Vec3{});
        counts.push_back(0.0f);
      }
      const auto id = found->second;
      sums[id] = shape_detail::add(sums[id], positions[v]);
      counts[id] += 1.0f;
      cluster_of[v] = id;
    }
    std::vector<Vec3> centroids(sums.size());
    for (std::size_t id = 0; id < sums.size(); ++id) {
      centroids[id] = shape_detail::scale(sums[id], 1.0f / counts[id]);
    }
    // Constraint: an overflowing sum would write an inf or nan position, so the centroid must be finite.
    for (const auto& centroid : centroids) {
      for (std::size_t axis = 0; axis < k_axes; ++axis) {
        if (!std::isfinite(centroid[axis])) return "a cluster centroid is not finite";
      }
    }
    const auto wedge = wedges_of(geometry, cluster_of);
    std::vector<std::uint32_t> kept;
    kept.reserve(geometry.triangles.size());
    for (std::size_t face = 0; face + k_axes <= geometry.triangles.size(); face += k_axes) {
      const auto first = geometry.triangles[face];
      const auto second = geometry.triangles[face + 1];
      const auto third = geometry.triangles[face + 2];
      if (cluster_of[first] == cluster_of[second] || cluster_of[second] == cluster_of[third] ||
          cluster_of[first] == cluster_of[third]) {
        continue;
      }
      kept.insert(kept.end(), {first, second, third});
    }
    std::vector<bool> drawn(positions.size(), false);
    for (const auto corner : kept) drawn[wedge[corner]] = true;
    std::map<std::uint32_t, std::uint32_t> output_of_wedge;
    for (std::size_t v = 0; v < positions.size(); ++v) {
      if (drawn[wedge[v]]) output_of_wedge.try_emplace(wedge[v], static_cast<std::uint32_t>(output_of_wedge.size()));
    }
    std::vector<std::uint32_t> representative(output_of_wedge.size(), 0);
    std::vector<std::uint32_t> cluster_of_output(output_of_wedge.size(), 0);
    std::vector<float> nearest(output_of_wedge.size(), std::numeric_limits<float>::max());
    float worst = 0.0f;
    for (std::size_t v = 0; v < positions.size(); ++v) {
      const Vec3 offset = shape_detail::sub(positions[v], centroids[cluster_of[v]]);
      const auto distance = shape_detail::dot(offset, offset);
      worst = std::max(worst, distance);
      if (!drawn[wedge[v]]) continue;
      const auto out = output_of_wedge.at(wedge[v]);
      if (distance < nearest[out]) {
        nearest[out] = distance;
        representative[out] = static_cast<std::uint32_t>(v);
        cluster_of_output[out] = cluster_of[v];
      }
    }
    std::vector<Vertex> vertices(output_of_wedge.size());
    for (std::size_t out = 0; out < vertices.size(); ++out) {
      vertices[out] = geometry.vertices[representative[out]];
      write_position(vertices[out], centroids[cluster_of_output[out]]);
    }
    std::vector<std::uint32_t> triangles;
    triangles.reserve(kept.size());
    for (const auto corner : kept) triangles.push_back(output_of_wedge.at(wedge[corner]));
    if (triangles.empty() || triangles.size() / k_axes > target) continue;
    if (!std::isfinite(worst)) return "a cluster error is not finite";
    best_vertices = std::move(vertices);
    best_triangles = std::move(triangles);
    best_error = std::sqrt(worst);
    break;
  }
  if (best_vertices.empty()) return "the grid cannot reach the requested ratio";
  geometry.vertices = std::move(best_vertices);
  geometry.triangles = std::move(best_triangles);
  if (error != nullptr) *error = best_error;
  return std::nullopt;
}

Vec3 perpendicular(const Vec3& normal) {
  std::size_t smallest = 0;
  for (std::size_t axis = 1; axis < k_axes; ++axis) {
    if (std::fabs(normal[axis]) < std::fabs(normal[smallest])) smallest = axis;
  }
  Vec3 axis{};
  axis[smallest] = 1.0f;
  const Vec3 result = shape_detail::cross(normal, axis);
  return shape_detail::dot(result, result) > 0.0f ? shape_detail::unit(result) : Vec3{1.0f, 0.0f, 0.0f};
}

Vec3 orthogonal_tangent(const Vec3& tangent, const Vec3& normal) {
  const Vec3 residual = shape_detail::sub(tangent, shape_detail::scale(normal, shape_detail::dot(tangent, normal)));
  const float residual_sq = shape_detail::dot(residual, residual);
  return residual_sq > k_min_sine_squared * shape_detail::dot(tangent, tangent) ? shape_detail::unit(residual)
                                                                                 : perpendicular(normal);
}

// Invariant: a stored unit vector never decodes shorter than half a unit, so anything shorter is invalid data.
bool is_unit_vector(const Vec3& value) {
  const auto length = shape_detail::dot(value, value);
  return std::isfinite(length) && length >= k_min_unit_length * k_min_unit_length &&
         length <= k_max_unit_length * k_max_unit_length;
}

}  // namespace

std::optional<std::string> apply_mesh_op(write_detail::Model& model, std::uint32_t block, ShapeOp op,
                                         const Layout& layout, float ratio, Delta* delta,
                                         std::vector<RemapRecord>* remap) {
  const auto type = write_detail::block_type(model, block);
  if (type != k_plain_type) {
    return "only BSTriShape is supported; " + std::string(type) +
           " keeps data after its triangle list that a changed count would invalidate";
  }
  if (layout.triangle_count == 0) return "no triangles";
  auto& bytes = model.blocks[block];
  Geometry geometry;
  if (const auto reason = read_geometry(bytes, layout, &geometry)) return reason;
  std::vector<Vec3> positions;
  const std::vector<Vec3>* finite_positions = nullptr;
  if (op == ShapeOp::remove_degenerate_triangles && layout.full) {
    positions = positions_of(geometry);
    finite_positions = &positions;
  }
  const bool tagged = remap != nullptr && !remap->empty() && op != ShapeOp::remove_degenerate_triangles;
  if (tagged) {
    if (remap->size() != geometry.vertices.size()) return "the remap holds a different number of records than vertices";
    for (std::size_t v = 0; v < geometry.vertices.size(); ++v) {
      remap_detail::append_record(geometry.vertices[v], (*remap)[v]);
    }
  }
  const auto vertices_before = geometry.vertices.size();
  const auto triangles_before = geometry.triangles.size() / k_axes;
  float error = 0.0f;
  std::optional<std::string> reason;
  switch (op) {
    case ShapeOp::weld_vertices:
      reason = weld_duplicate_vertices(geometry);
      break;
    case ShapeOp::remove_degenerate_triangles:
      reason = drop_degenerate_triangles(geometry, finite_positions);
      break;
    case ShapeOp::remove_unused_vertices:
      reason = drop_unused_vertices(geometry);
      break;
    case ShapeOp::simplify:
      if (!layout.full) {
        reason = "half-precision positions have no exact cluster centroid";
      } else if (layout.skinned) {
        reason = "skinned, so a cluster would merge vertices with different bone weights";
      } else {
        reason = simplify_geometry(geometry, ratio, &error);
      }
      break;
    default:
      reason = "not a mesh operation";
      break;
  }
  if (reason) return reason;
  if (geometry.vertices.size() > k_max_vertices) return "the vertex count does not fit a tri-shape";
  if (tagged) {
    remap->clear();
    for (auto& vertex : geometry.vertices) {
      remap->push_back(remap_detail::record_at(vertex, layout.stride));
      vertex.resize(layout.stride);
    }
  }
  write_geometry(bytes, layout, geometry);
  if (delta != nullptr) {
    delta->vertices_before = vertices_before;
    delta->vertices_after = geometry.vertices.size();
    delta->triangles_before = triangles_before;
    delta->triangles_after = geometry.triangles.size() / k_axes;
    delta->max_error = error;
  }
  return std::nullopt;
}

std::optional<std::string> sanitize_normals(Bytes& bytes, const Shape& shape, const Layout& layout) {
  const auto positions = shape_detail::to_vec3(shape.verts);
  for (const auto& point : positions) {
    for (std::size_t axis = 0; axis < k_axes; ++axis) {
      if (!std::isfinite(point[axis])) return "a vertex position is not finite";
    }
  }
  if (!layout.has_normal && !layout.has_tangent) return "no normals or tangents stored";
  Geometry geometry;
  if (const auto reason = read_geometry(bytes, layout, &geometry)) return reason;
  std::vector<Vec3> fallback(positions.size(), Vec3{});
  for (std::size_t face = 0; face + k_axes <= geometry.triangles.size(); face += k_axes) {
    const auto& a = positions[geometry.triangles[face]];
    const auto& b = positions[geometry.triangles[face + 1]];
    const auto& c = positions[geometry.triangles[face + 2]];
    const Vec3 area = shape_detail::cross(shape_detail::sub(b, a), shape_detail::sub(c, a));
    for (std::size_t corner = 0; corner < k_axes; ++corner) {
      const auto vertex = geometry.triangles[face + corner];
      fallback[vertex] = shape_detail::add(fallback[vertex], area);
    }
  }
  auto normals = layout.has_normal ? shape_detail::to_vec3(shape.normals) : std::vector<Vec3>{};
  auto tangents = layout.has_tangent ? shape_detail::to_vec3(shape.tangents) : std::vector<Vec3>{};
  const bool has_bitangent = layout.has_normal && layout.has_tangent;
  const auto bitangents = has_bitangent ? shape_detail::to_vec3(shape.bitangents) : std::vector<Vec3>{};
  bool repaired = false;
  for (std::size_t v = 0; v < geometry.vertices.size(); ++v) {
    auto& vertex = geometry.vertices[v];
    const auto face = fallback[v];
    const Vec3 face_normal = shape_detail::dot(face, face) > 0.0f ? shape_detail::unit(face) : Vec3{0.0f, 0.0f, 1.0f};
    bool frame_changed = false;
    if (layout.has_normal && !is_unit_vector(normals[v])) {
      normals[v] = face_normal;
      write_unit(vertex, layout.normal_at, face_normal);
      frame_changed = true;
    }
    if (layout.has_tangent) {
      const Vec3 reference = layout.has_normal ? normals[v] : face_normal;
      const bool invalid = !is_unit_vector(tangents[v]);
      const bool skewed = !invalid && layout.has_normal &&
                          std::fabs(shape_detail::dot(shape_detail::unit(tangents[v]), reference)) > k_max_frame_dot;
      if (invalid || skewed) {
        tangents[v] = invalid ? perpendicular(reference) : orthogonal_tangent(tangents[v], reference);
        write_unit(vertex, layout.normal_at + (layout.has_normal ? 4 : 0), tangents[v]);
        frame_changed = true;
      }
    }
    if (frame_changed) repaired = true;
    if (has_bitangent && frame_changed) {
      const Vec3 frame = shape_detail::cross(normals[v], tangents[v]);
      const float side = shape_detail::dot(bitangents[v], frame) < 0.0f ? -1.0f : 1.0f;
      const Vec3 bitangent = shape_detail::scale(frame, side);
      if (layout.full) {
        write_int(vertex, layout.bitangent_x_at, std::bit_cast<std::uint32_t>(bitangent[0]));
      } else {
        write_int(vertex, layout.bitangent_x_at, half_bits(bitangent[0]));
      }
      vertex[layout.normal_at + 3] = static_cast<std::byte>(unit_byte(bitangent[1]));
      vertex[layout.normal_at + 7] = static_cast<std::byte>(unit_byte(bitangent[2]));
    }
  }
  if (!repaired) return "no invalid normals or tangents";
  write_geometry(bytes, layout, geometry);
  return std::nullopt;
}

}  // namespace fo4::nif::mesh_detail
