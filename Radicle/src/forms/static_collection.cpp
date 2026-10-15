#include "fo4/forms/static_collection.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_scol = Signature::from_string("SCOL");
constexpr auto k_onam = Signature::from_string("ONAM");
constexpr std::size_t k_placement_size = 28;

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

float read_f32(const std::vector<std::byte>& data, std::size_t offset) {
  return std::bit_cast<float>(read_u32(data, offset));
}

StaticPlacementView decode_placement(const std::vector<std::byte>& data,
                                     std::size_t offset) {
  return {
      .x = read_f32(data, offset + 0),
      .y = read_f32(data, offset + 4),
      .z = read_f32(data, offset + 8),
      .rx = read_f32(data, offset + 12),
      .ry = read_f32(data, offset + 16),
      .rz = read_f32(data, offset + 20),
      .scale = read_f32(data, offset + 24),
  };
}

using fo4::detail::find_mutable_record;
using fo4::detail::mark_record_modified;

constexpr std::string_view k_static_part_targets[] = {
    "ACTI", "ALCH", "AMMO", "BOOK", "CONT", "DOOR", "FLOR",
    "FURN", "MISC", "MSTT", "STAT", "TERM", "WEAP"};

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_f32(std::vector<std::byte>& data, std::size_t offset, float value) {
  write_u32(data, offset, std::bit_cast<std::uint32_t>(value));
}

void write_placement(std::vector<std::byte>& data,
                     std::size_t offset,
                     const StaticPlacementView& placement) {
  const float fields[] = {placement.x,  placement.y,  placement.z,     placement.rx,
                          placement.ry, placement.rz, placement.scale};
  for (std::size_t index = 0; index < std::size(fields); ++index) {
    write_f32(data, offset + index * sizeof(float), fields[index]);
  }
}

Subrecord placements_subrecord(std::span<const StaticPlacementView> placements) {
  Subrecord subrecord;
  subrecord.signature = sig_data;
  subrecord.data.resize(placements.size() * k_placement_size);
  for (std::size_t index = 0; index < placements.size(); ++index) {
    write_placement(subrecord.data, index * k_placement_size, placements[index]);
  }
  return subrecord;
}

RawFormId encode_part_static(const FormKey& static_record, const PluginFormIdContext& context) {
  RawFormId raw;
  try {
    raw = encode_plugin_form_id(static_record, context);
  } catch (const FormIdError& error) {
    throw StaticCollectionError("cannot encode static collection part FormKey: " +
                                std::string(error.what()));
  }
  if (raw.value() == 0) {
    throw StaticCollectionError("static collection part cannot encode to a null form ID");
  }
  return raw;
}

Subrecord onam_subrecord(const FormKey& static_record, const PluginFormIdContext& context) {
  Subrecord subrecord;
  subrecord.signature = k_onam;
  subrecord.data.resize(4);
  write_u32(subrecord.data, 0, encode_part_static(static_record, context).value());
  return subrecord;
}

void validate_part_target(const Plugin& plugin, const FormKey& static_record) {
  if (static_record.owner() != plugin.key) return;
  const auto* target = find_record(plugin, static_record);
  if (target == nullptr) {
    throw StaticCollectionError("static collection part does not resolve to a record in this plugin: " +
                                static_record.to_string());
  }
  const auto target_type = target->header.signature.to_string();
  if (std::none_of(std::begin(k_static_part_targets), std::end(k_static_part_targets),
                   [&](std::string_view name) { return name == target_type; })) {
    throw StaticCollectionError(static_record.to_string() + " is " + target_type +
                                ", which SCOL parts cannot reference");
  }
}

struct PartSlot {
  std::size_t onam;
  bool has_data;
};

std::vector<PartSlot> part_slots(const Record& record) {
  std::vector<PartSlot> slots;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto signature = record.subrecords[index].signature;
    if (signature == k_onam) {
      const bool has_data = index + 1 < record.subrecords.size() &&
                            record.subrecords[index + 1].signature == sig_data;
      slots.push_back(PartSlot{index, has_data});
    } else if (signature == sig_data && (slots.empty() || slots.back().onam + 1 != index)) {
      throw StaticCollectionError("SCOL DATA must follow the ONAM of its part");
    }
  }
  return slots;
}

