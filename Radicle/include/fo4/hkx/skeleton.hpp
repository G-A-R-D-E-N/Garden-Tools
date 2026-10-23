#pragma once

#include "fo4/hkx/packfile.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

struct HkxQsTransform {
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{};
  std::array<float, 3> scale{};
};

struct HkxSkeleton {
  std::string name;
  std::vector<std::string> bone_names;
  std::vector<std::int16_t> parent_indices;
  std::vector<HkxQsTransform> reference_pose;
};

HkxStatus read_skeleton(const HkxPackfile& packfile, HkxSkeleton& out);

}  // namespace fo4::hkx
