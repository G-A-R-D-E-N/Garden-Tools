#include "fo4/nif/weight_transfer.hpp"

#include "fo4/nif/shape_ops.hpp"
#include "import_mesh.hpp"
#include "shape_ops_internal.hpp"
#include "skin_weights.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_max_projection_pairs = 200000000;

using Vec = std::array<double, 3>;

Vec vec_of(const Point& p) { return {p[0], p[1], p[2]}; }
Vec minus(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec plus(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec times(const Vec& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec cross(const Vec& a, const Vec& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

struct Closest {
  Vec point;
  std::array<double, 3> bary;
};

Closest closest_on_triangle(const Vec& p, const Vec& a, const Vec& b, const Vec& c) {
  const Vec ab = minus(b, a);
  const Vec ac = minus(c, a);
  const Vec ap = minus(p, a);
  const double d1 = dot(ab, ap);
  const double d2 = dot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) return {a, {1.0, 0.0, 0.0}};
  const Vec bp = minus(p, b);
  const double d3 = dot(ab, bp);
  const double d4 = dot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) return {b, {0.0, 1.0, 0.0}};
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    const double v = d1 / (d1 - d3);
    return {plus(a, times(ab, v)), {1.0 - v, v, 0.0}};
  }
  const Vec cp = minus(p, c);
  const double d5 = dot(ab, cp);
  const double d6 = dot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) return {c, {0.0, 0.0, 1.0}};
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    const double w = d2 / (d2 - d6);
    return {plus(a, times(ac, w)), {1.0 - w, 0.0, w}};
  }
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return {plus(b, times(minus(c, b), w)), {0.0, 1.0 - w, w}};
  }
  const double denom = 1.0 / (va + vb + vc);
  const double v = vb * denom;
  const double w = vc * denom;
  return {plus(a, plus(times(ab, v), times(ac, w))), {1.0 - v - w, v, w}};
}

struct Triangle {
  std::array<std::uint32_t, 3> corner{};
  Vec lo{};
  Vec hi{};
  Vec face{};
  bool usable{false};
};

void check_finite(std::span<const Point> points, const char* what) {
  for (const auto& point : points) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) {
      throw std::invalid_argument(std::string(what) + " has a position that is not finite");
    }
  }
}

void check_distance(float max_distance) {
  if (!(max_distance >= 0.0f) || !std::isfinite(max_distance)) {
    throw std::invalid_argument("the maximum distance must be finite and not negative");
  }
}

double box_distance_sq(const Triangle& triangle, const Vec& p) {
  double sum = 0.0;
  for (std::size_t k = 0; k < 3; ++k) {
    const double gap = std::max({triangle.lo[k] - p[k], 0.0, p[k] - triangle.hi[k]});
    sum += gap * gap;
  }
  return sum;
}

std::vector<std::int32_t> nearest_vertices(std::span<const Point> targets, std::span<const Point> reference,
                                           std::vector<double>& distances) {
  if (targets.size() * reference.size() > k_max_projection_pairs) {
    throw std::invalid_argument("the projection would compare past " + std::to_string(k_max_projection_pairs) +
                                " pairs");
  }
  std::vector<std::int32_t> nearest(targets.size(), -1);
  distances.assign(targets.size(), 0.0);
  for (std::size_t t = 0; t < targets.size(); ++t) {
    const Vec p = vec_of(targets[t]);
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t r = 0; r < reference.size(); ++r) {
      const Vec offset = minus(p, vec_of(reference[r]));
      const double d = dot(offset, offset);
      if (d < best) {
        best = d;
        nearest[t] = static_cast<std::int32_t>(r);
      }
    }
    distances[t] = std::sqrt(best);
  }
  return nearest;
}

}  // namespace

