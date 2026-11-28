#include "fo4/hkx/scene.hpp"

#include "fo4/binary/raw.hpp"
#include "fo4/hkx/layout.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

using fo4::binary::raw::read_i32;

constexpr std::array<std::string_view, 3> kSceneClasses{"hkxScene", "hkxEnvironment", "hkAlignSceneToNodeOptions"};
constexpr std::string_view kRootClass = "hkRootLevelContainer";
constexpr std::string_view kVariantClass = "hkRootLevelContainerNamedVariant";
constexpr std::int32_t kMaxVariants = 4096;
// Constraint: an hkArray header is the data pointer at +0 and the element count at +8.
constexpr std::size_t kArrayCountAt = 8;

std::string clean(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char value : text) {
    const auto byte = static_cast<std::uint8_t>(value);
    out.push_back(byte < 0x20U || byte >= 0x7FU ? '?' : value);
  }
  return out;
}

std::string hex_signature(std::uint32_t signature) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text{"0x"};
  for (int shift = 28; shift >= 0; shift -= 4) text.push_back(kDigits[(signature >> shift) & 0xFu]);
  return text;
}

// Requires local fixups sorted by source, which read_packfile guarantees.
std::optional<std::uint32_t> local_target(const HkxSection& section, std::uint32_t source) {
  const auto found = std::lower_bound(section.local_fixups.begin(), section.local_fixups.end(), source,
                                      [](const HkxLocalFixup& fixup, std::uint32_t value) { return fixup.source < value; });
  if (found == section.local_fixups.end() || found->source != source) return std::nullopt;
  return found->destination;
}

std::optional<std::uint32_t> global_target(const HkxSection& section, std::size_t target_section, std::uint32_t source) {
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.source == source && fixup.section == target_section) return fixup.destination;
  }
  return std::nullopt;
}

std::int32_t object_starting_at(const HkxPackfile& packfile, std::uint32_t offset) {
  const auto found = std::lower_bound(packfile.objects.begin(), packfile.objects.end(), offset,
                                      [](const HkxObjectView& object, std::uint32_t value) { return object.offset < value; });
  if (found == packfile.objects.end() || found->offset != offset) return -1;
  return static_cast<std::int32_t>(found - packfile.objects.begin());
}

std::string string_member(const HkxSection& data, std::uint32_t at, std::size_t index, std::string_view label,
                          std::vector<std::string>& diagnostics) {
  const auto target = local_target(data, at);
  if (!target) return {};
  const auto text = cstring_at(data.data, *target);
  if (!text) {
    diagnostics.push_back("variant " + std::to_string(index) + " " + std::string(label) +
                          " is not a terminated string");
    return {};
  }
  return clean(*text);
}

}  // namespace

bool is_scene_class(std::string_view class_name) {
  return std::find(kSceneClasses.begin(), kSceneClasses.end(), class_name) != kSceneClasses.end();
}

HkxStatus read_scene_structure(const HkxPackfile& packfile, HkxSceneStructure& out) {
  out = HkxSceneStructure{};
  if (packfile.sections.empty() || packfile.data_section >= packfile.sections.size()) return HkxStatus::MissingSection;
  const HkxSection& data = packfile.sections[packfile.data_section];
  out.object_count = packfile.objects.size();

  const HkxObjectView* root = nullptr;
  for (std::size_t index = 0; index < packfile.objects.size(); ++index) {
    const HkxObjectView& object = packfile.objects[index];
    const bool layout_known = find_layout(object.class_name, object.signature) != nullptr;
    if (!layout_known) {
      const std::string name = clean(object.class_name);
      if (std::find(out.unknown_classes.begin(), out.unknown_classes.end(), name) == out.unknown_classes.end()) {
        out.unknown_classes.push_back(name);
        out.diagnostics.push_back("no verified layout for " + name + " (signature " + hex_signature(object.signature) +
                                  "): kept as opaque bytes");
        if (is_scene_class(object.class_name))
          out.diagnostics.push_back("scene class " + name + ": units, up axis and environment variables are not decoded");
      }
    }
    if (is_scene_class(object.class_name)) {
      out.scene_objects.push_back({clean(object.class_name), index, object.signature, object.data.size(), layout_known});
    }
    if (root == nullptr && object.class_name == kRootClass && layout_known) root = &object;
  }

  if (root == nullptr) {
    out.diagnostics.push_back("no hkRootLevelContainer with a verified signature: asset structure not read");
    return HkxStatus::Ok;
  }
  out.has_root = true;

  const HkxClassLayout* container = find_layout(kRootClass);
  const HkxClassLayout* entry = find_layout(kVariantClass);
  if (container == nullptr || entry == nullptr) return HkxStatus::UnsupportedLayout;
  const auto array_at = find_member_offset(*container, "namedVariants");
  const auto name_at = find_member_offset(*entry, "name");
  const auto class_at = find_member_offset(*entry, "className");
  const auto variant_at = find_member_offset(*entry, "variant");
  if (!array_at || !name_at || !class_at || !variant_at) return HkxStatus::UnsupportedLayout;
  if (root->data.size() < static_cast<std::size_t>(*array_at) + kArrayCountAt + sizeof(std::int32_t)) {
    return HkxStatus::BadSection;
  }

  const std::int32_t count = read_i32(root->data, static_cast<std::size_t>(*array_at) + kArrayCountAt);
  if (count < 0 || count > kMaxVariants) return HkxStatus::BadSection;
  if (count == 0) return HkxStatus::Ok;
  const auto entries_at = local_target(data, root->offset + *array_at);
  if (!entries_at) return HkxStatus::BadSection;
  const std::uint64_t entries_end =
      static_cast<std::uint64_t>(*entries_at) + static_cast<std::uint64_t>(count) * entry->size;
  if (entries_end > data.data.size()) return HkxStatus::BadSection;

  for (std::int32_t slot = 0; slot < count; ++slot) {
    const auto base = static_cast<std::uint32_t>(*entries_at + static_cast<std::uint32_t>(slot) * entry->size);
    const auto index = static_cast<std::size_t>(slot);
    HkxSceneVariant variant;
    variant.name = string_member(data, base + *name_at, index, "name", out.diagnostics);
    variant.declared_class = string_member(data, base + *class_at, index, "class name", out.diagnostics);
    const auto target = global_target(data, packfile.data_section, base + *variant_at);
    if (!target) {
      out.diagnostics.push_back("variant " + std::to_string(index) + " (" + variant.name + ") has no object");
    } else {
      variant.object = object_starting_at(packfile, *target);
      if (variant.object < 0) {
        out.diagnostics.push_back("variant " + std::to_string(index) + " (" + variant.name +
                                  ") does not point at the start of an object");
      } else {
        const HkxObjectView& object = packfile.objects[static_cast<std::size_t>(variant.object)];
        variant.class_name = clean(object.class_name);
        variant.signature = object.signature;
        variant.layout_known = find_layout(object.class_name, object.signature) != nullptr;
        if (!variant.declared_class.empty() && variant.declared_class != variant.class_name) {
          out.diagnostics.push_back("variant " + std::to_string(index) + " (" + variant.name + ") declares " +
                                    variant.declared_class + " but holds " + variant.class_name);
        }
      }
    }
    out.variants.push_back(std::move(variant));
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
