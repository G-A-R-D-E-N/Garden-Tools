#include "fo4/nif/uv.hpp"

#include "uv_cell.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace fo4::nif::uv {

namespace {

using detail::Cell;
using detail::CellHash;
using detail::make_cell;
using detail::set_comp;
using detail::tri_count;
using detail::tri_ok;
using detail::unique_selection;

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();

double js_min(double a, double b) { return std::isnan(a) || std::isnan(b) ? k_nan : std::min(a, b); }

double js_max(double a, double b) { return std::isnan(a) || std::isnan(b) ? k_nan : std::max(a, b); }

double js_round(double x) {
  const double r = std::floor(x);
  return x - r >= 0.5 ? r + 1 : r;
}

std::vector<bool> hidden_mask(Indices hidden, std::size_t count) {
  std::vector<bool> mask(count, false);
  for (const std::uint32_t h : hidden) {
    if (h < count) mask[h] = true;
  }
  return mask;
}

std::vector<std::uint32_t> ascending(const std::vector<bool>& mask) {
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < mask.size(); ++i) {
    if (mask[i]) out.push_back(static_cast<std::uint32_t>(i));
  }
  return out;
}

double side(Floats uvs, std::size_t i, std::size_t j, double u, double v) {
  const double ax = uvs[2 * i], ay = uvs[2 * i + 1];
  return (static_cast<double>(uvs[2 * j]) - ax) * (v - ay) - (static_cast<double>(uvs[2 * j + 1]) - ay) * (u - ax);
}

double seg_dist(Floats uvs, std::size_t i, std::size_t j, double u, double v) {
  const double ax = uvs[2 * i], ay = uvs[2 * i + 1];
  const double dx = static_cast<double>(uvs[2 * j]) - ax, dy = static_cast<double>(uvs[2 * j + 1]) - ay;
  const double len2 = dx * dx + dy * dy;
  const double s = len2 != 0 ? js_min(1, js_max(0, ((u - ax) * dx + (v - ay) * dy) / len2)) : 0;
  return std::hypot(u - ax - s * dx, v - ay - s * dy);
}

double snap_centre(double x, double size) {
  const double tile = x > 0 ? std::ceil(x) - 1 : std::floor(x);
  return tile + (js_min(size - 1, std::floor((x - tile) * size)) + 0.5) / size;
}

}  // namespace

std::vector<std::uint32_t> visible_verts(std::size_t vertex_count, Indices tris, Indices hidden) {
  const auto is_hidden = hidden_mask(hidden, tri_count(tris));
  std::vector<bool> visible(vertex_count, false), used(vertex_count, false);
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (!tri_ok(tris, t, vertex_count)) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      used[tris[3 * t + k]] = true;
      if (!is_hidden[t]) visible[tris[3 * t + k]] = true;
    }
  }
  for (std::size_t i = 0; i < vertex_count; ++i) {
    if (!used[i]) visible[i] = true;
  }
  return ascending(visible);
}

std::optional<IndexPair> pick_edge(Floats uvs, Indices tris, Indices hidden, double u, double v, double max_dist) {
  const std::size_t n = uvs.size() >> 1;
  const auto is_hidden = hidden_mask(hidden, tri_count(tris));
  std::optional<IndexPair> best;
  double best_d = std::numeric_limits<double>::infinity();
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (is_hidden[t] || !tri_ok(tris, t, n)) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      const std::uint32_t i = tris[3 * t + k], j = tris[3 * t + ((k + 1) % 3)];
      const double d = seg_dist(uvs, i, j, u, v);
      if (d <= max_dist && d < best_d) {
        best = IndexPair{i, j};
        best_d = d;
      }
    }
  }
  return best;
}

