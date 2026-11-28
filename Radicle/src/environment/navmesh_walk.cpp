#include "fo4/environment/navmesh_walk.hpp"

#include "record_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <map>

namespace fo4::env {
namespace {

using detail::Reader;
using Vec = std::array<float, 3>;

constexpr std::size_t k_race_flags2_offset = 88;
constexpr float k_min_inset_length = 1e-4f;

Vec to_walk_space(const Vec& p) {
  return {p[0] * k_walk_metres_per_unit, p[2] * k_walk_metres_per_unit, -p[1] * k_walk_metres_per_unit};
}

bool triangle_is_valid(const NavmeshGeometry& geometry, const NavmeshTriangle& t) {
  const auto count = geometry.vertices.size();
  const auto [a, b, c] = t.vertices;
  return a < count && b < count && c < count && a != b && b != c && a != c;
}

std::optional<std::size_t> valid_triangle(const NavmeshGeometry& geometry, std::int64_t index) {
  if (index < 0 || static_cast<std::size_t>(index) >= geometry.triangles.size()) return std::nullopt;
  if (!triangle_is_valid(geometry, geometry.triangles[static_cast<std::size_t>(index)])) return std::nullopt;
  return static_cast<std::size_t>(index);
}

struct EdgePoints {
  Vec middle;
  Vec centroid;
};

EdgePoints edge_points(const NavmeshGeometry& geometry, std::size_t triangle, std::size_t edge) {
  const auto& v = geometry.triangles[triangle].vertices;
  const auto a = to_walk_space(geometry.vertices[v[edge]]);
  const auto b = to_walk_space(geometry.vertices[v[(edge + 1) % 3]]);
  const auto c = to_walk_space(geometry.vertices[v[(edge + 2) % 3]]);
  EdgePoints out;
  for (std::size_t i = 0; i < 3; ++i) {
    out.middle[i] = (a[i] + b[i]) / 2.0f;
    out.centroid[i] = (a[i] + b[i] + c[i]) / 3.0f;
  }
  return out;
}

std::optional<Vec> inset(const EdgePoints& points, float reach) {
  Vec inward;
  float length_squared = 0.0f;
  for (std::size_t i = 0; i < 3; ++i) {
    inward[i] = points.centroid[i] - points.middle[i];
    length_squared += inward[i] * inward[i];
  }
  const float length = std::sqrt(length_squared);
  if (!(length >= k_min_inset_length)) return std::nullopt;
  const float step = std::min(reach, length * 0.5f);
  Vec out;
  for (std::size_t i = 0; i < 3; ++i) out[i] = points.middle[i] + inward[i] / length * step;
  return out;
}

class TargetCache {
public:
  TargetCache(const Environment& env, const FormKey& own_key, const NavmeshGeometry& own)
      : env_(env), own_key_(own_key), own_(own) {}

