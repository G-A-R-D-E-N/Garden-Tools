#include "fo4/nif/hknp_hull.hpp"

#include "hknp_encode_internal.hpp"

#include "fo4/nif/hknp_mass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using hknp_detail::cross;
using hknp_detail::dot;
using hknp_detail::subtracted;

// Constraint: a point is outside a face only past this tolerance, relative to the largest extent.
constexpr float k_hull_epsilon = 1.0e-5f;
// Constraint: hulls above 252 vertices are refused, the largest vanilla hull size.
constexpr std::size_t k_max_hull_vertices = 252;
// Constraint: more than 300000000 plane tests refuse the hull as too expensive.
constexpr std::uint64_t k_max_hull_plane_tests = 300000000;
// Constraint: vertex and plane counts pad to multiples of four.
constexpr std::size_t k_hull_pad = 4;
// Constraint: shape flags are the box's, as no general hull flag is measured.
constexpr std::uint32_t k_hull_shape_flags = 0x01000143U;
// Constraint: face angle bytes hold 255 steps over pi and cap at 128.
constexpr std::uint8_t k_hull_max_face_min_half_angle = 128;
constexpr double k_hull_angle_per_radian = 255.0 / 3.14159265358979323846;

struct Face {
  std::array<std::uint32_t, 3> v{};
  HknpVec4 plane{{0.0f, 0.0f, 0.0f, 0.0f}};
  bool alive{true};
};

struct Polygon {
  std::vector<std::uint32_t> loop;
  HknpVec4 plane{{0.0f, 0.0f, 0.0f, 0.0f}};
};

float plane_distance(const HknpVec4& plane, const HknpVec3& point) {
  return plane[0] * point[0] + plane[1] * point[1] + plane[2] * point[2] + plane[3];
}

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t lo = std::min(a, b);
  const std::uint32_t hi = std::max(a, b);
  return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

// Constraint: a face is degenerate by its height over its longest edge.
HknpVec4 face_plane(const std::vector<HknpVec3>& points, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                    float height_epsilon) {
  const HknpVec3 ab = subtracted(points[b], points[a]);
  const HknpVec3 ac = subtracted(points[c], points[a]);
  const HknpVec3 cb = subtracted(points[c], points[b]);
  const HknpVec3 normal = cross(ab, ac);
  const float twice_area = std::sqrt(dot(normal, normal));
  const float longest = std::max({std::sqrt(dot(ab, ab)), std::sqrt(dot(ac, ac)), std::sqrt(dot(cb, cb))});
  if (!(twice_area > longest * height_epsilon)) throw std::invalid_argument("a convex hull face is degenerate");
  const HknpVec3 unit{normal[0] / twice_area, normal[1] / twice_area, normal[2] / twice_area};
  return HknpVec4{unit[0], unit[1], unit[2], -dot(unit, points[a])};
}

float extent_of(const std::vector<HknpVec3>& points) {
  HknpVec3 low = points.front();
  HknpVec3 high = points.front();
  for (const auto& point : points) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      low[axis] = std::min(low[axis], point[axis]);
      high[axis] = std::max(high[axis], point[axis]);
    }
  }
  return std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2]});
}

void add_face(std::vector<Face>& faces, const std::vector<HknpVec3>& points, std::uint32_t a, std::uint32_t b,
              std::uint32_t c, float epsilon) {
  Face face;
  face.v = {a, b, c};
  face.plane = face_plane(points, a, b, c, epsilon);
  faces.push_back(face);
}

