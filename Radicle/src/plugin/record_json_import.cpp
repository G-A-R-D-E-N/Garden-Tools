#include "fo4/plugin/record_json_import.hpp"

#include "fo4/plugin/record_edit.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using Json = nlohmann::json;

struct MalformedRecordJson : std::runtime_error {
  using std::runtime_error::runtime_error;
};

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

std::optional<std::vector<std::byte>> decode_hex(const std::string& text) {
  if (text.size() % 2 != 0) return std::nullopt;
  std::vector<std::byte> bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t index = 0; index < text.size(); index += 2) {
    const int high = hex_value(text[index]);
    const int low = hex_value(text[index + 1]);
    if (high < 0 || low < 0) return std::nullopt;
    bytes.push_back(static_cast<std::byte>((high << 4) | low));
  }
  return bytes;
}

std::string subrecord_text(const Json& sub) {
  auto signature = sub.at("sig").get<std::string>();
  if (signature.size() != 4) throw MalformedRecordJson("subrecord signature must be four characters");
  return signature;
}

const schema::ScalarFieldDescriptor* find_scalar_field(Signature record,
                                                       const std::string& path,
                                                       const std::string& name) {
  for (const auto& field : schema::scalar_fields(record)) {
    if (field.path == path && field.scalar.field_name == name) return &field;
  }
  return nullptr;
}

RecordJsonEntryStatus apply_scalar_value(Plugin& plugin,
                                         const FormKey& key,
                                         Signature record,
                                         const std::string& path,
                                         const std::string& name,
                                         const std::string& value,
                                         std::optional<std::size_t> slot,
                                         std::map<std::string, std::size_t>& occurrences) {
  const auto* field = find_scalar_field(record, path, name);
  if (field == nullptr) return RecordJsonEntryStatus::unknown_path;
  // Constraint: the export skips occurrences it cannot decode, so its slot wins over counting rows.
  auto& counted = occurrences[path + "|" + name];
  const auto index = slot.value_or(counted);
  counted = index + 1;
  const auto storage = field->scalar.storage;
  const bool is_string =
      storage == schema::ScalarStorage::zstring || storage == schema::ScalarStorage::fixed_string;
  try {
    const auto status = is_string ? set_record_string(plugin, key, *field, index, value)
                                  : set_record_scalar(plugin, key, *field, index, value);
    switch (status) {
      case ScalarMutationStatus::updated:
        return RecordJsonEntryStatus::applied;
      case ScalarMutationStatus::unchanged:
        return RecordJsonEntryStatus::unchanged;
      case ScalarMutationStatus::next_alias_id_too_low:
        return RecordJsonEntryStatus::invalid_value;
      case ScalarMutationStatus::record_missing:
      case ScalarMutationStatus::subrecord_missing:
        return RecordJsonEntryStatus::refused;
    }
  } catch (const schema::ScalarError&) {
    return RecordJsonEntryStatus::invalid_value;
  } catch (const std::exception&) {
    return RecordJsonEntryStatus::refused;
  }
  return RecordJsonEntryStatus::refused;
}

RecordJsonEntryStatus apply_localized_value(const Plugin& plugin,
                                            const FormKey& key,
                                            Signature record,
                                            Signature subrecord,
                                            std::size_t index,
                                            const std::string& value) {
  const auto* found = find_record(plugin, key);
  if (found == nullptr || index >= found->subrecords.size() ||
      found->subrecords[index].signature != subrecord) {
    return RecordJsonEntryStatus::refused;
  }
  try {
    const auto current = text::bytes_to_utf8(
        schema::read_localized_string(plugin, record, subrecord, found->subrecords[index]).inline_text());
    return current == value ? RecordJsonEntryStatus::unchanged : RecordJsonEntryStatus::invalid_value;
  } catch (const std::exception&) {
    return RecordJsonEntryStatus::refused;
  }
}

RecordJsonEntryStatus apply_raw(Plugin& plugin,
                                const FormKey& key,
                                Signature subrecord,
                                std::size_t index,
                                const std::string& hex,
                                bool allow_raw) {
  const auto* found = find_record(plugin, key);
  if (found == nullptr || index >= found->subrecords.size() ||
      found->subrecords[index].signature != subrecord) {
    return RecordJsonEntryStatus::refused;
  }
  const auto bytes = decode_hex(hex);
  if (!bytes) return RecordJsonEntryStatus::invalid_value;
  if (*bytes == found->subrecords[index].data) return RecordJsonEntryStatus::unchanged;
  if (!allow_raw) return RecordJsonEntryStatus::refused;
  auto* target = detail::locate_mutable_record(plugin, key);
  if (target == nullptr || index >= target->subrecords.size()) return RecordJsonEntryStatus::refused;
  target->subrecords[index].data = *bytes;
  detail::mark_record_modified(plugin, *target);
  return RecordJsonEntryStatus::applied;
}

}

