#include "skin_weights.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace fo4::nif::skin_detail {
namespace {

using mesh_detail::Delta;
using shape_detail::Layout;
using shape_detail::Vec3;
using write_detail::Bytes;
using write_detail::half_bits;
using write_detail::write_int;

constexpr std::size_t k_influences = 4;
constexpr std::size_t k_skin_record_bytes = 12;
constexpr std::size_t k_index_offset = 8;

struct Influences {
  std::array<float, k_influences> weight{};
  std::array<std::uint8_t, k_influences> bone{};
};

using Skins = std::vector<Influences>;

Skins influences_of(const Shape& shape) {
  Skins skins(shape.weights.size() / k_influences);
  for (std::size_t v = 0; v < skins.size(); ++v) {
    for (std::size_t i = 0; i < k_influences; ++i) {
      skins[v].weight[i] = shape.weights[v * k_influences + i];
      skins[v].bone[i] = shape.bone_indices[v * k_influences + i];
    }
  }
  return skins;
}

float total(const Influences& skin) { return skin.weight[0] + skin.weight[1] + skin.weight[2] + skin.weight[3]; }

void rescale(Influences& skin) {
  const float sum = total(skin);
  if (!(sum > 0.0f)) return;
  for (auto& weight : skin.weight) weight /= sum;
}

void normalize(Skins& skins) {
  for (auto& skin : skins) rescale(skin);
}

bool limit(Skins& skins, std::size_t keep) {
  bool limited = false;
  for (auto& skin : skins) {
    const auto used = static_cast<std::size_t>(
        std::count_if(skin.weight.begin(), skin.weight.end(), [](float weight) { return weight != 0.0f; }));
    if (used <= keep) continue;
    std::array<std::size_t, k_influences> order{0, 1, 2, 3};
    std::stable_sort(order.begin(), order.end(),
                     [&skin](std::size_t a, std::size_t b) { return skin.weight[a] > skin.weight[b]; });
    for (std::size_t k = keep; k < k_influences; ++k) skin.weight[order[k]] = 0.0f;
    rescale(skin);
    limited = true;
  }
  return limited;
}

bool prune(Skins& skins, float threshold) {
  bool pruned = false;
  for (auto& skin : skins) {
    bool dropped = false;
    for (auto& weight : skin.weight) {
      if (weight != 0.0f && weight < threshold) {
        weight = 0.0f;
        dropped = true;
      }
    }
    if (!dropped) continue;
    rescale(skin);
    pruned = true;
  }
  return pruned;
}

std::vector<Vec3> global_points(const NifFile& nif, std::uint32_t block, const std::vector<float>& verts) {
  const auto xf = shape_to_global(nif, block);
  auto points = shape_detail::to_vec3(verts);
  for (auto& point : points) point = shape_detail::place(xf, point);
  return points;
}

std::optional<std::string> copy_from(const NifFile& nif, std::uint32_t source_block, std::uint32_t block,
                                     const Shape& target, Skins& skins) {
  if (source_block == block) return "the copy source is this shape";
  const auto source = read_shape(nif, source_block);
  if (source.verts.empty()) return "the copy source has no vertex data";
  if (!shape_detail::read_layout(nif, source_block).skinned || source.skin_ref < 0) {
    return "the copy source has no skin";
  }
  if (source.bones.empty()) return "the copy source skin has no bones";
  auto source_skins = influences_of(source);
  for (auto& skin : source_skins) {
    for (auto& weight : skin.weight) weight = std::max(weight, 0.0f);
  }

  std::vector<bool> used(source.bones.size(), false);
  for (const auto& skin : source_skins) {
    for (std::size_t i = 0; i < k_influences; ++i) {
      if (!(skin.weight[i] > 0.0f)) continue;
      const auto bone = static_cast<std::size_t>(skin.bone[i]);
      if (bone >= source.bones.size()) {
        return "the copy source names bone " + std::to_string(bone) + ", past its bone list";
      }
      used[bone] = true;
    }
  }
  std::vector<std::size_t> target_bone(source.bones.size(), 0);
  std::string missing;
  for (std::size_t b = 0; b < source.bones.size(); ++b) {
    const auto& name = source.bones[b].name;
    const auto found = std::find_if(target.bones.begin(), target.bones.end(),
                                    [&name](const Bone& bone) { return bone.name == name; });
    if (found != target.bones.end()) {
      target_bone[b] = static_cast<std::size_t>(found - target.bones.begin());
    } else if (used[b]) {
      missing += (missing.empty() ? "" : ", ") + name;
    }
  }
  if (!missing.empty()) return "the target skin lacks bone(s) " + missing;

  const auto source_points = global_points(nif, source_block, source.verts);
  const auto target_points = global_points(nif, block, target.verts);
  // ponytail: brute-force nearest search is O(n*m); requires a uniform grid over the source points for large shapes.
  for (std::size_t t = 0; t < skins.size(); ++t) {
    std::size_t nearest = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for (std::size_t s = 0; s < source_points.size(); ++s) {
      const auto offset = shape_detail::sub(target_points[t], source_points[s]);
      const auto distance = shape_detail::dot(offset, offset);
      if (distance < nearest_distance) {
        nearest = s;
        nearest_distance = distance;
      }
    }
    skins[t] = source_skins[nearest];
    for (std::size_t i = 0; i < k_influences; ++i) {
      skins[t].bone[i] = skins[t].weight[i] > 0.0f ? static_cast<std::uint8_t>(target_bone[skins[t].bone[i]])
                                                   : std::uint8_t{0};
    }
  }
  return std::nullopt;
}

std::optional<std::string> write_skins(Bytes& bytes, const Layout& layout, const Skins& before, const Skins& after,
                                       Delta* delta) {
  for (std::size_t v = 0; v < after.size(); ++v) {
    const auto finite = std::all_of(after[v].weight.begin(), after[v].weight.end(),
                                    [](float weight) { return std::isfinite(weight); });
    if (!finite) return "vertex " + std::to_string(v) + " has a non-finite weight";
    if (total(before[v]) != 0.0f && !(total(after[v]) > 0.0f)) {
      return "vertex " + std::to_string(v) + " would end with no positive bone weight";
    }
  }
  std::size_t changed = 0;
  float largest = 0.0f;
  for (std::size_t v = 0; v < after.size(); ++v) {
    if (total(before[v]) == 0.0f && total(after[v]) == 0.0f) continue;
    Bytes record(k_skin_record_bytes);
    for (std::size_t i = 0; i < k_influences; ++i) {
      largest = std::max(largest, std::fabs(after[v].weight[i] - before[v].weight[i]));
      write_int(record, 2 * i, half_bits(after[v].weight[i]));
      record[k_index_offset + i] = std::byte{after[v].bone[i]};
    }
    const auto at = layout.vertices_at + v * layout.stride + layout.skin_at;
    const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(at);
    if (std::equal(record.begin(), record.end(), first)) continue;
    std::copy(record.begin(), record.end(), first);
    ++changed;
  }
  if (changed == 0) return "the weights would not change";
  if (delta != nullptr) {
    delta->vertices_changed = changed;
    delta->max_weight_change = largest;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> apply_skin_op(const NifFile& nif, const ShapeOpRequest& request, std::uint32_t block,
                                         const Shape& shape, const Layout& layout, Bytes& bytes, Delta* delta) {
  if (!layout.skinned || shape.skin_ref < 0) return "no skin";
  if (shape.bones.empty()) return "the skin has no bones";
  const auto before = influences_of(shape);
  auto after = before;
  switch (request.op) {
    case ShapeOp::normalize_weights:
      normalize(after);
      break;
    case ShapeOp::limit_influences:
      if (!limit(after, request.max_influences)) {
        return "no vertex has more than " + std::to_string(request.max_influences) + " influences";
      }
      break;
    case ShapeOp::prune_weights:
      if (!prune(after, request.prune_threshold)) return "no weight is below the threshold";
      break;
    case ShapeOp::copy_weights:
      if (const auto reason = copy_from(nif, *request.source_block, block, shape, after)) return reason;
      break;
    default:
      return "not a skin weight operation";
  }
  return write_skins(bytes, layout, before, after, delta);
}

}  // namespace fo4::nif::skin_detail
