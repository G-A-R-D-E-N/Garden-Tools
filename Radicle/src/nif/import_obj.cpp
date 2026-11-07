#include "import_mesh.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::nif::import_detail {
namespace {

constexpr float k_half_max = 65504.0f;
constexpr float k_degenerate_uv = 1e-12f;
constexpr std::string_view k_blanks = " \t\r";

struct Corner {
  std::int64_t position{-1};
  std::int64_t uv{-1};
  std::int64_t normal{-1};
};

struct Parsed {
  std::string name;
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::vector<std::vector<Corner>> faces;
};

[[noreturn]] void fail(std::size_t line, const std::string& message) {
  throw std::runtime_error("OBJ line " + std::to_string(line) + ": " + message);
}

Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 scale(const Vec3& a, float factor) { return {a[0] * factor, a[1] * factor, a[2] * factor}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 unit(const Vec3& a) { return scale(a, 1.0f / std::sqrt(dot(a, a))); }
Vec3 drop_along(const Vec3& v, const Vec3& axis) { return sub(v, scale(axis, dot(axis, v))); }
Vec3 perpendicular(const Vec3& normal) {
  return unit(cross(normal, std::fabs(normal[0]) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f}));
}

std::vector<std::string_view> fields_of(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t pos = line.find_first_not_of(k_blanks);
  while (pos != std::string_view::npos) {
    const auto end = line.find_first_of(k_blanks, pos);
    fields.push_back(line.substr(pos, end - pos));
    pos = end == std::string_view::npos ? end : line.find_first_not_of(k_blanks, end);
  }
  return fields;
}

float parse_float(std::string_view text, std::size_t line) {
  float value = 0.0f;
  const char* last = text.data() + text.size();
  const auto [end, ec] = std::from_chars(text.data(), last, value);
  if (ec != std::errc{} || end != last || !std::isfinite(value)) {
    fail(line, "'" + std::string(text) + "' is not a finite number");
  }
  return value;
}

std::int64_t parse_index(std::string_view text, std::size_t count, std::string_view kind, std::size_t line) {
  std::int64_t value = 0;
  const char* last = text.data() + text.size();
  const auto [end, ec] = std::from_chars(text.data(), last, value);
  const auto total = static_cast<std::int64_t>(count);
  if (ec != std::errc{} || end != last || value == 0 || value > total || value < -total) {
    fail(line, std::string(kind) + " index '" + std::string(text) + "' is out of range (" + std::to_string(count) +
                   " defined)");
  }
  return value > 0 ? value - 1 : total + value;
}

Corner parse_corner(std::string_view token, const Parsed& parsed, std::size_t line) {
  constexpr auto npos = std::string_view::npos;
  const auto first = token.find('/');
  const auto second = first == npos ? npos : token.find('/', first + 1);
  Corner corner;
  corner.position = parse_index(token.substr(0, first), parsed.positions.size(), "vertex", line);
  if (first == npos) return corner;
  const auto uv_text = token.substr(first + 1, second == npos ? npos : second - first - 1);
  if (!uv_text.empty()) corner.uv = parse_index(uv_text, parsed.uvs.size(), "texture", line);
  if (second != npos && second + 1 < token.size()) {
    corner.normal = parse_index(token.substr(second + 1), parsed.normals.size(), "normal", line);
  }
  return corner;
}

Vec3 read_vec3(const std::vector<std::string_view>& fields, std::size_t line) {
  if (fields.size() < 4) fail(line, std::string(fields[0]) + " needs three numbers");
  return {parse_float(fields[1], line), parse_float(fields[2], line), parse_float(fields[3], line)};
}

Vec3 axis_map(const Vec3& p, bool from_blender) { return from_blender ? Vec3{p[0], -p[2], p[1]} : p; }

Parsed parse(std::string_view text, bool from_blender) {
  Parsed parsed;
  std::size_t start = 0;
  for (std::size_t line = 1; start < text.size(); ++line) {
    const auto end = std::min(text.find('\n', start), text.size());
    const auto fields = fields_of(text.substr(start, end - start));
    start = end + 1;
    if (fields.empty() || fields[0].front() == '#') continue;
    const auto keyword = fields[0];
    if (keyword == "v") {
      parsed.positions.push_back(axis_map(read_vec3(fields, line), from_blender));
    } else if (keyword == "vn") {
      const auto normal = read_vec3(fields, line);
      if (dot(normal, normal) == 0.0f) fail(line, "normal has zero length");
      parsed.normals.push_back(unit(axis_map(normal, from_blender)));
    } else if (keyword == "vt") {
      if (fields.size() < 3) fail(line, "vt needs two numbers");
      const Vec2 uv{parse_float(fields[1], line), parse_float(fields[2], line)};
      if (std::fabs(uv[0]) > k_half_max || std::fabs(uv[1]) > k_half_max) {
        fail(line, "texture coordinate is outside the half-float range");
      }
      parsed.uvs.push_back(uv);
    } else if (keyword == "f") {
      if (fields.size() < 4) fail(line, "face needs at least three vertices");
      std::vector<Corner> corners;
      for (std::size_t i = 1; i < fields.size(); ++i) corners.push_back(parse_corner(fields[i], parsed, line));
      parsed.faces.push_back(std::move(corners));
    } else if ((keyword == "o" || keyword == "g") && parsed.name.empty() && fields.size() > 1) {
      parsed.name = std::string(fields[1]);
    }
  }
  return parsed;
}

std::vector<Vec3> smooth_normals(const Parsed& parsed) {
  std::vector<Vec3> sums(parsed.positions.size());
  for (const auto& face : parsed.faces) {
    for (std::size_t i = 1; i + 1 < face.size(); ++i) {
      const std::array<std::size_t, 3> corners{static_cast<std::size_t>(face[0].position),
                                               static_cast<std::size_t>(face[i].position),
                                               static_cast<std::size_t>(face[i + 1].position)};
      const Vec3 area = cross(sub(parsed.positions[corners[1]], parsed.positions[corners[0]]),
                              sub(parsed.positions[corners[2]], parsed.positions[corners[0]]));
      for (const auto index : corners) sums[index] = add(sums[index], area);
    }
  }
  for (auto& normal : sums) normal = dot(normal, normal) > 0.0f ? unit(normal) : Vec3{0.0f, 0.0f, 1.0f};
  return sums;
}

}

