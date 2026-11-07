#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/signature.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_refr = Signature::from_string("REFR");
constexpr auto k_achr = Signature::from_string("ACHR");
constexpr auto k_name = Signature::from_string("NAME");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_xscl = Signature::from_string("XSCL");

std::uint32_t read_u32_le(const std::vector<std::byte>& data, std::size_t offset) {
  if (offset > data.size() || data.size() - offset < 4) {
    throw PlacedReferenceError("placed-reference payload is truncated");
  }
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 2])) << 16) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 3])) << 24);
}

float read_f32_le(const std::vector<std::byte>& data, std::size_t offset) {
  return std::bit_cast<float>(read_u32_le(data, offset));
}

void write_u32_le(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  if (offset > data.size() || data.size() - offset < 4) {
    throw PlacedReferenceError("placed-reference DATA payload is truncated");
  }
  data[offset] = std::byte{static_cast<std::uint8_t>(value)};
  data[offset + 1] = std::byte{static_cast<std::uint8_t>(value >> 8)};
  data[offset + 2] = std::byte{static_cast<std::uint8_t>(value >> 16)};
  data[offset + 3] = std::byte{static_cast<std::uint8_t>(value >> 24)};
}

void write_f32_le(std::vector<std::byte>& data, std::size_t offset, float value) {
  write_u32_le(data, offset, std::bit_cast<std::uint32_t>(value));
}

const Subrecord* unique_subrecord(const Record& record, Signature signature) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw PlacedReferenceError(
          record.header.signature.to_string() + " contains duplicate " +
          signature.to_string() + " subrecords");
    }
    found = &subrecord;
  }
  return found;
}

Subrecord* unique_subrecord(Record& record, Signature signature) {
  Subrecord* found = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw PlacedReferenceError(
          record.header.signature.to_string() + " contains duplicate " +
          signature.to_string() + " subrecords");
    }
    found = &subrecord;
  }
  return found;
}

std::string read_zstring(const Subrecord* subrecord) {
  if (subrecord == nullptr || subrecord->data.empty()) return {};
  const auto* begin = reinterpret_cast<const char*>(subrecord->data.data());
  const auto* end = begin + subrecord->data.size();
  const auto* cursor = begin;
  while (cursor != end && *cursor != '\0') ++cursor;
  return std::string(begin, cursor);
}

PositionRotation read_transform(const Subrecord& data) {
  if (data.data.size() != 24) {
    throw PlacedReferenceError("placed reference DATA must be exactly 24 bytes");
  }
  return PositionRotation{
      .x = read_f32_le(data.data, 0),
      .y = read_f32_le(data.data, 4),
      .z = read_f32_le(data.data, 8),
      .rx = read_f32_le(data.data, 12),
      .ry = read_f32_le(data.data, 16),
      .rz = read_f32_le(data.data, 20),
  };
}

float read_scale(const Record& record) {
  const auto* scale = unique_subrecord(record, k_xscl);
  if (scale == nullptr) return 1.0f;
  if (scale->data.size() != 4) {
    throw PlacedReferenceError("placed reference XSCL must be exactly 4 bytes");
  }
  return read_f32_le(scale->data, 0);
}

std::optional<FormKey> read_base(const Plugin& plugin, const Record& record) {
  const auto* name = unique_subrecord(record, k_name);
  if (name == nullptr) return std::nullopt;
  if (name->data.size() != 4) {
    throw PlacedReferenceError("placed reference NAME must be exactly 4 bytes");
  }
  const RawFormId raw{read_u32_le(name->data, 0)};
  if (raw.is_null()) return std::nullopt;
  return resolve_plugin_form_id(raw, plugin_form_id_context(plugin));
}

const Group* find_cell_children_group(const Group& group, std::uint32_t label) {
  const Group* found = nullptr;
  const auto visit = [&](const auto& self, const Group& current) -> void {
    if (current.header.group_type == static_cast<std::int32_t>(GroupType::cell_children) &&
        current.header.label_as_u32() == label) {
      if (found != nullptr) {
        throw PlacedReferenceError("duplicate CELL child groups for one cell FormID");
      }
      found = &current;
    }
    for (const auto& child : current.children) {
      const auto* nested = std::get_if<Group>(&child);
      if (nested != nullptr) self(self, *nested);
    }
  };
  visit(visit, group);
  return found;
}

