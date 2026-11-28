#include "fo4/environment/navmesh_path.hpp"

#include "fo4/environment/placed.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <tuple>

namespace fo4::env {
namespace {

using Vec = std::array<float, 3>;

struct Mesh {
  std::size_t order{0};
  FormKey key;
  NavmeshGeometry geometry;
  std::vector<std::vector<FormKey>> doors;
  std::vector<Vec> centroids;
};

using NodeKey = std::pair<const Mesh*, std::uint32_t>;

float distance(const Vec& a, const Vec& b) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool triangle_is_valid(const NavmeshGeometry& geometry, const NavmeshTriangle& t) {
  const auto count = geometry.vertices.size();
  const auto [a, b, c] = t.vertices;
  return a < count && b < count && c < count && a != b && b != c && a != c;
}

bool valid_triangle(const NavmeshGeometry& geometry, std::int64_t index) {
  return index >= 0 && static_cast<std::size_t>(index) < geometry.triangles.size() &&
         triangle_is_valid(geometry, geometry.triangles[static_cast<std::size_t>(index)]);
}

Vec edge_midpoint(const Mesh& mesh, std::uint32_t triangle, std::size_t edge) {
  const auto& v = mesh.geometry.triangles[triangle].vertices;
  const auto& a = mesh.geometry.vertices[v[edge]];
  const auto& b = mesh.geometry.vertices[v[(edge + 1) % 3]];
  return {(a[0] + b[0]) / 2.0f, (a[1] + b[1]) / 2.0f, (a[2] + b[2]) / 2.0f};
}

class Search {
public:
  Search(const Environment& env, const NavmeshPathOptions& options) : env_(env), options_(options) {}

  const Mesh* mesh(const FormKey& key, bool lenient) {
    const auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.get();
    auto& slot = cache_[key];
    const auto* ref = env_.winner(key);
    if (ref == nullptr || ref->record->header.signature != Signature::from_string("NAVM")) return nullptr;
    try {
      auto geometry = decode_navmesh(env_, *ref);
      if (geometry) slot = build(key, std::move(*geometry));
    } catch (const std::exception&) {
      if (!lenient) throw;
    }
    return slot.get();
  }

  enum class Gate { open, needs_opening, closed };

  Gate gate(const Mesh& m, std::uint32_t triangle) {
    Gate out = Gate::open;
    for (const auto& door : m.doors[triangle]) {
      switch (verdict(door)) {
        case PathVerdict::passable: break;
        case PathVerdict::needs_opening: out = Gate::needs_opening; break;
        case PathVerdict::unknown:
          if (!options_.allow_unknown_doors) return Gate::closed;
          break;
        case PathVerdict::blocked: return Gate::closed;
      }
    }
    return out;
  }

  bool class_allowed(const Mesh& m, std::uint32_t triangle) const {
    return (m.geometry.triangles[triangle].traversal & options_.forbidden_class) == 0;
  }

private:
  std::unique_ptr<Mesh> build(const FormKey& key, NavmeshGeometry geometry) {
    auto out = std::make_unique<Mesh>();
    out->order = cache_.size();
    out->key = key;
    out->geometry = std::move(geometry);
    const auto& g = out->geometry;
    out->doors.resize(g.triangles.size());
    out->centroids.resize(g.triangles.size());
    for (std::size_t t = 0; t < g.triangles.size(); ++t) {
      if (!triangle_is_valid(g, g.triangles[t])) continue;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        float sum = 0.0f;
        for (const auto v : g.triangles[t].vertices) sum += g.vertices[v][axis];
        out->centroids[t][axis] = sum / 3.0f;
      }
    }
    for (const auto& link : g.doors) {
      if (link.door.owner().empty() || !valid_triangle(g, link.triangle)) continue;
      out->doors[static_cast<std::size_t>(link.triangle)].push_back(link.door);
    }
    return out;
  }

  PathVerdict verdict(const FormKey& door) {
    const auto it = verdicts_.find(door);
    if (it != verdicts_.end()) return it->second;
    DoorState state;
    for (const auto& [key, value] : options_.door_states) {
      if (key == door) state = value;
    }
    std::optional<LockData> lock;
    const auto* ref = env_.winner(door);
    if (ref != nullptr && is_placed_reference_signature(ref->record->header.signature)) {
      try {
        lock = decode_placed_reference(env_, *ref).lock;
      } catch (const std::exception&) {
        lock.reset();
      }
    }
    return verdicts_[door] = door_verdict(lock, state);
  }

