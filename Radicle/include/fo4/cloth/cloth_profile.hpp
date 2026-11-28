#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::cloth {

struct BoneScale {
  double x{1.0};
  double y{1.0};
  double z{1.0};

  friend bool operator==(const BoneScale&, const BoneScale&) = default;
};

struct BoneEntry {
  std::string name;
  BoneScale scale;

  friend bool operator==(const BoneEntry&, const BoneEntry&) = default;
};

struct ClothProfile {
  std::vector<BoneEntry> bones;

  friend bool operator==(const ClothProfile&, const ClothProfile&) = default;
};

std::string cloth_profile_path(std::string_view outfit_nif);

ClothProfile parse_cloth_profile(std::string_view json);

std::string serialize_cloth_profile(const ClothProfile& profile);

struct ClothBonePairing {
  std::vector<std::string> profile_only;
  std::vector<std::string> mesh_only;

  friend bool operator==(const ClothBonePairing&, const ClothBonePairing&) = default;
};

ClothBonePairing compare_cloth_bones(const ClothProfile& profile, std::span<const std::string> mesh_bones);

}  // namespace fo4::cloth
