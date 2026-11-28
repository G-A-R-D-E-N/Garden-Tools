#include "fo4/nif/collision_plain.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<double, 9>;

constexpr double k_uniform_tolerance = 1.0e-4;
constexpr int k_max_depth = 40;
constexpr std::uint32_t k_layer_mask = 0xFF;
constexpr std::size_t k_min_hull_points = 4;
// Constraint: engine axis i takes Havok axis k_source[i] times k_sign[i], the swap (x, y, z) to (x, z, -y).
constexpr std::array<std::size_t, 3> k_source{0, 2, 1};
constexpr std::array<double, 3> k_sign{1.0, 1.0, -1.0};

struct Affine {
  Mat3 m{1, 0, 0, 0, 1, 0, 0, 0, 1};
  Vec3 t{};
};

Vec3 apply_matrix(const Mat3& m, const Vec3& v) {
  return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
          m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Vec3 apply(const Affine& a, const HknpVec3& p) {
  const Vec3 moved = apply_matrix(a.m, {p[0], p[1], p[2]});
  return {moved[0] + a.t[0], moved[1] + a.t[1], moved[2] + a.t[2]};
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
  Mat3 out{};
  for (std::size_t r = 0; r < 3; ++r) {
    for (std::size_t c = 0; c < 3; ++c) {
      out[3 * r + c] = a[3 * r] * b[c] + a[3 * r + 1] * b[3 + c] + a[3 * r + 2] * b[6 + c];
    }
  }
  return out;
}

double determinant(const Mat3& m) {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
         m[2] * (m[3] * m[7] - m[4] * m[6]);
}

Vec3 column(const Mat3& m, std::size_t c) { return {m[c], m[3 + c], m[6 + c]}; }

double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

struct Similarity {
  bool uniform{false};
  double scale{1.0};
  Mat3 rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
  double det{1.0};
};

Similarity decompose(const Affine& a) {
  Similarity out;
  out.det = determinant(a.m);
  const std::array<Vec3, 3> cols{column(a.m, 0), column(a.m, 1), column(a.m, 2)};
  std::array<double, 3> norms{};
  for (std::size_t k = 0; k < 3; ++k) norms[k] = std::sqrt(dot(cols[k], cols[k]));
  const double mean = (norms[0] + norms[1] + norms[2]) / 3.0;
  if (!(mean > 0.0) || !std::isfinite(mean)) return out;
  for (std::size_t k = 0; k < 3; ++k) {
    if (std::fabs(norms[k] - mean) > k_uniform_tolerance * mean) return out;
    if (std::fabs(dot(cols[k], cols[(k + 1) % 3])) > k_uniform_tolerance * mean * mean) return out;
  }
  out.uniform = true;
  out.scale = mean;
  for (std::size_t i = 0; i < 9; ++i) out.rotation[i] = a.m[i] / mean;
  return out;
}

std::array<float, 3> engine_point(const Vec3& p) {
  return {static_cast<float>(k_sign[0] * p[k_source[0]]), static_cast<float>(k_sign[1] * p[k_source[1]]),
          static_cast<float>(k_sign[2] * p[k_source[2]])};
}

std::array<float, 9> engine_basis(const Mat3& r) {
  std::array<float, 9> out{};
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      out[3 * i + j] = static_cast<float>(k_sign[i] * k_sign[j] * r[3 * k_source[i] + k_source[j]]);
    }
  }
  return out;
}

Mat3 quaternion_matrix(const HknpVec4& q) {
  double x = q[0];
  double y = q[1];
  double z = q[2];
  double w = q[3];
  const double length = std::sqrt(x * x + y * y + z * z + w * w);
  if (!(length > 0.0) || !std::isfinite(length)) return {1, 0, 0, 0, 1, 0, 0, 0, 1};
  x /= length;
  y /= length;
  z /= length;
  w /= length;
  return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
          2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
          2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
}