// Requires four points that are neither coincident, collinear, nor coplanar; anything else raises std::invalid_argument.
std::array<std::uint32_t, 4> initial_tetrahedron(const std::vector<HknpVec3>& points, float epsilon) {
  std::array<std::uint32_t, 6> extremes{};
  std::size_t count = 0;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    std::size_t low = 0;
    std::size_t high = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
      if (points[i][axis] < points[low][axis]) low = i;
      if (points[i][axis] > points[high][axis]) high = i;
    }
    extremes[count++] = static_cast<std::uint32_t>(low);
    extremes[count++] = static_cast<std::uint32_t>(high);
  }

  float widest = 0.0f;
  std::array<std::uint32_t, 2> base{extremes[0], extremes[1]};
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      const HknpVec3 delta = subtracted(points[extremes[j]], points[extremes[i]]);
      const float length = std::sqrt(dot(delta, delta));
      if (length > widest) {
        widest = length;
        base = {extremes[i], extremes[j]};
      }
    }
  }
  if (!(widest > epsilon)) throw std::invalid_argument("a convex hull needs points that are not coincident");

  const HknpVec3 along = subtracted(points[base[1]], points[base[0]]);
  std::uint32_t third = base[0];
  float furthest_line = 0.0f;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const HknpVec3 offset = subtracted(points[i], points[base[0]]);
    const HknpVec3 perpendicular = cross(along, offset);
    const float distance = std::sqrt(dot(perpendicular, perpendicular)) / widest;
    if (distance > furthest_line) {
      furthest_line = distance;
      third = static_cast<std::uint32_t>(i);
    }
  }
  if (!(furthest_line > epsilon)) throw std::invalid_argument("a convex hull needs points that are not collinear");

  const HknpVec4 plane = face_plane(points, base[0], base[1], third, epsilon);
  std::uint32_t fourth = base[0];
  float furthest_plane = 0.0f;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const float distance = std::fabs(plane_distance(plane, points[i]));
    if (distance > furthest_plane) {
      furthest_plane = distance;
      fourth = static_cast<std::uint32_t>(i);
    }
  }
  if (!(furthest_plane > epsilon)) {
    throw std::invalid_argument("a convex hull needs points that are not coplanar");
  }
  if (plane_distance(plane, points[fourth]) > 0.0f) std::swap(base[1], third);
  return {base[0], base[1], third, fourth};
}

void expand_hull(const std::vector<HknpVec3>& points, std::vector<Face>& faces, float epsilon) {
  const std::size_t iteration_cap = 8 * points.size() + 64;
  std::vector<std::uint32_t> stamp(points.size(), 0);
  std::uint64_t plane_tests = 0;
  const auto count_plane_test = [&plane_tests] {
    if (++plane_tests > k_max_hull_plane_tests) {
      throw hull_limit_error(
          "a convex hull needs at most 300000000 point-against-face plane tests; the box collision choice still works");
    }
  };
  for (std::size_t iteration = 0; iteration < iteration_cap; ++iteration) {
    std::size_t seed = faces.size();
    std::uint32_t apex = 0;
    float furthest = epsilon;
    for (std::size_t p = 0; p < points.size(); ++p) {
      for (std::size_t f = 0; f < faces.size(); ++f) {
        if (!faces[f].alive) continue;
        count_plane_test();
        const float distance = plane_distance(faces[f].plane, points[p]);
        if (distance > furthest) {
          furthest = distance;
          apex = static_cast<std::uint32_t>(p);
          seed = f;
        }
      }
    }
    if (seed == faces.size()) return;

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> edges;
    for (std::size_t f = 0; f < faces.size(); ++f) {
      if (!faces[f].alive) continue;
      for (std::size_t e = 0; e < 3; ++e) {
        edges[edge_key(faces[f].v[e], faces[f].v[(e + 1) % 3])].push_back(f);
      }
    }

    std::vector<char> visible(faces.size(), 0);
    std::vector<std::size_t> pending{seed};
    visible[seed] = 1;
    while (!pending.empty()) {
      const std::size_t f = pending.back();
      pending.pop_back();
      for (std::size_t e = 0; e < 3; ++e) {
        const auto found = edges.find(edge_key(faces[f].v[e], faces[f].v[(e + 1) % 3]));
        if (found == edges.end()) continue;
        for (const std::size_t neighbour : found->second) {
          if (neighbour == f || visible[neighbour] != 0) continue;
          count_plane_test();
          if (plane_distance(faces[neighbour].plane, points[apex]) > 0.0f) {
            visible[neighbour] = 1;
            pending.push_back(neighbour);
          }
        }
      }
    }

    std::vector<std::pair<std::uint32_t, std::uint32_t>> horizon;
    for (std::size_t f = 0; f < faces.size(); ++f) {
      if (visible[f] == 0) continue;
      for (std::size_t e = 0; e < 3; ++e) {
        const std::uint32_t a = faces[f].v[e];
        const std::uint32_t b = faces[f].v[(e + 1) % 3];
        bool inside = false;
        const auto found = edges.find(edge_key(a, b));
        if (found != edges.end()) {
          for (const std::size_t neighbour : found->second) {
            if (neighbour != f && visible[neighbour] != 0) inside = true;
          }
        }
        if (!inside) horizon.emplace_back(a, b);
      }
    }
    if (horizon.size() < 3) throw std::invalid_argument("a convex hull visible region has no boundary");

    for (std::size_t f = 0; f < faces.size(); ++f) {
      if (visible[f] != 0) faces[f].alive = false;
    }
    for (const auto& edge : horizon) add_face(faces, points, edge.first, edge.second, apex, epsilon);

    // Constraint: the vertex cap is checked on every pass, before the hull grows past it.
    const auto pass = static_cast<std::uint32_t>(iteration + 1);
    std::size_t named = 0;
    for (const auto& face : faces) {
      if (!face.alive) continue;
      for (const std::uint32_t index : face.v) {
        if (stamp[index] == pass) continue;
        stamp[index] = pass;
        ++named;
      }
    }
    if (named > k_max_hull_vertices) {
      throw hull_limit_error("a convex hull holds at most 252 vertices");
    }
  }
  throw std::invalid_argument("a convex hull did not converge");
}

