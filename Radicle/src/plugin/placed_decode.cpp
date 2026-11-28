#include "fo4/plugin/placed_decode.hpp"

#include "fo4/binary/checked.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/signature.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using fo4::binary::checked::read_f32;
using fo4::binary::checked::read_u32;

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
  const auto* scale = unique_subrecord(record, sig_xscl);
  if (scale == nullptr) return 1.0f;
  if (scale->data.size() != 4) {
    throw PlacedReferenceError("placed reference XSCL must be exactly 4 bytes");
  }
  return read_f32_le(scale->data, 0);
}

std::optional<FormKey> read_base(const Plugin& plugin, const Record& record) {
  const auto* name = unique_subrecord(record, sig_name);
  if (name == nullptr) return std::nullopt;
  if (name->data.size() != 4) {
    throw PlacedReferenceError("placed reference NAME must be exactly 4 bytes");
  }
  const RawFormId raw{read_u32_le(name->data, 0)};
  if (raw.is_null()) return std::nullopt;
  return resolve_plugin_form_id(raw, plugin_form_id_context(plugin));
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

}  // namespace

namespace placed_decode {

std::optional<PlacedReferenceView> decode_placed_reference(
    const Plugin& plugin,
    const Record& record,
    bool persistent) {
  if (!is_placed_reference_signature(record.header.signature)) return std::nullopt;
  const auto kind = record.header.signature == sig_refr ? PlacedReferenceKind::object
                    : record.header.signature == sig_achr ? PlacedReferenceKind::npc
                                                        : PlacedReferenceKind::variant;

  const auto deleted = record.header.flags.is_deleted();
  const auto* data = unique_subrecord(record, sig_data);
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

std::vector<StaticCollectionPartView> decode_static_collection_parts(
    const Plugin& plugin,
    const Record& record) {
  if (record.header.signature != sig_scol) {
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
    if (subrecord.signature == sig_onam) {
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
    if (subrecord.data.size() % k_static_placement_size != 0) {
      throw StaticCollectionError("SCOL part DATA size is not a multiple of 28 bytes");
    }
    current.placements.reserve(subrecord.data.size() / k_static_placement_size);
    for (std::size_t offset = 0; offset < subrecord.data.size(); offset += k_static_placement_size) {
      current.placements.push_back(decode_placement(subrecord.data, offset));
    }
  }

  flush();
  return parts;
}

}  // namespace placed_decode
}  // namespace fo4