// Constraint: eight distinct vertices that each sit on the low or high value of every axis are a box.
bool box_extents(const HknpShape& shape, HknpVec3& low, HknpVec3& high) {
  if (shape.vertices.size() < 8) return false;
  low = shape.vertices.front();
  high = shape.vertices.front();
  for (const auto& v : shape.vertices) {
    for (std::size_t k = 0; k < 3; ++k) {
      low[k] = std::min(low[k], v[k]);
      high[k] = std::max(high[k], v[k]);
    }
  }
  std::vector<HknpVec3> distinct;
  for (const auto& v : shape.vertices) {
    for (std::size_t k = 0; k < 3; ++k) {
      if (v[k] != low[k] && v[k] != high[k]) return false;
    }
    if (std::find(distinct.begin(), distinct.end(), v) == distinct.end()) distinct.push_back(v);
  }
  return distinct.size() == 8;
}

Affine place(const Affine& parent, const HknpInstance& instance) {
  Mat3 local{};
  for (std::size_t r = 0; r < 3; ++r) {
    for (std::size_t c = 0; c < 3; ++c) local[3 * r + c] = instance.rotation_rows[r][c] * instance.scale[c];
  }
  Affine out;
  out.m = multiply(parent.m, local);
  const Vec3 moved = apply_matrix(parent.m, {instance.translation[0], instance.translation[1], instance.translation[2]});
  out.t = {moved[0] + parent.t[0], moved[1] + parent.t[1], moved[2] + parent.t[2]};
  return out;
}

Affine scaled(const Affine& parent, const HknpVec3& scale) {
  Affine out = parent;
  for (std::size_t r = 0; r < 3; ++r) {
    for (std::size_t c = 0; c < 3; ++c) out.m[3 * r + c] = parent.m[3 * r + c] * scale[c];
  }
  return out;
}

class Walker {
public:
  Walker(const HknpSystem& system, const HknpBody& body, PlainCollision& out, std::uint32_t system_index,
         std::uint32_t body_index)
      : system_(system), body_(body), out_(out), system_index_(system_index), body_index_(body_index) {}

  void emit(int index, const Affine& a, int depth) {
    if (depth > k_max_depth) throw fo4::parse_error(0, "collision shape nesting is deeper than the reader allows");
    if (index < 0 || static_cast<std::size_t>(index) >= system_.shapes.size()) {
      ++out_.skipped_shapes;
      return;
    }
    const HknpShape& shape = system_.shapes[static_cast<std::size_t>(index)];
    switch (shape.kind) {
      case HknpShapeKind::compound:
        for (const auto& instance : shape.instances) {
          if (instance.shape < 0 && instance.child_object == k_no_object) continue;
          emit(instance.shape, place(a, instance), depth + 1);
        }
        return;
      case HknpShapeKind::scaled_convex:
        emit(shape.child, scaled(a, shape.scale), depth + 1);
        return;
      case HknpShapeKind::sphere:
        emit_round(shape, a, false);
        return;
      case HknpShapeKind::capsule:
        emit_round(shape, a, true);
        return;
      case HknpShapeKind::convex:
        emit_convex(shape, a);
        return;
      case HknpShapeKind::compressed_mesh:
        emit_mesh(shape, a);
        return;
    }
  }

private:
  PlainShape start(PlainShapeKind kind, const HknpShape& shape) const {
    PlainShape out;
    out.kind = kind;
    out.system = system_index_;
    out.body = body_index_;
    out.material = shape.material_crc != 0 ? shape.material_crc : body_.resolved_material_crc;
    return out;
  }

