#include "fo4/nif/model_ops.hpp"

#include "import_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif::model_detail {
namespace {

using import_detail::Influence;
using import_detail::Mesh;
using import_detail::Vec2;
using import_detail::Vec4;

constexpr float k_epsilon = 1e-6f;
constexpr std::size_t k_max_vertices = 65535;

bool finite_vec(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

Vec3 add_v(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 sub_v(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 scale_v(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot_v(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross_v(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length_sq(const Vec3& a) { return dot_v(a, a); }

// Invariant: weights blend per bone, so the two endpoints need not order their slots alike.
// Invariant: the four heaviest bones survive and are renormalized.
void blend_weights(const Mesh& mesh, std::uint32_t a, std::uint32_t b, float t, Vec4& weights, Influence& bones) {
  struct Slot {
    std::uint8_t bone{0};
    float weight{0.0f};
  };
  // Constraint: the array is filled at fixed offsets, so every subscript is in range by construction.
  std::array<Slot, 8> slots{};
  for (std::size_t k = 0; k < 4; ++k) {
    slots[k] = Slot{mesh.bones[a][k], mesh.weights[a][k] * (1.0f - t)};
    slots[k + 4] = Slot{mesh.bones[b][k], mesh.weights[b][k] * t};
  }
  for (std::size_t i = 0; i < slots.size(); ++i) {
    for (std::size_t j = i + 1; j < slots.size(); ++j) {
      if (slots[j].bone != slots[i].bone) continue;
      slots[i].weight += slots[j].weight;
      slots[j].weight = 0.0f;
    }
  }
  std::sort(slots.begin(), slots.end(), [](const Slot& x, const Slot& y) {
    if (x.weight != y.weight) return x.weight > y.weight;
    return x.bone < y.bone;
  });
  weights = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
  bones = Influence{0, 0, 0, 0};
  float total = 0.0f;
  for (std::size_t k = 0; k < 4; ++k) total += slots[k].weight;
  if (!(total > 0.0f)) return;
  for (std::size_t k = 0; k < 4; ++k) {
    weights[k] = slots[k].weight / total;
    bones[k] = slots[k].bone;
  }
}

// Invariant: floating channels interpolate linearly.
// Invariant: skin weights merge by bone index, never by slot position.
std::uint32_t add_blend(Mesh& mesh, std::uint32_t a, std::uint32_t b, float t) {
  const auto lerp = [t](float x, float y) { return x + (y - x) * t; };
  const std::uint32_t out = static_cast<std::uint32_t>(mesh.positions.size());
  mesh.positions.push_back({lerp(mesh.positions[a][0], mesh.positions[b][0]),
                            lerp(mesh.positions[a][1], mesh.positions[b][1]),
                            lerp(mesh.positions[a][2], mesh.positions[b][2])});
  if (mesh.normals.size() == mesh.positions.size() - 1) {
    mesh.normals.push_back({lerp(mesh.normals[a][0], mesh.normals[b][0]),
                            lerp(mesh.normals[a][1], mesh.normals[b][1]),
                            lerp(mesh.normals[a][2], mesh.normals[b][2])});
  }
  if (mesh.uvs.size() == mesh.positions.size() - 1) {
    mesh.uvs.push_back({lerp(mesh.uvs[a][0], mesh.uvs[b][0]), lerp(mesh.uvs[a][1], mesh.uvs[b][1])});
  }
  if (mesh.tangents.size() == mesh.positions.size() - 1) {
    mesh.tangents.push_back({lerp(mesh.tangents[a][0], mesh.tangents[b][0]),
                             lerp(mesh.tangents[a][1], mesh.tangents[b][1]),
                             lerp(mesh.tangents[a][2], mesh.tangents[b][2])});
  }
  if (mesh.bitangents.size() == mesh.positions.size() - 1) {
    mesh.bitangents.push_back({lerp(mesh.bitangents[a][0], mesh.bitangents[b][0]),
                               lerp(mesh.bitangents[a][1], mesh.bitangents[b][1]),
                               lerp(mesh.bitangents[a][2], mesh.bitangents[b][2])});
  }
  if (mesh.colours.size() == mesh.positions.size() - 1) {
    mesh.colours.push_back({lerp(mesh.colours[a][0], mesh.colours[b][0]),
                            lerp(mesh.colours[a][1], mesh.colours[b][1]),
                            lerp(mesh.colours[a][2], mesh.colours[b][2]),
                            lerp(mesh.colours[a][3], mesh.colours[b][3])});
  }
  if (mesh.weights.size() == mesh.positions.size() - 1 && mesh.bones.size() == mesh.positions.size() - 1) {
    Vec4 weights;
    Influence bones;
    blend_weights(mesh, a, b, t, weights, bones);
    mesh.weights.push_back(weights);
    mesh.bones.push_back(bones);
  }
  return out;
}

std::uint32_t add_copy(Mesh& mesh, std::uint32_t v) {
  const std::uint32_t out = static_cast<std::uint32_t>(mesh.positions.size());
  mesh.positions.push_back(mesh.positions[v]);
  if (mesh.normals.size() == mesh.positions.size() - 1) mesh.normals.push_back(mesh.normals[v]);
  if (mesh.uvs.size() == mesh.positions.size() - 1) mesh.uvs.push_back(mesh.uvs[v]);
  if (mesh.tangents.size() == mesh.positions.size() - 1) mesh.tangents.push_back(mesh.tangents[v]);
  if (mesh.bitangents.size() == mesh.positions.size() - 1) mesh.bitangents.push_back(mesh.bitangents[v]);
  if (mesh.colours.size() == mesh.positions.size() - 1) mesh.colours.push_back(mesh.colours[v]);
  if (mesh.weights.size() == mesh.positions.size() - 1) mesh.weights.push_back(mesh.weights[v]);
  if (mesh.bones.size() == mesh.positions.size() - 1) mesh.bones.push_back(mesh.bones[v]);
  return out;
}

bool channels_consistent(const Mesh& mesh) {
  const std::size_t n = mesh.positions.size();
  return (mesh.normals.empty() || mesh.normals.size() == n) && (mesh.uvs.empty() || mesh.uvs.size() == n) &&
         (mesh.tangents.empty() || mesh.tangents.size() == n) &&
         (mesh.bitangents.empty() || mesh.bitangents.size() == n) &&
         (mesh.colours.empty() || mesh.colours.size() == n) && (mesh.weights.empty() || mesh.weights.size() == n) &&
         (mesh.bones.empty() || mesh.bones.size() == n);
}

using Edge = std::pair<std::uint32_t, std::uint32_t>;

Edge undirected(std::uint32_t a, std::uint32_t b) { return a < b ? Edge{a, b} : Edge{b, a}; }

std::map<Edge, std::vector<std::uint32_t>> edge_tris(const Mesh& mesh) {
  std::map<Edge, std::vector<std::uint32_t>> found;
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    const auto tri = static_cast<std::uint32_t>(t / 3);
    for (std::size_t k = 0; k < 3; ++k) {
      const Edge edge = undirected(mesh.tris[t + k], mesh.tris[t + (k + 1) % 3]);
      found[edge].push_back(tri);
    }
  }
  return found;
}

float tri_area_sq(const Mesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  return length_sq(cross_v(sub_v(mesh.positions[b], mesh.positions[a]), sub_v(mesh.positions[c], mesh.positions[a])));
}

}  // namespace

std::optional<std::string> extrude_faces(Mesh& mesh, const std::vector<std::uint32_t>& tris, float distance) {
  if (tris.empty()) return "no triangles selected";
  if (!(distance > 0.0f) || !std::isfinite(distance)) return "the extrude distance must be positive and finite";
  if (!channels_consistent(mesh)) return "vertex channels have different counts";
  const std::size_t faces = mesh.tris.size() / 3;
  for (const auto tri : tris) {
    if (tri >= faces) return "triangle " + std::to_string(tri) + " is past the mesh";
  }
  if (mesh.normals.size() != mesh.positions.size()) return "extrusion follows vertex normals, which are missing";
  for (const auto& normal : mesh.normals) {
    if (!finite_vec(normal) || length_sq(normal) <= 0.0f) return "a vertex normal is not finite or is zero";
  }
  for (const auto& point : mesh.positions) {
    if (!finite_vec(point)) return "a vertex position is not finite";
  }
  // Invariant: the caller's mesh stays untouched unless the whole splice succeeds.
  Mesh work = mesh;
  const std::size_t original = work.positions.size();
  std::vector<bool> selected(original, false);
  for (const auto tri : tris) {
    for (std::size_t k = 0; k < 3; ++k) selected[work.tris[3 * tri + k]] = true;
  }
  std::map<std::uint32_t, std::uint32_t> duplicate;
  for (std::size_t v = 0; v < original; ++v) {
    if (selected[v]) duplicate[static_cast<std::uint32_t>(v)] = add_copy(work, static_cast<std::uint32_t>(v));
  }
  for (const auto& [old_v, new_v] : duplicate) {
    work.positions[new_v] = add_v(work.positions[old_v], scale_v(work.normals[old_v], distance));
  }
  for (const auto tri : tris) {
    for (std::size_t k = 0; k < 3; ++k) work.tris[3 * tri + k] = duplicate[work.tris[3 * tri + k]];
  }
  std::map<Edge, std::uint32_t> uses;
  for (const auto tri : tris) {
    for (std::size_t k = 0; k < 3; ++k) {
      // Invariant: uses counts run in the retargeted duplicate index space.
      uses[undirected(work.tris[3 * tri + k], work.tris[3 * tri + (k + 1) % 3])]++;
    }
  }
  // Invariant: boundary edges are walked in the duplicate index space.
  // Invariant: side walls join duplicate to original, facing away from the cap.
  std::map<std::uint32_t, std::uint32_t> original_of;
  for (const auto& [old_v, new_v] : duplicate) original_of[new_v] = old_v;
  for (const auto tri : tris) {
    const std::uint32_t v0 = work.tris[3 * tri];
    const std::uint32_t v1 = work.tris[3 * tri + 1];
    const std::uint32_t v2 = work.tris[3 * tri + 2];
    const std::array<Edge, 3> tri_edges{undirected(v0, v1), undirected(v1, v2), undirected(v2, v0)};
    const std::array<std::uint32_t, 3> corners{v0, v1, v2};
    for (std::size_t k = 0; k < 3; ++k) {
      if (uses[tri_edges[k]] != 1) continue;
      const std::uint32_t a = corners[k];
      const std::uint32_t b = corners[(k + 1) % 3];
      const std::uint32_t a0 = original_of[a];
      const std::uint32_t b0 = original_of[b];
      work.tris.insert(work.tris.end(), {a0, b0, b, a0, b, a});
    }
  }
  if (work.positions.size() > k_max_vertices) return "the extruded mesh needs past 65535 vertices";
  mesh = std::move(work);
  return std::nullopt;
}

std::optional<std::string> bridge_loops(Mesh& mesh, const std::vector<std::uint32_t>& loop_a,
                                        const std::vector<std::uint32_t>& loop_b) {
  if (loop_a.size() != loop_b.size()) return "the loops have different lengths";
  if (loop_a.size() < 2) return "a bridge needs loops of at least two vertices";
  if (!channels_consistent(mesh)) return "vertex channels have different counts";
  std::set<std::uint32_t> seen;
  for (const auto v : loop_a) {
    if (v >= mesh.positions.size()) return "loop A names vertex " + std::to_string(v) + ", past the mesh";
    if (!seen.insert(v).second) return "loop A repeats vertex " + std::to_string(v);
  }
  for (const auto v : loop_b) {
    if (v >= mesh.positions.size()) return "loop B names vertex " + std::to_string(v) + ", past the mesh";
    if (!seen.insert(v).second) return "a vertex appears in both loops";
  }
  const std::size_t n = loop_a.size();
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t a0 = loop_a[i];
    const std::uint32_t a1 = loop_a[(i + 1) % n];
    const std::uint32_t b0 = loop_b[i];
    const std::uint32_t b1 = loop_b[(i + 1) % n];
    mesh.tris.insert(mesh.tris.end(), {a0, a1, b1, a0, b1, b0});
  }
  return std::nullopt;
}

std::optional<std::string> knife_plane(Mesh& target, const Vec3& normal, float offset, KnifeResult* result,
                                       float epsilon) {
  if (!finite_vec(normal) || length_sq(normal) <= 0.0f) return "the knife normal must be non-zero and finite";
  if (!std::isfinite(offset)) return "the knife offset must be finite";
  if (!(epsilon > 0.0f) || !std::isfinite(epsilon)) return "the knife epsilon must be positive and finite";
  if (!channels_consistent(target)) return "vertex channels have different counts";
  // Invariant: the caller's mesh stays untouched unless the whole cut succeeds.
  Mesh mesh = target;
  const float length = std::sqrt(length_sq(normal));
  const Vec3 n = scale_v(normal, 1.0f / length);
  const float plane = offset / length;
  std::vector<float> signed_distance(mesh.positions.size());
  for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
    if (!finite_vec(mesh.positions[v])) return "a vertex position is not finite";
    signed_distance[v] = dot_v(n, mesh.positions[v]) - plane;
  }
  std::map<Edge, std::uint32_t> midpoint_of;
  auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
    const Edge edge = undirected(a, b);
    const auto found = midpoint_of.find(edge);
    if (found != midpoint_of.end()) return found->second;
    const float da = signed_distance[a];
    const float db = signed_distance[b];
    const float t = (std::fabs(da - db) <= 0.0f) ? 0.5f : da / (da - db);
    const std::uint32_t m = add_blend(mesh, a, b, std::clamp(t, 0.0f, 1.0f));
    midpoint_of[edge] = m;
    return m;
  };
  std::vector<std::uint32_t> tris;
  tris.reserve(mesh.tris.size());
  KnifeResult local;
  const auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (tri_area_sq(mesh, a, b, c) <= 0.0f) {
      local.degenerate_dropped++;
      return;
    }
    tris.insert(tris.end(), {a, b, c});
    local.triangles_added++;
  };
  const std::size_t faces = mesh.tris.size() / 3;
  for (std::size_t t = 0; t < faces; ++t) {
    const std::uint32_t v[3] = {mesh.tris[3 * t], mesh.tris[3 * t + 1], mesh.tris[3 * t + 2]};
    const float d[3] = {signed_distance[v[0]], signed_distance[v[1]], signed_distance[v[2]]};
    int above = 0;
    int below = 0;
    int on = 0;
    for (const auto value : d) {
      if (value > epsilon) {
        above++;
      } else if (value < -epsilon) {
        below++;
      } else {
        on++;
      }
    }
    if (above == 0 || below == 0) {
      tris.insert(tris.end(), {v[0], v[1], v[2]});
      continue;
    }
    if (on == 1) {
      // Invariant: one corner on the plane splits the opposite edge and fans through it.
      std::size_t o = 0;
      while (std::fabs(d[o]) > epsilon) o++;
      const std::uint32_t p = v[o];
      const std::uint32_t a = v[(o + 1) % 3];
      const std::uint32_t b = v[(o + 2) % 3];
      const std::uint32_t m = midpoint(a, b);
      emit(p, a, m);
      emit(p, m, b);
      continue;
    }
    if (on == 2) {
      tris.insert(tris.end(), {v[0], v[1], v[2]});
      continue;
    }
    // Invariant: with no corner on the plane the lone corner faces a quad.
    std::size_t lone = 0;
    const bool lone_above = (above == 1);
    for (std::size_t k = 0; k < 3; ++k) {
      const bool is_above = d[k] > epsilon;
      if (is_above == lone_above) lone = k;
    }
    const std::uint32_t p = v[lone];
    const std::uint32_t a = v[(lone + 1) % 3];
    const std::uint32_t b = v[(lone + 2) % 3];
    const std::uint32_t ma = midpoint(p, a);
    const std::uint32_t mb = midpoint(p, b);
    emit(p, ma, mb);
    emit(ma, a, b);
    emit(ma, b, mb);
  }
  if (mesh.positions.size() > k_max_vertices) return "the cut needs past 65535 vertices";
  mesh.tris = std::move(tris);
  target = std::move(mesh);
  if (result != nullptr) *result = local;
  return std::nullopt;
}

namespace {

// Invariant: a fan walk leaves through triangle interiors on a consistent side.
struct FanWalk {
  // Invariant: spokes lists the arc vertices in walk order.
  std::vector<std::uint32_t> spokes;
  // Invariant: triangles[k] holds spokes[k] to spokes[k+1].
  std::vector<std::uint32_t> tris;
  bool reached{false};
};

FanWalk walk_fan(const Mesh& mesh, const std::map<Edge, std::vector<std::uint32_t>>& adjacency, std::uint32_t v,
                 std::uint32_t from, std::uint32_t to) {
  FanWalk walk;
  walk.spokes.push_back(from);
  std::uint32_t prev_tri = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t spoke = from;
  for (std::size_t guard = 0; guard <= adjacency.size() + 2; ++guard) {
    if (spoke == to) {
      walk.reached = true;
      return walk;
    }
    const auto edge = undirected(v, spoke);
    const auto found = adjacency.find(edge);
    if (found == adjacency.end()) return walk;
    std::uint32_t next_tri = std::numeric_limits<std::uint32_t>::max();
    for (const auto tri : found->second) {
      if (tri != prev_tri) {
        next_tri = tri;
        break;
      }
    }
    if (next_tri == std::numeric_limits<std::uint32_t>::max()) return walk;
    // Invariant: leave through the other edge of this triangle that touches v.
    const std::uint32_t c[3] = {mesh.tris[3 * next_tri], mesh.tris[3 * next_tri + 1], mesh.tris[3 * next_tri + 2]};
    std::uint32_t next_spoke = std::numeric_limits<std::uint32_t>::max();
    for (const auto corner : c) {
      if (corner != v && corner != spoke) next_spoke = corner;
    }
    if (next_spoke == std::numeric_limits<std::uint32_t>::max()) return walk;
    walk.tris.push_back(next_tri);
    walk.spokes.push_back(next_spoke);
    prev_tri = next_tri;
    spoke = next_spoke;
  }
  return walk;
}

bool point_in_triangle(const Mesh& mesh, const Vec3& p, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                       float epsilon) {
  const Vec3 ab = sub_v(mesh.positions[b], mesh.positions[a]);
  const Vec3 ac = sub_v(mesh.positions[c], mesh.positions[a]);
  const Vec3 ap = sub_v(p, mesh.positions[a]);
  const Vec3 normal = cross_v(ab, ac);
  const float normal_sq = length_sq(normal);
  if (normal_sq <= 0.0f) return false;
  const float plane_distance = dot_v(ap, normal) / std::sqrt(normal_sq);
  if (std::fabs(plane_distance) > epsilon) return false;
  const float d00 = dot_v(ab, ab);
  const float d01 = dot_v(ab, ac);
  const float d11 = dot_v(ac, ac);
  const float d20 = dot_v(ap, ab);
  const float d21 = dot_v(ap, ac);
  const float denom = d00 * d11 - d01 * d01;
  if (std::fabs(denom) <= 0.0f) return false;
  const float beta = (d11 * d20 - d01 * d21) / denom;
  const float gamma = (d00 * d21 - d01 * d20) / denom;
  return beta >= -epsilon && gamma >= -epsilon && (beta + gamma) <= 1.0f + epsilon;
}

std::optional<std::string> cut_loop_in_place(Mesh& mesh, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ring) {
  if (ring.size() < 3) return "a loop cut needs a closed ring of at least three edges";
  if (!channels_consistent(mesh)) return "vertex channels have different counts";
  const auto adjacency = edge_tris(mesh);
  const auto edge_exists = [&](std::uint32_t a, std::uint32_t b) {
    return adjacency.find(undirected(a, b)) != adjacency.end();
  };
  for (const auto& [a, b] : ring) {
    if (a >= mesh.positions.size() || b >= mesh.positions.size()) return "the ring names a vertex past the mesh";
    if (a == b) return "the ring holds a zero-length edge";
    if (!edge_exists(a, b)) return "ring edge " + std::to_string(a) + "-" + std::to_string(b) + " is not a mesh edge";
  }
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const auto& [a, b] = ring[i];
    const auto& [c, d] = ring[(i + 1) % ring.size()];
    if (b != c && b != d && a != c && a != d) {
      return "ring edges " + std::to_string(i) + " and " + std::to_string((i + 1) % ring.size()) +
             " do not share a vertex";
    }
  }
  std::map<std::uint32_t, std::uint32_t> edge_use;
  for (const auto& [a, b] : ring) {
    edge_use[a]++;
    edge_use[b]++;
  }
  for (const auto& [v, uses] : edge_use) {
    if (uses > 2) return "vertex " + std::to_string(v) + " appears in more than two ring edges";
  }

