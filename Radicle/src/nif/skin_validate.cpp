#include "fo4/nif/skin_validate.hpp"

#include "nif_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_slots = 4;
// Constraint: four half-float weights stay within this sum tolerance of one after rounding.
constexpr float k_sum_tolerance = 0.001f;
constexpr std::string_view k_bone_data_type = "BSSkin::BoneData";
constexpr std::size_t k_names_listed = 4;

std::string count_text(std::size_t count, std::string_view noun) {
  return std::to_string(count) + " " + std::string(noun);
}

// Requires the block's skin reference; returns the bind transform count, or 0 when the chain is broken.
std::size_t bind_transforms(const NifFile& nif, const Shape& shape) {
  if (shape.skin_ref < 0) return 0;
  const auto instance = static_cast<std::uint32_t>(shape.skin_ref);
  if (instance >= nif.blocks().size() || nif.blocks()[instance].type != "BSSkin::Instance") return 0;
  auto cursor = detail::block_cursor(nif, instance);
  cursor.read_i32();
  const auto data = cursor.read_i32();
  if (data < 0 || static_cast<std::size_t>(data) >= nif.blocks().size()) return 0;
  if (nif.blocks()[static_cast<std::size_t>(data)].type != k_bone_data_type) return 0;
  auto bones = detail::block_cursor(nif, static_cast<std::uint32_t>(data));
  return bones.read_u32();
}

std::string bone_names(const Shape& shape, const std::vector<bool>& used) {
  std::string names;
  std::size_t listed = 0;
  std::size_t total = 0;
  for (std::size_t bone = 0; bone < used.size(); ++bone) {
    if (used[bone]) continue;
    ++total;
    if (listed == k_names_listed) continue;
    names += (listed == 0 ? "" : ", ") + shape.bones[bone].name;
    ++listed;
  }
  if (total > listed) names += " and " + std::to_string(total - listed) + " more";
  return names;
}

}  // namespace

bool SkinReport::ok() const noexcept {
  return std::none_of(findings.begin(), findings.end(),
                      [](const SkinFinding& finding) { return finding.level == SkinFindingLevel::error; });
}

std::size_t SkinReport::errors() const noexcept {
  return static_cast<std::size_t>(std::count_if(findings.begin(), findings.end(),
                                                [](const SkinFinding& finding) {
                                                  return finding.level == SkinFindingLevel::error;
                                                }));
}

std::size_t SkinReport::warnings() const noexcept {
  return findings.size() - errors();
}

SkinReport validate_skin(const NifFile& nif, const Shape& shape) {
  SkinReport report;
  report.block = shape.block;
  report.bones = shape.bones.size();
  if (shape.bones.empty()) return report;
  report.bind_transforms = bind_transforms(nif, shape);

  // Constraint: read_shape refuses a bind count that disagrees with the bones, so a Shape only exists when they match.
  const auto vertices = shape.verts.size() / 3;
  if (shape.weights.size() != vertices * k_slots || shape.bone_indices.size() != vertices * k_slots) {
    report.findings.push_back({SkinFindingLevel::error, "skin",
                               "it names " + count_text(report.bones, "bone(s)") + " but holds " +
                                   count_text(shape.weights.size(), "weight(s)") + " for " +
                                   count_text(vertices, "vertices")});
    return report;
  }

  std::vector<bool> used(shape.bones.size(), false);
  std::size_t not_finite = 0;
  std::size_t negative = 0;
  std::size_t out_of_range = 0;
  std::size_t repeated = 0;
  std::size_t loose = 0;
  std::size_t off_sum = 0;
  float worst = 0.0f;

  for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
    float total = 0.0f;
    bool broken = false;
    const auto base = vertex * k_slots;
    for (std::size_t slot = 0; slot < k_slots; ++slot) {
      const float weight = shape.weights[base + slot];
      if (!std::isfinite(weight)) {
        ++not_finite;
        broken = true;
        continue;
      }
      if (weight < 0.0f) {
        ++negative;
        continue;
      }
      if (weight == 0.0f) continue;
      total += weight;
      const auto bone = shape.bone_indices[base + slot];
      if (static_cast<std::size_t>(bone) >= shape.bones.size()) {
        ++out_of_range;
        continue;
      }
      // Constraint: a bone used by two vertices is normal rigging, so a repeat means two slots of this vertex.
      bool duplicate = false;
      for (std::size_t earlier = 0; earlier < slot; ++earlier) {
        const std::size_t before = base + earlier;
        if (shape.weights[before] > 0.0f && shape.bone_indices[before] == bone) duplicate = true;
      }
      if (duplicate) {
        ++repeated;
        continue;
      }
      used[bone] = true;
    }
    if (broken) continue;
    // Constraint: vanilla leaves some vertices with no weight at all, so only a nonzero total can miss one.
    if (total <= 0.0f) {
      ++loose;
      continue;
    }
    const float gap = std::fabs(total - 1.0f);
    if (gap > k_sum_tolerance) {
      ++off_sum;
      worst = std::max(worst, gap);
    }
  }

  for (bool bone : used) report.used_bones += bone ? 1U : 0U;

  if (not_finite > 0) {
    report.findings.push_back({SkinFindingLevel::error, "vertices",
                               count_text(not_finite, "hold a weight that is not a finite number")});
  }
  if (negative > 0) {
    report.findings.push_back({SkinFindingLevel::error, "weights",
                               count_text(negative, "carry a weight below zero")});
  }
  if (out_of_range > 0) {
    report.findings.push_back({SkinFindingLevel::error, "weights",
                               count_text(out_of_range, "name a bone this shape does not have; it names ") +
                                   std::to_string(shape.bones.size())});
  }
  if (repeated > 0) {
    report.findings.push_back({SkinFindingLevel::error, "weights",
                               count_text(repeated, "put the same bone in two slots of one vertex")});
  }
  if (loose > 0) {
    report.findings.push_back({SkinFindingLevel::error, "vertices",
                               count_text(loose, "have no weight at all, so nothing moves them")});
  }
  if (off_sum > 0) {
    report.findings.push_back({SkinFindingLevel::warning, "vertices",
                               count_text(off_sum, "do not add up to one, the worst by ") + std::to_string(worst)});
  }
  if (report.used_bones < report.bones) {
    report.findings.push_back({SkinFindingLevel::warning, "bones",
                               count_text(report.bones - report.used_bones, "move no vertex of this shape: ") +
                                   bone_names(shape, used)});
  }
  return report;
}

}  // namespace fo4::nif