  void emit_round(const HknpShape& shape, const Affine& a, bool capsule) {
    const Similarity fit = decompose(a);
    if (!fit.uniform) {
      ++out_.skipped_shapes;
      return;
    }
    const auto end_a = engine_point(apply(a, shape.point_a));
    const auto end_b = capsule ? engine_point(apply(a, shape.point_b)) : end_a;
    const double dx = static_cast<double>(end_b[0]) - end_a[0];
    const double dy = static_cast<double>(end_b[1]) - end_a[1];
    const double dz = static_cast<double>(end_b[2]) - end_a[2];
    const bool round_only = !capsule || std::sqrt(dx * dx + dy * dy + dz * dz) < k_degenerate_capsule_metres;
    PlainShape out = start(round_only ? PlainShapeKind::sphere : PlainShapeKind::capsule, shape);
    out.radius = static_cast<float>(shape.radius * fit.scale);
    if (round_only) {
      for (std::size_t k = 0; k < 3; ++k) out.origin[k] = 0.5f * (end_a[k] + end_b[k]);
    } else {
      out.point_a = end_a;
      out.point_b = end_b;
    }
    out_.shapes.push_back(std::move(out));
  }

  void emit_convex(const HknpShape& shape, const Affine& a) {
    const Similarity fit = decompose(a);
    HknpVec3 low{};
    HknpVec3 high{};
    if (fit.uniform && fit.det > 0.0 && box_extents(shape, low, high)) {
      PlainShape out = start(PlainShapeKind::box, shape);
      const HknpVec3 centre{(low[0] + high[0]) / 2.0f, (low[1] + high[1]) / 2.0f, (low[2] + high[2]) / 2.0f};
      out.origin = engine_point(apply(a, centre));
      out.basis = engine_basis(fit.rotation);
      for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t k = k_source[i];
        out.half_extents[i] = static_cast<float>(((high[k] - low[k]) / 2.0 + shape.convex_radius) * fit.scale);
      }
      out_.shapes.push_back(std::move(out));
      return;
    }
    if (shape.vertices.size() < k_min_hull_points) {
      ++out_.skipped_shapes;
      return;
    }
    PlainShape out = start(PlainShapeKind::convex_hull, shape);
    out.margin = static_cast<float>(shape.convex_radius * std::cbrt(std::fabs(determinant(a.m))));
    out.points.reserve(shape.vertices.size() * 3);
    for (const auto& vertex : shape.vertices) {
      const auto p = engine_point(apply(a, vertex));
      out.points.insert(out.points.end(), p.begin(), p.end());
    }
    out_.shapes.push_back(std::move(out));
  }

  void emit_mesh(const HknpShape& shape, const Affine& a) {
    if (shape.mesh_vertices.empty() || shape.mesh_triangles.empty()) {
      ++out_.skipped_shapes;
      return;
    }
    PlainShape out = start(PlainShapeKind::triangle_mesh, shape);
    out.points.reserve(shape.mesh_vertices.size() * 3);
    for (const auto& vertex : shape.mesh_vertices) {
      const auto p = engine_point(apply(a, vertex));
      out.points.insert(out.points.end(), p.begin(), p.end());
    }
    const bool mirrored = determinant(a.m) < 0.0;
    const auto add_triangle = [&](std::uint32_t i, std::uint32_t j, std::uint32_t k, std::uint32_t material) {
      out.indices.push_back(i);
      out.indices.push_back(mirrored ? k : j);
      out.indices.push_back(mirrored ? j : k);
      out.triangle_materials.push_back(material);
    };
    for (const auto& triangle : shape.mesh_triangles) {
      add_triangle(triangle.indices[0], triangle.indices[1], triangle.indices[2], triangle.material_crc);
      if (triangle.quad) add_triangle(triangle.indices[0], triangle.indices[2], triangle.indices[3], triangle.material_crc);
    }
    out_.shapes.push_back(std::move(out));
  }

  const HknpSystem& system_;
  const HknpBody& body_;
  PlainCollision& out_;
  std::uint32_t system_index_;
  std::uint32_t body_index_;
};

