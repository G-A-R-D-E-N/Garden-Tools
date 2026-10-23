#include "fo4/hkx/layout.hpp"

namespace fo4::hkx {
namespace {

#include "hkx_layouts.generated.inc"

}  // namespace

const HkxClassLayout* find_layout(std::string_view class_name, std::uint32_t signature) {
  const HkxClassLayout* layout = find_layout(class_name);
  return layout != nullptr && layout->signature == signature ? layout : nullptr;
}

const HkxClassLayout* find_layout(std::string_view class_name) {
  for (const HkxClassLayout& layout : k_hkx_classes) {
    if (layout.name == class_name) return &layout;
  }
  return nullptr;
}

std::optional<std::uint32_t> find_member_offset(const HkxClassLayout& layout, std::string_view member_name) {
  for (const HkxMemberLayout& member : layout.members) {
    if (member.name == member_name) return member.offset;
  }
  return std::nullopt;
}

}  // namespace fo4::hkx
