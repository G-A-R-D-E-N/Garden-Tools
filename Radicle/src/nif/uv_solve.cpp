#include "fo4/nif/uv.hpp"

#include "uv_cell.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fo4::nif::uv {

namespace {

using detail::Cell;
using detail::CellHash;
using detail::make_cell;
using detail::set_comp;
using detail::tri_count;
using detail::tri_ok;

using Pairs = std::vector<IndexPair>;
using Anchored = std::unordered_set<std::uint32_t>;

template <typename Fn>
void each_edge(std::size_t n, Indices tris, Fn&& fn) {
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (!tri_ok(tris, t, n)) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      const std::uint32_t a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
      if (a != b) fn(a, b);
    }
  }
}

std::optional<std::uint32_t> next_island(const Pairs& pairs, Indices ids, const Anchored& anchored) {
  std::optional<std::uint32_t> best;
  for (const IndexPair& p : pairs) {
    const std::uint32_t ia = ids[p.first], ib = ids[p.second];
    const bool anchored_a = anchored.contains(ia), anchored_b = anchored.contains(ib);
    if (anchored_a == anchored_b) continue;
    const std::uint32_t cand = anchored_a ? ib : ia;
    if (!best || cand < *best) best = cand;
  }
  return best;
}

std::optional<std::uint32_t> unanchored_seed(const Pairs& pairs, Indices ids, const Anchored& anchored) {
  std::optional<std::uint32_t> best;
  for (const IndexPair& p : pairs) {
    const std::uint32_t ia = ids[p.first], ib = ids[p.second];
    if (anchored.contains(ia) || anchored.contains(ib)) continue;
    const std::uint32_t cand = std::min(ia, ib);
    if (!best || cand < *best) best = cand;
  }
  return best;
}

void stitch_island(std::vector<float>& out, Indices ids, std::size_t n, std::uint32_t m, const Pairs& pairs, const Anchored& anchored) {
  Pairs corr;
  for (const IndexPair& p : pairs) {
    if (ids[p.first] == m && anchored.contains(ids[p.second])) corr.push_back({p.first, p.second});
    else if (ids[p.second] == m && anchored.contains(ids[p.first])) corr.push_back({p.second, p.first});
  }
  if (corr.empty()) return;
  const std::size_t k = corr.size();
  double pu = 0, pv = 0, qu = 0, qv = 0;
  for (const IndexPair& c : corr) {
    pu += out[2 * c.first];
    pv += out[2 * c.first + 1];
    qu += out[2 * c.second];
    qv += out[2 * c.second + 1];
  }
  pu /= static_cast<double>(k);
  pv /= static_cast<double>(k);
  qu /= static_cast<double>(k);
  qv /= static_cast<double>(k);
  double dot = 0, cross = 0;
  for (const IndexPair& c : corr) {
    const double xu = out[2 * c.first] - pu, xv = out[2 * c.first + 1] - pv;
    const double yu = out[2 * c.second] - qu, yv = out[2 * c.second + 1] - qv;
    dot += xu * yu + xv * yv;
    cross += xu * yv - xv * yu;
  }
  const double rot = k > 1 ? std::atan2(cross, dot) : 0.0, cs = std::cos(rot), sn = std::sin(rot);
  for (std::size_t i = 0; i < n; ++i) {
    if (ids[i] != m) continue;
    const double xu = out[2 * i] - pu, xv = out[2 * i + 1] - pv;
    set_comp(out, 2 * i, qu + xu * cs - xv * sn);
    set_comp(out, 2 * i + 1, qv + xu * sn + xv * cs);
  }
  std::vector<std::pair<double, double>> mids;
  for (const IndexPair& c : corr) mids.emplace_back((static_cast<double>(out[2 * c.first]) + out[2 * c.second]) / 2,
                                                    (static_cast<double>(out[2 * c.first + 1]) + out[2 * c.second + 1]) / 2);
  for (std::size_t j = 0; j < corr.size(); ++j) {
    set_comp(out, 2 * corr[j].first, mids[j].first);
    set_comp(out, 2 * corr[j].first + 1, mids[j].second);
    set_comp(out, 2 * corr[j].second, mids[j].first);
    set_comp(out, 2 * corr[j].second + 1, mids[j].second);
  }
}

