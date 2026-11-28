#include "fo4/graph/record_json.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {
namespace {

constexpr char k_hex_digits[] = "0123456789abcdef";

void append_json_string(std::string& out, std::string_view text) {
  const auto converted = text::bytes_to_utf8(text);
  out += '"';
  for (const char ch : converted) {
    const auto byte = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (byte < 0x20) {
          out += "\\u00";
          out += k_hex_digits[byte >> 4];
          out += k_hex_digits[byte & 0x0F];
        } else {
          out += ch;
        }
        break;
    }
  }
  out += '"';
}

std::string hex_bytes(std::span<const std::byte> data) {
  std::string out;
  out.reserve(data.size() * 2);
  for (const auto byte : data) {
    const auto value = static_cast<unsigned char>(byte);
    out += k_hex_digits[value >> 4];
    out += k_hex_digits[value & 0x0F];
  }
  return out;
}

std::string editor_id_text(const Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != sig_edid || sub.data.empty()) continue;
    const auto* begin = reinterpret_cast<const char*>(sub.data.data());
    const auto* end = begin + sub.data.size();
    const auto* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
  }
  return {};
}

std::string scalar_field_json(std::string_view name, std::string_view path, std::string_view value,
                              std::size_t slot) {
  std::string out = "{\"name\":";
  append_json_string(out, name);
  out += ",\"path\":";
  append_json_string(out, path);
  out += ",\"value\":";
  append_json_string(out, value);
  // Invariant: slot is the field's own occurrence index, counting the occurrences this export skipped.
  out += ",\"slot\":" + std::to_string(slot);
  out += '}';
  return out;
}

std::string localized_field_json(Signature record, Signature subrecord, const schema::LocalizedString& value) {
  std::string out = "{\"name\":";
  append_json_string(out, subrecord.to_string());
  out += ",\"path\":";
  append_json_string(out, record.to_string() + "\\" + subrecord.to_string());
  out += ",\"value\":";
  append_json_string(out, value.inline_text());
  if (value.id) out += ",\"stringId\":" + std::to_string(*value.id);
  out += '}';
  return out;
}

std::vector<std::vector<std::string>> scalar_rows(const Record& record) {
  std::vector<std::vector<std::string>> rows(record.subrecords.size());
  if (record.undecodable) return rows;
  const auto signature = record.header.signature;
  for (const auto& field : schema::scalar_fields(signature)) {
    if (schema::is_localized_field(signature, field.scalar.subrecord)) continue;
    const auto resolved =
        schema::resolve_field_slots(record, schema::scalar_field_shape(field.scalar), field.scalar.address);
    if (resolved.duplicate || resolved.malformed) continue;
    for (std::size_t slot_index = 0; slot_index < resolved.slots.size(); ++slot_index) {
      const auto& slot = resolved.slots[slot_index];
      if (!slot.valid) continue;
      const auto& payload = record.subrecords[slot.subrecord].data;
      try {
        const auto bytes = schema::scalar_slot_bytes(payload, slot, field.scalar);
        if (bytes.empty()) continue;
        const auto decoded = schema::decode_scalar(bytes, field.scalar);
        rows[slot.subrecord].push_back(
            scalar_field_json(field.scalar.field_name, field.path, decoded, slot_index));
      } catch (const schema::ScalarError&) {
        continue;
      }
    }
  }
  return rows;
}

}

std::string record_to_json(const Plugin& plugin, const FormKey& key, const RecordJsonOptions& options) {
  const auto* found = find_record(plugin, key);
  if (found == nullptr) throw std::out_of_range("record is not in plugin: " + key.to_string());
  const auto& record = *found;
  const auto rows = scalar_rows(record);

  std::string out = "{\"formKey\":";
  append_json_string(out, record.form_key.to_string());
  out += ",\"signature\":";
  append_json_string(out, record.header.signature.to_string());
  out += ",\"flags\":" + std::to_string(record.header.flags.value());
  out += ",\"editorId\":";
  append_json_string(out, editor_id_text(record));
  out += ",\"undecodable\":";
  out += record.undecodable ? "true" : "false";
  out += ",\"subrecords\":[";

  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto& sub = record.subrecords[index];
    if (index != 0) out += ',';
    out += "{\"sig\":";
    append_json_string(out, sub.signature.to_string());

    std::optional<schema::LocalizedString> localized;
    if (schema::is_localized_field(record.header.signature, sub.signature)) {
      try {
        localized = schema::read_localized_string(plugin, record.header.signature, sub.signature, sub);
      } catch (const std::exception&) {
        localized.reset();
      }
    }
    std::vector<std::string> fields = rows[index];
    if (localized) fields = {localized_field_json(record.header.signature, sub.signature, *localized)};

    out += ",\"fields\":[";
    for (std::size_t field = 0; field < fields.size(); ++field) {
      if (field != 0) out += ',';
      out += fields[field];
    }
    out += ']';
    if (fields.empty() && options.include_raw) {
      out += ",\"raw\":\"" + hex_bytes(sub.data) + "\"";
    }
    out += '}';
  }
  out += "]}";
  return out;
}

}