// Constraint: every loop vertex of a group member lies on the seed plane within tolerance.
bool loop_on_plane(const HknpVec4& plane, const std::vector<HknpVec3>& points, const std::uint32_t* loop,
                   std::size_t count, float epsilon) {
  for (std::size_t i = 0; i < count; ++i) {
    if (std::fabs(plane_distance(plane, points[loop[i]])) > epsilon) return false;
  }
  return true;
}

void emit_triangles(const std::vector<Face>& faces, const std::vector<std::size_t>& group,
                    std::vector<Polygon>& out) {
  for (const std::size_t f : group) {
    Polygon polygon;
    polygon.loop.assign(faces[f].v.begin(), faces[f].v.end());
    polygon.plane = faces[f].plane;
    out.push_back(std::move(polygon));
  }
}

// Constraint: a merged loop must be one cycle over all its boundary edges, else keep the triangles.
void emit_group(const std::vector<Face>& faces, const std::vector<std::size_t>& group, std::vector<Polygon>& out) {
  if (group.size() == 1) {
    emit_triangles(faces, group, out);
    return;
  }
  std::unordered_map<std::uint64_t, std::size_t> directed;
  for (const std::size_t f : group) {
    for (std::size_t e = 0; e < 3; ++e) {
      const std::uint32_t a = faces[f].v[e];
      const std::uint32_t b = faces[f].v[(e + 1) % 3];
      ++directed[(static_cast<std::uint64_t>(a) << 32) | b];
    }
  }
  std::unordered_map<std::uint32_t, std::uint32_t> next;
  std::size_t boundary = 0;
  for (const auto& [key, count] : directed) {
    if (count != 1) continue;
    const auto a = static_cast<std::uint32_t>(key >> 32);
    const auto b = static_cast<std::uint32_t>(key & 0xFFFFFFFFU);
    if (directed.find((static_cast<std::uint64_t>(b) << 32) | a) != directed.end()) continue;
    ++boundary;
    if (!next.emplace(a, b).second) {
      emit_triangles(faces, group, out);
      return;
    }
  }
  if (boundary < 3) {
    emit_triangles(faces, group, out);
    return;
  }
  std::vector<std::uint32_t> loop;
  // Ordering: the cycle starts at the lowest boundary vertex, not at hash order.
  std::uint32_t start = next.begin()->first;
  for (const auto& entry : next) start = std::min(start, entry.first);
  std::uint32_t current = start;
  for (std::size_t step = 0; step <= boundary; ++step) {
    const auto found = next.find(current);
    if (found == next.end()) break;
    loop.push_back(current);
    current = found->second;
    if (current == start) break;
  }
  if (current != start || loop.size() != boundary) {
    emit_triangles(faces, group, out);
    return;
  }
  Polygon polygon;
  polygon.loop = std::move(loop);
  polygon.plane = faces[group.front()].plane;
  out.push_back(std::move(polygon));
}

std::vector<Polygon> merge_coplanar(const std::vector<Face>& faces, const std::vector<HknpVec3>& points,
                                    float epsilon) {
  std::vector<char> grouped(faces.size(), 0);
  std::vector<Polygon> polygons;
  for (std::size_t seed = 0; seed < faces.size(); ++seed) {
    if (!faces[seed].alive || grouped[seed] != 0) continue;
    std::vector<std::size_t> group{seed};
    grouped[seed] = 1;
    for (std::size_t other = seed + 1; other < faces.size(); ++other) {
      if (!faces[other].alive || grouped[other] != 0) continue;
      if (!loop_on_plane(faces[seed].plane, points, faces[other].v.data(), faces[other].v.size(), epsilon)) {
        continue;
      }
      group.push_back(other);
      grouped[other] = 1;
    }
    emit_group(faces, group, polygons);
  }
  return polygons;
}

