#include "fo4/nif/sclp_fit.hpp"

#include "fo4/nif/sidecars.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

constexpr float k_unit = 1.0f;

}  // namespace

FitScales fit_sclp_scales(const std::vector<FitBone>& bones, float epsilon) {
  if (!(epsilon > 0.0f) || !std::isfinite(epsilon)) {
    throw std::invalid_argument("fit_sclp_scales needs a positive finite epsilon");
  }
  FitScales result;
  std::set<std::string> seen;
  for (const auto& bone : bones) {
    if (bone.name.empty()) throw std::invalid_argument("fit_sclp_scales needs a name for every bone");
    if (!seen.insert(bone.name).second) {
      throw std::invalid_argument("fit_sclp_scales names bone \"" + bone.name + "\" twice");
    }
    SclpBone scaled;
    scaled.name = bone.name;
    bool scaled_any = false;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(bone.reference[axis]) || !std::isfinite(bone.fitted[axis])) {
        throw std::invalid_argument("bone \"" + bone.name + "\" has a non-finite offset");
      }
      if (std::fabs(bone.reference[axis]) <= epsilon) {
        scaled.scale[axis] = k_unit;
        continue;
      }
      const float ratio = bone.fitted[axis] / bone.reference[axis];
      if (!(ratio > 0.0f) || !std::isfinite(ratio)) {
        throw std::invalid_argument("bone \"" + bone.name + "\" has a ratio that is not a positive scale");
      }
      scaled.scale[axis] = ratio;
      if (ratio != k_unit) scaled_any = true;
    }
    if (!scaled_any) {
      result.skipped.push_back(bone.name + ": unchanged within the epsilon");
      continue;
    }
    result.bones.push_back(std::move(scaled));
  }
  return result;
}

}  // namespace fo4::nif
