#include "fo4/nif/uv.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace fo4::nif::uv {

namespace {

using detail::tri_count;
using detail::tri_ok;
using Vec3 = std::array<double, 3>;

constexpr double k_cancel_ratio = 1e-3;

std::optional<Vec3> unit(const Vec3& v) {
  const double len = std::hypot(v[0], v[1], v[2]);
  if (len > 1e-12 && std::isfinite(len)) return Vec3{v[0] / len, v[1] / len, v[2] / len};
  return std::nullopt;
}

double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

Vec3 point(Floats verts, std::size_t i) { return {verts[3 * i], verts[3 * i + 1], verts[3 * i + 2]}; }

Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

std::optional<Vec3> face_normal(Floats verts, std::size_t a, std::size_t b, std::size_t c) {
  const Vec3 pa = point(verts, a), pb = point(verts, b), pc = point(verts, c);
  const Vec3 n = cross({pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]}, {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]});
  if (!std::isfinite(n[0] + n[1] + n[2])) return std::nullopt;
  return n;
}

struct Basis {
  Vec3 u{};
  Vec3 v{};
};

std::optional<Basis> basis_for(Floats verts, Indices tris, Indices ids, std::uint32_t island, std::size_t n, const Vec3& normal) {
  double best_len = 0;
  std::optional<Vec3> best_dir;
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (!tri_ok(tris, t, n) || ids[tris[3 * t]] != island) continue;
    for (std::size_t k = 0; k < 3; ++k) {
      const std::size_t a = tris[3 * t + k], b = tris[3 * t + ((k + 1) % 3)];
      const double dx = static_cast<double>(verts[3 * b]) - verts[3 * a];
      const double dy = static_cast<double>(verts[3 * b + 1]) - verts[3 * a + 1];
      const double dz = static_cast<double>(verts[3 * b + 2]) - verts[3 * a + 2];
      const double len = std::hypot(dx, dy, dz);
      if (len <= best_len || len <= 1e-12) continue;
      const Vec3 d{dx / len, dy / len, dz / len};
      if (std::abs(dot(d, normal)) > 0.9) continue;
      best_len = len;
      best_dir = d;
    }
  }
  std::optional<Vec3> u;
  if (best_dir) {
    const double dn = dot(*best_dir, normal);
    u = unit({(*best_dir)[0] - normal[0] * dn, (*best_dir)[1] - normal[1] * dn, (*best_dir)[2] - normal[2] * dn});
  }
  if (!u) {
    const Vec3 helper = std::abs(normal[2]) < 0.9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    u = unit({helper[1] * normal[2] - helper[2] * normal[1], helper[2] * normal[0] - helper[0] * normal[2],
              helper[0] * normal[1] - helper[1] * normal[0]});
  }
  if (!u) return std::nullopt;
  const Vec3 v = cross(normal, *u);
  if (!std::isfinite(v[0] + v[1] + v[2])) return std::nullopt;
  return Basis{*u, v};
}

}  // namespace

std::map<std::uint32_t, Plane> island_planes(Floats verts, Indices tris, Indices ids, Indices selection) {
  const std::size_t n = std::min(verts.size() / 3, ids.size());
  std::unordered_set<std::uint32_t> keep;
  for (const std::uint32_t i : selection) {
    if (i < n) keep.insert(ids[i]);
  }
  std::unordered_map<std::uint32_t, Vec3> sums;
  std::unordered_map<std::uint32_t, double> abs_sums;
  std::unordered_map<std::uint32_t, std::array<double, 4>> centroids;
  for (std::size_t i = 0; i < n; ++i) {
    if (!keep.contains(ids[i])) continue;
    auto& c = centroids[ids[i]];
    c[0] += verts[3 * i];
    c[1] += verts[3 * i + 1];
    c[2] += verts[3 * i + 2];
    c[3] += 1;
  }
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (!tri_ok(tris, t, n)) continue;
    const std::uint32_t island = ids[tris[3 * t]];
    if (!keep.contains(island)) continue;
    const auto fn = face_normal(verts, tris[3 * t], tris[3 * t + 1], tris[3 * t + 2]);
    if (!fn) continue;
    auto& sum = sums[island];
    sum[0] += (*fn)[0];
    sum[1] += (*fn)[1];
    sum[2] += (*fn)[2];
    abs_sums[island] += std::hypot((*fn)[0], (*fn)[1], (*fn)[2]);
  }
  std::map<std::uint32_t, Plane> planes;
  for (const auto& [island, sum] : sums) {
    const double abs_sum = abs_sums[island];
    if (!(std::hypot(sum[0], sum[1], sum[2]) > k_cancel_ratio * abs_sum)) continue;
    const auto normal = unit(sum);
    const auto found = centroids.find(island);
    if (!normal || found == centroids.end() || found->second[3] == 0) continue;
    const auto basis = basis_for(verts, tris, ids, island, n, *normal);
    if (!basis) continue;
    const auto& c = found->second;
    const double cu = (c[0] * basis->u[0] + c[1] * basis->u[1] + c[2] * basis->u[2]) / c[3];
    const double cv = (c[0] * basis->v[0] + c[1] * basis->v[1] + c[2] * basis->v[2]) / c[3];
    if (!std::isfinite(cu) || !std::isfinite(cv)) continue;
    planes[island] = Plane{basis->u, basis->v, cu, cv};
  }
  return planes;
}

ProjectResult smart_project(Floats uvs, Floats verts, Indices tris, Indices ids, Indices selection, double margin) {
  std::vector<float> out(uvs.begin(), uvs.end());
  const std::size_t n = std::min({uvs.size() >> 1, verts.size() / 3, ids.size()});
  std::unordered_set<std::uint32_t> wanted;
  for (const std::uint32_t i : selection) {
    if (i < n) wanted.insert(ids[i]);
  }
  const auto planes = island_planes(verts, tris, ids, selection);
  std::vector<std::uint32_t> projected;
  for (std::size_t i = 0; i < n; ++i) {
    const auto found = planes.find(ids[i]);
    if (found == planes.end()) continue;
    const Plane& plane = found->second;
    const Vec3 p = point(verts, i);
    const double u = dot(p, plane.u) - plane.cu;
    const double v = dot(p, plane.v) - plane.cv;
    if (!std::isfinite(u) || !std::isfinite(v)) continue;
    out[2 * i] = static_cast<float>(u);
    out[2 * i + 1] = static_cast<float>(v);
    projected.push_back(static_cast<std::uint32_t>(i));
  }
  ProjectResult result;
  result.skipped = static_cast<std::uint32_t>(wanted.size() > planes.size() ? wanted.size() - planes.size() : 0);
  if (projected.empty()) {
    result.uvs = std::move(out);
    return result;
  }
  auto packed = pack_islands(out, ids, projected, margin);
  if (!packed) {
    result.uvs.assign(uvs.begin(), uvs.end());
    result.fits = false;
    return result;
  }
  result.uvs = std::move(*packed);
  result.projected = static_cast<std::uint32_t>(projected.size());
  return result;
}

}  // namespace fo4::nif::uv