void require_scol(const Record& record) {
  if (record.header.signature != k_scol) {
    throw StaticCollectionError("static collection edit requires a SCOL record");
  }
}

void require_placements(std::span<const StaticPlacementView> placements) {
  if (placements.empty()) {
    throw StaticCollectionError("static collection part needs at least one placement");
  }
}

}  // namespace

std::vector<StaticCollectionPartView> decode_static_collection_parts(
    const Plugin& plugin,
    const Record& record) {
  if (record.header.signature != k_scol) {
    throw StaticCollectionError("static collection reader requires a SCOL record");
  }

  const auto context = plugin_form_id_context(plugin);
  std::vector<StaticCollectionPartView> parts;
  StaticCollectionPartView current;
  bool open = false;

  auto flush = [&]() {
    if (!open) return;
    if (!current.static_record.owner().empty() || current.static_record.local().value() != 0) {
      parts.push_back(std::move(current));
    }
    current = StaticCollectionPartView{};
    open = false;
  };

  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_onam) {
      flush();
      if (subrecord.data.size() != 4) {
        throw StaticCollectionError("SCOL part ONAM must be exactly 4 bytes");
      }
      open = true;
      current.static_record = resolve_plugin_form_id(
          RawFormId{read_u32(subrecord.data, 0)}, context);
      continue;
    }

    if (subrecord.signature != sig_data) continue;
    if (open && !current.placements.empty()) {
      throw StaticCollectionError("SCOL part contains duplicate DATA placement arrays");
    }
    open = true;
    if (subrecord.data.size() % k_placement_size != 0) {
      throw StaticCollectionError("SCOL part DATA size is not a multiple of 28 bytes");
    }
    current.placements.reserve(subrecord.data.size() / k_placement_size);
    for (std::size_t offset = 0; offset < subrecord.data.size(); offset += k_placement_size) {
      current.placements.push_back(decode_placement(subrecord.data, offset));
    }
  }

  flush();
  return parts;
}

bool add_static_collection_part(Plugin& plugin,
                                const FormKey& scol,
                                const FormKey& static_record,
                                std::span<const StaticPlacementView> placements) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, scol);
  if (record == nullptr) return false;
  require_scol(*record);
  require_placements(placements);
  validate_part_target(plugin, static_record);
  part_slots(*record);
  const auto context = plugin_form_id_context(plugin);
  record->subrecords.push_back(onam_subrecord(static_record, context));
  record->subrecords.push_back(placements_subrecord(placements));
  mark_record_modified(plugin, *record);
  return true;
}

bool remove_static_collection_part(Plugin& plugin, const FormKey& scol, std::size_t part_index) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, scol);
  if (record == nullptr) return false;
  require_scol(*record);
  const auto slots = part_slots(*record);
  if (part_index >= slots.size()) {
    throw StaticCollectionError("static collection part index is out of range");
  }
  if (slots.size() == 1) {
    throw StaticCollectionError("static collection must keep at least one part, Parts is required");
  }
  const auto& slot = slots[part_index];
  const auto first = record->subrecords.begin() + static_cast<std::ptrdiff_t>(slot.onam);
  record->subrecords.erase(first, first + (slot.has_data ? 2 : 1));
  mark_record_modified(plugin, *record);
  return true;
}

bool set_static_collection_part_placements(Plugin& plugin,
                                           const FormKey& scol,
                                           std::size_t part_index,
                                           std::span<const StaticPlacementView> placements) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, scol);
  if (record == nullptr) return false;
  require_scol(*record);
  const auto slots = part_slots(*record);
  if (part_index >= slots.size()) {
    throw StaticCollectionError("static collection part index is out of range");
  }
  require_placements(placements);
  const auto& slot = slots[part_index];
  const auto data = record->subrecords.begin() + static_cast<std::ptrdiff_t>(slot.onam + 1);
  if (slot.has_data) {
    *data = placements_subrecord(placements);
  } else {
    record->subrecords.insert(data, placements_subrecord(placements));
  }
  mark_record_modified(plugin, *record);
  return true;
}

}  // namespace fo4::forms