std::string_view to_string(RecordJsonEntryStatus status) noexcept {
  switch (status) {
    case RecordJsonEntryStatus::applied:
      return "applied";
    case RecordJsonEntryStatus::unchanged:
      return "unchanged";
    case RecordJsonEntryStatus::unknown_path:
      return "unknown_path";
    case RecordJsonEntryStatus::invalid_value:
      return "invalid_value";
    case RecordJsonEntryStatus::refused:
      return "refused";
  }
  return "refused";
}

std::string_view to_string(RecordJsonImportStatus status) noexcept {
  switch (status) {
    case RecordJsonImportStatus::committed:
      return "committed";
    case RecordJsonImportStatus::refused:
      return "refused";
    case RecordJsonImportStatus::malformed_json:
      return "malformed_json";
    case RecordJsonImportStatus::form_key_mismatch:
      return "form_key_mismatch";
    case RecordJsonImportStatus::signature_mismatch:
      return "signature_mismatch";
    case RecordJsonImportStatus::record_missing:
      return "record_missing";
    case RecordJsonImportStatus::record_undecodable:
      return "record_undecodable";
  }
  return "refused";
}

RecordJsonImportResult record_apply_json(Plugin& plugin,
                                         const FormKey& key,
                                         std::string_view json_text,
                                         const RecordJsonImportOptions& options) {
  RecordJsonImportResult result;
  Json document;
  try {
    document = Json::parse(json_text.begin(), json_text.end());
  } catch (const Json::exception&) {
    result.status = RecordJsonImportStatus::malformed_json;
    return result;
  }

  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    result.status = RecordJsonImportStatus::record_missing;
    return result;
  }
  if (record->undecodable) {
    result.status = RecordJsonImportStatus::record_undecodable;
    return result;
  }
  const auto signature = record->header.signature;

  std::string json_key;
  std::string json_signature;
  try {
    json_key = document.at("formKey").get<std::string>();
    json_signature = document.at("signature").get<std::string>();
  } catch (const Json::exception&) {
    result.status = RecordJsonImportStatus::malformed_json;
    return result;
  }
  if (json_signature != signature.to_string()) {
    result.status = RecordJsonImportStatus::signature_mismatch;
    return result;
  }
  bool key_matches = false;
  try {
    key_matches = FormKey::parse(json_key) == key;
  } catch (const std::exception&) {
    key_matches = false;
  }
  if (!key_matches && !options.ignore_form_key) {
    result.status = RecordJsonImportStatus::form_key_mismatch;
    return result;
  }

  Plugin working = plugin;
  std::map<std::string, std::size_t> occurrences;
  try {
    const auto& subrecords = document.at("subrecords");
    if (!subrecords.is_array()) throw MalformedRecordJson("subrecords must be an array");
    for (std::size_t index = 0; index < subrecords.size(); ++index) {
      const auto& sub = subrecords[index];
      const auto sub_text = subrecord_text(sub);
      const auto sub_signature = Signature::from_string(sub_text);
      const auto expected_path = signature.to_string() + "\\" + sub_text;

      if (const auto fields = sub.find("fields"); fields != sub.end()) {
        if (!fields->is_array()) throw MalformedRecordJson("fields must be an array");
        const bool localized = schema::is_localized_field(signature, sub_signature);
        for (const auto& field : *fields) {
          const auto path = field.at("path").get<std::string>();
          const auto name = field.at("name").get<std::string>();
          const auto& value = field.at("value");
          RecordJsonEntryStatus status = RecordJsonEntryStatus::invalid_value;
          if (value.is_string()) {
            const auto text = value.get<std::string>();
            if (localized) {
              status = path == expected_path
                           ? apply_localized_value(working, key, signature, sub_signature, index, text)
                           : RecordJsonEntryStatus::unknown_path;
            } else {
              std::optional<std::size_t> slot;
              if (const auto found = field.find("slot"); found != field.end()) {
                if (!found->is_number_unsigned()) throw MalformedRecordJson("slot must be an unsigned integer");
                slot = found->get<std::size_t>();
              }
              status = apply_scalar_value(working, key, signature, path, name, text, slot, occurrences);
            }
          }
          result.entries.push_back({path, status});
        }
      }

      if (const auto raw = sub.find("raw"); raw != sub.end()) {
        const auto hex = raw->get<std::string>();
        const auto status = apply_raw(working, key, sub_signature, index, hex, options.allow_raw);
        result.entries.push_back({expected_path, status});
      }
    }
  } catch (const Json::exception&) {
    result.status = RecordJsonImportStatus::malformed_json;
    return result;
  } catch (const MalformedRecordJson&) {
    result.status = RecordJsonImportStatus::malformed_json;
    return result;
  }

  for (const auto& entry : result.entries) {
    const bool acceptable = entry.status == RecordJsonEntryStatus::applied ||
                            entry.status == RecordJsonEntryStatus::unchanged ||
                            (entry.status == RecordJsonEntryStatus::unknown_path && options.skip_unknown);
    if (!acceptable) {
      result.status = RecordJsonImportStatus::refused;
      return result;
    }
  }

  plugin = std::move(working);
  result.status = RecordJsonImportStatus::committed;
  return result;
}

}