  // Invariant: every ring edge gains its midpoint first.
  std::map<Edge, std::uint32_t> mid_of;
  for (const auto& [a, b] : ring) mid_of[undirected(a, b)] = add_blend(mesh, a, b, 0.5f);

  // Invariant: pass 2 lays auxiliary midpoints on the side the cut segment crosses.
  // Invariant: a cut running straight through the vertex needs no fan split.
  constexpr float k_straight_eps = 1e-4f;
  const auto spoke_of = [&](std::uint32_t v, const Edge& e) { return e.first == v ? e.second : e.first; };
  const auto is_boundary_vertex = [&](std::uint32_t vertex) {
    for (const auto& [edge, users] : adjacency) {
      if ((edge.first == vertex || edge.second == vertex) && users.size() == 1) return true;
    }
    return false;
  };
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const Edge prev = undirected(ring[i].first, ring[i].second);
    const Edge next = undirected(ring[(i + 1) % ring.size()].first, ring[(i + 1) % ring.size()].second);
    // Invariant: v is the shared ring vertex of the two consecutive edges.
    std::uint32_t v = std::numeric_limits<std::uint32_t>::max();
    for (const auto corner : {prev.first, prev.second}) {
      if (corner == next.first || corner == next.second) v = corner;
    }
    const std::uint32_t in_spoke = spoke_of(v, prev);
    const std::uint32_t out_spoke = spoke_of(v, next);
    if (in_spoke == out_spoke) return "the ring doubles back on itself";
    // Constraint: a cut that ends at a mesh boundary corner stops there, so no fan arc is bridged.
    if (is_boundary_vertex(v)) continue;
    const std::uint32_t m_in = mid_of.at(prev);
    const std::uint32_t m_out = mid_of.at(next);
    const Vec3 mid = scale_v(add_v(mesh.positions[m_in], mesh.positions[m_out]), 0.5f);
    if (length_sq(sub_v(mid, mesh.positions[v])) <= k_straight_eps * k_straight_eps) continue;
    FanWalk forward = walk_fan(mesh, adjacency, v, in_spoke, out_spoke);
    if (!forward.reached) return "the fan around vertex " + std::to_string(v) + " cannot be walked";
    bool forward_holds = false;
    for (const auto tri : forward.tris) {
      if (point_in_triangle(mesh, mid, mesh.tris[3 * tri], mesh.tris[3 * tri + 1], mesh.tris[3 * tri + 2],
                            k_epsilon)) {
        forward_holds = true;
        break;
      }
    }
    // Invariant: crossed spokes are the interior fan vertices of the chosen arc.
    if (!forward_holds) {
      // Invariant: the other arc walks from out_spoke back to in_spoke.
      FanWalk backward = walk_fan(mesh, adjacency, v, out_spoke, in_spoke);
      if (!backward.reached) return "the fan around vertex " + std::to_string(v) + " cannot be walked";
      bool backward_holds = false;
      for (const auto tri : backward.tris) {
        if (point_in_triangle(mesh, mid, mesh.tris[3 * tri], mesh.tris[3 * tri + 1], mesh.tris[3 * tri + 2],
                              k_epsilon)) {
          backward_holds = true;
          break;
        }
      }
      if (!backward_holds) {
        return "the cut through vertex " + std::to_string(v) + " matches neither fan side";
      }
      for (std::size_t k = 1; k + 1 < backward.spokes.size(); ++k) {
        const Edge spoke = undirected(v, backward.spokes[k]);
        if (mid_of.find(spoke) == mid_of.end()) mid_of[spoke] = add_blend(mesh, v, backward.spokes[k], 0.5f);
      }
    } else {
      for (std::size_t k = 1; k + 1 < forward.spokes.size(); ++k) {
        const Edge spoke = undirected(v, forward.spokes[k]);
        if (mid_of.find(spoke) == mid_of.end()) mid_of[spoke] = add_blend(mesh, v, forward.spokes[k], 0.5f);
      }
    }
  }