std::optional<std::array<std::uint32_t, 3>> pick_face(Floats uvs, Indices tris, Indices hidden, double u, double v) {
  const std::size_t n = uvs.size() >> 1;
  const auto is_hidden = hidden_mask(hidden, tri_count(tris));
  for (std::size_t t = tri_count(tris); t-- > 0;) {
    if (is_hidden[t] || !tri_ok(tris, t, n)) continue;
    const std::uint32_t a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
    if (side(uvs, a, b, uvs[2 * c], uvs[2 * c + 1]) == 0) continue;
    const double d1 = side(uvs, a, b, u, v), d2 = side(uvs, b, c, u, v), d3 = side(uvs, c, a, u, v);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    if (!(neg && pos)) return std::array<std::uint32_t, 3>{a, b, c};
  }
  return std::nullopt;
}

std::vector<std::uint32_t> box_verts(Floats uvs, Indices tris, Indices hidden, const Box& box, BoxMode mode) {
  const std::size_t n = uvs.size() >> 1;
  const auto inside = [&](std::size_t i) { return point_in_box(uvs[2 * i], uvs[2 * i + 1], box); };
  std::vector<bool> out(n, false);
  if (mode == BoxMode::vertex) {
    for (const std::uint32_t i : visible_verts(n, tris, hidden)) out[i] = inside(i);
    return ascending(out);
  }
  const auto is_hidden = hidden_mask(hidden, tri_count(tris));
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (is_hidden[t] || !tri_ok(tris, t, n)) continue;
    const std::array<std::uint32_t, 3> ids{tris[3 * t], tris[3 * t + 1], tris[3 * t + 2]};
    const std::array<bool, 3> ins{inside(ids[0]), inside(ids[1]), inside(ids[2])};
    if (mode == BoxMode::face) {
      if (ins[0] && ins[1] && ins[2]) {
        for (const std::uint32_t i : ids) out[i] = true;
      }
      continue;
    }
    for (std::size_t k = 0; k < 3; ++k) {
      const std::size_t j = (k + 1) % 3;
      if (ins[k] && ins[j]) {
        out[ids[k]] = true;
        out[ids[j]] = true;
      }
    }
  }
  return ascending(out);
}

std::vector<std::uint32_t> invert(Indices visible, Indices selection) {
  const std::unordered_set<std::uint32_t> chosen(selection.begin(), selection.end());
  std::unordered_set<std::uint32_t> seen;
  std::vector<std::uint32_t> out;
  for (const std::uint32_t i : visible) {
    if (chosen.contains(i) || !seen.insert(i).second) continue;
    out.push_back(i);
  }
  return out;
}

std::vector<std::uint32_t> linked(Indices ids, Indices selection) {
  std::unordered_set<std::uint32_t> picked;
  for (const std::uint32_t i : selection) {
    if (i < ids.size()) picked.insert(ids[i]);
  }
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (picked.contains(ids[i])) out.push_back(static_cast<std::uint32_t>(i));
  }
  return out;
}

std::vector<std::uint32_t> selected_tris(Indices tris, std::size_t vertex_count, Indices selection, Indices hidden) {
  const auto is_hidden = hidden_mask(hidden, tri_count(tris));
  std::vector<bool> chosen(vertex_count, false);
  for (const std::size_t i : unique_selection(vertex_count, selection)) chosen[i] = true;
  std::vector<std::uint32_t> out;
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (is_hidden[t] || !tri_ok(tris, t, vertex_count)) continue;
    if (chosen[tris[3 * t]] && chosen[tris[3 * t + 1]] && chosen[tris[3 * t + 2]]) out.push_back(static_cast<std::uint32_t>(t));
  }
  return out;
}

std::vector<float> merge_at_centre(Floats uvs, Indices selection) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const auto box = bounds(uvs, selection);
  if (!box) return out;
  const double cu = (box->min_u + box->max_u) / 2, cv = (box->min_v + box->max_v) / 2;
  for (const std::size_t i : unique_selection(uvs.size() >> 1, selection)) {
    set_comp(out, 2 * i, cu);
    set_comp(out, 2 * i + 1, cv);
  }
  return out;
}

