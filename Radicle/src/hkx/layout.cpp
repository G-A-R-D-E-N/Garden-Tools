#include "fo4/hkx/layout.hpp"

#include <cstddef>
#include <vector>

namespace fo4::hkx {
namespace {

using K = HkxFieldKind;

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

std::vector<HkxMemberLayout> all_members(const HkxClassLayout& layout) {
  constexpr std::size_t kDepthCeiling = 16;
  std::vector<const HkxClassLayout*> chain{&layout};
  while (chain.size() < kDepthCeiling && !chain.back()->parent.empty()) {
    const HkxClassLayout* parent = find_layout(chain.back()->parent);
    if (parent == nullptr) break;
    chain.push_back(parent);
  }
  std::vector<HkxMemberLayout> members;
  for (auto link = chain.rbegin(); link != chain.rend(); ++link) {
    members.insert(members.end(), (*link)->members.begin(), (*link)->members.end());
  }
  return members;
}

std::string enum_value_name(const HkxEnumLayout& table, bool flags, std::int64_t value) {
  for (const HkxEnumValue& entry : table.values) {
    if (entry.value == value) return std::string(entry.name);
  }
  if (!flags || value <= 0) return {};
  std::string text;
  std::int64_t rest = value;
  for (const HkxEnumValue& entry : table.values) {
    if (entry.value <= 0 || (entry.value & rest) != entry.value) continue;
    if (!text.empty()) text.push_back('|');
    text += entry.name;
    rest &= ~entry.value;
  }
  return rest == 0 ? text : std::string{};
}

std::optional<std::uint32_t> find_member_offset(const HkxClassLayout& layout, std::string_view member_name) {
  for (const HkxMemberLayout& member : layout.members) {
    if (member.name == member_name) return member.offset;
  }
  return std::nullopt;
}

}  // namespace fo4::hkx