void reconcile_seams(std::vector<float>& out, const Pairs& pairs) {
  std::unordered_map<std::uint32_t, std::uint32_t> parent;
  const auto find = [&](std::uint32_t x) {
    std::uint32_t r = x;
    for (;;) {
      const auto it = parent.find(r);
      if (it == parent.end() || it->second == r) break;
      r = it->second;
    }
    std::uint32_t y = x;
    while (y != r) {
      const std::uint32_t next = parent.at(y);
      parent[y] = r;
      y = next;
    }
    return r;
  };
  std::vector<std::uint32_t> members;
  std::unordered_set<std::uint32_t> known;
  for (const IndexPair& p : pairs) {
    if (known.insert(p.first).second) members.push_back(p.first);
    if (known.insert(p.second).second) members.push_back(p.second);
    const std::uint32_t root_a = find(p.first);
    const std::uint32_t root_b = find(p.second);
    parent[root_a] = root_b;
  }
  std::unordered_map<std::uint32_t, std::array<double, 3>> sums;
  for (const std::uint32_t v : members) {
    auto& s = sums.try_emplace(find(v), std::array<double, 3>{0, 0, 0}).first->second;
    s[0] += out[2 * v];
    s[1] += out[2 * v + 1];
    s[2] += 1;
  }
  for (const std::uint32_t v : members) {
    const auto& s = sums.at(find(v));
    set_comp(out, 2 * v, s[0] / s[2]);
    set_comp(out, 2 * v + 1, s[1] / s[2]);
  }
}

struct Live {
  std::uint32_t t{0};
  std::array<double, 6> c{};
  double min_u{0.0}, max_u{0.0}, min_v{0.0}, max_v{0.0};
};

std::array<std::array<double, 2>, 3> edge_normals(const std::array<double, 6>& c) {
  std::array<std::array<double, 2>, 3> out{};
  for (std::size_t e = 0; e < 3; ++e) {
    const std::size_t j = (e + 1) % 3;
    const double du = c[2 * j] - c[2 * e], dv = c[2 * j + 1] - c[2 * e + 1], len = std::hypot(du, dv);
    out[e] = {-dv / len, du / len};
  }
  return out;
}

std::pair<double, double> projection(const std::array<double, 6>& c, double ax, double ay) {
  const double d0 = c[0] * ax + c[1] * ay, d1 = c[2] * ax + c[3] * ay, d2 = c[4] * ax + c[5] * ay;
  return {std::min({d0, d1, d2}), std::max({d0, d1, d2})};
}

bool interiors_overlap(const std::array<double, 6>& p, const std::array<double, 6>& q) {
  for (const auto* shape : {&p, &q}) {
    for (const auto& axis : edge_normals(*shape)) {
      const auto [pa, pb] = projection(p, axis[0], axis[1]);
      const auto [qa, qb] = projection(q, axis[0], axis[1]);
      if (!(std::min(pb, qb) - std::max(pa, qa) > 1e-9)) return false;
    }
  }
  return true;
}

}  // namespace

std::vector<std::uint32_t> boundary_verts(std::size_t vertex_count, Indices tris) {
  struct Use {
    std::uint32_t lo{0};
    std::uint32_t hi{0};
    std::uint32_t uses{0};
  };
  std::unordered_map<std::uint64_t, Use> edges;
  each_edge(vertex_count, tris, [&](std::uint32_t a, std::uint32_t b) {
    const std::uint32_t lo = std::min(a, b), hi = std::max(a, b);
    auto& use = edges.try_emplace((static_cast<std::uint64_t>(lo) << 32) | hi, Use{lo, hi, 0}).first->second;
    ++use.uses;
  });
  std::vector<bool> mark(vertex_count, false);
  for (const auto& entry : edges) {
    if (entry.second.uses != 1) continue;
    mark[entry.second.lo] = true;
    mark[entry.second.hi] = true;
  }
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < vertex_count; ++i) {
    if (mark[i]) out.push_back(static_cast<std::uint32_t>(i));
  }
  return out;
}

