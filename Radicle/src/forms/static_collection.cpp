#include "fo4/forms/static_collection.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
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

}  // namespace

std::vector<StaticCollectionPartView> decode_static_collection_parts(
    const Plugin& plugin,
    const Record& record) {
  if (record.header.signature != k_scol) {
    throw StaticCollectionError("static collection reader requires a SCOL record");
  }

  const auto context = plugin_form_id_context(plugin);
  std::vector<StaticCollectionPartView> parts;
  std::optional<StaticCollectionPartView> current;

  const auto flush = [&]() mutable {
    if (!current.has_value()) return;
    if (!current->static_record.owner().empty() || current->static_record.local().value() != 0) {
      parts.push_back(std::move(*current));
    }
    current.reset();
  };

  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_onam) {
      flush();
      if (subrecord.data.size() != 4) {
        throw StaticCollectionError("SCOL part ONAM must be exactly 4 bytes");
      }
      current.emplace();
      current->static_record = resolve_plugin_form_id(
          RawFormId{read_u32(subrecord.data, 0)}, context);
      continue;
    }

    if (subrecord.signature != sig_data) continue;
    if (current.has_value() && !current->placements.empty()) {
      throw StaticCollectionError("SCOL part contains duplicate DATA placement arrays");
    }
    if (!current.has_value()) current.emplace();
    if (subrecord.data.size() % k_placement_size != 0) {
      throw StaticCollectionError("SCOL part DATA size is not a multiple of 28 bytes");
    }
    current->placements.reserve(subrecord.data.size() / k_placement_size);
    for (std::size_t offset = 0; offset < subrecord.data.size(); offset += k_placement_size) {
      current->placements.push_back(decode_placement(subrecord.data, offset));
    }
  }

  flush();
  return parts;
}

}  // namespace fo4::forms
