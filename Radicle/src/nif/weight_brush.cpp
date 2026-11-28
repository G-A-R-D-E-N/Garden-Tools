#include "fo4/nif/weight_brush.hpp"

#include "skin_weights.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_slots = k_max_influences_per_vertex;
constexpr float k_same_weight = 1e-6f;
constexpr const char* k_unchanged = "the weights would not change";

bool is_locked(std::span<const std::uint8_t> locked, std::int32_t bone) {
  return std::any_of(locked.begin(), locked.end(),
                     [bone](std::uint8_t value) { return static_cast<std::int32_t>(value) == bone; });
}

void add_weight(std::vector<Influence>& list, std::int32_t bone, float weight) {
  for (auto& item : list) {
    if (item.bone == bone) {
      item.weight += weight;
      return;
    }
  }
  list.push_back({bone, weight});
}

std::vector<Influence> wide_of(const VertexWeights& vertex) {
  std::vector<Influence> list;
  for (std::size_t k = 0; k < k_slots; ++k) {
    if (vertex.weight[k] > 0.0f) add_weight(list, vertex.bone[k], vertex.weight[k]);
  }
  return list;
}

float weight_of(const VertexWeights& vertex, std::int32_t bone) {
  float total = 0.0f;
  for (std::size_t k = 0; k < k_slots; ++k) {
    if (vertex.weight[k] > 0.0f && vertex.bone[k] == bone) total += vertex.weight[k];
  }
  return total;
}

float largest_change(const VertexWeights& a, const VertexWeights& b) {
  float largest = 0.0f;
  for (std::size_t k = 0; k < k_slots; ++k) {
    if (a.weight[k] > 0.0f) largest = std::max(largest, std::fabs(a.weight[k] - weight_of(b, a.bone[k])));
    if (b.weight[k] > 0.0f) largest = std::max(largest, std::fabs(b.weight[k] - weight_of(a, b.bone[k])));
  }
  return largest;
}

float falloff_at(BrushFalloff kind, float t) {
  switch (kind) {
    case BrushFalloff::smoothstep:
      return t * t * (3.0f - 2.0f * t);
    case BrushFalloff::linear:
      return t;
    case BrushFalloff::constant:
      return 1.0f;
  }
  throw std::invalid_argument("the brush falloff is not known");
}

std::vector<std::vector<std::uint32_t>> neighbours_of(std::size_t count, std::span<const std::uint16_t> triangles) {
  if (triangles.size() % 3 != 0) throw std::invalid_argument("the triangle list is not a multiple of three");
  std::vector<std::vector<std::uint32_t>> around(count);
  for (std::size_t i = 0; i < triangles.size(); i += 3) {
    for (std::size_t k = 0; k < 3; ++k) {
      if (triangles[i + k] >= count) throw std::invalid_argument("a triangle names a vertex past the mesh");
    }
    for (std::size_t k = 0; k < 3; ++k) {
      const std::uint32_t a = triangles[i + k];
      const std::uint32_t b = triangles[i + (k + 1) % 3];
      around[a].push_back(b);
      around[b].push_back(a);
    }
  }
  for (auto& list : around) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
  }
  return around;
}

bool paint_vertex(std::vector<Influence>& list, const WeightBrush& brush, float effect,
                  std::span<const std::uint8_t> locked) {
  const auto bone = static_cast<std::int32_t>(brush.bone);
  if (is_locked(locked, bone)) return false;
  double locked_sum = 0.0;
  double others = 0.0;
  double current = 0.0;
  for (const auto& item : list) {
    if (item.bone == bone) {
      current = item.weight;
    } else if (is_locked(locked, item.bone)) {
      locked_sum += item.weight;
    } else {
      others += item.weight;
    }
  }
  const double room = std::max(0.0, 1.0 - locked_sum);
  double next = current;
  switch (brush.mode) {
    case BrushMode::add:
      next = std::min(current + effect, 1.0);
      break;
    case BrushMode::subtract:
      next = std::max(current - effect, 0.0);
      break;
    case BrushMode::replace:
      next = current + (static_cast<double>(brush.target) - current) * effect;
      break;
    case BrushMode::smooth:
      return false;
  }
  next = std::min(next, room);
  double factor = 1.0;
  if (others > 0.0) {
    factor = (room - next) / others;
  } else {
    if (!(next > 0.0)) return false;
    next = room;
  }
  std::vector<Influence> out;
  for (const auto& item : list) {
    if (item.bone == bone) continue;
    if (is_locked(locked, item.bone)) {
      out.push_back(item);
    } else {
      out.push_back({item.bone, static_cast<float>(item.weight * factor)});
    }
  }
  if (next > 0.0) out.push_back({bone, static_cast<float>(next)});
  list = std::move(out);
  return true;
}