std::vector<SurfaceHit> project_onto_surface(std::span<const Point> targets, std::span<const Point> positions,
                                             std::span<const std::uint16_t> triangles, float max_distance) {
  if (triangles.size() % 3 != 0) throw std::invalid_argument("the triangle list is not a multiple of three");
  const std::size_t count = triangles.size() / 3;
  if (count == 0) throw std::invalid_argument("the reference surface has no triangles");
  check_distance(max_distance);
  check_finite(targets, "the target");
  check_finite(positions, "the reference");
  if (targets.size() * count > k_max_projection_pairs) {
    throw std::invalid_argument("the projection would compare past " + std::to_string(k_max_projection_pairs) +
                                " pairs");
  }
  std::vector<Triangle> tris(count);
  std::vector<Vec> vertex_normal(positions.size(), Vec{0.0, 0.0, 0.0});
  for (std::size_t i = 0; i < count; ++i) {
    auto& tri = tris[i];
    std::array<Vec, 3> corner{};
    for (std::size_t k = 0; k < 3; ++k) {
      const auto index = triangles[3 * i + k];
      if (index >= positions.size()) throw std::invalid_argument("a triangle names a vertex past the reference");
      tri.corner[k] = index;
      corner[k] = vec_of(positions[index]);
    }
    const Vec normal = cross(minus(corner[1], corner[0]), minus(corner[2], corner[0]));
    const double area_sq = dot(normal, normal);
    tri.usable = area_sq > 0.0 && std::isfinite(area_sq);
    if (!tri.usable) continue;
    tri.face = times(normal, 1.0 / std::sqrt(area_sq));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      tri.lo[axis] = std::min({corner[0][axis], corner[1][axis], corner[2][axis]});
      tri.hi[axis] = std::max({corner[0][axis], corner[1][axis], corner[2][axis]});
    }
    for (const auto index : tri.corner) vertex_normal[index] = plus(vertex_normal[index], normal);
  }
  std::vector<SurfaceHit> hits(targets.size());
  for (std::size_t t = 0; t < targets.size(); ++t) {
    const Vec p = vec_of(targets[t]);
    double best = std::numeric_limits<double>::infinity();
    std::size_t best_tri = count;
    Closest best_hit{};
    for (std::size_t i = 0; i < count; ++i) {
      if (!tris[i].usable || !(box_distance_sq(tris[i], p) < best)) continue;
      const auto& corner = tris[i].corner;
      const auto hit = closest_on_triangle(p, vec_of(positions[corner[0]]), vec_of(positions[corner[1]]),
                                           vec_of(positions[corner[2]]));
      const Vec offset = minus(p, hit.point);
      const double d = dot(offset, offset);
      if (d < best) {
        best = d;
        best_tri = i;
        best_hit = hit;
      }
    }
    if (best_tri == count) continue;
    const double distance = std::sqrt(best);
    if (max_distance > 0.0f && distance > static_cast<double>(max_distance)) continue;
    const auto& corner = tris[best_tri].corner;
    Vec blended{0.0, 0.0, 0.0};
    for (std::size_t k = 0; k < 3; ++k) blended = plus(blended, times(vertex_normal[corner[k]], best_hit.bary[k]));
    const double length = std::sqrt(dot(blended, blended));
    const Vec normal = length > 0.0 && std::isfinite(length) ? times(blended, 1.0 / length) : tris[best_tri].face;
    auto& out = hits[t];
    out.triangle = static_cast<std::int32_t>(best_tri);
    for (std::size_t k = 0; k < 3; ++k) {
      out.bary[k] = static_cast<float>(best_hit.bary[k]);
      out.point[k] = static_cast<float>(best_hit.point[k]);
      out.normal[k] = static_cast<float>(normal[k]);
    }
    out.distance = static_cast<float>(distance);
    out.signed_distance = static_cast<float>(dot(minus(p, best_hit.point), normal));
  }
  return hits;
}

std::vector<std::int32_t> map_bones_by_name(std::span<const std::string> source, std::span<const std::string> target) {
  std::vector<std::int32_t> map(source.size(), -1);
  for (std::size_t s = 0; s < source.size(); ++s) {
    if (source[s].empty()) continue;
    std::size_t matches = 0;
    std::size_t index = 0;
    for (std::size_t t = 0; t < target.size(); ++t) {
      if (target[t] != source[s]) continue;
      ++matches;
      index = t;
    }
    if (matches == 1 && index <= 255) map[s] = static_cast<std::int32_t>(index);
  }
  return map;
}