std::array<double, 3> offset_of(const HknpVec3& point, const HknpVec3& base) {
  return {static_cast<double>(point[0]) - static_cast<double>(base[0]),
          static_cast<double>(point[1]) - static_cast<double>(base[1]),
          static_cast<double>(point[2]) - static_cast<double>(base[2])};
}

double dot_of(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<double, 3> cross_of(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

struct Solid {
  float volume{0.0f};
  HknpVec3 center{};
  HknpMat3 inertia{};
};

// Constraint: tetrahedra are summed in double relative to one hull vertex.
Solid solid_of(const std::vector<HknpVec3>& points, const std::vector<Polygon>& polygons) {
  const HknpVec3 base = points[polygons.front().loop.front()];
  double volume = 0.0;
  std::array<double, 3> moment{0.0, 0.0, 0.0};
  std::array<std::array<double, 3>, 3> second{};
  for (const auto& polygon : polygons) {
    for (std::size_t k = 1; k + 1 < polygon.loop.size(); ++k) {
      const auto a = offset_of(points[polygon.loop[0]], base);
      const auto b = offset_of(points[polygon.loop[k]], base);
      const auto c = offset_of(points[polygon.loop[k + 1]], base);
      // Invariant: each fan tetrahedron has a positive signed volume.
      const double tetra = dot_of(a, cross_of(b, c)) / 6.0;
      volume += tetra;
      std::array<double, 3> sum{};
      for (std::size_t i = 0; i < 3; ++i) {
        sum[i] = a[i] + b[i] + c[i];
        moment[i] += tetra * sum[i] / 4.0;
      }
      for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
          second[i][j] += tetra / 20.0 * (a[i] * a[j] + b[i] * b[j] + c[i] * c[j] + sum[i] * sum[j]);
        }
      }
    }
  }
  if (!(volume > 0.0)) throw std::invalid_argument("a convex hull encloses no volume");
  Solid solid;
  solid.volume = static_cast<float>(volume);
  std::array<double, 3> center{0.0, 0.0, 0.0};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    center[axis] = moment[axis] / volume;
    solid.center[axis] = static_cast<float>(static_cast<double>(base[axis]) + center[axis]);
  }
  std::array<std::array<double, 3>, 3> shifted{};
  double trace = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      shifted[i][j] = second[i][j] - volume * center[i] * center[j];
    }
    trace += shifted[i][i];
  }
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      const double value = i == j ? trace - shifted[i][i] : -shifted[i][j];
      solid.inertia[i][j] = static_cast<float>(value);
    }
  }
  return solid;
}

// Constraint: the stored orientation is a unit quaternion (x, y, z, w) whose rotation has determinant +1.
HknpVec4 quaternion_of(const std::array<std::array<double, 3>, 3>& r) {
  const double trace = r[0][0] + r[1][1] + r[2][2];
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 0.0;
  if (trace > 0.0) {
    const double s = std::sqrt(trace + 1.0) * 2.0;
    w = 0.25 * s;
    x = (r[2][1] - r[1][2]) / s;
    y = (r[0][2] - r[2][0]) / s;
    z = (r[1][0] - r[0][1]) / s;
  } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
    const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
    w = (r[2][1] - r[1][2]) / s;
    x = 0.25 * s;
    y = (r[0][1] + r[1][0]) / s;
    z = (r[0][2] + r[2][0]) / s;
  } else if (r[1][1] > r[2][2]) {
    const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
    w = (r[0][2] - r[2][0]) / s;
    x = (r[0][1] + r[1][0]) / s;
    y = 0.25 * s;
    z = (r[1][2] + r[2][1]) / s;
  } else {
    const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
    w = (r[1][0] - r[0][1]) / s;
    x = (r[0][2] + r[2][0]) / s;
    y = (r[1][2] + r[2][1]) / s;
    z = 0.25 * s;
  }
  if (w < 0.0) {
    x = -x;
    y = -y;
    z = -z;
    w = -w;
  }
  const double norm = std::sqrt(x * x + y * y + z * z + w * w);
  const auto unit = [norm](double value) { return std::fabs(value / norm) < 1.0e-7 ? 0.0 : value / norm; };
  return HknpVec4{static_cast<float>(unit(x)), static_cast<float>(unit(y)), static_cast<float>(unit(z)),
                  static_cast<float>(unit(w))};
}