bool smooth_vertex(std::vector<Influence>& list, std::span<const std::uint32_t> around,
                   const std::vector<VertexWeights>& snapshot, float effect, std::span<const std::uint8_t> locked) {
  if (around.empty()) return false;
  std::vector<Influence> mean;
  const float share = 1.0f / static_cast<float>(around.size());
  for (const auto neighbour : around) {
    const auto& other = snapshot[neighbour];
    for (std::size_t k = 0; k < k_slots; ++k) {
      if (other.weight[k] > 0.0f && !is_locked(locked, other.bone[k])) add_weight(mean, other.bone[k], other.weight[k] * share);
    }
  }
  std::vector<Influence> own;
  std::vector<Influence> out;
  double locked_sum = 0.0;
  for (const auto& item : list) {
    if (is_locked(locked, item.bone)) {
      out.push_back(item);
      locked_sum += item.weight;
    } else {
      own.push_back(item);
    }
  }
  std::vector<Influence> smoothed;
  std::vector<std::int32_t> bones;
  for (const auto& item : own) bones.push_back(item.bone);
  for (const auto& item : mean) bones.push_back(item.bone);
  std::sort(bones.begin(), bones.end());
  bones.erase(std::unique(bones.begin(), bones.end()), bones.end());
  for (const auto bone : bones) {
    float have = 0.0f;
    float want = 0.0f;
    for (const auto& item : own) {
      if (item.bone == bone) have = item.weight;
    }
    for (const auto& item : mean) {
      if (item.bone == bone) want = item.weight;
    }
    const float blended = have + (want - have) * effect;
    if (blended > 0.0f) smoothed.push_back({bone, blended});
  }
  double smoothed_total = 0.0;
  for (const auto& item : smoothed) smoothed_total += item.weight;
  if (!(smoothed_total > 0.0)) return false;
  const double room = std::max(0.0, 1.0 - locked_sum);
  const double factor = room / smoothed_total;
  for (const auto& item : smoothed) {
    const auto weight = static_cast<float>(item.weight * factor);
    if (weight > 0.0f) out.push_back({item.bone, weight});
  }
  list = std::move(out);
  return true;
}

void check_brush(const WeightBrush& brush) {
  if (!(brush.radius > 0.0f) || !std::isfinite(brush.radius)) {
    throw std::invalid_argument("the brush radius must be positive and finite");
  }
  for (const float value : brush.center) {
    if (!std::isfinite(value)) throw std::invalid_argument("the brush center must be finite");
  }
  if (!(brush.strength >= 0.0f) || !std::isfinite(brush.strength)) {
    throw std::invalid_argument("the brush strength must not be negative");
  }
  if (!(brush.target >= 0.0f && brush.target <= 1.0f)) {
    throw std::invalid_argument("the brush target must be between 0 and 1");
  }
  if (brush.mode != BrushMode::smooth && brush.bone > 255) {
    throw std::invalid_argument("the brush bone must be between 0 and 255");
  }
  if (brush.mode > BrushMode::smooth) throw std::invalid_argument("the brush mode is not known");
}

}  // namespace

VertexWeights pack_influences(std::span<const Influence> influences, std::span<const std::uint8_t> locked_bones) {
  std::vector<Influence> list;
  for (const auto& item : influences) {
    if (item.bone < 0 || item.bone > 255) throw std::invalid_argument("an influence names a bone outside 0 to 255");
    if (!std::isfinite(item.weight) || item.weight < 0.0f) {
      throw std::invalid_argument("an influence has an invalid weight");
    }
    if (item.weight > 0.0f) add_weight(list, item.bone, item.weight);
  }
  std::sort(list.begin(), list.end(), [&locked_bones](const Influence& a, const Influence& b) {
    const bool a_locked = is_locked(locked_bones, a.bone);
    const bool b_locked = is_locked(locked_bones, b.bone);
    if (a_locked != b_locked) return a_locked;
    if (a.weight != b.weight) return a.weight > b.weight;
    return a.bone < b.bone;
  });
  if (list.size() > k_slots) list.resize(k_slots);
  double locked_sum = 0.0;
  double free_sum = 0.0;
  for (const auto& item : list) (is_locked(locked_bones, item.bone) ? locked_sum : free_sum) += item.weight;
  if (free_sum > 0.0) {
    const double factor = std::max(0.0, 1.0 - locked_sum) / free_sum;
    for (auto& item : list) {
      if (!is_locked(locked_bones, item.bone)) item.weight = static_cast<float>(item.weight * factor);
    }
  }
  std::sort(list.begin(), list.end(), [](const Influence& a, const Influence& b) {
    if (a.weight != b.weight) return a.weight > b.weight;
    return a.bone < b.bone;
  });
  VertexWeights packed;
  std::size_t slot = 0;
  for (const auto& item : list) {
    if (!(item.weight > 0.0f)) continue;
    packed.bone[slot] = static_cast<std::uint8_t>(item.bone);
    packed.weight[slot] = item.weight;
    ++slot;
  }
  return packed;
}