WeightTransferReport transfer_weights(std::span<const Point> targets, std::span<VertexWeights> target_weights,
                                      std::span<const Point> reference, std::span<const std::uint16_t> triangles,
                                      std::span<const VertexWeights> reference_weights,
                                      std::span<const std::int32_t> bone_map, const WeightTransferOptions& options) {
  if (targets.size() != target_weights.size()) throw std::invalid_argument("the target needs one weight record per vertex");
  if (reference.size() != reference_weights.size()) {
    throw std::invalid_argument("the reference needs one weight record per vertex");
  }
  if (reference.empty()) throw std::invalid_argument("the reference has no vertices");
  if (options.method > TransferMethod::barycentric) throw std::invalid_argument("the transfer method is not known");
  check_distance(options.max_distance);
  for (const auto mapped : bone_map) {
    if (mapped > 255) throw std::invalid_argument("the bone map names a target bone past 255");
  }
  for (const auto& record : reference_weights) {
    for (const float weight : record.weight) {
      if (!std::isfinite(weight) || weight < 0.0f) throw std::invalid_argument("the reference has an invalid weight");
    }
  }
  const bool barycentric = options.method == TransferMethod::barycentric;
  std::vector<SurfaceHit> hits;
  std::vector<std::int32_t> nearest;
  std::vector<double> distances;
  if (barycentric) {
    hits = project_onto_surface(targets, reference, triangles, options.max_distance);
  } else {
    check_finite(targets, "the target");
    check_finite(reference, "the reference");
    nearest = nearest_vertices(targets, reference, distances);
  }
  WeightTransferReport report;
  for (std::size_t t = 0; t < targets.size(); ++t) {
    std::array<std::uint32_t, 3> corner{};
    std::array<double, 3> share{};
    std::size_t corners = 1;
    if (barycentric) {
      if (hits[t].triangle < 0) {
        ++report.out_of_reach;
        continue;
      }
      corners = 3;
      for (std::size_t k = 0; k < 3; ++k) {
        corner[k] = triangles[3 * static_cast<std::size_t>(hits[t].triangle) + k];
        share[k] = hits[t].bary[k];
      }
    } else {
      if (options.max_distance > 0.0f && distances[t] > static_cast<double>(options.max_distance)) {
        ++report.out_of_reach;
        continue;
      }
      corner[0] = static_cast<std::uint32_t>(nearest[t]);
      share[0] = 1.0;
    }
    std::vector<Influence> list;
    for (std::size_t k = 0; k < corners; ++k) {
      const auto& record = reference_weights[corner[k]];
      for (std::size_t slot = 0; slot < k_max_influences_per_vertex; ++slot) {
        if (!(record.weight[slot] > 0.0f)) continue;
        const auto source_bone = static_cast<std::size_t>(record.bone[slot]);
        const std::int32_t mapped = source_bone < bone_map.size() ? bone_map[source_bone] : -1;
        if (mapped < 0) continue;
        const auto weight = static_cast<float>(share[k] * record.weight[slot]);
        if (weight > 0.0f) list.push_back({mapped, weight});
      }
    }
    const auto packed = pack_influences(list);
    if (!(packed.weight[0] > 0.0f)) {
      ++report.unmapped;
      continue;
    }
    target_weights[t] = packed;
    ++report.transferred;
  }
  return report;
}

