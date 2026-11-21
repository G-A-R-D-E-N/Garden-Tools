#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fo4::hkx {

struct HkxMemberLayout {
  std::string_view name;
  std::uint32_t offset;
};

struct HkxClassLayout {
  std::string_view name;
  std::uint32_t signature;
  std::uint32_t size;
  std::span<const HkxMemberLayout> members;
};

const HkxClassLayout* find_layout(std::string_view class_name, std::uint32_t signature);
const HkxClassLayout* find_layout(std::string_view class_name);
std::optional<std::uint32_t> find_member_offset(const HkxClassLayout& layout, std::string_view member_name);

}  // namespace fo4::hkx