std::vector<float> relax_uvs(Floats uvs, Indices tris, Indices selection, double iterations) {
  const std::size_t n = uvs.size() >> 1;
  const int passes = std::isnan(iterations) ? 0 : static_cast<int>(std::min(200.0, std::max(0.0, std::floor(iterations))));
  std::vector<bool> boundary(n, false);
  for (const std::uint32_t v : boundary_verts(n, tris)) boundary[v] = true;
  std::vector<std::uint32_t> movers;
  std::vector<std::ptrdiff_t> slot(n, -1);
  for (const std::size_t i : detail::unique_selection(n, selection)) {
    if (boundary[i]) continue;
    slot[i] = static_cast<std::ptrdiff_t>(movers.size());
    movers.push_back(static_cast<std::uint32_t>(i));
  }
  std::vector<std::vector<std::uint32_t>> neighbours(movers.size());
  std::unordered_set<std::uint64_t> seen;
  const auto link = [&](std::uint32_t from, std::uint32_t to) {
    const std::ptrdiff_t s = slot[from];
    if (s < 0) return;
    if (seen.insert((static_cast<std::uint64_t>(from) << 32) | to).second) neighbours[static_cast<std::size_t>(s)].push_back(to);
  };
  each_edge(n, tris, [&](std::uint32_t a, std::uint32_t b) {
    link(a, b);
    link(b, a);
  });
  std::vector<float> cur(uvs.begin(), uvs.end());
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<float> next = cur;
    for (std::size_t m = 0; m < movers.size(); ++m) {
      const auto& ns = neighbours[m];
      if (ns.empty()) continue;
      double su = 0, sv = 0;
      for (const std::uint32_t j : ns) {
        su += cur[2 * j];
        sv += cur[2 * j + 1];
      }
      set_comp(next, 2 * movers[m], su / static_cast<double>(ns.size()));
      set_comp(next, 2 * movers[m] + 1, sv / static_cast<double>(ns.size()));
    }
    cur = std::move(next);
  }
  return cur;
}

std::vector<IndexPair> find_stitch_pairs(Floats verts, Indices ids, Indices selection, double eps) {
  if (!(eps >= 1e-9)) return {};
  const std::size_t n = std::min(verts.size() / 3, ids.size());
  const auto finite = [&](std::size_t i) { return std::isfinite(verts[3 * i]) && std::isfinite(verts[3 * i + 1]) && std::isfinite(verts[3 * i + 2]); };
  std::vector<std::uint32_t> pts;
  for (const std::size_t i : detail::unique_selection(n, selection)) {
    if (finite(i)) pts.push_back(static_cast<std::uint32_t>(i));
  }
  std::sort(pts.begin(), pts.end());
  const auto cell_of = [&](std::size_t i) {
    return std::array<double, 3>{std::floor(verts[3 * i] / eps), std::floor(verts[3 * i + 1] / eps), std::floor(verts[3 * i + 2] / eps)};
  };
  std::unordered_map<Cell<3>, std::vector<std::uint32_t>, CellHash<3>> grid;
  for (const std::uint32_t i : pts) grid[make_cell(cell_of(i))].push_back(i);
  const double eps2 = eps * eps;
  Pairs out;
  std::vector<std::uint32_t> near;
  for (const std::uint32_t a : pts) {
    const auto c = cell_of(a);
    near.clear();
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
          const auto it = grid.find(make_cell(std::array<double, 3>{c[0] + dx, c[1] + dy, c[2] + dz}));
          if (it == grid.end()) continue;
          for (const std::uint32_t b : it->second) {
            if (b > a) near.push_back(b);
          }
        }
      }
    }
    std::sort(near.begin(), near.end());
    near.erase(std::unique(near.begin(), near.end()), near.end());
    for (const std::uint32_t b : near) {
      if (ids[a] == ids[b]) continue;
      const double ex = static_cast<double>(verts[3 * a]) - verts[3 * b];
      const double ey = static_cast<double>(verts[3 * a + 1]) - verts[3 * b + 1];
      const double ez = static_cast<double>(verts[3 * a + 2]) - verts[3 * b + 2];
      if (ex * ex + ey * ey + ez * ez <= eps2) out.push_back({a, b});
    }
  }
  return out;
}