  // Invariant: one cut point splits toward its opposite corner.
  // Invariant: two cut points on adjacent edges split into three.
  std::vector<std::uint32_t> tris;
  tris.reserve(mesh.tris.size() + ring.size() * 6);
  const auto mid_on = [&](std::uint32_t tri, std::size_t k) -> std::optional<std::uint32_t> {
    const std::uint32_t a = mesh.tris[3 * tri + k];
    const std::uint32_t b = mesh.tris[3 * tri + (k + 1) % 3];
    const auto found = mid_of.find(undirected(a, b));
    if (found == mid_of.end()) return std::nullopt;
    return found->second;
  };
  const std::size_t faces = mesh.tris.size() / 3;
  for (std::size_t t = 0; t < faces; ++t) {
    const std::uint32_t tri = static_cast<std::uint32_t>(t);
    std::vector<std::pair<std::size_t, std::uint32_t>> mids;
    for (std::size_t k = 0; k < 3; ++k) {
      if (const auto m = mid_on(tri, k)) mids.emplace_back(k, *m);
    }
    if (mids.empty()) {
      tris.insert(tris.end(), {mesh.tris[3 * tri], mesh.tris[3 * tri + 1], mesh.tris[3 * tri + 2]});
      continue;
    }
    if (mids.size() == 1) {
      const std::uint32_t a = mesh.tris[3 * tri + mids[0].first];
      const std::uint32_t b = mesh.tris[3 * tri + (mids[0].first + 1) % 3];
      const std::uint32_t c = mesh.tris[3 * tri + (mids[0].first + 2) % 3];
      const std::uint32_t m = mids[0].second;
      tris.insert(tris.end(), {a, m, c, m, b, c});
      continue;
    }
    if (mids.size() == 2) {
      // Constraint: the two cut edges must share a corner of this triangle.
      const std::size_t e0 = mids[0].first;
      const std::size_t e1 = mids[1].first;
      const bool adjacent = (e0 + 1) % 3 == e1 || (e1 + 1) % 3 == e0;
      if (!adjacent) {
        return "the loop crosses one triangle through opposite edges";
      }
      const std::size_t shared = (e0 + 1) % 3 == e1 ? (e0 + 1) % 3 : (e1 + 1) % 3;
      const std::uint32_t s = mesh.tris[3 * tri + shared];
      const std::uint32_t p = mesh.tris[3 * tri + (shared + 1) % 3];
      const std::uint32_t q = mesh.tris[3 * tri + (shared + 2) % 3];
      // Invariant: the corner piece keeps the winding and the remaining quad splits in two.
      const std::uint32_t m_sp = mid_of.at(undirected(s, p));
      const std::uint32_t m_sq = mid_of.at(undirected(s, q));
      tris.insert(tris.end(), {s, m_sp, m_sq, m_sp, p, q, m_sp, q, m_sq});
      continue;
    }
    return "the loop puts three cut points in one triangle";
  }
  mesh.tris = std::move(tris);
  return std::nullopt;
}