  const Environment& env_;
  const NavmeshPathOptions& options_;
  std::map<FormKey, std::unique_ptr<Mesh>> cache_;
  std::map<FormKey, PathVerdict> verdicts_;
};

struct Record {
  float g{std::numeric_limits<float>::infinity()};
  NodeKey parent{nullptr, 0};
  bool via_link{false};
  bool needs_opening{false};
};

struct Entry {
  float f;
  float g;
  NodeKey node;
};

struct Later {
  bool operator()(const Entry& a, const Entry& b) const {
    return std::tuple(a.f, a.node.first->order, a.node.second) > std::tuple(b.f, b.node.first->order, b.node.second);
  }
};

}  // namespace

NavmeshPath find_navmesh_path(const Environment& env, const FormKey& start_navmesh, std::uint32_t start_triangle,
                              const FormKey& goal_navmesh, std::uint32_t goal_triangle,
                              const NavmeshPathOptions& options) {
  Search search(env, options);
  NavmeshPath out;
  const Mesh* start_mesh = search.mesh(start_navmesh, false);
  const Mesh* goal_mesh = search.mesh(goal_navmesh, false);
  if (start_mesh == nullptr || goal_mesh == nullptr || !valid_triangle(start_mesh->geometry, start_triangle) ||
      !valid_triangle(goal_mesh->geometry, goal_triangle)) {
    out.status = NavmeshPathStatus::invalid_endpoint;
    return out;
  }

  const NodeKey start{start_mesh, start_triangle};
  const NodeKey goal{goal_mesh, goal_triangle};
  const Vec goal_point = goal_mesh->centroids[goal_triangle];
  const auto heuristic = [&](const NodeKey& n) {
    return n.first == goal_mesh ? distance(n.first->centroids[n.second], goal_point) : 0.0f;
  };

  std::map<NodeKey, Record> records;
  std::priority_queue<Entry, std::vector<Entry>, Later> open;
  const auto admit = [&](const NodeKey& n, Search::Gate& gate) {
    return search.class_allowed(*n.first, n.second) && (gate = search.gate(*n.first, n.second)) != Search::Gate::closed;
  };

  Search::Gate gate = Search::Gate::open;
  if (!admit(start, gate)) return out;
  records[start] = {0.0f, {nullptr, 0}, false, gate == Search::Gate::needs_opening};
  open.push({heuristic(start), 0.0f, start});

  const auto relax = [&](const NodeKey& from, float g_from, const Mesh* to_mesh, std::uint32_t to_triangle,
                         float step, bool via_link) {
    const NodeKey to{to_mesh, to_triangle};
    Search::Gate to_gate = Search::Gate::open;
    if (!admit(to, to_gate)) return;
    const float g = g_from + step;
    auto& record = records[to];
    if (!(g < record.g)) return;
    record = {g, from, via_link, to_gate == Search::Gate::needs_opening};
    open.push({g + heuristic(to), g, to});
  };

  while (!open.empty()) {
    const Entry current = open.top();
    open.pop();
    if (current.g > records[current.node].g) continue;
    if (current.node == goal) break;
    const Mesh& mesh = *current.node.first;
    const auto triangle = current.node.second;
    const auto& tri = mesh.geometry.triangles[triangle];
    for (std::size_t e = 0; e < 3; ++e) {
      const auto slot = tri.edges[e];
      if (slot < 0) continue;
      if (!tri.edge_is_extra(e)) {
        if (!valid_triangle(mesh.geometry, slot)) continue;
        const auto next = static_cast<std::uint32_t>(slot);
        relax(current.node, current.g, &mesh, next, distance(mesh.centroids[triangle], mesh.centroids[next]), false);
        continue;
      }
      if (static_cast<std::size_t>(slot) >= mesh.geometry.edges.size()) continue;
      const auto& link = mesh.geometry.edges[static_cast<std::size_t>(slot)];
      if (link.navmesh.owner().empty() || link.edge > 2) continue;
      if (edge_link_verdict(link.type) == PathVerdict::unknown && !options.allow_unknown_links) continue;
      const Mesh* target = search.mesh(link.navmesh, true);
      if (target == nullptr || !valid_triangle(target->geometry, link.triangle)) continue;
      const auto next = static_cast<std::uint32_t>(link.triangle);
      const float step = distance(mesh.centroids[triangle], edge_midpoint(mesh, triangle, e)) +
                         distance(edge_midpoint(*target, next, link.edge), target->centroids[next]);
      relax(current.node, current.g, target, next, step, true);
    }
  }

  const auto found = records.find(goal);
  if (found == records.end() || found->second.g == std::numeric_limits<float>::infinity()) return out;
  out.status = NavmeshPathStatus::found;
  out.cost = found->second.g;
  for (NodeKey at = goal;;) {
    const auto& record = records[at];
    out.nodes.push_back({at.first->key, at.second, record.via_link, record.needs_opening});
    if (at == start) break;
    at = record.parent;
  }
  std::reverse(out.nodes.begin(), out.nodes.end());
  return out;
}

}  // namespace fo4::env
