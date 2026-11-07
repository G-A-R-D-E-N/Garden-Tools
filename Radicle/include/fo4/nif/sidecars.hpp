#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct SsfBoneDelta {
  std::string bone_name;
  std::vector<std::uint32_t> segment_ids;
  bool operator==(const SsfBoneDelta&) const = default;
};

struct SsfShape {
  std::string name;
  std::optional<std::string> base_bone;
  std::vector<SsfBoneDelta> deltas;
  std::string other_fields;
  bool operator==(const SsfShape&) const = default;
};

struct SclpBone {
  std::string name;
  float scale[3]{1.0f, 1.0f, 1.0f};
  bool operator==(const SclpBone&) const = default;
};

std::uint32_t bone_id_hash(std::string_view name);
std::vector<SsfShape> read_ssf(std::string_view text);
std::string write_ssf(const std::vector<SsfShape>& shapes);
std::vector<SclpBone> read_sclp(std::string_view text);
std::string write_sclp(const std::vector<SclpBone>& bones);

}  // namespace fo4::nif
