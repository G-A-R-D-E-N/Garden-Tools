#include "text_export_internal.hpp"

#include "fo4/schema/field_address.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace fo4::text_export {
namespace {

bool is_integral(schema::ScalarStorage storage) {
  return storage != schema::ScalarStorage::f32 && storage != schema::ScalarStorage::zstring &&
         storage != schema::ScalarStorage::fixed_string && storage != schema::ScalarStorage::bytes;
}

std::string named_value(const schema::ScalarDescriptor& scalar, std::string text) {
  if (scalar.names.empty() || !is_integral(scalar.storage)) return text;
  const auto value = std::stoll(text);
  if (scalar.presentation == schema::ScalarPresentation::enumeration) {
    const auto name = schema::scalar_value_name(scalar, value);
    return name ? std::string(*name) : text;
  }
  if (scalar.presentation != schema::ScalarPresentation::flags) return text;
  std::string out;
  std::int64_t remaining = value;
  for (const auto& entry : scalar.names) {
    if (entry.value == 0 || (value & entry.value) == 0) continue;
    if (!out.empty()) out += '|';
    out += std::string(entry.name);
    remaining &= ~entry.value;
  }
  if (out.empty()) return text;
  if (remaining != 0) out += '|' + std::to_string(remaining);
  return out;
}

std::string display_text(const schema::ScalarDescriptor& scalar, std::string value) {
  if (scalar.storage == schema::ScalarStorage::fixed_string) {
    const auto end = value.find_last_not_of('\0');
    value.erase(end == std::string::npos ? 0 : end + 1);
  }
  if (scalar.storage == schema::ScalarStorage::zstring || scalar.storage == schema::ScalarStorage::fixed_string) {
    return text::windows1252_to_utf8(value);
  }
  return named_value(scalar, std::move(value));
}

}  // namespace

Json schema_labels_json(const Record& record, bool localized) {
  Json labels = Json::array();
  for (const auto& field : schema::scalar_fields(record.header.signature)) {
    const auto& scalar = field.scalar;
    const auto resolved =
        schema::resolve_field_slots(record, schema::scalar_field_shape(scalar), scalar.address);
    if (resolved.duplicate || resolved.malformed || resolved.misaligned) continue;
    const auto slot = std::find_if(resolved.slots.begin(), resolved.slots.end(),
                                   [](const auto& candidate) { return candidate.valid; });
    if (slot == resolved.slots.end()) continue;
    const auto& data = record.subrecords[slot->subrecord].data;
    if (localized && scalar.storage == schema::ScalarStorage::zstring && data.size() == 4 &&
        (scalar.localized || schema::is_localized_field(record.header.signature, scalar.subrecord))) {
      continue;
    }
    std::string value;
    try {
      value = display_text(scalar, schema::decode_scalar(schema::scalar_slot_bytes(data, *slot, scalar), scalar));
    } catch (const schema::ScalarError&) {
      continue;
    }
    if (!text::is_valid_utf8(value)) continue;
    Json entry = Json::object();
    entry["subrecord"] = scalar.subrecord.to_string();
    entry["field"] = std::string(field.path);
    if (scalar.embedded) entry["offset"] = slot->offset;
    entry["value"] = std::move(value);
    labels.push_back(std::move(entry));
  }
  return labels;
}

}  // namespace fo4::text_export