// Invariant: a bevel face carries the inset pair of one adjacent triangle.
struct BevelFace {
  std::uint32_t tri{0};
  std::uint32_t a{0};
  std::uint32_t b{0};
  bool forward{false};
};

// Invariant: one selected edge yields the inset pairs of its adjacent triangles.
struct BevelEdge {
  std::uint32_t a{0};
  std::uint32_t b{0};
  std::vector<BevelFace> faces;
};

std::optional<std::string> bevel_in_place(Mesh& work,
                                          const std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges,
                                          float width) {
  if (edges.empty()) return "no edges selected";
  if (!(width > 0.0f && width < 0.5f) || !std::isfinite(width)) {
    return "the bevel width must be between 0 and 0.5";
  }
  if (!channels_consistent(work)) return "vertex channels have different counts";
  const auto adjacency = edge_tris(work);
  std::set<Edge> selected;
  std::set<std::uint32_t> endpoints;
  for (const auto& [a, b] : edges) {
    if (a >= work.positions.size() || b >= work.positions.size()) return "the selection names a vertex past the mesh";
    if (a == b) return "the selection holds a zero-length edge";
    const Edge edge = undirected(a, b);
    if (!selected.insert(edge).second) return "edge " + std::to_string(a) + "-" + std::to_string(b) + " repeats";
    // Constraint: selected edges share no endpoint, so each corner keeps one inset.
    if (!endpoints.insert(a).second) return "vertex " + std::to_string(a) + " belongs to two selected edges";
    if (!endpoints.insert(b).second) return "vertex " + std::to_string(b) + " belongs to two selected edges";
    const auto found = adjacency.find(edge);
    if (found == adjacency.end()) {
      return "edge " + std::to_string(a) + "-" + std::to_string(b) + " is not a mesh edge";
    }
    if (found->second.size() > 2) {
      return "edge " + std::to_string(a) + "-" + std::to_string(b) + " is not manifold";
    }
  }
  // Constraint: every endpoint may touch only the adjacent triangles, so the bevel leaves no crack.
  std::map<std::uint32_t, std::set<std::uint32_t>> vertex_tris;
  for (std::size_t t = 0; t + 2 < work.tris.size(); t += 3) {
    for (std::size_t k = 0; k < 3; ++k) {
      vertex_tris[work.tris[t + k]].insert(static_cast<std::uint32_t>(t / 3));
    }
  }
  std::vector<BevelEdge> plan;
  for (const auto& edge : selected) {
    BevelEdge item;
    item.a = edge.first;
    item.b = edge.second;
    const auto found = adjacency.find(edge);
    if (found == adjacency.end() || found->second.empty()) return "the selection lost its edge";
    for (const auto tri : found->second) {
      if (vertex_tris[item.a].size() != found->second.size() ||
          vertex_tris[item.b].size() != found->second.size()) {
        return "bevel needs an edge whose endpoints touch no other triangle";
      }
      const std::uint32_t c[3] = {work.tris[3 * tri], work.tris[3 * tri + 1], work.tris[3 * tri + 2]};
      std::uint32_t third = std::numeric_limits<std::uint32_t>::max();
      bool forward = false;
      for (std::size_t k = 0; k < 3; ++k) {
        if (c[k] == item.a && c[(k + 1) % 3] == item.b) forward = true;
        if (c[k] != item.a && c[k] != item.b) third = c[k];
      }
      if (third == std::numeric_limits<std::uint32_t>::max()) return "an adjacent triangle holds the whole edge";
      BevelFace face;
      face.tri = tri;
      face.forward = forward;
      // Invariant: each endpoint is chamfered at width into the adjacent face.
      face.a = add_blend(work, item.a, third, width);
      face.b = add_blend(work, item.b, third, width);
      item.faces.push_back(face);
    }
    plan.push_back(std::move(item));
  }
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> corner;
  for (const auto& item : plan) {
    for (const auto& face : item.faces) {
      corner[{face.tri, item.a}] = face.a;
      corner[{face.tri, item.b}] = face.b;
    }
  }
  for (std::size_t t = 0; t + 2 < work.tris.size(); t += 3) {
    const auto tri = static_cast<std::uint32_t>(t / 3);
    for (std::size_t k = 0; k < 3; ++k) {
      const auto found = corner.find({tri, work.tris[t + k]});
      if (found != corner.end()) work.tris[t + k] = found->second;
    }
  }
  for (const auto& item : plan) {
    const BevelFace* first = nullptr;
    const BevelFace* second = nullptr;
    for (const auto& face : item.faces) {
      if (face.forward) {
        first = &face;
      } else {
        second = &face;
      }
    }
    if (first != nullptr && second != nullptr) {
      // Invariant: the chamfer quad closes the gap between the two adjacent faces.
      // Invariant: each endpoint cap closes the corner its two insets leave behind.
      work.tris.insert(work.tris.end(), {second->a, second->b, first->b, second->a, first->b, first->a,
                                         item.a, second->a, first->a, item.b, first->b, second->b});
    } else if (first != nullptr) {
      // Invariant: a boundary edge keeps a strip between the original edge and its inset.
      work.tris.insert(work.tris.end(), {item.a, item.b, first->b, item.a, first->b, first->a});
    } else {
      // Invariant: a single face that runs the edge backwards keeps its winding in the strip.
      work.tris.insert(work.tris.end(), {item.b, item.a, second->a, item.b, second->a, second->b});
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> cut_loop(Mesh& mesh, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ring) {
  // Invariant: the caller's mesh stays untouched unless the whole cut succeeds.
  Mesh work = mesh;
  if (const auto reason = cut_loop_in_place(work, ring)) return reason;
  if (work.positions.size() > k_max_vertices) return "the cut needs past 65535 vertices";
  mesh = std::move(work);
  return std::nullopt;
}

std::optional<std::string> bevel_edges(Mesh& mesh, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges,
                                       float width) {
  // Invariant: the caller's mesh stays untouched unless the whole bevel succeeds.
  Mesh work = mesh;
  if (const auto reason = bevel_in_place(work, edges, width)) return reason;
  if (work.positions.size() > k_max_vertices) return "the bevel needs past 65535 vertices";
  mesh = std::move(work);
  return std::nullopt;
}

std::optional<std::string> transform_about_pivot(Mesh& mesh, const ModalTransform& xf) {
  if (!channels_consistent(mesh)) return "vertex channels have different counts";
  for (const auto value : xf.rotation) {
    if (!std::isfinite(value)) return "the rotation is not finite";
  }
  for (const auto value : {xf.scale[0], xf.scale[1], xf.scale[2]}) {
    if (!std::isfinite(value) || value == 0.0f) return "the scale must be finite and non-zero";
  }
  for (const auto value :
       {xf.translation[0], xf.translation[1], xf.translation[2], xf.pivot[0], xf.pivot[1], xf.pivot[2]}) {
    if (!std::isfinite(value)) return "the translation and pivot must be finite";
  }
  const float norm_sq =
      xf.rotation[0] * xf.rotation[0] + xf.rotation[1] * xf.rotation[1] + xf.rotation[2] * xf.rotation[2] +
      xf.rotation[3] * xf.rotation[3];
  if (!(norm_sq > 0.0f) || !std::isfinite(norm_sq)) return "the rotation is not a usable quaternion";
  const float inv = 1.0f / std::sqrt(norm_sq);
  const float x = xf.rotation[0] * inv;
  const float y = xf.rotation[1] * inv;
  const float z = xf.rotation[2] * inv;
  const float w = xf.rotation[3] * inv;
  const std::array<float, 9> r{1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                               2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                               2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
  const auto turn = [&](const Vec3& v) {
    return Vec3{r[0] * v[0] + r[1] * v[1] + r[2] * v[2], r[3] * v[0] + r[4] * v[1] + r[5] * v[2],
                r[6] * v[0] + r[7] * v[1] + r[8] * v[2]};
  };
  const auto place = [&](Vec3 p) {
    p = sub_v(p, xf.pivot);
    p = turn(p);
    p = {p[0] * xf.scale[0], p[1] * xf.scale[1], p[2] * xf.scale[2]};
    p = add_v(p, xf.pivot);
    return add_v(p, xf.translation);
  };
  const auto direct = [&](Vec3 v) {
    v = turn(v);
    return Vec3{v[0] * xf.scale[0], v[1] * xf.scale[1], v[2] * xf.scale[2]};
  };
  // Invariant: the caller's mesh stays untouched unless every channel transforms finite.
  Mesh work = mesh;
  for (auto& p : work.positions) {
    if (!finite_vec(p)) return "a vertex position is not finite";
    p = place(p);
    if (!finite_vec(p)) return "a transformed position is not finite";
  }
  // Invariant: direction channels are re-orthogonalized by rotation only.
  // Constraint: a non-uniform modal scale requires a tangent-frame rebuild afterwards.
  const bool uniform = xf.scale[0] == xf.scale[1] && xf.scale[1] == xf.scale[2];
  auto move_directions = [&](std::vector<Vec3>& vectors) {
    for (auto& v : vectors) {
      v = uniform ? direct(v) : turn(v);
      const float squared = length_sq(v);
      if (!(squared > 0.0f) || !std::isfinite(squared)) return false;
      v = scale_v(v, 1.0f / std::sqrt(squared));
    }
    return true;
  };
  if (work.normals.size() == work.positions.size() && !move_directions(work.normals)) {
    return "a transformed normal is not finite";
  }
  if (work.tangents.size() == work.positions.size() && !move_directions(work.tangents)) {
    return "a transformed tangent is not finite";
  }
  if (work.bitangents.size() == work.positions.size() && !move_directions(work.bitangents)) {
    return "a transformed bitangent is not finite";
  }
  mesh = std::move(work);
  return std::nullopt;
}

}  // namespace fo4::nif::model_detail