std::vector<float> stitch_selection(Floats uvs, Floats verts, Indices ids, Indices selection, double eps) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const std::size_t n = std::min(uvs.size() >> 1, ids.size());
  const Indices bounded = ids.first(n);
  const Pairs pairs = find_stitch_pairs(verts, bounded, selection, eps);
  if (pairs.empty()) return out;
  Anchored anchored{bounded[pairs[0].first]};
  for (;;) {
    auto m = next_island(pairs, bounded, anchored);
    while (m) {
      stitch_island(out, bounded, n, *m, pairs, anchored);
      anchored.insert(*m);
      m = next_island(pairs, bounded, anchored);
    }
    const auto seed = unanchored_seed(pairs, bounded, anchored);
    if (!seed) {
      reconcile_seams(out, pairs);
      return out;
    }
    anchored.insert(*seed);
  }
}

std::vector<std::uint32_t> overlapping_faces(Floats uvs, Indices tris, Indices hidden) {
  const std::size_t n = uvs.size() >> 1;
  const std::size_t count = tri_count(tris);
  std::vector<bool> is_hidden(count, false);
  for (const std::uint32_t h : hidden) {
    if (h < count) is_hidden[h] = true;
  }
  std::vector<Live> live;
  for (std::size_t t = 0; t < count; ++t) {
    if (is_hidden[t] || !tri_ok(tris, t, n)) continue;
    Live f;
    f.t = static_cast<std::uint32_t>(t);
    bool finite = true;
    for (std::size_t k = 0; k < 3; ++k) {
      f.c[2 * k] = uvs[2 * tris[3 * t + k]];
      f.c[2 * k + 1] = uvs[2 * tris[3 * t + k] + 1];
      finite = finite && std::isfinite(f.c[2 * k]) && std::isfinite(f.c[2 * k + 1]);
    }
    if (!finite) continue;
    const auto& c = f.c;
    const double twice_area = (c[2] - c[0]) * (c[5] - c[1]) - (c[3] - c[1]) * (c[4] - c[0]);
    if (std::abs(twice_area) <= 2e-12) continue;
    f.min_u = std::min({c[0], c[2], c[4]});
    f.max_u = std::max({c[0], c[2], c[4]});
    f.min_v = std::min({c[1], c[3], c[5]});
    f.max_v = std::max({c[1], c[3], c[5]});
    live.push_back(f);
  }
  if (live.size() < 2) return {};
  double extent = 0;
  for (const Live& f : live) extent += std::max(f.max_u - f.min_u, f.max_v - f.min_v);
  const double cell = std::max(extent / static_cast<double>(live.size()), 1e-9);
  std::unordered_map<Cell<2>, std::vector<std::size_t>, CellHash<2>> grid;
  std::vector<std::size_t> large;
  for (std::size_t j = 0; j < live.size(); ++j) {
    const Live& f = live[j];
    const double u0 = std::floor(f.min_u / cell), u1 = std::floor(f.max_u / cell);
    const double v0 = std::floor(f.min_v / cell), v1 = std::floor(f.max_v / cell);
    if (!((u1 - u0 + 1) * (v1 - v0 + 1) <= 1024)) {
      large.push_back(j);
      continue;
    }
    const int du = static_cast<int>(u1 - u0), dv = static_cast<int>(v1 - v0);
    for (int a = 0; a <= du; ++a) {
      for (int b = 0; b <= dv; ++b) grid[make_cell(std::array<double, 2>{u0 + a, v0 + b})].push_back(j);
    }
  }
  std::vector<bool> hit(count, false);
  const auto test = [&](const Live& p, const Live& q) {
    if (std::min(p.max_u, q.max_u) - std::max(p.min_u, q.min_u) <= 1e-9) return;
    if (std::min(p.max_v, q.max_v) - std::max(p.min_v, q.min_v) <= 1e-9) return;
    if (!interiors_overlap(p.c, q.c)) return;
    hit[p.t] = true;
    hit[q.t] = true;
  };
  for (const auto& entry : grid) {
    const auto& bucket = entry.second;
    for (std::size_t x = 0; x < bucket.size(); ++x) {
      for (std::size_t y = x + 1; y < bucket.size(); ++y) {
        if (bucket[x] != bucket[y]) test(live[bucket[x]], live[bucket[y]]);
      }
    }
  }
  for (const std::size_t j : large) {
    for (std::size_t k = 0; k < live.size(); ++k) {
      if (k != j) test(live[j], live[k]);
    }
  }
  std::vector<std::uint32_t> out;
  for (std::size_t t = 0; t < count; ++t) {
    if (hit[t]) out.push_back(static_cast<std::uint32_t>(t));
  }
  return out;
}

}  // namespace fo4::nif::uv
