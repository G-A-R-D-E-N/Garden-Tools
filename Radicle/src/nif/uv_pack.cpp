#include "fo4/nif/uv.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fo4::nif::uv {

namespace {

using detail::set_comp;
using detail::tri_count;
using detail::tri_ok;

double uv_area(Floats uvs, std::size_t a, std::size_t b, std::size_t c) {
  const double ax = uvs[2 * a], ay = uvs[2 * a + 1];
  return std::abs((uvs[2 * b] - ax) * (uvs[2 * c + 1] - ay) - (uvs[2 * b + 1] - ay) * (uvs[2 * c] - ax)) / 2;
}

double area_3d(Floats verts, std::size_t a, std::size_t b, std::size_t c) {
  const double ux = static_cast<double>(verts[3 * b]) - verts[3 * a];
  const double uy = static_cast<double>(verts[3 * b + 1]) - verts[3 * a + 1];
  const double uz = static_cast<double>(verts[3 * b + 2]) - verts[3 * a + 2];
  const double vx = static_cast<double>(verts[3 * c]) - verts[3 * a];
  const double vy = static_cast<double>(verts[3 * c + 1]) - verts[3 * a + 1];
  const double vz = static_cast<double>(verts[3 * c + 2]) - verts[3 * a + 2];
  return std::hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) / 2;
}

std::pair<double, double> tri_areas(Floats uvs, Floats verts, Indices tris, std::size_t t) {
  const std::size_t a = tris[3 * t], b = tris[3 * t + 1], c = tris[3 * t + 2];
  return {uv_area(uvs, a, b, c), area_3d(verts, a, b, c)};
}

std::unordered_set<std::uint32_t> sel_islands(Indices ids, std::size_t n, Indices selection) {
  std::unordered_set<std::uint32_t> out;
  for (const std::uint32_t i : selection) {
    if (i < n) out.insert(ids[i]);
  }
  return out;
}

struct IslandBox {
  std::uint32_t island{0};
  Box box{};
};

std::vector<IslandBox> island_boxes(Floats uvs, Indices ids, std::size_t n, const std::unordered_set<std::uint32_t>& keep) {
  std::vector<IslandBox> out;
  std::unordered_map<std::uint32_t, std::size_t> at;
  for (std::size_t i = 0; i < n; ++i) {
    if (!keep.contains(ids[i])) continue;
    const double u = uvs[2 * i], v = uvs[2 * i + 1];
    const auto slot = at.try_emplace(ids[i], out.size());
    if (slot.second) {
      out.push_back({ids[i], {u, v, u, v}});
      continue;
    }
    Box& b = out[slot.first->second].box;
    b.min_u = std::min(b.min_u, u);
    b.min_v = std::min(b.min_v, v);
    b.max_u = std::max(b.max_u, u);
    b.max_v = std::max(b.max_v, v);
  }
  return out;
}

struct Item {
  std::uint32_t island{0};
  Box box{};
  double w{0.0};
  double h{0.0};
};

using Slots = std::vector<std::pair<double, double>>;

std::optional<Slots> place(const std::vector<Item>& items, double s, double m) {
  Slots slots;
  double x = m, y = m, shelf = 0;
  for (const Item& it : items) {
    const double w = it.w * s, h = it.h * s;
    if (x + w > 1 - m) {
      x = m;
      y += shelf + m;
      shelf = 0;
    }
    if (x + w > 1 - m || y + h > 1 - m) return std::nullopt;
    slots.emplace_back(x, y);
    x += w + m;
    shelf = std::max(shelf, h);
  }
  return slots;
}

}  // namespace

std::optional<std::vector<float>> pack_islands(Floats uvs, Indices ids, Indices selection, double margin) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const std::size_t n = std::min(uvs.size() >> 1, ids.size());
  std::vector<Item> items;
  for (const IslandBox& ib : island_boxes(uvs, ids, n, sel_islands(ids, n, selection))) {
    const double w = ib.box.max_u - ib.box.min_u, h = ib.box.max_v - ib.box.min_v;
    if (w > 1e-12 || h > 1e-12) items.push_back({ib.island, ib.box, w, h});
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& p, const Item& q) { return p.h > q.h; });
  if (items.empty()) return out;
  const double m = std::isfinite(margin) ? std::min(0.25, std::max(0.0, margin)) : 0.0;
  double max_ext = 0;
  for (const Item& it : items) max_ext = std::max(max_ext, std::max(it.w, it.h));
  double lo = 0, hi = 1 / max_ext;
  std::optional<Slots> best;
  for (int iter = 0; iter < 40; ++iter) {
    const double mid = (lo + hi) / 2;
    if (auto slots = place(items, mid, m)) {
      lo = mid;
      best = std::move(slots);
    } else {
      hi = mid;
    }
  }
  if (!best) return std::nullopt;
  std::unordered_map<std::uint32_t, std::size_t> at;
  for (std::size_t k = 0; k < items.size(); ++k) at.emplace(items[k].island, k);
  for (std::size_t i = 0; i < n; ++i) {
    const auto found = at.find(ids[i]);
    if (found == at.end()) continue;
    const Item& it = items[found->second];
    const auto& slot = (*best)[found->second];
    set_comp(out, 2 * i, slot.first + (static_cast<double>(uvs[2 * i]) - it.box.min_u) * lo);
    set_comp(out, 2 * i + 1, slot.second + (static_cast<double>(uvs[2 * i + 1]) - it.box.min_v) * lo);
  }
  return out;
}

