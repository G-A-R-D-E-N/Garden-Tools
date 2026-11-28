#include "shape_ops_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

namespace fo4::nif::shape_detail {
namespace {

constexpr float k_smooth_all_degrees = 180.0f;
constexpr float k_same_direction_cosine = 0.9999f;

std::uint32_t find_root(std::vector<std::uint32_t>& parent, std::uint32_t item) {
  while (parent[item] != item) {
    parent[item] = parent[parent[item]];
    item = parent[item];
  }
  return item;
}

void join(std::vector<std::uint32_t>& parent, std::uint32_t first, std::uint32_t second) {
  parent[find_root(parent, first)] = find_root(parent, second);
}

float angle_between(const Vec3& a, const Vec3& b) {
  const float lengths = std::sqrt(dot(a, a) * dot(b, b));
  if (lengths <= 0.0f) return 0.0f;
  return std::acos(std::clamp(dot(a, b) / lengths, -1.0f, 1.0f));
}

bool same_direction(const Vec3& a, const Vec3& b) {
  if (dot(a, a) <= 0.0f || dot(b, b) <= 0.0f) return true;
  return dot(unit(a), unit(b)) >= k_same_direction_cosine;
}

}  // namespace

SmoothedNormals smooth_normals(const Mesh& mesh, float crease_degrees) {
  const auto corners = mesh.tris.size();
  const auto faces = corners / 3;
  std::map<Vec3, std::uint32_t> point_of_position;
  std::vector<std::uint32_t> point(mesh.positions.size());
  for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
    const auto next = static_cast<std::uint32_t>(point_of_position.size());
    point[v] = point_of_position.try_emplace(mesh.positions[v], next).first->second;
  }

  std::vector<Vec3> face_normal(faces);
  std::vector<float> corner_angle(corners);
  for (std::size_t f = 0; f < faces; ++f) {
    const auto& p0 = mesh.positions[mesh.tris[3 * f]];
    const auto& p1 = mesh.positions[mesh.tris[3 * f + 1]];
    const auto& p2 = mesh.positions[mesh.tris[3 * f + 2]];
    const Vec3 raw = cross(sub(p1, p0), sub(p2, p0));
    if (dot(raw, raw) > 0.0f) face_normal[f] = unit(raw);
    corner_angle[3 * f] = angle_between(sub(p1, p0), sub(p2, p0));
    corner_angle[3 * f + 1] = angle_between(sub(p0, p1), sub(p2, p1));
    corner_angle[3 * f + 2] = angle_between(sub(p0, p2), sub(p1, p2));
  }

  const auto none = static_cast<std::uint32_t>(corners);
  std::vector<std::uint32_t> parent(corners);
  std::iota(parent.begin(), parent.end(), std::uint32_t{0});
  const bool smooth_all = crease_degrees >= k_smooth_all_degrees;
  if (smooth_all) {
    std::vector<std::uint32_t> first_corner(point_of_position.size(), none);
    for (std::size_t c = 0; c < corners; ++c) {
      const auto p = point[mesh.tris[c]];
      if (first_corner[p] == none) {
        first_corner[p] = static_cast<std::uint32_t>(c);
      } else {
        join(parent, first_corner[p], static_cast<std::uint32_t>(c));
      }
    }
  } else {
    const float cos_crease = std::cos(crease_degrees * std::numbers::pi_v<float> / 180.0f);
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::size_t>> faces_on_edge;
    for (std::size_t f = 0; f < faces; ++f) {
      for (std::size_t i = 0; i < 3; ++i) {
        const auto a = point[mesh.tris[3 * f + i]];
        const auto b = point[mesh.tris[3 * f + (i + 1) % 3]];
        faces_on_edge[std::pair<std::uint32_t, std::uint32_t>{std::min(a, b), std::max(a, b)}].push_back(f);
      }
    }
    const auto join_at_point = [&](std::size_t f, std::size_t g, std::uint32_t x) {
      for (std::size_t k = 0; k < 3; ++k) {
        if (point[mesh.tris[3 * f + k]] != x) continue;
        for (std::size_t m = 0; m < 3; ++m) {
          if (point[mesh.tris[3 * g + m]] == x) {
            join(parent, static_cast<std::uint32_t>(3 * f + k), static_cast<std::uint32_t>(3 * g + m));
          }
        }
      }
    };
    for (const auto& [edge, sharing] : faces_on_edge) {
      for (std::size_t i = 0; i < sharing.size(); ++i) {
        for (std::size_t j = i + 1; j < sharing.size(); ++j) {
          const auto f = sharing[i];
          const auto g = sharing[j];
          if (dot(face_normal[f], face_normal[g]) < cos_crease) continue;
          for (const auto x : {edge.first, edge.second}) join_at_point(f, g, x);
        }
      }
    }
  }

  std::vector<Vec3> cluster(corners, Vec3{});
  for (std::size_t c = 0; c < corners; ++c) {
    const auto root = find_root(parent, static_cast<std::uint32_t>(c));
    cluster[root] = add(cluster[root], scale(face_normal[c / 3], corner_angle[c]));
  }
  std::map<std::pair<std::uint32_t, std::uint32_t>, float> angle_of_group;
  for (std::size_t c = 0; c < corners; ++c) {
    angle_of_group[{mesh.tris[c], find_root(parent, static_cast<std::uint32_t>(c))}] += corner_angle[c];
  }

  SmoothedNormals result{mesh.normals, {}};
  for (auto first = angle_of_group.begin(); first != angle_of_group.end();) {
    const auto vertex = first->first.first;
    auto last = first;
    auto dominant = first;
    for (; last != angle_of_group.end() && last->first.first == vertex; ++last) {
      if (last->second > dominant->second) dominant = last;
    }
    const auto& chosen = cluster[dominant->first.second];
    for (auto group = first; group != last; ++group) {
      if (!same_direction(chosen, cluster[group->first.second])) {
        result.unhonoured.push_back(vertex);
        break;
      }
    }
    if (dot(chosen, chosen) > 0.0f) result.normals[vertex] = unit(chosen);
    first = last;
  }
  return result;
}

}  // namespace fo4::nif::shape_detail