void add_tangent_frame(Mesh& mesh) {
  std::vector<Vec3> tangent_sums(mesh.positions.size());
  std::vector<Vec3> bitangent_sums(mesh.positions.size());
  for (std::size_t t = 0; t < mesh.tris.size(); t += 3) {
    const std::array<std::size_t, 3> corners{mesh.tris[t], mesh.tris[t + 1], mesh.tris[t + 2]};
    const Vec3 edge1 = sub(mesh.positions[corners[1]], mesh.positions[corners[0]]);
    const Vec3 edge2 = sub(mesh.positions[corners[2]], mesh.positions[corners[0]]);
    const float du1 = mesh.uvs[corners[1]][0] - mesh.uvs[corners[0]][0];
    const float dv1 = mesh.uvs[corners[1]][1] - mesh.uvs[corners[0]][1];
    const float du2 = mesh.uvs[corners[2]][0] - mesh.uvs[corners[0]][0];
    const float dv2 = mesh.uvs[corners[2]][1] - mesh.uvs[corners[0]][1];
    const float determinant = du1 * dv2 - du2 * dv1;
    if (std::fabs(determinant) < k_degenerate_uv) continue;
    const Vec3 tangent = scale(sub(scale(edge1, dv2), scale(edge2, dv1)), 1.0f / determinant);
    const Vec3 bitangent = scale(sub(scale(edge2, du1), scale(edge1, du2)), 1.0f / determinant);
    for (const auto index : corners) {
      tangent_sums[index] = add(tangent_sums[index], tangent);
      bitangent_sums[index] = add(bitangent_sums[index], bitangent);
    }
  }
  for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
    const Vec3& normal = mesh.normals[v];
    const Vec3 along = drop_along(tangent_sums[v], normal);
    const Vec3 tangent = dot(along, along) > 0.0f ? unit(along) : perpendicular(normal);
    const Vec3 across = drop_along(drop_along(bitangent_sums[v], normal), tangent);
    mesh.tangents.push_back(tangent);
    mesh.bitangents.push_back(dot(across, across) > 0.0f ? unit(across) : cross(normal, tangent));
  }
}

Mesh read_obj(std::string_view text, bool from_blender) {
  const auto parsed = parse(text, from_blender);
  if (parsed.faces.empty()) throw std::runtime_error("OBJ has no faces to import");
  const auto smooth = smooth_normals(parsed);
  Mesh mesh;
  mesh.name = parsed.name;
  std::map<std::array<std::int64_t, 3>, std::uint32_t> unique;
  const auto vertex_for = [&](const Corner& corner) {
    const std::array<std::int64_t, 3> key{corner.position, corner.uv, corner.normal};
    const auto [slot, inserted] = unique.try_emplace(key, static_cast<std::uint32_t>(unique.size()));
    if (inserted) {
      const auto position = static_cast<std::size_t>(corner.position);
      mesh.positions.push_back(parsed.positions[position]);
      mesh.normals.push_back(corner.normal >= 0 ? parsed.normals[static_cast<std::size_t>(corner.normal)]
                                                : smooth[position]);
      mesh.uvs.push_back(corner.uv >= 0 ? parsed.uvs[static_cast<std::size_t>(corner.uv)] : Vec2{0.0f, 0.0f});
    }
    return slot->second;
  };
  for (const auto& face : parsed.faces) {
    for (std::size_t i = 1; i + 1 < face.size(); ++i) {
      for (const auto index : {vertex_for(face[0]), vertex_for(face[i]), vertex_for(face[i + 1])}) {
        mesh.tris.push_back(index);
      }
    }
  }
  add_tangent_frame(mesh);
  return mesh;
}

std::array<float, 4> bounding_sphere(const std::vector<Vec3>& positions) {
  if (positions.empty()) return {0.0f, 0.0f, 0.0f, 0.0f};
  std::array<float, 3> center{};
  const auto count = static_cast<float>(positions.size());
  for (const auto& p : positions) {
    for (std::size_t k = 0; k < 3; ++k) center[k] += p[k] / count;
  }
  float radius_sq = 0.0f;
  for (const auto& p : positions) {
    float distance_sq = 0.0f;
    for (std::size_t k = 0; k < 3; ++k) distance_sq += (p[k] - center[k]) * (p[k] - center[k]);
    radius_sq = std::max(radius_sq, distance_sq);
  }
  return {center[0], center[1], center[2], std::sqrt(radius_sq)};
}

}