std::vector<float> average_island_scale(Floats uvs, Floats verts, Indices tris, Indices ids, Indices selection) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const std::size_t n = std::min({uvs.size() >> 1, verts.size() / 3, ids.size()});
  const auto boxes = island_boxes(uvs, ids, n, sel_islands(ids, n, selection));
  std::unordered_set<std::uint32_t> kept;
  for (const IslandBox& ib : boxes) kept.insert(ib.island);
  std::unordered_map<std::uint32_t, std::pair<double, double>> area;
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (!tri_ok(tris, t, n)) continue;
    const std::uint32_t isl = ids[tris[3 * t]];
    if (!kept.contains(isl)) continue;
    const auto [uv, a3] = tri_areas(uvs, verts, tris, t);
    auto& sum = area[isl];
    sum.first += uv;
    sum.second += a3;
  }
  struct Valid {
    std::uint32_t island;
    double r, a3, cu, cv;
  };
  std::vector<Valid> valid;
  for (const IslandBox& ib : boxes) {
    const auto found = area.find(ib.island);
    const double uv = found == area.end() ? 0.0 : found->second.first;
    const double a3 = found == area.end() ? 0.0 : found->second.second;
    if (uv > 1e-12 && a3 > 1e-12) {
      valid.push_back({ib.island, std::sqrt(uv / a3), a3, (ib.box.min_u + ib.box.max_u) / 2, (ib.box.min_v + ib.box.max_v) / 2});
    }
  }
  if (valid.size() < 2) return out;
  double weighted = 0, total = 0;
  for (const Valid& v : valid) {
    weighted += v.a3 * v.r;
    total += v.a3;
  }
  const double target = weighted / total;
  if (!std::isfinite(target) || target <= 0) return out;
  const auto clamp_scale = [](double k) { return std::isfinite(k) ? std::min(1000.0, std::max(0.001, k)) : 1.0; };
  std::unordered_map<std::uint32_t, Valid> fix;
  for (const Valid& v : valid) fix.emplace(v.island, v);
  for (std::size_t i = 0; i < n; ++i) {
    const auto found = fix.find(ids[i]);
    if (found == fix.end()) continue;
    const Valid& v = found->second;
    const double k = clamp_scale(target / v.r);
    set_comp(out, 2 * i, v.cu + (static_cast<double>(uvs[2 * i]) - v.cu) * k);
    set_comp(out, 2 * i + 1, v.cv + (static_cast<double>(uvs[2 * i + 1]) - v.cv) * k);
  }
  return out;
}

std::vector<float> tri_stretch(Floats uvs, Floats verts, Indices tris) {
  const std::size_t n = std::min(uvs.size() >> 1, verts.size() / 3);
  std::vector<std::pair<double, double>> areas(tri_count(tris), {0.0, 0.0});
  for (std::size_t t = 0; t < areas.size(); ++t) {
    if (tri_ok(tris, t, n)) areas[t] = tri_areas(uvs, verts, tris, t);
  }
  const auto good = [](const std::pair<double, double>& p) { return p.first > 1e-12 && p.second > 1e-12; };
  double uv_sum = 0, a3_sum = 0;
  for (const auto& p : areas) {
    if (good(p)) {
      uv_sum += p.first;
      a3_sum += p.second;
    }
  }
  const double m = uv_sum / a3_sum;
  const double ln4 = std::log(4.0);
  std::vector<float> out(areas.size());
  for (std::size_t t = 0; t < areas.size(); ++t) {
    out[t] = good(areas[t]) ? static_cast<float>(std::min(1.0, std::abs(std::log(areas[t].first / areas[t].second / m)) / ln4)) : 0.0f;
  }
  return out;
}

}  // namespace fo4::nif::uv
