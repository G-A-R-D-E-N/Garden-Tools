#pragma once

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fo4::hkx::detail {

template <typename T>
T value_at(std::span<const std::byte> bytes, std::size_t at) {
  T value{};
  std::memcpy(&value, bytes.data() + at, sizeof value);
  return value;
}

inline bool in_section(const HkxSection& section, std::size_t at, std::size_t width) {
  return at <= section.data.size() && width <= section.data.size() - at;
}

inline bool transform_is_finite(const HkxQsTransform& transform) {
  const auto finite = [](float value) { return std::isfinite(value); };
  return std::all_of(transform.translation.begin(), transform.translation.end(), finite) &&
         std::all_of(transform.rotation.begin(), transform.rotation.end(), finite) &&
         std::all_of(transform.scale.begin(), transform.scale.end(), finite);
}

// Requires the source-ordered table read_packfile sorts and de-duplicates, so one probe is enough.
inline std::optional<std::size_t> local_target(const HkxSection& section, std::size_t source) {
  const auto found = std::lower_bound(
      section.local_fixups.begin(), section.local_fixups.end(), source,
      [](const HkxLocalFixup& fixup, std::size_t value) { return fixup.source < value; });
  if (found == section.local_fixups.end() || found->source != source) return std::nullopt;
  return found->destination;
}

// Requires the global fixup form too: a pointer to another object is a global fixup, not a local one.
inline std::optional<std::size_t> pointer_target(const HkxPackfile& packfile, const HkxSection& section,
                                                 std::size_t source) {
  if (const auto local = local_target(section, source)) return local;
  for (const HkxRemoteFixup& fixup : section.global_fixups) {
    if (fixup.source != source) continue;
    if (fixup.section != packfile.data_section) return std::nullopt;
    if (!in_section(section, fixup.destination, 1)) return std::nullopt;
    return static_cast<std::size_t>(fixup.destination);
  }
  return std::nullopt;
}

struct Elements {
  std::size_t first{0};
  std::size_t count{0};
};

// Requires the element count at field + pointer_size; a zero count needs no fixup and no room.
inline HkxStatus elements_at(const HkxSection& section, std::size_t field, std::size_t pointer_size,
                             std::size_t element_size, Elements& out) {
  if (!in_section(section, field, pointer_size + 4)) return HkxStatus::FixupOutOfRange;
  const auto count = value_at<std::int32_t>(section.data, field + pointer_size);
  if (count < 0) return HkxStatus::BadSection;
  out.count = static_cast<std::size_t>(count);
  if (out.count == 0) return HkxStatus::Ok;
  const auto target = local_target(section, field);
  if (!target || !in_section(section, *target, out.count * element_size)) return HkxStatus::FixupOutOfRange;
  out.first = *target;
  return HkxStatus::Ok;
}

// Requires the object view to hold the whole layout: a following object that starts inside it cuts the view short.
inline bool holds_layout(const HkxObjectView& object, const HkxClassLayout& layout) {
  return object.data.size() >= layout.size;
}

// Offset of one member of an object, or nullopt when the layout lacks it or the object view cannot hold it.
inline std::optional<std::size_t> member_field(const HkxSection& section, const HkxClassLayout& layout,
                                               const HkxObjectView& object, std::string_view member) {
  const auto offset = find_member_offset(layout, member);
  if (!offset || !holds_layout(object, layout)) return std::nullopt;
  const std::size_t at = object.offset + static_cast<std::size_t>(*offset);
  if (!in_section(section, at, 4)) return std::nullopt;
  return at;
}

inline HkxStatus read_string(const HkxSection& section, std::size_t at, std::string& out) {
  const auto text = cstring_at(section.data, at);
  if (!text) return HkxStatus::FixupOutOfRange;
  out.assign(text->begin(), text->end());
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx::detail