const Group* find_cell_children_group(const Plugin& plugin, const FormKey& cell) {
  const auto label = encode_plugin_form_id(cell, plugin_form_id_context(plugin)).value();
  const Group* found = nullptr;
  for (const auto& group : plugin.groups) {
    const auto* candidate = find_cell_children_group(group, label);
    if (candidate == nullptr) continue;
    if (found != nullptr) {
      throw PlacedReferenceError("duplicate CELL child groups for one cell FormID");
    }
    found = candidate;
  }
  return found;
}

void collect_reference_records(const Plugin& plugin,
                               const Group& group,
                               bool persistent,
                               std::vector<PlacedReferenceView>& out) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      auto decoded = decode_placed_reference(plugin, *record, persistent);
      if (decoded.has_value()) out.push_back(std::move(*decoded));
      continue;
    }
    collect_reference_records(plugin, std::get<Group>(child), persistent, out);
  }
}

}  // namespace

std::optional<PlacedReferenceView> decode_placed_reference(
    const Plugin& plugin,
    const Record& record,
    bool persistent) {
  if (!is_placed_reference_signature(record.header.signature)) return std::nullopt;
  const auto kind = record.header.signature == k_refr ? PlacedReferenceKind::object
                    : record.header.signature == k_achr ? PlacedReferenceKind::npc
                                                        : PlacedReferenceKind::variant;

  const auto deleted = record.header.flags.is_deleted();
  const auto* data = unique_subrecord(record, k_data);
  if (data == nullptr && !deleted) {
    throw PlacedReferenceError(
        record.header.signature.to_string() + " is missing required DATA position/rotation");
  }

  return PlacedReferenceView{
      .record = &record,
      .kind = kind,
      .base = read_base(plugin, record),
      .editor_id = read_zstring(unique_subrecord(record, sig_edid)),
      .transform = data != nullptr ? read_transform(*data) : PositionRotation{},
      .scale = read_scale(record),
      .persistent = persistent,
      .deleted = deleted,
  };
}

std::vector<PlacedReferenceView> placed_references_for_cell(
    const Plugin& plugin,
    const FormKey& cell) {
  const auto* children = find_cell_children_group(plugin, cell);
  if (children == nullptr) return {};

  std::vector<PlacedReferenceView> out;
  for (const auto& child : children->children) {
    const auto* group = std::get_if<Group>(&child);
    if (group == nullptr) continue;
    const auto type = static_cast<GroupType>(group->header.group_type);
    if (type == GroupType::cell_persistent_children) {
      collect_reference_records(plugin, *group, true, out);
    } else if (type == GroupType::cell_temporary_children) {
      collect_reference_records(plugin, *group, false, out);
    }
  }
  return out;
}

bool set_placed_reference_transform(
    Plugin& plugin,
    const FormKey& reference,
    const PositionRotation& transform) {
  auto next = plugin;
  assign_record_form_keys(next);

  Record* target = nullptr;
  visit_records(next, [&](Record& record) {
    if (target == nullptr && record.form_key == reference) target = &record;
  });
  if (target == nullptr) return false;
  if (!is_placed_reference_signature(target->header.signature)) {
    throw PlacedReferenceError("transform mutation requires a placed-reference record");
  }
  require_decodable(*target);
  if (!std::isfinite(transform.x) || !std::isfinite(transform.y) || !std::isfinite(transform.z) ||
      !std::isfinite(transform.rx) || !std::isfinite(transform.ry) || !std::isfinite(transform.rz)) {
    throw PlacedReferenceError("placed-reference values must be finite");
  }

  auto* data = unique_subrecord(*target, k_data);
  if (data == nullptr) {
    throw PlacedReferenceError("placed reference is missing required DATA position/rotation");
  }
  if (data->data.size() != 24) {
    throw PlacedReferenceError("placed reference DATA must be exactly 24 bytes");
  }

  write_f32_le(data->data, 0, transform.x);
  write_f32_le(data->data, 4, transform.y);
  write_f32_le(data->data, 8, transform.z);
  write_f32_le(data->data, 12, transform.rx);
  write_f32_le(data->data, 16, transform.ry);
  write_f32_le(data->data, 20, transform.rz);

  target->original_range.clear();
  target->decompressed_payload.clear();
  target->header.data_size = 0;
  target->uncompressed_size = 0;
  next.original_range.clear();
  plugin = std::move(next);
  return true;
}

}  // namespace fo4::forms
