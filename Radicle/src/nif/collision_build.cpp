#include "fo4/nif/collision_build.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"

#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_axes = 3;
constexpr std::size_t k_max_hull_vertices = 255;
constexpr float k_min_tolerance = 1e-6f;
constexpr std::uint16_t k_no_index = 0xFFFFU;

using Vec3 = std::array<double, k_axes>;
using PositionKey = std::array<std::uint32_t, k_axes>;

Vec3 sub(const Vec3& a, const Vec3& b) { return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return Vec3{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double length(const Vec3& a) { return std::sqrt(dot(a, a)); }

PositionKey key_of(const float* position) {
  return PositionKey{std::bit_cast<std::uint32_t>(position[0]), std::bit_cast<std::uint32_t>(position[1]),
                     std::bit_cast<std::uint32_t>(position[2])};
}

struct Face {
  std::array<std::uint16_t, 3> corners{};
  Vec3 normal{};
  double offset{0.0};
  double area{0.0};
};

}  // namespace

MeshHull build_convex_hull_from_mesh(std::span<const float> verts, std::span<const std::uint16_t> tris,
                                    std::uint32_t material_crc, float tolerance_nif_units,
                                    float game_units_per_metre) {
  if (verts.size() % k_axes != 0 || verts.empty()) throw std::invalid_argument("mesh vertices must be whole xyz positions");
  if (tris.size() % k_axes != 0 || tris.empty()) throw std::invalid_argument("mesh triangles must be whole faces");
  if (!std::isfinite(tolerance_nif_units) || tolerance_nif_units < 0.0f) {
    throw std::invalid_argument("hull tolerance must be finite and non-negative");
  }
  if (!(game_units_per_metre > 0.0f) || !std::isfinite(game_units_per_metre)) {
    throw std::invalid_argument("game units per metre must be finite and positive");
  }
  const auto vertex_count = verts.size() / k_axes;
  for (const float value : verts) {
    if (!std::isfinite(value)) throw std::invalid_argument("a mesh vertex position is not finite");
  }
  for (const auto index : tris) {
    if (static_cast<std::size_t>(index) >= vertex_count) throw std::invalid_argument("a mesh triangle index is out of range");
  }
  const double slack = std::max(static_cast<double>(tolerance_nif_units), static_cast<double>(k_min_tolerance));

  std::vector<Vec3> positions;
  positions.reserve(vertex_count);
  for (std::size_t v = 0; v < vertex_count; ++v) {
    positions.push_back(Vec3{verts[3 * v], verts[3 * v + 1], verts[3 * v + 2]});
  }

  std::vector<bool> referenced(vertex_count, false);
  for (std::size_t face = 0; face + k_axes <= tris.size(); face += k_axes) {
    const auto a = tris[face], b = tris[face + 1], c = tris[face + 2];
    if (a == b || b == c || a == c) continue;
    referenced[a] = true;
    referenced[b] = true;
    referenced[c] = true;
  }
  std::map<PositionKey, std::uint16_t> distinct_of_key;
  std::vector<std::uint16_t> distinct_of_vertex(vertex_count, k_no_index);
  std::vector<Vec3> distinct;
  for (std::size_t v = 0; v < vertex_count; ++v) {
    if (!referenced[v]) continue;
    const auto key = key_of(&verts[3 * v]);
    const auto [found, inserted] = distinct_of_key.try_emplace(key, static_cast<std::uint16_t>(distinct.size()));
    if (inserted) distinct.push_back(positions[v]);
    distinct_of_vertex[v] = found->second;
  }
  if (distinct.empty()) throw std::invalid_argument("the mesh has no referenced vertex position");
  if (distinct.size() > k_max_hull_vertices) {
    throw std::invalid_argument("the mesh has more than 255 distinct vertex positions, which a convex shape cannot hold");
  }

  std::vector<Face> faces;
  std::size_t considered = 0;
  for (std::size_t face = 0; face + k_axes <= tris.size(); face += k_axes) {
    const auto first = distinct_of_vertex[tris[face]];
    const auto second = distinct_of_vertex[tris[face + 1]];
    const auto third = distinct_of_vertex[tris[face + 2]];
    if (first == k_no_index || second == k_no_index || third == k_no_index) continue;
    if (first == second || second == third || first == third) continue;
    const Vec3 normal = cross(sub(distinct[second], distinct[first]), sub(distinct[third], distinct[first]));
    const double area2 = length(normal);
    if (!(area2 > 1e-12)) continue;
    ++considered;
    const Vec3 unit{normal[0] / area2, normal[1] / area2, normal[2] / area2};
    const double offset = dot(unit, distinct[first]);
    bool outward = true;
    bool inward = true;
    bool others = false;
    for (std::size_t p = 0; p < distinct.size(); ++p) {
      if (p == first || p == second || p == third) continue;
      others = true;
      const double side = dot(unit, distinct[p]) - offset;
      if (side > slack) outward = false;
      if (side < -slack) inward = false;
    }
    if (!others || (!outward && !inward)) continue;
    faces.push_back(Face{{first, second, third}, unit, offset, area2 / 2.0});
  }
  if (faces.empty()) throw std::invalid_argument("the mesh has no supporting plane, so it has no convex hull face");

  std::vector<bool> supported(distinct.size(), false);
  for (std::size_t p = 0; p < distinct.size(); ++p) {
    for (const auto& face : faces) {
      if (std::fabs(dot(face.normal, distinct[p]) - face.offset) <= slack) {
        supported[p] = true;
        break;
      }
    }
  }
  for (std::size_t p = 0; p < distinct.size(); ++p) {
    if (!supported[p]) throw std::invalid_argument("a mesh vertex lies inside every hull plane, so the mesh is not convex");
  }
  if (distinct.size() < 4) throw std::invalid_argument("the mesh hull has fewer than four vertices, so it is not volumetric");

  Vec3 centroid{0.0, 0.0, 0.0};
  Vec3 low = distinct.front();
  Vec3 high = distinct.front();
  for (const auto& point : distinct) {
    for (std::size_t axis = 0; axis < k_axes; ++axis) {
      low[axis] = std::min(low[axis], point[axis]);
      high[axis] = std::max(high[axis], point[axis]);
      centroid[axis] += point[axis];
    }
  }
  for (std::size_t axis = 0; axis < k_axes; ++axis) centroid[axis] /= static_cast<double>(distinct.size());
  double thinnest = std::numeric_limits<double>::max();
  for (std::size_t axis = 0; axis < k_axes; ++axis) thinnest = std::min(thinnest, high[axis] - low[axis]);
  if (!(thinnest > slack)) throw std::invalid_argument("the mesh hull has no axis thicker than the tolerance");

  double nearest = std::numeric_limits<double>::max();
  double volume = 0.0;
  for (const auto& face : faces) {
    const double distance = std::fabs(dot(face.normal, centroid) - face.offset);
    nearest = std::min(nearest, distance);
    volume += face.area * distance / 3.0;
  }
  // Invariant: a zero inradius means every hull vertex lies in one plane, which is rotation-independent flatness.
  if (!(nearest > slack)) throw std::invalid_argument("the mesh hull is flat: no vertex lies off a face plane by more than the tolerance");

  std::vector<std::uint16_t> compact(distinct.size(), k_no_index);
  std::vector<HknpVec3> hull_vertices;
  hull_vertices.reserve(distinct.size());
  for (std::size_t v = 0; v < vertex_count; ++v) {
    if (!referenced[v]) continue;
    const auto p = distinct_of_vertex[v];
    if (p == k_no_index || compact[p] != k_no_index) continue;
    compact[p] = static_cast<std::uint16_t>(hull_vertices.size());
    const auto& point = distinct[p];
    hull_vertices.push_back(HknpVec3{static_cast<float>(point[0] / game_units_per_metre),
                                     static_cast<float>(point[1] / game_units_per_metre),
                                     static_cast<float>(point[2] / game_units_per_metre)});
  }

  MeshHull hull;
  hull.shape.kind = HknpShapeKind::convex;
  hull.shape.class_name = "hknpConvexShape";
  hull.shape.shape_flags = k_convex_shape_flags;
  hull.shape.convex_radius = 0.0f;
  hull.shape.material_crc = material_crc;
  hull.shape.vertices = std::move(hull_vertices);
  hull.nearest_face_distance_metres = static_cast<float>(nearest / game_units_per_metre);
  hull.volume_metres3 = static_cast<float>(volume / (static_cast<double>(game_units_per_metre) *
                                                    static_cast<double>(game_units_per_metre) *
                                                    static_cast<double>(game_units_per_metre)));
  hull.triangles_considered = considered;
  hull.distinct_positions = distinct.size();
  hull.face_indices.reserve(faces.size() * k_axes);
  for (const auto& face : faces) {
    for (const auto corner : face.corners) hull.face_indices.push_back(compact[corner]);
  }
  return hull;
}

std::vector<std::byte> encode_collision_payload(std::span<const float> verts, std::span<const std::uint16_t> tris,
                                               std::uint32_t material_crc, const HknpStaticBody& body,
                                               float tolerance_nif_units, float game_units_per_metre) {
  const auto hull = build_convex_hull_from_mesh(verts, tris, material_crc, tolerance_nif_units, game_units_per_metre);
  return write_hknp_packfile(author_hknp_static_body(hull.shape, body));
}

std::vector<std::byte> replace_collision_blob(const NifFile& nif, std::uint32_t block, std::span<const std::byte> payload) {
  if (block >= nif.blocks().size()) {
    throw parse_error(0, "collision block " + std::to_string(block) + " is not in the file");
  }
  const auto& info = nif.blocks()[block];
  if (!is_collision_system_type(info.type)) {
    throw parse_error(info.offset, "block " + std::to_string(block) + " is not a bhkPhysicsSystem or bhkRagdollSystem");
  }
  if (info.size < 4) throw parse_error(info.offset, "the collision block is shorter than its length prefix");
  const auto stored = static_cast<std::size_t>(write_detail::read_int<std::uint32_t>(nif.bytes(), info.offset));
  if (stored + 4 > info.size) {
    throw parse_error(info.offset, "the collision blob length runs past the end of its block");
  }
  if (payload.empty()) throw std::invalid_argument("a collision payload may not be empty");
  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("the collision payload is too large for its length prefix");
  }
  // Constraint: the payload must already be one hknp packfile, because read_nif only validates the NIF structure.
  read_hknp_packfile(payload);
  auto model = write_detail::load_model(nif);
  const auto tail_length = info.size - 4 - stored;
  const auto tail = std::span<const std::byte>(nif.bytes()).subspan(info.offset + 4 + stored, tail_length);
  std::vector<std::byte> bytes(4 + payload.size() + tail.size());
  const auto size = static_cast<std::uint32_t>(payload.size());
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<std::uint8_t>((size >> (8 * i)) & 0xFFU));
  }
  std::copy(payload.begin(), payload.end(), bytes.begin() + 4);
  std::copy(tail.begin(), tail.end(), bytes.begin() + 4 + static_cast<std::ptrdiff_t>(payload.size()));
  model.blocks[block] = std::move(bytes);
  auto out = write_detail::serialize(model);
  read_nif(out);
  return out;
}
}  // namespace fo4::nif
