#include "fo4/nif/uv.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace fo4::nif::uv {

namespace {

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t lo = std::min(a, b), hi = std::max(a, b);
  return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

}  // namespace

std::vector<IndexPair> seams_in_selection(Indices tris, Indices selection, Indices hidden) {
  const std::unordered_set<std::uint32_t> chosen(selection.begin(), selection.end());
  const std::size_t count = tris.size() / 3;
  std::vector<bool> is_hidden(count, false);
  for (const std::uint32_t h : hidden) {
    if (h < count) is_hidden[h] = true;
  }
  std::unordered_set<std::uint64_t> seen;
  std::vector<IndexPair> out;
  for (std::size_t t = 0; t < count; ++t) {
    if (is_hidden[t]) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      const std::uint32_t a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
      if (a == b || !chosen.contains(a) || !chosen.contains(b)) continue;
      if (seen.insert(edge_key(a, b)).second) out.push_back({std::min(a, b), std::max(a, b)});
    }
  }
  return out;
}

SplitPlan split_plan(std::size_t vertex_count, Indices tris, std::span<const IndexPair> seams) {
  SplitPlan plan;
  plan.tris.assign(tris.begin(), tris.end());
  const std::size_t count = tris.size() / 3;
  std::unordered_set<std::uint64_t> seam_keys;
  for (const IndexPair& s : seams) seam_keys.insert(edge_key(s.first, s.second));
  std::vector<std::vector<std::uint32_t>> incident(vertex_count);
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> edge_tris;
  std::vector<bool> seam_verts(vertex_count, false);
  for (std::size_t t = 0; t < count; ++t) {
    const std::uint32_t a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
    if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;
    const auto tri = static_cast<std::uint32_t>(t);
    if (a != b) incident[a].push_back(tri);
    if (b != c) incident[b].push_back(tri);
    if (c != a) incident[c].push_back(tri);
    for (const IndexPair& e : {IndexPair{a, b}, IndexPair{b, c}, IndexPair{c, a}}) {
      if (e.first == e.second) continue;
      const std::uint64_t key = edge_key(e.first, e.second);
      if (seam_keys.contains(key)) {
        seam_verts[e.first] = true;
        seam_verts[e.second] = true;
      }
      edge_tris[key].push_back(tri);
    }
  }
  for (std::size_t v = 0; v < vertex_count; ++v) {
    const auto& list = incident[v];
    if (list.size() < 2 || !seam_verts[v]) continue;
    const auto vertex = static_cast<std::uint32_t>(v);
    std::unordered_map<std::uint32_t, std::size_t> pos;
    for (std::size_t i = 0; i < list.size(); ++i) pos[list[i]] = i;
    std::vector<std::size_t> parent(list.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&](std::size_t start) {
      std::size_t i = start;
      while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
      }
      return i;
    };
    const auto join = [&](std::size_t p, std::size_t q) {
      const std::size_t ra = find(p), rb = find(q);
      if (ra != rb) parent[rb] = ra;
    };
    for (const std::uint32_t t : list) {
      for (std::size_t k = 0; k < 3; ++k) {
        const std::uint32_t a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
        if (a != vertex && b != vertex) continue;
        const std::uint32_t other = a == vertex ? b : a;
        if (other == vertex) continue;
        const std::uint64_t key = edge_key(vertex, other);
        if (seam_keys.contains(key)) continue;
        const auto shared = edge_tris.find(key);
        if (shared == edge_tris.end()) continue;
        for (const std::uint32_t s : shared->second) {
          if (s == t) continue;
          const auto from = pos.find(t), to = pos.find(s);
          if (from != pos.end() && to != pos.end()) join(from->second, to->second);
        }
      }
    }
    std::unordered_map<std::size_t, std::uint32_t> target;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const std::uint32_t t = list[i];
      const std::size_t root = find(i);
      auto slot = target.find(root);
      if (slot == target.end()) {
        const std::uint32_t to = target.empty() ? vertex : static_cast<std::uint32_t>(vertex_count + plan.sources.size());
        if (to != vertex) plan.sources.push_back(vertex);
        slot = target.emplace(root, to).first;
      }
      for (std::size_t k = 0; k < 3; ++k) {
        if (tris[3 * t + k] == vertex) plan.tris[3 * t + k] = slot->second;
      }
    }
  }
  return plan;
}

}  // namespace fo4::nif::uv