struct BodyFrame {
  // Invariant: moments are physical principal moments, indexed by the body axis each one leans on.
  HknpVec3 moments{};
  HknpVec4 orientation{{0, 0, 0, 1}};
};

// Constraint: each principal slot goes to the body axis it leans on most, so an axis-aligned hull keeps x, y, z order.
BodyFrame body_frame(const HknpMat3& inertia) {
  const HknpPrincipalAxes principal = principal_axes(inertia);
  std::array<std::size_t, 3> order{0, 1, 2};
  std::array<std::size_t, 3> best = order;
  double best_score = -1.0;
  do {
    double score = 0.0;
    for (std::size_t k = 0; k < 3; ++k) score += std::fabs(static_cast<double>(principal.axes[k][order[k]]));
    if (score > best_score + 1.0e-12) {
      best_score = score;
      best = order;
    }
  } while (std::next_permutation(order.begin(), order.end()));

  BodyFrame frame;
  std::array<std::array<double, 3>, 3> rotation{};
  for (std::size_t k = 0; k < 3; ++k) {
    const std::size_t axis = best[k];
    frame.moments[axis] = principal.moments[k];
    std::size_t big = 0;
    for (std::size_t i = 1; i < 3; ++i) {
      if (std::fabs(principal.axes[k][i]) > std::fabs(principal.axes[k][big])) big = i;
    }
    const double sign = principal.axes[k][big] < 0.0f ? -1.0 : 1.0;
    for (std::size_t i = 0; i < 3; ++i) rotation[i][axis] = sign * static_cast<double>(principal.axes[k][i]);
  }
  const double determinant =
      rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1]) -
      rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0]) +
      rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0]);
  if (determinant < 0.0) {
    for (std::size_t i = 0; i < 3; ++i) rotation[i][0] = -rotation[i][0];
  }
  frame.orientation = quaternion_of(rotation);
  return frame;
}

// Constraint: the stored byte is round(255 * smallest bend / pi), capped at 128.
std::vector<std::uint8_t> face_min_half_angles(const std::vector<Polygon>& polygons) {
  std::unordered_map<std::uint64_t, std::size_t> owner;
  for (std::size_t f = 0; f < polygons.size(); ++f) {
    const auto& loop = polygons[f].loop;
    for (std::size_t k = 0; k < loop.size(); ++k) {
      const std::uint64_t key = (static_cast<std::uint64_t>(loop[k]) << 32) | loop[(k + 1) % loop.size()];
      owner.emplace(key, f);
    }
  }
  std::vector<std::uint8_t> angles(polygons.size(), k_hull_max_face_min_half_angle);
  const double cap = static_cast<double>(k_hull_max_face_min_half_angle) / k_hull_angle_per_radian;
  for (std::size_t f = 0; f < polygons.size(); ++f) {
    const auto& loop = polygons[f].loop;
    const HknpVec4& plane = polygons[f].plane;
    double smallest = cap;
    for (std::size_t k = 0; k < loop.size(); ++k) {
      const std::uint64_t key = (static_cast<std::uint64_t>(loop[(k + 1) % loop.size()]) << 32) | loop[k];
      const auto found = owner.find(key);
      if (found == owner.end() || found->second == f) continue;
      const HknpVec4& other = polygons[found->second].plane;
      const double alignment = std::max(-1.0, std::min(1.0, static_cast<double>(plane[0]) * other[0] +
                                                            static_cast<double>(plane[1]) * other[1] +
                                                            static_cast<double>(plane[2]) * other[2]));
      smallest = std::min(smallest, std::acos(alignment));
    }
    const double scaled = std::floor(smallest * k_hull_angle_per_radian + 0.5);
    angles[f] = static_cast<std::uint8_t>(std::min<double>(k_hull_max_face_min_half_angle, scaled));
  }
  return angles;
}

}  // namespace