  const NavmeshGeometry* find(const FormKey& key) {
    if (key == own_key_) return &own_;
    const auto it = cache_.find(key);
    if (it != cache_.end()) return it->second ? &*it->second : nullptr;
    auto& slot = cache_[key];
    const auto* ref = env_.winner(key);
    if (ref != nullptr && ref->record->header.signature == sig("NAVM")) {
      try {
        slot = decode_navmesh(env_, *ref);
      } catch (const std::exception&) {
        slot.reset();
      }
    }
    return slot ? &*slot : nullptr;
  }

private:
  const Environment& env_;
  const FormKey& own_key_;
  const NavmeshGeometry& own_;
  std::map<FormKey, std::optional<NavmeshGeometry>> cache_;
};

void collect_links(const NavmeshGeometry& geometry, TargetCache& targets, float reach, WalkView& out) {
  for (std::size_t t = 0; t < geometry.triangles.size(); ++t) {
    if (!valid_triangle(geometry, static_cast<std::int64_t>(t))) continue;
    const auto& triangle = geometry.triangles[t];
    for (std::size_t e = 0; e < 3; ++e) {
      if (!triangle.edge_is_extra(e)) continue;
      const auto slot = triangle.edges[e];
      if (slot < 0 || static_cast<std::size_t>(slot) >= geometry.edges.size()) continue;
      const auto& link = geometry.edges[static_cast<std::size_t>(slot)];
      if (link.navmesh.owner().empty() || link.edge > 2) continue;
      const auto* destination = targets.find(link.navmesh);
      if (destination == nullptr) continue;
      const auto target = valid_triangle(*destination, link.triangle);
      if (!target) continue;
      const auto start = inset(edge_points(geometry, t, e), reach);
      const auto end = inset(edge_points(*destination, *target, link.edge), reach);
      if (!start || !end) continue;
      out.links.push_back({*start, *end, static_cast<std::uint32_t>(t), static_cast<std::uint8_t>(e), link.navmesh,
                           static_cast<std::uint32_t>(*target), link.edge, edge_link_verdict(link.type)});
    }
  }
}

void collect_doors(const NavmeshGeometry& geometry, WalkView& out) {
  std::map<FormKey, std::size_t> index;
  for (const auto& link : geometry.doors) {
    if (link.door.owner().empty()) continue;
    const auto triangle = valid_triangle(geometry, link.triangle);
    if (!triangle) continue;
    const auto [it, inserted] = index.try_emplace(link.door, out.doors.size());
    if (inserted) out.doors.push_back({link.door, {}});
    out.doors[it->second].triangles.push_back(static_cast<std::uint32_t>(*triangle));
  }
  for (auto& door : out.doors) {
    std::sort(door.triangles.begin(), door.triangles.end());
    door.triangles.erase(std::unique(door.triangles.begin(), door.triangles.end()), door.triangles.end());
  }
}

}  // namespace

std::optional<WalkView> build_walk_view(const Environment& env, const RecordRef& navmesh, float inset_metres) {
  const auto geometry = decode_navmesh(env, navmesh);
  if (!geometry) return std::nullopt;
  WalkView out;
  out.navmesh = navmesh.record->form_key;
  out.has_grid = !geometry->parent.world.owner().empty();
  if (out.has_grid) {
    out.grid_x = geometry->parent.grid_x;
    out.grid_y = geometry->parent.grid_y;
  }
  out.vertices.reserve(geometry->vertices.size());
  for (const auto& vertex : geometry->vertices) out.vertices.push_back(to_walk_space(vertex));
  out.triangles.reserve(geometry->triangles.size());
  out.triangle_class.reserve(geometry->triangles.size());
  for (const auto& triangle : geometry->triangles) {
    if (triangle_is_valid(*geometry, triangle)) {
      out.triangles.push_back({triangle.vertices[0], triangle.vertices[1], triangle.vertices[2]});
      out.triangle_class.push_back(triangle.traversal);
    } else {
      out.triangles.push_back({0, 0, 0});
      out.triangle_class.push_back(0);
      ++out.skipped_triangles;
    }
  }
  TargetCache targets(env, out.navmesh, *geometry);
  collect_links(*geometry, targets, inset_metres, out);
  collect_doors(*geometry, out);
  return out;
}

std::optional<AgentProfile> agent_profile(const Environment& env, const std::optional<FormKey>& race) {
  AgentProfile out;
  out.radius = k_walk_default_radius_units * k_walk_metres_per_unit;
  out.height = k_walk_default_height_units * k_walk_metres_per_unit;
  if (!race) return out;
  const auto* ref = env.winner(*race);
  if (ref == nullptr || ref->record->header.signature != sig("RACE")) return std::nullopt;
  const Reader reader(env, *ref, sig("RACE"));
  for (const auto& sub : ref->record->subrecords) {
    if (sub.signature != sig("DATA")) continue;
    if (sub.data.size() < k_race_flags2_offset + 4) reader.fail(sub.signature, "is too short to hold Flags 2");
    out.large = (Reader::u32(sub, k_race_flags2_offset) & k_race_flags2_large_pathing) != 0;
    out.forbidden_class = out.large ? k_walk_class_large_forbidden : 0;
    return out;
  }
  reader.fail(sig("DATA"), "is missing");
}

}  // namespace fo4::env
