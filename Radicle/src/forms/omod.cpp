#include "fo4/forms/omod.hpp"

#include "fo4/plugin/reader.hpp"
#include "../plugin/record_edit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_omod = Signature::from_string("OMOD");
constexpr auto k_data = Signature::from_string("DATA");

constexpr std::size_t k_header_size = 20;
constexpr std::size_t k_include_size = 7;
constexpr std::size_t k_property_size = 24;
constexpr std::size_t k_item_size = 8;

std::uint8_t read_u8(std::span<const std::byte> data, std::size_t offset) {
  return std::to_integer<std::uint8_t>(data[offset]);
}

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

float read_f32(std::span<const std::byte> data, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_u8(std::vector<std::byte>& data, std::size_t offset, std::uint8_t value) {
  data[offset] = static_cast<std::byte>(value);
}

void write_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_f32(std::vector<std::byte>& data, std::size_t offset, float value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::array<std::byte, 4> read_quad(std::span<const std::byte> data, std::size_t offset) {
  std::array<std::byte, 4> value{};
  std::memcpy(value.data(), data.data() + offset, value.size());
  return value;
}

std::uint32_t quad_u32(const std::array<std::byte, 4>& value) {
  std::uint32_t result = 0;
  std::memcpy(&result, value.data(), sizeof(result));
  return result;
}

float quad_f32(const std::array<std::byte, 4>& value) {
  float result = 0.0f;
  std::memcpy(&result, value.data(), sizeof(result));
  return result;
}

std::size_t read_count(std::span<const std::byte> data, std::size_t& cursor, std::size_t width) {
  if (cursor + sizeof(std::uint32_t) > data.size()) {
    throw OmodError("OMOD data ends before an array count");
  }
  const auto count = read_u32(data, cursor);
  cursor += sizeof(std::uint32_t);
  if (count > (data.size() - cursor) / width) {
    throw OmodError("OMOD data array count runs past the payload");
  }
  return count;
}

const Subrecord* find_data(const Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_data) return &subrecord;
  }
  return nullptr;
}

std::vector<std::byte> u32_payload(std::uint32_t value) {
  std::vector<std::byte> data(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
  return data;
}

}  // namespace

OmodData read_omod_data(const Plugin& plugin, const Record& record) {
  if (record.header.signature != k_omod) {
    throw OmodError("record does not carry object modification data: " +
                    record.header.signature.to_string());
  }
  const auto* subrecord = find_data(record);
  if (subrecord == nullptr) throw OmodError("OMOD record has no data subrecord");
  const auto& payload = subrecord->data;
  if (payload.size() < k_header_size) throw OmodError("OMOD data is shorter than its header");

  const auto context = plugin_form_id_context(plugin);
  OmodData data;
  const auto include_count = read_u32(payload, 0);
  const auto property_count = read_u32(payload, 4);
  data.unknown_bool1 = read_u8(payload, 8);
  data.unknown_bool2 = read_u8(payload, 9);
  data.form_type = read_u32(payload, 10);
  data.max_rank = read_u8(payload, 14);
  data.level_tier_scaled_offset = read_u8(payload, 15);
  data.attach_point = resolve_plugin_form_id(RawFormId{read_u32(payload, 16)}, context);

  std::size_t cursor = k_header_size;
  const auto slot_count = read_count(payload, cursor, 4);
  for (std::size_t slot = 0; slot < slot_count; ++slot) {
    data.attach_parent_slots.push_back(
        resolve_plugin_form_id(RawFormId{read_u32(payload, cursor)}, context));
    cursor += 4;
  }

  const auto item_count = read_count(payload, cursor, k_item_size);
  for (std::size_t item = 0; item < item_count; ++item) {
    OmodItem entry;
    entry.value1 = read_quad(payload, cursor);
    entry.value2 = read_quad(payload, cursor + 4);
    data.items.push_back(entry);
    cursor += k_item_size;
  }

  if (include_count > (payload.size() - cursor) / k_include_size) {
    throw OmodError("OMOD include count runs past the payload");
  }
  for (std::size_t include = 0; include < include_count; ++include) {
    OmodInclude entry;
    entry.mod = resolve_plugin_form_id(RawFormId{read_u32(payload, cursor)}, context);
    entry.minimum_level = read_u8(payload, cursor + 4);
    entry.optional = read_u8(payload, cursor + 5);
    entry.use_all = read_u8(payload, cursor + 6);
    data.includes.push_back(entry);
    cursor += k_include_size;
  }

  if (static_cast<std::size_t>(property_count) * k_property_size != payload.size() - cursor) {
    throw OmodError("OMOD property count does not match the payload length");
  }
  for (std::size_t row = 0; row < property_count; ++row) {
    OmodProperty property;
    property.value_type = read_u8(payload, cursor);
    property.function_type = read_u8(payload, cursor + 4);
    property.property = read_u16(payload, cursor + 8);
    property.value1 = read_quad(payload, cursor + 12);
    property.value2 = read_quad(payload, cursor + 16);
    property.step = read_f32(payload, cursor + 20);
    data.properties.push_back(property);
    cursor += k_property_size;
  }
  return data;
}

