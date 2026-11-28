#include "fo4/nif/uv.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fo4::nif::uv {

namespace {

using detail::tri_count;
using detail::tri_ok;
using Xy = std::array<std::array<double, 2>, 3>;
using Local = std::unordered_map<std::uint32_t, std::size_t>;

std::optional<Xy> local_2d(Floats verts, std::size_t a, std::size_t b, std::size_t c) {
  const double ax = verts[3 * a], ay = verts[3 * a + 1], az = verts[3 * a + 2];
  const double e1x = verts[3 * b] - ax, e1y = verts[3 * b + 1] - ay, e1z = verts[3 * b + 2] - az;
  const double len1 = std::hypot(e1x, e1y, e1z);
  if (!(len1 > 1e-12)) return std::nullopt;
  const double ux = e1x / len1, uy = e1y / len1, uz = e1z / len1;
  const double e2x = verts[3 * c] - ax, e2y = verts[3 * c + 1] - ay, e2z = verts[3 * c + 2] - az;
  const double x3 = e2x * ux + e2y * uy + e2z * uz;
  const double y3 = std::hypot(e2x - ux * x3, e2y - uy * x3, e2z - uz * x3);
  if (!(y3 > 1e-12)) return std::nullopt;
  return Xy{{{0, 0}, {len1, 0}, {x3, y3}}};
}

struct Face {
  std::array<std::uint32_t, 3> tri{};
  Xy xy{};
};

std::vector<Face> usable_faces(Floats verts, Indices tris, const Local& local) {
  std::vector<Face> out;
  const std::size_t n = verts.size() / 3;
  for (std::size_t t = 0; t < tri_count(tris); ++t) {
    if (!tri_ok(tris, t, n)) continue;
    const std::array<std::uint32_t, 3> tri{tris[3 * t], tris[3 * t + 1], tris[3 * t + 2]};
    if (!local.contains(tri[0]) || !local.contains(tri[1]) || !local.contains(tri[2])) continue;
    if (auto xy = local_2d(verts, tri[0], tri[1], tri[2])) out.push_back({tri, *xy});
  }
  return out;
}

Local local_of(const std::vector<std::uint32_t>& members) {
  Local local;
  for (std::size_t i = 0; i < members.size(); ++i) local[members[i]] = i;
  return local;
}

std::unordered_map<std::uint32_t, std::size_t> regions_of(Floats verts, Indices tris, const std::vector<std::uint32_t>& members) {
  const Local local = local_of(members);
  std::vector<std::size_t> parent(members.size());
  for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
  const auto root = [&](std::size_t start) {
    std::size_t i = start;
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  std::unordered_set<std::uint32_t> linked;
  for (const Face& face : usable_faces(verts, tris, local)) {
    const std::size_t a = local.at(face.tri[0]), b = local.at(face.tri[1]), c = local.at(face.tri[2]);
    parent[root(a)] = root(b);
    parent[root(b)] = root(c);
    for (const std::uint32_t v : face.tri) linked.insert(v);
  }
  std::unordered_map<std::uint32_t, std::size_t> out;
  for (const std::uint32_t v : linked) out[v] = root(local.at(v));
  return out;
}

using PinMap = std::map<std::uint32_t, std::array<double, 2>>;

struct System {
  std::vector<std::size_t> row_start;
  std::vector<std::size_t> row_col;
  std::vector<double> row_val;
  std::size_t row_count{0};
  std::vector<double> rhs;
  std::vector<double> diag;
  std::size_t dof{0};
};

std::optional<System> build_system(Floats verts, Indices tris, const std::vector<std::uint32_t>& members,
                                   const std::unordered_map<std::uint32_t, std::size_t>& free_base, const PinMap& pins) {
  const Local local = local_of(members);
  System sys;
  for (const Face& face : usable_faces(verts, tris, local)) {
    const auto& xy = face.xy;
    const std::array<std::array<double, 2>, 3> w{{{xy[2][0] - xy[1][0], xy[2][1] - xy[1][1]}, {-xy[2][0], -xy[2][1]}, {xy[1][0], 0}}};
    for (const bool imaginary : {false, true}) {
      sys.row_start.push_back(sys.row_col.size());
      sys.rhs.push_back(0);
      for (std::size_t j = 0; j < 3; ++j) {
        const double re = w[j][0], im = w[j][1];
        const double u_coef = imaginary ? im : re;
        const double v_coef = imaginary ? re : -im;
        const std::uint32_t vertex = face.tri[j];
        const auto base = free_base.find(vertex);
        if (base != free_base.end()) {
          if (u_coef != 0) {
            sys.row_col.push_back(base->second);
            sys.row_val.push_back(u_coef);
          }
          if (v_coef != 0) {
            sys.row_col.push_back(base->second + 1);
            sys.row_val.push_back(v_coef);
          }
        } else {
          const auto pin = pins.find(vertex);
          const std::array<double, 2> value = pin == pins.end() ? std::array<double, 2>{0, 0} : pin->second;
          sys.rhs.back() -= u_coef * value[0] + v_coef * value[1];
        }
      }
    }
  }
  if (sys.rhs.empty()) return std::nullopt;
  sys.row_start.push_back(sys.row_col.size());
  sys.dof = 2 * free_base.size();
  sys.diag.assign(sys.dof, 0.0);
  for (std::size_t i = 0; i < sys.row_col.size(); ++i) sys.diag[sys.row_col[i]] += sys.row_val[i] * sys.row_val[i];
  sys.row_count = sys.rhs.size();
  return sys;
}

struct Solved {
  std::vector<double> x;
  bool converged{false};
};

std::optional<Solved> solve_system(const System& sys) {
  const std::size_t dof = sys.dof, row_count = sys.row_count;
  if (dof == 0 || row_count == 0) return std::nullopt;
  std::vector<double> row_vec(row_count);
  const auto mul_m = [&](const std::vector<double>& vec, std::vector<double>& out) {
    for (std::size_t row = 0; row < row_count; ++row) {
      double sum = 0;
      for (std::size_t k = sys.row_start[row]; k < sys.row_start[row + 1]; ++k) sum += sys.row_val[k] * vec[sys.row_col[k]];
      out[row] = sum;
    }
  };
  const auto mul_mt = [&](const std::vector<double>& vec, std::vector<double>& out) {
    std::fill(out.begin(), out.end(), 0.0);
    for (std::size_t row = 0; row < row_count; ++row) {
      const double value = vec[row];
      if (value == 0) continue;
      for (std::size_t k = sys.row_start[row]; k < sys.row_start[row + 1]; ++k) out[sys.row_col[k]] += sys.row_val[k] * value;
    }
  };
  std::vector<double> rhs_normal(dof);
  mul_mt(sys.rhs, rhs_normal);
  double base = 0;
  for (std::size_t i = 0; i < dof; ++i) base += rhs_normal[i] * rhs_normal[i];
  if (!std::isfinite(base)) return std::nullopt;
  double trace = 0;
  for (std::size_t i = 0; i < dof; ++i) trace += sys.diag[i];
  if (!(trace > 0) || !std::isfinite(trace)) return std::nullopt;
  const double lambda = 1e-12 * trace / static_cast<double>(dof);
  std::vector<double> x(dof, 0.0), r = rhs_normal, z(dof), p(dof), ap(dof), products(dof);
  const auto normal_product = [&](const std::vector<double>& vec, std::vector<double>& out) {
    mul_m(vec, row_vec);
    mul_mt(row_vec, out);
    for (std::size_t i = 0; i < dof; ++i) out[i] += lambda * vec[i];
  };
  const auto pre = [&](const std::vector<double>& vec, std::vector<double>& out) {
    for (std::size_t i = 0; i < dof; ++i) {
      const double d = sys.diag[i] + lambda;
      out[i] = d > 0 ? vec[i] / d : vec[i];
    }
  };
  normal_product(x, ap);
  for (std::size_t i = 0; i < dof; ++i) r[i] -= ap[i];
  pre(r, z);
  p = z;
  double rz = 0;
  for (std::size_t i = 0; i < dof; ++i) rz += r[i] * z[i];
  const double target = 1e-22 * base;
  for (int iter = 0; iter < 2000 && rz > target; ++iter) {
    normal_product(p, ap);
    double pap = 0;
    for (std::size_t i = 0; i < dof; ++i) pap += p[i] * ap[i];
    if (!(pap > 0) || !std::isfinite(pap)) return std::nullopt;
    const double alpha = rz / pap;
    for (std::size_t i = 0; i < dof; ++i) {
      x[i] += alpha * p[i];
      r[i] -= alpha * ap[i];
    }
    pre(r, z);
    double rz_next = 0;
    for (std::size_t i = 0; i < dof; ++i) rz_next += r[i] * z[i];
    const double beta = rz_next / rz;
    rz = rz_next;
    for (std::size_t i = 0; i < dof; ++i) p[i] = z[i] + beta * p[i];
    if (!std::isfinite(rz)) return std::nullopt;
    if (rz <= target) break;
  }
  for (std::size_t i = 0; i < dof; ++i) {
    if (!std::isfinite(x[i])) return std::nullopt;
  }
  normal_product(x, products);
  double res = 0;
  for (std::size_t i = 0; i < dof; ++i) {
    const double d = rhs_normal[i] - products[i];
    res += d * d;
  }
  const double scale = std::max(1.0, std::sqrt(base));
  return Solved{std::move(x), std::sqrt(res) <= 1e-9 * scale};
}

}  // namespace

UnwrapResult lscm_unwrap(Floats uvs, Floats verts, Indices tris, Indices ids, Indices selection, std::span<const Pin> pins) {
  UnwrapResult result;
  result.uvs.assign(uvs.begin(), uvs.end());
  auto& out = result.uvs;
  const std::size_t n = std::min({uvs.size() >> 1, verts.size() / 3, ids.size()});
  std::set<std::uint32_t> wanted;
  for (const std::uint32_t i : selection) {
    if (i < n) wanted.insert(ids[i]);
  }
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> members_of;
  for (std::size_t i = 0; i < n; ++i) {
    if (wanted.contains(ids[i])) members_of[ids[i]].push_back(static_cast<std::uint32_t>(i));
  }
  std::unordered_map<std::uint32_t, std::array<double, 2>> pin_at;
  for (const Pin& pin : pins) pin_at[pin.vertex] = {pin.u, pin.v};
  for (const std::uint32_t island : wanted) {
    const std::vector<std::uint32_t>& members = members_of[island];
    PinMap island_pins;
    for (const std::uint32_t v : members) {
      const auto pin = pin_at.find(v);
      if (pin != pin_at.end()) island_pins[v] = pin->second;
    }
    if (island_pins.size() < 2) {
      ++result.skipped;
      continue;
    }
    double spread = 0;
    const auto first = island_pins.begin()->second;
    for (auto it = std::next(island_pins.begin()); it != island_pins.end(); ++it) {
      spread = std::max(spread, std::hypot(it->second[0] - first[0], it->second[1] - first[1]));
    }
    if (!(spread > 1e-6)) {
      ++result.skipped;
      continue;
    }
    const auto regions = regions_of(verts, tris, members);
    std::unordered_map<std::size_t, int> pin_count;
    for (const auto& entry : island_pins) {
      const auto r = regions.find(entry.first);
      if (r != regions.end()) ++pin_count[r->second];
    }
    std::unordered_set<std::size_t> weak;
    for (const auto& entry : regions) {
      const auto found = pin_count.find(entry.second);
      if ((found == pin_count.end() ? 0 : found->second) < 2) weak.insert(entry.second);
    }
    std::vector<std::uint32_t> solvable;
    for (const std::uint32_t v : members) {
      const auto r = regions.find(v);
      if (r != regions.end() && !weak.contains(r->second)) solvable.push_back(v);
    }
    if (solvable.empty()) {
      ++result.skipped;
      continue;
    }
    result.skipped += static_cast<std::uint32_t>(weak.size());
    std::unordered_map<std::uint32_t, std::size_t> free_base;
    for (const std::uint32_t v : solvable) {
      if (!island_pins.contains(v)) free_base.emplace(v, 2 * free_base.size());
    }
    const auto sys = build_system(verts, tris, solvable, free_base, island_pins);
    if (!sys) {
      ++result.skipped;
      continue;
    }
    const auto solved = solve_system(*sys);
    if (!solved || !solved->converged) {
      ++result.skipped;
      continue;
    }
    bool wrote = false;
    for (const std::uint32_t v : solvable) {
      const auto pin = island_pins.find(v);
      const auto base = free_base.find(v);
      if (pin == island_pins.end() && (base == free_base.end() || (sys->diag[base->second] == 0 && sys->diag[base->second + 1] == 0))) continue;
      const double u = pin != island_pins.end() ? pin->second[0] : solved->x[base->second];
      const double w = pin != island_pins.end() ? pin->second[1] : solved->x[base->second + 1];
      if (!std::isfinite(u) || !std::isfinite(w)) continue;
      out[2 * v] = static_cast<float>(u);
      out[2 * v + 1] = static_cast<float>(w);
      wrote = true;
    }
    if (wrote) ++result.unwrapped;
    else ++result.skipped;
  }
  return result;
}

}  // namespace fo4::nif::uv