ConformReport conform_to_surface(std::span<Point> positions, std::span<const Point> reference,
                                 std::span<const std::uint16_t> triangles, const ConformOptions& options) {
  if (!std::isfinite(options.offset)) throw std::invalid_argument("the conform offset must be finite");
  if (options.mode > ConformMode::push_out) throw std::invalid_argument("the conform mode is not known");
  const auto hits = project_onto_surface(positions, reference, triangles, options.max_distance);
  ConformReport report;
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const auto& hit = hits[i];
    if (hit.triangle < 0) {
      ++report.out_of_reach;
      continue;
    }
    if (options.mode == ConformMode::push_out && hit.signed_distance >= options.offset) continue;
    const Point target{hit.point[0] + hit.normal[0] * options.offset, hit.point[1] + hit.normal[1] * options.offset,
                       hit.point[2] + hit.normal[2] * options.offset};
    const float dx = target[0] - positions[i][0];
    const float dy = target[1] - positions[i][1];
    const float dz = target[2] - positions[i][2];
    const float move = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(move > 0.0f)) continue;
    positions[i] = target;
    ++report.moved;
    report.max_move = std::max(report.max_move, move);
  }
  return report;
}

namespace {

std::vector<std::byte> transfer_between(const NifFile& source_nif, std::uint32_t source_shape,
                                        const NifFile& nif, std::uint32_t target_shape,
                                        const WeightTransferOptions& options, WeightTransferReport* report) {
  const auto source_weights = read_shape_weights(source_nif, source_shape);
  auto target_weights = read_shape_weights(nif, target_shape);
  const auto source = read_shape(source_nif, source_shape);
  const auto target = read_shape(nif, target_shape);
  if (source.bones.empty() || target.bones.empty()) throw std::invalid_argument("a skin has no resolvable bones");
  std::vector<std::string> source_names;
  std::vector<std::string> target_names;
  for (const auto& bone : source.bones) source_names.push_back(bone.name);
  for (const auto& bone : target.bones) target_names.push_back(bone.name);
  const auto map = map_bones_by_name(source_names, target_names);
  std::vector<bool> used(source.bones.size(), false);
  for (const auto& record : source_weights) {
    for (std::size_t slot = 0; slot < k_max_influences_per_vertex; ++slot) {
      if (!(record.weight[slot] > 0.0f)) continue;
      if (record.bone[slot] >= source.bones.size()) {
        throw std::runtime_error("the source names bone " + std::to_string(record.bone[slot]) +
                                 ", past its bone list");
      }
      used[record.bone[slot]] = true;
    }
  }
  std::string missing;
  for (std::size_t b = 0; b < used.size(); ++b) {
    if (!used[b] || map[b] >= 0) continue;
    missing += (missing.empty() ? "" : ", ") + (source_names[b].empty() ? "bone " + std::to_string(b) : source_names[b]);
  }
  if (!missing.empty()) throw std::runtime_error("the target skin has no single bone named " + missing);
  const auto source_points = skin_detail::global_vertex_points(source_nif, source_shape, source.verts);
  const auto target_points = skin_detail::global_vertex_points(nif, target_shape, target.verts);
  const auto result =
      transfer_weights(target_points, target_weights, source_points, source.tris, source_weights, map, options);
  if (report != nullptr) *report = result;
  if (result.transferred == 0) return nif.bytes();
  return write_shape_weights(nif, target_shape, target_weights);
}

}  // namespace

std::vector<std::byte> transfer_shape_weights(const NifFile& nif, std::uint32_t source_shape,
                                              std::uint32_t target_shape, const WeightTransferOptions& options,
                                              WeightTransferReport* report) {
  if (source_shape == target_shape) throw std::invalid_argument("the weight source is the target");
  return transfer_between(nif, source_shape, nif, target_shape, options, report);
}

std::vector<std::byte> transfer_shape_weights(std::span<const std::byte> source_nif, std::uint32_t source_shape,
                                              std::span<const std::byte> target_nif, std::uint32_t target_shape,
                                              const WeightTransferOptions& options, WeightTransferReport* report) {
  const auto source = read_nif(source_nif);
  const auto target = read_nif(target_nif);
  return transfer_between(source, source_shape, target, target_shape, options, report);
}