HknpShape make_hknp_hull(const std::vector<HknpVec3>& points, const std::vector<HknpTriangle>& triangles,
                         std::uint32_t material_crc, HknpVec4* inertia_orientation) {
  if (triangles.empty()) throw std::invalid_argument("a convex hull needs at least one triangle");
  std::vector<char> seen(points.size(), 0);
  std::vector<HknpVec3> corners;
  for (const auto& triangle : triangles) {
    for (const std::uint32_t index : triangle) {
      if (std::size_t{index} >= points.size()) {
        throw std::invalid_argument("a convex hull triangle names a point outside the list");
      }
      if (seen[index] != 0) continue;
      seen[index] = 1;
      for (const float value : points[index]) {
        if (!std::isfinite(value)) throw std::invalid_argument("a convex hull point is not finite");
      }
      corners.push_back(points[index]);
    }
  }
  if (corners.size() < 4) throw std::invalid_argument("a convex hull needs at least four points");

  const float extent = extent_of(corners);
  const float epsilon = std::max(extent, 1.0f) * k_hull_epsilon;
  const auto tetrahedron = initial_tetrahedron(corners, epsilon);
  std::vector<Face> faces;
  add_face(faces, corners, tetrahedron[0], tetrahedron[1], tetrahedron[2], epsilon);
  add_face(faces, corners, tetrahedron[1], tetrahedron[0], tetrahedron[3], epsilon);
  add_face(faces, corners, tetrahedron[2], tetrahedron[1], tetrahedron[3], epsilon);
  add_face(faces, corners, tetrahedron[0], tetrahedron[2], tetrahedron[3], epsilon);
  expand_hull(corners, faces, epsilon);
  const std::vector<Polygon> polygons = merge_coplanar(faces, corners, epsilon);

  std::vector<std::uint32_t> used;
  std::unordered_map<std::uint32_t, std::uint8_t> slot;
  for (const auto& polygon : polygons) {
    for (const std::uint32_t index : polygon.loop) {
      if (slot.find(index) != slot.end()) continue;
      if (used.size() >= k_max_hull_vertices) {
        throw hull_limit_error("a convex hull holds at most 252 vertices");
      }
      slot.emplace(index, static_cast<std::uint8_t>(used.size()));
      used.push_back(index);
    }
  }

  HknpShape shape;
  shape.kind = HknpShapeKind::convex;
  shape.class_name = "hknpConvexPolytopeShape";
  shape.shape_flags = k_hull_shape_flags;
  shape.convex_radius = 0.0f;
  shape.material_crc = material_crc;
  for (const std::uint32_t index : used) shape.vertices.push_back(corners[index]);
  const std::vector<std::uint8_t> angles = face_min_half_angles(polygons);
  for (std::size_t f = 0; f < polygons.size(); ++f) {
    const auto& polygon = polygons[f];
    shape.planes.push_back(polygon.plane);
    std::vector<std::uint8_t> loop;
    loop.reserve(polygon.loop.size());
    for (const std::uint32_t index : polygon.loop) loop.push_back(slot.at(index));
    shape.faces.push_back(std::move(loop));
    shape.face_min_half_angles.push_back(angles[f]);
  }
  // Constraint: vertex slots repeat the last vertex; plane slots are (0, 0, 1, 0), then zeros.
  const std::size_t vertex_pad = (k_hull_pad - shape.vertices.size() % k_hull_pad) % k_hull_pad;
  for (std::size_t i = 0; i < vertex_pad; ++i) shape.vertices.push_back(shape.vertices.back());
  const std::size_t plane_pad = (k_hull_pad - shape.planes.size() % k_hull_pad) % k_hull_pad;
  if (plane_pad > 0) shape.planes.push_back(HknpVec4{0.0f, 0.0f, 1.0f, 0.0f});
  for (std::size_t i = 1; i < plane_pad; ++i) shape.planes.push_back(HknpVec4{0.0f, 0.0f, 0.0f, 0.0f});

  const Solid solid = solid_of(corners, polygons);
  const BodyFrame frame = body_frame(solid.inertia);
  shape.has_mass_properties = true;
  shape.mass_properties.center = solid.center;
  for (std::size_t k = 0; k < 3; ++k) {
    shape.mass_properties.inertia_raw[k] = frame.moments[k] * k_stored_inertia_factor;
  }
  if (inertia_orientation != nullptr) *inertia_orientation = frame.orientation;
  shape.mass_properties.volume = solid.volume;
  shape.mass_properties.mass = solid.volume;
  shape.mass_properties.packed_orientation = hknp_detail::k_identity_mass_orientation;
  return shape;
}

}  // namespace fo4::nif
