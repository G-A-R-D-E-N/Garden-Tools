#include "fo4/nif/uv.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace fo4::nif::uv {

namespace {

double middle(std::vector<double> xs) {
  std::sort(xs.begin(), xs.end(), [](double a, double b) { return std::isnan(b) ? !std::isnan(a) : a < b; });
  const std::size_t m = xs.size() >> 1;
  return xs.size() % 2 ? xs[m] : (xs[m - 1] + xs[m]) / 2;
}

std::vector<std::size_t> valid_selection(Floats uvs, Indices selection) {
  std::vector<std::size_t> out;
  const std::size_t n = uvs.size() >> 1;
  for (const std::uint32_t i : selection) {
    if (i < n) out.push_back(i);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace

std::vector<std::uint32_t> islands(std::size_t vertex_count, Indices tris) {
  std::vector<std::size_t> parent(vertex_count);
  for (std::size_t i = 0; i < vertex_count; ++i) parent[i] = i;
  const auto root = [&](std::size_t start) {
    std::size_t i = start;
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
    const std::size_t a = tris[t], b = tris[t + 1], c = tris[t + 2];
    if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;
    parent[root(a)] = root(b);
    parent[root(b)] = root(c);
  }
  std::unordered_map<std::size_t, std::uint32_t> ids;
  std::vector<std::uint32_t> out(vertex_count);
  for (std::size_t i = 0; i < vertex_count; ++i) {
    const auto inserted = ids.try_emplace(root(i), static_cast<std::uint32_t>(ids.size()));
    out[i] = inserted.first->second;
  }
  return out;
}

std::optional<Box> bounds(Floats uvs, Indices selection) {
  const auto sel = valid_selection(uvs, selection);
  if (sel.empty()) return std::nullopt;
  Box box{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
  for (const std::size_t i : sel) {
    const double u = uvs[2 * i], v = uvs[2 * i + 1];
    box.min_u = std::min(box.min_u, u);
    box.min_v = std::min(box.min_v, v);
    box.max_u = std::max(box.max_u, u);
    box.max_v = std::max(box.max_v, v);
  }
  return box;
}

std::optional<Point> median(Floats uvs, Indices selection) {
  const auto sel = valid_selection(uvs, selection);
  if (sel.empty()) return std::nullopt;
  std::vector<double> us, vs;
  for (const std::size_t i : sel) {
    us.push_back(uvs[2 * i]);
    vs.push_back(uvs[2 * i + 1]);
  }
  return Point{middle(us), middle(vs)};
}

Point resolve_pivot(Floats uvs, Indices selection, PivotMode mode) {
  if (mode == PivotMode::tile) return {0.5, 0.5};
  if (mode == PivotMode::median) return median(uvs, selection).value_or(Point{0.5, 0.5});
  const auto box = bounds(uvs, selection);
  return box ? Point{(box->min_u + box->max_u) / 2, (box->min_v + box->max_v) / 2} : Point{0.5, 0.5};
}

std::vector<float> transform_sel(Floats uvs, Indices selection, const Xform& xform) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const double c = std::cos(xform.rot_rad), s = std::sin(xform.rot_rad);
  for (const std::size_t i : valid_selection(uvs, selection)) {
    const double dx = (static_cast<double>(uvs[2 * i]) - xform.pivot.u) * xform.sx;
    const double dy = (static_cast<double>(uvs[2 * i + 1]) - xform.pivot.v) * xform.sy;
    out[2 * i] = static_cast<float>(xform.pivot.u + dx * c - dy * s + xform.tx);
    out[2 * i + 1] = static_cast<float>(xform.pivot.v + dx * s + dy * c + xform.ty);
  }
  return out;
}

bool point_in_box(double u, double v, const Box& box) {
  return u >= box.min_u && u <= box.max_u && v >= box.min_v && v <= box.max_v;
}

std::int64_t pick_nearest(Floats uvs, double u, double v, double max_dist) {
  std::int64_t best = -1;
  double best_d = max_dist * max_dist;
  for (std::size_t i = 0; 2 * i + 1 < uvs.size(); ++i) {
    const double du = static_cast<double>(uvs[2 * i]) - u, dv = static_cast<double>(uvs[2 * i + 1]) - v;
    const double d = du * du + dv * dv;
    if (d <= best_d) {
      best = static_cast<std::int64_t>(i);
      best_d = d;
    }
  }
  return best;
}

View frame_view(Floats uvs, double width, double height) {
  std::vector<std::uint32_t> all(uvs.size() >> 1);
  for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<std::uint32_t>(i);
  const Box box = bounds(uvs, all).value_or(Box{0, 0, 1, 1});
  const double min_u = std::min(box.min_u, 0.0), max_u = std::max(box.max_u, 1.0);
  const double min_v = std::min(box.min_v, 0.0), max_v = std::max(box.max_v, 1.0);
  const double scale = 0.9 * std::min(width / (max_u - min_u), height / (max_v - min_v));
  return {scale, width / 2 - ((min_u + max_u) / 2) * scale, height / 2 - ((min_v + max_v) / 2) * scale};
}

}  // namespace fo4::nif::uv