WeightBrushReport apply_weight_brush(std::span<const Point> positions, std::span<const std::uint16_t> triangles,
                                     std::span<VertexWeights> vertices, const WeightBrush& brush,
                                     std::span<const std::uint8_t> locked_bones) {
  check_brush(brush);
  if (positions.size() != vertices.size()) {
    throw std::invalid_argument("the brush needs one position per vertex");
  }
  for (const auto& point : positions) {
    if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) {
      throw std::invalid_argument("the brush has a position that is not finite");
    }
  }
  for (std::size_t v = 0; v < vertices.size(); ++v) {
    for (std::size_t k = 0; k < k_slots; ++k) {
      const float weight = vertices[v].weight[k];
      if (!std::isfinite(weight) || weight < 0.0f) {
        throw std::invalid_argument("vertex " + std::to_string(v) + " has an invalid weight");
      }
    }
  }
  const bool smooth = brush.mode == BrushMode::smooth;
  std::vector<std::vector<std::uint32_t>> around;
  if (smooth) around = neighbours_of(vertices.size(), triangles);
  const std::vector<VertexWeights> snapshot(vertices.begin(), vertices.end());
  const float radius_sq = brush.radius * brush.radius;
  WeightBrushReport report;
  for (std::size_t v = 0; v < snapshot.size(); ++v) {
    const float dx = positions[v][0] - brush.center[0];
    const float dy = positions[v][1] - brush.center[1];
    const float dz = positions[v][2] - brush.center[2];
    const float dist_sq = dx * dx + dy * dy + dz * dz;
    if (!(dist_sq < radius_sq)) continue;
    const float t = 1.0f - std::sqrt(dist_sq) / brush.radius;
    const float effect = std::min(brush.strength * falloff_at(brush.falloff, t), 1.0f);
    if (!(effect > 0.0f)) continue;
    ++report.in_reach;
    auto list = wide_of(snapshot[v]);
    const bool edited = smooth ? smooth_vertex(list, around[v], snapshot, effect, locked_bones)
                               : paint_vertex(list, brush, effect, locked_bones);
    if (!edited) {
      ++report.skipped;
      continue;
    }
    const auto after = pack_influences(list, locked_bones);
    const float change = largest_change(snapshot[v], after);
    if (!(change > k_same_weight)) continue;
    vertices[v] = after;
    ++report.changed;
    report.max_weight_change = std::max(report.max_weight_change, change);
  }
  return report;
}

std::vector<VertexWeights> read_shape_weights(const NifFile& nif, std::uint32_t shape) {
  if (shape >= nif.blocks().size() || !is_shape_block_type(nif.blocks()[shape].type)) {
    throw std::invalid_argument("read_shape_weights names a block that is not a tri-shape");
  }
  const auto decoded = read_shape(nif, shape);
  if (decoded.verts.empty()) throw std::invalid_argument("the shape has no vertex data");
  if (!shape_detail::read_layout(nif, shape).skinned || decoded.skin_ref < 0) {
    throw std::invalid_argument("the shape has no skin");
  }
  if (decoded.bone_indices.size() != decoded.verts.size() / 3 * 4 || decoded.weights.size() != decoded.bone_indices.size()) {
    throw std::invalid_argument("the skin weights do not cover the vertices");
  }
  const auto skins = skin_detail::read_skins(decoded);
  std::vector<VertexWeights> vertices(skins.size());
  for (std::size_t v = 0; v < skins.size(); ++v) {
    vertices[v].bone = skins[v].bone;
    vertices[v].weight = skins[v].weight;
  }
  return vertices;
}

std::vector<std::byte> write_shape_weights(const NifFile& nif, std::uint32_t shape,
                                           std::span<const VertexWeights> weights) {
  const auto before = read_shape_weights(nif, shape);
  if (weights.size() != before.size()) throw std::invalid_argument("write_shape_weights needs one record per vertex");
  const auto decoded = read_shape(nif, shape);
  const auto layout = shape_detail::read_layout(nif, shape);
  const auto old_skins = skin_detail::read_skins(decoded);
  auto new_skins = old_skins;
  for (std::size_t v = 0; v < weights.size(); ++v) {
    new_skins[v].bone = weights[v].bone;
    new_skins[v].weight = weights[v].weight;
  }
  auto model = write_detail::load_model(nif);
  mesh_detail::Delta delta;
  if (const auto reason = skin_detail::write_skin_bytes(model.blocks[shape], layout, old_skins, new_skins, &delta)) {
    if (*reason == k_unchanged) return nif.bytes();
    throw std::invalid_argument(*reason);
  }
  return write_detail::serialize(model);
}

std::vector<std::byte> brush_shape_weights(const NifFile& nif, std::uint32_t shape, const WeightBrush& brush,
                                           std::span<const std::uint8_t> locked_bones, WeightBrushReport* report) {
  auto vertices = read_shape_weights(nif, shape);
  const auto decoded = read_shape(nif, shape);
  if (decoded.bones.empty()) throw std::invalid_argument("the skin has no resolvable bones");
  if (brush.mode != BrushMode::smooth && brush.bone >= decoded.bones.size()) {
    throw std::invalid_argument("the brush names a bone past the skin");
  }
  const auto points = skin_detail::global_vertex_points(nif, shape, decoded.verts);
  const auto result = apply_weight_brush(points, decoded.tris, vertices, brush, locked_bones);
  if (report != nullptr) *report = result;
  if (result.changed == 0) return nif.bytes();
  return write_shape_weights(nif, shape, vertices);
}

}  // namespace fo4::nif
