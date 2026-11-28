#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <algorithm>
#include <string>
#include <sstream>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace hknp_mesh_walk {

struct Triangle {
  std::vector<fo4::nif::HknpVec3> corners;
  std::uint32_t material{0};
};

struct Walk {
  std::vector<std::size_t> leaves;
  bool in_range{true};
};

inline std::uint32_t u32_at(const fo4::nif::HknpPackObject& object, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(object.bytes.at(offset + i))) << (8 * i);
  }
  return value;
}

inline std::uint32_t local_target(const fo4::nif::HknpPackObject& object, std::size_t source) {
  for (const auto& fixup : object.local) {
    if (fixup.source == source) return fixup.target;
  }
  return 0xFFFFFFFFU;
}

// Constraint: a mesh-tree node is four bytes; an internal node stores (leftLeafCount << 1) | 1 at +3, so its right child is index + 2 * left.
inline Walk mesh_tree_leaves(const fo4::nif::HknpPackObject& data, std::uint32_t at, std::size_t nodes) {
  Walk walk;
  std::vector<std::size_t> pending{0};
  while (!pending.empty()) {
    const std::size_t index = pending.back();
    pending.pop_back();
    if (index >= nodes) {
      walk.in_range = false;
      continue;
    }
    const auto link = std::to_integer<std::uint8_t>(data.bytes.at(at + 4 * index + 3));
    if ((link & 1U) == 0) {
      walk.leaves.push_back(link / 2U);
      continue;
    }
    const std::size_t left = link / 2U;
    pending.push_back(index + 1);
    pending.push_back(index + 2 * left);
  }
  return walk;
}

// Constraint: a section-tree node is five bytes; an internal one marks 0x80 at +3 with its left leaf count at +4, and a leaf names its section at +4.
inline Walk section_tree_leaves(const fo4::nif::HknpPackObject& data, std::uint32_t at, std::size_t nodes) {
  Walk walk;
  std::vector<std::size_t> pending{0};
  while (!pending.empty()) {
    const std::size_t index = pending.back();
    pending.pop_back();
    if (index >= nodes) {
      walk.in_range = false;
      continue;
    }
    const std::size_t node = at + 5 * index;
    const auto kind = std::to_integer<std::uint8_t>(data.bytes.at(node + 3));
    const std::size_t value = std::to_integer<std::uint8_t>(data.bytes.at(node + 4));
    if (kind == 0) {
      walk.leaves.push_back(value);
      continue;
    }
    pending.push_back(index + 1);
    pending.push_back(index + 2 * value);
  }
  return walk;
}

inline std::vector<Triangle> triangles_of(const fo4::nif::HknpShape& shape) {
  std::vector<Triangle> out;
  out.reserve(shape.mesh_triangles.size());
  for (const auto& tri : shape.mesh_triangles) {
    Triangle t;
    const std::size_t used = tri.quad ? 4 : 3;
    for (std::size_t k = 0; k < used; ++k) t.corners.push_back(shape.mesh_vertices.at(tri.indices[k]));
    t.material = tri.material_crc;
    out.push_back(std::move(t));
  }
  return out;
}

inline float distance(const fo4::nif::HknpVec3& a, const fo4::nif::HknpVec3& b) {
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// Constraint: a triangle matches when it has the same corner count and material and every corner has a counterpart within tol.
struct MatchResult {
  std::size_t matched{0};
  std::size_t unmatched{0};
  float worst{0.0f};
  std::string first_unmatched;
};

inline std::string describe_triangle(const Triangle& triangle) {
  std::ostringstream out;
  for (const auto& corner : triangle.corners) out << " (" << corner[0] << ' ' << corner[1] << ' ' << corner[2] << ')';
  out << " material " << triangle.material;
  return out.str();
}

// Names the output triangle closest to one no output matched, ignoring material and whether it is used.
inline std::string describe_unmatched(const Triangle& in, const std::vector<Triangle>& output, float tol) {
  float best = std::numeric_limits<float>::max();
  const Triangle* nearest = nullptr;
  for (const auto& candidate : output) {
    float worst_corner = 0.0f;
    for (const auto& corner : in.corners) {
      float closest = std::numeric_limits<float>::max();
      for (const auto& other : candidate.corners) closest = std::min(closest, distance(corner, other));
      worst_corner = std::max(worst_corner, closest);
    }
    if (worst_corner < best) {
      best = worst_corner;
      nearest = &candidate;
    }
  }
  std::ostringstream out;
  out << "tolerance " << tol << ", input" << describe_triangle(in);
  if (nearest != nullptr) out << ", nearest output off by " << best << ":" << describe_triangle(*nearest);
  return out.str();
}

// Constraint: the search key must not depend on corner order, since the encoder may start a face at another corner.
inline float min_x(const Triangle& triangle) {
  float lowest = std::numeric_limits<float>::max();
  for (const auto& corner : triangle.corners) lowest = std::min(lowest, corner[0]);
  return lowest;
}

inline MatchResult match_triangles(const std::vector<Triangle>& input, std::vector<Triangle> output, float tol) {
  std::sort(output.begin(), output.end(), [](const Triangle& a, const Triangle& b) {
    return min_x(a) < min_x(b);
  });
  std::vector<bool> used(output.size(), false);
  MatchResult result;
  for (const auto& in : input) {
    const float lo = min_x(in) - tol;
    const auto first = std::lower_bound(output.begin(), output.end(), lo, [](const Triangle& t, float value) {
      return min_x(t) < value;
    });
    bool found = false;
    float best = std::numeric_limits<float>::max();
    std::size_t best_index = 0;
    for (auto it = first; it != output.end() && min_x(*it) <= min_x(in) + tol; ++it) {
      const std::size_t index = static_cast<std::size_t>(it - output.begin());
      if (used[index] || it->material != in.material || it->corners.size() != in.corners.size()) continue;
      float worst_corner = 0.0f;
      bool all = true;
      for (const auto& corner : in.corners) {
        float nearest = std::numeric_limits<float>::max();
        for (const auto& other : it->corners) nearest = std::min(nearest, distance(corner, other));
        if (nearest > tol) {
          all = false;
          break;
        }
        worst_corner = std::max(worst_corner, nearest);
      }
      if (all && worst_corner < best) {
        best = worst_corner;
        best_index = index;
        found = true;
      }
    }
    if (found) {
      used[best_index] = true;
      ++result.matched;
      result.worst = std::max(result.worst, best);
    } else {
      ++result.unmatched;
      if (result.first_unmatched.empty()) result.first_unmatched = describe_unmatched(in, output, tol);
    }
  }
  return result;
}

}  // namespace hknp_mesh_walk