OmodDataWrite write_omod_data(Plugin& plugin, const FormKey& key, const OmodData& data) {
  assign_record_form_keys(plugin);
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  if (record == nullptr) return OmodDataWrite::record_missing;
  if (record->header.signature != k_omod) return OmodDataWrite::unsupported_record;
  Subrecord* target = nullptr;
  for (auto& subrecord : record->subrecords) {
    if (subrecord.signature == k_data) {
      target = &subrecord;
      break;
    }
  }
  if (target == nullptr) return OmodDataWrite::missing_data_subrecord;

  const auto context = plugin_form_id_context(plugin);
  std::vector<std::byte> payload(k_header_size, std::byte{0});
  write_u32(payload, 0, static_cast<std::uint32_t>(data.includes.size()));
  write_u32(payload, 4, static_cast<std::uint32_t>(data.properties.size()));
  write_u8(payload, 8, data.unknown_bool1);
  write_u8(payload, 9, data.unknown_bool2);
  write_u32(payload, 10, data.form_type);
  write_u8(payload, 14, data.max_rank);
  write_u8(payload, 15, data.level_tier_scaled_offset);
  write_u32(payload, 16, encode_plugin_form_id(data.attach_point, context).value());

  const auto slot_count = u32_payload(static_cast<std::uint32_t>(data.attach_parent_slots.size()));
  payload.insert(payload.end(), slot_count.begin(), slot_count.end());
  for (const auto& slot : data.attach_parent_slots) {
    const auto encoded = u32_payload(encode_plugin_form_id(slot, context).value());
    payload.insert(payload.end(), encoded.begin(), encoded.end());
  }

  const auto item_count = u32_payload(static_cast<std::uint32_t>(data.items.size()));
  payload.insert(payload.end(), item_count.begin(), item_count.end());
  for (const auto& item : data.items) {
    payload.insert(payload.end(), item.value1.begin(), item.value1.end());
    payload.insert(payload.end(), item.value2.begin(), item.value2.end());
  }

  for (const auto& include : data.includes) {
    std::vector<std::byte> encoded(k_include_size, std::byte{0});
    write_u32(encoded, 0, encode_plugin_form_id(include.mod, context).value());
    write_u8(encoded, 4, include.minimum_level);
    write_u8(encoded, 5, include.optional);
    write_u8(encoded, 6, include.use_all);
    payload.insert(payload.end(), encoded.begin(), encoded.end());
  }

  for (const auto& property : data.properties) {
    std::vector<std::byte> encoded(k_property_size, std::byte{0});
    write_u8(encoded, 0, property.value_type);
    write_u8(encoded, 4, property.function_type);
    write_u16(encoded, 8, property.property);
    std::memcpy(encoded.data() + 12, property.value1.data(), property.value1.size());
    std::memcpy(encoded.data() + 16, property.value2.data(), property.value2.size());
    write_f32(encoded, 20, property.step);
    payload.insert(payload.end(), encoded.begin(), encoded.end());
  }

  target->data = std::move(payload);
  fo4::detail::mark_record_modified(plugin, *record);
  return OmodDataWrite::written;
}

std::optional<FormKey> omod_property_value1_form(const Plugin& plugin,
                                                 const OmodProperty& property) {
  if (property.value_type != 4 && property.value_type != 6) return std::nullopt;
  return resolve_plugin_form_id(RawFormId{quad_u32(property.value1)},
                                plugin_form_id_context(plugin));
}

std::optional<float> omod_property_value1_float(const OmodProperty& property) {
  if (property.value_type != 1) return std::nullopt;
  return quad_f32(property.value1);
}

std::optional<std::uint32_t> omod_property_value1_int(const OmodProperty& property) {
  switch (property.value_type) {
    case 0:
    case 2:
    case 4:
    case 5:
      return quad_u32(property.value1);
    default:
      return std::nullopt;
  }
}

std::optional<float> omod_property_value2_float(const OmodProperty& property) {
  if (property.value_type != 1 && property.value_type != 6) return std::nullopt;
  return quad_f32(property.value2);
}

std::optional<std::uint32_t> omod_property_value2_int(const OmodProperty& property) {
  switch (property.value_type) {
    case 0:
    case 2:
    case 4:
      return quad_u32(property.value2);
    default:
      return std::nullopt;
  }
}

}  // namespace fo4::forms