void fill_mass(const HknpSystem& system, const HknpBody& body, PlainBody& out) {
  if (!body.dynamic) return;
  if (body.motion_index < system.inertias.size()) {
    const HknpDynInertia& inertia = system.inertias[body.motion_index];
    if (inertia.mass > 0.0f) {
      out.has_mass = true;
      out.mass = inertia.mass;
      out.has_center_of_mass = true;
      out.center_of_mass =
          engine_point({inertia.center[0], inertia.center[1], inertia.center[2]});
      return;
    }
  }
  if (body.shape >= 0) {
    const HknpShape& shape = system.shapes[static_cast<std::size_t>(body.shape)];
    if (shape.has_mass_properties && shape.mass_properties.mass > 0.0f) {
      out.has_mass = true;
      out.mass = shape.mass_properties.mass;
    }
  }
}

}  // namespace

void append_plain_collision(const HknpSystem& system, std::uint32_t system_index, PlainCollision& out) {
  for (std::size_t b = 0; b < system.bodies.size(); ++b) {
    const HknpBody& body = system.bodies[b];
    PlainBody plain;
    plain.system = system_index;
    plain.body = static_cast<std::uint32_t>(b);
    plain.layer = body.packed_filter & k_layer_mask;
    plain.dynamic = body.dynamic;
    plain.trigger_type = body.trigger_type;
    plain.origin = engine_point({body.position[0], body.position[1], body.position[2]});
    plain.basis = engine_basis(quaternion_matrix(body.orientation));
    fill_mass(system, body, plain);
    plain.first_shape = out.shapes.size();
    const std::uint32_t skipped_before = out.skipped_shapes;
    if (body.shape >= 0) {
      Walker(system, body, out, system_index, plain.body).emit(body.shape, Affine{}, 0);
    } else if (body.shape_object != k_no_object) {
      ++out.skipped_shapes;
    }
    plain.shape_count = out.shapes.size() - plain.first_shape;
    plain.skipped_shapes = out.skipped_shapes - skipped_before;
    out.bodies.push_back(plain);
  }
}

PlainCollisionTransform plain_node_transform(const NifFile& nif, std::int32_t node) {
  PlainCollisionTransform out;
  if (node < 0 || static_cast<std::size_t>(node) >= nif.nodes().size()) return out;
  const Transform world = shape_to_global(nif, nif.nodes()[static_cast<std::size_t>(node)].block);
  const double units = static_cast<double>(k_hknp_game_units_per_metre);
  out.translation = engine_point({world.t[0] / units, world.t[1] / units, world.t[2] / units});
  Mat3 rotation{};
  for (std::size_t i = 0; i < 9; ++i) rotation[i] = world.r[i];
  out.rotation = engine_basis(rotation);
  out.scale = world.s;
  return out;
}

void attach_collision_owners(PlainCollision& plain, const NifFile& nif, std::span<const CollisionBlob> blobs) {
  for (auto& body : plain.bodies) {
    if (body.system >= blobs.size()) continue;
    for (const auto& owner : blobs[body.system].owners) {
      if (owner.body_id != body.body) continue;
      body.has_owner = true;
      body.owner_block = owner.owner;
      body.owner_target_node = owner.target_node;
      body.owner_transform = plain_node_transform(nif, owner.target_node);
      break;
    }
  }
}

PlainCollision build_plain_collision(std::span<const CollisionBlob> blobs) {
  PlainCollision out;
  for (std::size_t i = 0; i < blobs.size(); ++i) {
    PlainCollision one;
    try {
      const HknpSystem system = decode_hknp_system(read_hknp_packfile(blobs[i].bytes));
      append_plain_collision(system, static_cast<std::uint32_t>(i), one);
    } catch (const fo4::parse_error&) {
      ++out.undecodable_systems;
      continue;
    }
    const std::size_t shift = out.shapes.size();
    for (auto& body : one.bodies) body.first_shape += shift;
    out.bodies.insert(out.bodies.end(), one.bodies.begin(), one.bodies.end());
    std::move(one.shapes.begin(), one.shapes.end(), std::back_inserter(out.shapes));
    out.skipped_shapes += one.skipped_shapes;
  }
  return out;
}

}  // namespace fo4::nif
