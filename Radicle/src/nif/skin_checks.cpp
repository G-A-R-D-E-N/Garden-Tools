#include "fo4/nif/skin_checks.hpp"

#include "nif_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_weights_per_vertex = 4;
// Constraint: each half-float weight rounds by at most 2^-12, so four exact weights stay within this sum tolerance.
constexpr float k_weight_sum_tolerance = 0.001f;
constexpr std::string_view k_bone_data_type = "BSSkin::BoneData";

std::string where(const Shape& shape) {
  return "shape '" + shape.name + "' (block " + std::to_string(shape.block) + ")";
}

std::string bind_fault(const NifFile& nif, const std::vector<Shape>& shapes) {
  for (const auto& shape : shapes) {
    if (shape.bones.empty()) continue;
    auto cursor = detail::block_cursor(nif, static_cast<std::uint32_t>(shape.skin_ref));
    cursor.read_i32();
    const auto data = cursor.read_i32();
    const bool present = data >= 0 && static_cast<std::size_t>(data) < nif.blocks().size() &&
                         nif.blocks()[static_cast<std::size_t>(data)].type == k_bone_data_type;
    if (!present) return where(shape) + " has no bind transforms: its skin holds no BSSkin::BoneData block";
  }
  return {};
}

std::string name_fault(const NifFile& nif, const std::vector<Shape>& shapes) {
  std::unordered_set<std::string> nodes;
  for (const auto& node : nif.nodes()) nodes.insert(node.name);
  for (const auto& shape : shapes) {
    for (const auto& bone : shape.bones) {
      if (bone.name.empty() || !nodes.contains(bone.name)) {
        return where(shape) + " bone '" + bone.name + "' does not name a node in this file";
      }
    }
  }
  return {};
}

std::string weight_fault(const std::vector<Shape>& shapes) {
  for (const auto& shape : shapes) {
    if (shape.bones.empty()) continue;
    for (std::size_t at = 0; at < shape.weights.size(); at += k_weights_per_vertex) {
      const auto vertex = std::to_string(at / k_weights_per_vertex);
      float total = 0.0f;
      for (std::size_t slot = 0; slot < k_weights_per_vertex; ++slot) {
        const float weight = shape.weights[at + slot];
        if (!std::isfinite(weight)) {
          return where(shape) + " vertex " + vertex + " has a weight that is not a finite number";
        }
        if (weight < 0.0f) return where(shape) + " vertex " + vertex + " has a negative weight";
        total += weight;
      }
      // Constraint: vanilla leaves some vertices with no weight at all, so an unweighted vertex is not a sum fault.
      if (total != 0.0f && std::fabs(total - 1.0f) > k_weight_sum_tolerance) {
        return where(shape) + " vertex " + vertex + " weights sum to " + std::to_string(total) + ", not 1";
      }
    }
  }
  return {};
}

}  // namespace

std::vector<VerifyCheck> skin_checks(const NifFile& nif, const std::vector<Shape>& shapes) {
  return {
      {"skin bind transforms are present", bind_fault(nif, shapes)},
      {"skin bone names resolve to nodes in the file", name_fault(nif, shapes)},
      {"skin weights are finite, non-negative and sum to one", weight_fault(shapes)},
  };
}

}  // namespace fo4::nif
