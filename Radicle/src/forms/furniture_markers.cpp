#include "fo4/forms/furniture_markers.hpp"
#include "authoring_position.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/schema/record_constraints.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

using fo4::forms::detail::authoring_position;
using fo4::binary::raw::read_u32;
using fo4::binary::raw::write_f32;
using fo4::binary::raw::write_u32;
using fo4::binary::raw::write_u8;

// Constraint: from form version 125 an element is 24 bytes (offsets, rotation, keyword, entry types, three unknown bytes); below it the entry types and unknown bytes are absent and the element is 20.
constexpr std::uint16_t k_current_form_version = 125;
constexpr std::size_t k_current_stride = 24;
constexpr std::size_t k_legacy_stride = 20;
constexpr std::size_t k_entry_types_offset = 20;
constexpr std::size_t k_slot_count = 22;
constexpr std::uint32_t k_slot_mask = (std::uint32_t{1} << k_slot_count) - 1;

struct Slot {
  bool unique{true};
  std::optional<std::size_t> at;
};

Slot locate(const Record& record, Signature signature) {
  Slot slot;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature != signature) continue;
    if (slot.at.has_value()) slot.unique = false;
    else slot.at = index;
  }
  return slot;
}

std::vector<std::byte> marker_payload(std::span<const FurnitureMarkerEdit> markers, std::size_t stride) {
  std::vector<std::byte> payload(markers.size() * stride, std::byte{0});
  for (std::size_t index = 0; index < markers.size(); ++index) {
    const auto base = index * stride;
    write_f32(payload, base, markers[index].offset_x);
    write_f32(payload, base + 4, markers[index].offset_y);
    write_f32(payload, base + 8, markers[index].offset_z);
    write_f32(payload, base + 12, markers[index].rotation_z);
    if (stride == k_current_stride) write_u8(payload, base + k_entry_types_offset, markers[index].entry_types);
  }
  return payload;
}

}  // namespace

FurnitureMarkersWrite set_furniture_markers(Plugin& plugin,
                                            const FormKey& record_key,
                                            std::span<const FurnitureMarkerEdit> markers) {
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return FurnitureMarkersWrite::record_missing;
  if (record->header.signature != sig_furn) return FurnitureMarkersWrite::unsupported_record;
  if (markers.size() > k_slot_count) return FurnitureMarkersWrite::too_many_markers;

  const auto snam = locate(*record, sig_snam);
  const auto mnam = locate(*record, sig_mnam);
  if (!snam.unique || !mnam.unique) return FurnitureMarkersWrite::unexpected_layout;
  if (mnam.at.has_value() && record->subrecords[*mnam.at].data.size() != 4) {
    return FurnitureMarkersWrite::unexpected_layout;
  }

  std::uint32_t word = mnam.at.has_value() ? read_u32(record->subrecords[*mnam.at].data, 0) : 0;
  word &= ~k_slot_mask;
  for (std::size_t index = 0; index < markers.size(); ++index) {
    if (markers[index].enabled) word |= std::uint32_t{1} << index;
  }

  Record edited = *record;
  auto& subrecords = edited.subrecords;
  if (mnam.at.has_value()) {
    write_u32(subrecords[*mnam.at].data, 0, word);
  } else if (word != 0) {
    const auto position = authoring_position(edited, sig_mnam);
    if (!position.has_value()) return FurnitureMarkersWrite::no_insertion_point;
    std::vector<std::byte> payload(4, std::byte{0});
    write_u32(payload, 0, word);
    subrecords.insert(subrecords.begin() + static_cast<std::ptrdiff_t>(*position),
                      Subrecord{sig_mnam, std::move(payload), false});
  }

  const auto current_snam = locate(edited, sig_snam);
  if (markers.empty()) {
    if (current_snam.at.has_value()) {
      subrecords.erase(subrecords.begin() + static_cast<std::ptrdiff_t>(*current_snam.at));
    }
  } else {
    const auto stride = record->header.form_version >= k_current_form_version ? k_current_stride : k_legacy_stride;
    auto payload = marker_payload(markers, stride);
    if (current_snam.at.has_value()) {
      subrecords[*current_snam.at].data = std::move(payload);
    } else {
      const auto position = authoring_position(edited, sig_snam);
      if (!position.has_value()) return FurnitureMarkersWrite::no_insertion_point;
      subrecords.insert(subrecords.begin() + static_cast<std::ptrdiff_t>(*position),
                        Subrecord{sig_snam, std::move(payload), false});
    }
  }

  *record = std::move(edited);
  fo4::detail::mark_record_modified(plugin, *record);
  return FurnitureMarkersWrite::written;
}

}  // namespace fo4::forms
