#pragma once

#include "fo4/nif/nif.hpp"
#include "import_mesh.hpp"
#include "write_model.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::nif::shape_detail {

using import_detail::Mesh;
using import_detail::Vec3;
using write_detail::Bytes;

struct Layout {
  std::size_t transform_at{0};
  std::size_t sphere_at{0};
  std::size_t vertices_at{0};
  std::size_t vertex_count{0};
  std::size_t stride{0};
  std::size_t triangles_at{0};
  std::size_t triangle_count{0};
  bool full{false};
  bool skinned{false};
  bool has_uv{false};
  bool has_normal{false};
  bool has_tangent{false};
  std::size_t normal_at{0};
  std::size_t bitangent_x_at{0};
};

struct Frame {
  std::vector<Vec3> normals;
  std::vector<Vec3> tangents;
  std::vector<Vec3> bitangents;
};

inline Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 scale(const Vec3& a, float factor) { return {a[0] * factor, a[1] * factor, a[2] * factor}; }
inline float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Vec3 unit(const Vec3& a) { return scale(a, 1.0f / std::sqrt(dot(a, a))); }

std::vector<Vec3> to_vec3(const std::vector<float>& flat);
Mesh shape_mesh(const Shape& shape);
// Requires a block that read_shape returns with vertices.
Layout read_layout(const NifFile& nif, std::uint32_t block);
Frame read_frame(const Shape& shape, const Layout& layout);
void write_frame(Bytes& bytes, const Layout& layout, const Frame& frame);
// Requires a full-precision layout, because half positions have no exact bake.
void write_positions(Bytes& bytes, const Layout& layout, const std::vector<Vec3>& positions);
void swap_winding(Bytes& bytes, const Layout& layout);
void write_sphere(Bytes& bytes, const Layout& layout, const std::array<float, 4>& sphere);
void write_identity_transform(Bytes& bytes, const Layout& layout);
// Requires mesh.normals to hold the authored normals, which are kept for vertices no face touches.
std::vector<Vec3> smooth_normals(const Mesh& mesh, float crease_degrees);

}  // namespace fo4::nif::shape_detail