std::vector<float> merge_by_distance(Floats uvs, Indices selection, double dist) {
  std::vector<float> out(uvs.begin(), uvs.end());
  if (!(dist >= 1e-9)) return out;
  const auto m = unique_selection(uvs.size() >> 1, selection);
  std::vector<std::size_t> parent(m.size());
  std::iota(parent.begin(), parent.end(), std::size_t{0});
  const auto find = [&](std::size_t start) {
    std::size_t i = start;
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  std::unordered_map<Cell<2>, std::vector<std::size_t>, CellHash<2>> grid;
  for (std::size_t k = 0; k < m.size(); ++k) {
    const double u = uvs[2 * m[k]], v = uvs[2 * m[k] + 1];
    if (!std::isfinite(u) || !std::isfinite(v)) continue;
    const double cx = std::floor(u / dist), cy = std::floor(v / dist);
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        const auto it = grid.find(make_cell(std::array<double, 2>{cx + dx, cy + dy}));
        if (it == grid.end()) continue;
        for (const std::size_t j : it->second) {
          const double du = static_cast<double>(uvs[2 * m[j]]) - u, dv = static_cast<double>(uvs[2 * m[j] + 1]) - v;
          if (du * du + dv * dv <= dist * dist) {
            const std::size_t root_k = find(k);
            const std::size_t root_j = find(j);
            parent[root_k] = root_j;
          }
        }
      }
    }
    grid[make_cell(std::array<double, 2>{cx, cy})].push_back(k);
  }
  std::unordered_map<std::size_t, std::array<double, 3>> sums;
  for (std::size_t k = 0; k < m.size(); ++k) {
    auto& s = sums.try_emplace(find(k), std::array<double, 3>{0, 0, 0}).first->second;
    s[0] += uvs[2 * m[k]];
    s[1] += uvs[2 * m[k] + 1];
    s[2] += 1;
  }
  for (std::size_t k = 0; k < m.size(); ++k) {
    const auto& s = sums.at(find(k));
    if (s[2] > 1) {
      set_comp(out, 2 * m[k], s[0] / s[2]);
      set_comp(out, 2 * m[k] + 1, s[1] / s[2]);
    }
  }
  return out;
}

std::vector<float> align_sel(Floats uvs, Indices selection, Axis axis, Align to) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const auto box = bounds(uvs, selection);
  if (!box) return out;
  const double lo = axis == Axis::u ? box->min_u : box->min_v, hi = axis == Axis::u ? box->max_u : box->max_v;
  const double x = to == Align::min ? lo : to == Align::max ? hi : (lo + hi) / 2;
  const std::size_t k = axis == Axis::u ? 0 : 1;
  for (const std::size_t i : unique_selection(uvs.size() >> 1, selection)) set_comp(out, 2 * i + k, x);
  return out;
}

std::vector<float> snap_grid(Floats uvs, Indices selection, double step) {
  std::vector<float> out(uvs.begin(), uvs.end());
  if (!(step > 0)) return out;
  for (const std::size_t i : unique_selection(uvs.size() >> 1, selection)) {
    for (std::size_t k = 0; k < 2; ++k) set_comp(out, 2 * i + k, js_round(static_cast<double>(uvs[2 * i + k]) / step) * step);
  }
  return out;
}

std::vector<float> snap_pixels(Floats uvs, Indices selection, double width, double height, bool centre) {
  std::vector<float> out(uvs.begin(), uvs.end());
  if (!(width > 0 && height > 0)) return out;
  const auto snap = [&](double x, double size) { return centre ? snap_centre(x, size) : js_round(x * size) / size; };
  for (const std::size_t i : unique_selection(uvs.size() >> 1, selection)) {
    set_comp(out, 2 * i, snap(uvs[2 * i], width));
    set_comp(out, 2 * i + 1, snap(uvs[2 * i + 1], height));
  }
  return out;
}

std::vector<float> clamp_tile(Floats uvs, Indices selection) {
  std::vector<float> out(uvs.begin(), uvs.end());
  for (const std::size_t i : unique_selection(uvs.size() >> 1, selection)) {
    for (std::size_t k = 0; k < 2; ++k) set_comp(out, 2 * i + k, js_min(1, js_max(0, uvs[2 * i + k])));
  }
  return out;
}

}  // namespace fo4::nif::uv