namespace shape_detail {

Bytes rewrite_positions(const NifFile& nif, std::uint32_t block, const Layout& layout, const std::vector<Vec3>& local) {
  const auto sphere = import_detail::bounding_sphere(local);
  for (const float term : sphere) {
    if (!std::isfinite(term)) throw std::runtime_error("the moved bounding sphere is not finite");
  }
  auto model = write_detail::load_model(nif);
  write_positions(model.blocks[block], layout, local);
  write_sphere(model.blocks[block], layout, sphere);
  auto written = write_detail::serialize(model);
  if (!layout.has_normal) return written;
  ShapeOpRequest request;
  request.op = ShapeOp::recalc_normals;
  request.blocks = {block};
  ShapeOpReport op_report;
  auto out = apply_shape_op(read_nif(written), request, &op_report);
  if (!op_report.skipped.empty()) {
    throw std::runtime_error("the moved shape keeps stale normals: " + op_report.skipped.front());
  }
  return out;
}

}  // namespace shape_detail

namespace {

std::vector<std::byte> conform_between(const NifFile& reference_nif, std::uint32_t reference_shape, const NifFile& nif,
                                       std::uint32_t target_shape, const ConformOptions& options,
                                       ConformReport* report) {
  const auto target = read_shape(nif, target_shape);
  const auto reference = read_shape(reference_nif, reference_shape);
  if (target.verts.empty()) throw std::invalid_argument("the conform target has no vertex data");
  if (reference.verts.empty() || reference.tris.empty()) {
    throw std::invalid_argument("the conform reference has no surface");
  }
  if (nif.blocks()[target_shape].type == "BSDynamicTriShape") {
    throw std::invalid_argument("BSDynamicTriShape keeps dynamic vertices that a conform would leave stale");
  }
  const auto layout = shape_detail::read_layout(nif, target_shape);
  if (!layout.full) throw std::invalid_argument("half-precision positions have no exact write-back");
  const auto xf = shape_to_global(nif, target_shape);
  if (!std::isfinite(xf.s) || !(xf.s > 0.0f)) throw std::invalid_argument("the target placement has no inverse");
  const auto global = skin_detail::global_vertex_points(nif, target_shape, target.verts);
  const auto reference_points = skin_detail::global_vertex_points(reference_nif, reference_shape, reference.verts);
  std::vector<Point> moved(global.begin(), global.end());
  const auto result = conform_to_surface(moved, reference_points, reference.tris, options);
  if (report != nullptr) *report = result;
  if (result.moved == 0) return nif.bytes();
  auto local = shape_detail::to_vec3(target.verts);
  const float inverse_scale = 1.0f / xf.s;
  for (std::size_t v = 0; v < local.size(); ++v) {
    if (moved[v] == global[v]) continue;
    const std::array<float, 3> d{moved[v][0] - xf.t[0], moved[v][1] - xf.t[1], moved[v][2] - xf.t[2]};
    for (std::size_t k = 0; k < 3; ++k) {
      local[v][k] = (xf.r[k] * d[0] + xf.r[3 + k] * d[1] + xf.r[6 + k] * d[2]) * inverse_scale;
      if (!std::isfinite(local[v][k])) throw std::runtime_error("a conformed vertex is not finite in the shape frame");
    }
  }
  return shape_detail::rewrite_positions(nif, target_shape, layout, local);
}

}  // namespace

std::vector<std::byte> conform_shape_to_surface(const NifFile& nif, std::uint32_t target_shape,
                                                std::uint32_t reference_shape, const ConformOptions& options,
                                                ConformReport* report) {
  if (target_shape == reference_shape) throw std::invalid_argument("the conform reference is the target");
  return conform_between(nif, reference_shape, nif, target_shape, options, report);
}

std::vector<std::byte> conform_shape_to_surface(std::span<const std::byte> reference_nif,
                                                std::uint32_t reference_shape, std::span<const std::byte> target_nif,
                                                std::uint32_t target_shape, const ConformOptions& options,
                                                ConformReport* report) {
  const auto reference = read_nif(reference_nif);
  const auto target = read_nif(target_nif);
  return conform_between(reference, reference_shape, target, target_shape, options, report);
}

}  // namespace fo4::nif
