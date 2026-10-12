#include "fo4/forms/eyes.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_eyes = Signature::from_string("EYES");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_icon = Signature::from_string("ICON");

std::optional<std::string> decode_zstring(const std::vector<std::byte>& data,
                                          bool four_byte_payload_is_opaque) {
  if (data.empty()) return std::nullopt;
  if (four_byte_payload_is_opaque && data.size() == 4) return std::nullopt;
  if (data.back() != std::byte{0}) return std::nullopt;
  if (std::find(data.begin(), data.end() - 1, std::byte{0}) != data.end() - 1) {
    return std::nullopt;
  }
  const auto* begin = reinterpret_cast<const char*>(data.data());
  return std::string(begin, begin + data.size() - 1);
}

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw EyesError("EYES string fields cannot contain embedded NUL bytes");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) {
    data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  data.push_back(std::byte{0});
  return data;
}

using fo4::detail::find_mutable_record;

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

const Subrecord* unique_subrecord(const Record& record,
                                  Signature signature,
                                  std::string_view name,
                                  bool required) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw EyesError("EYES contains duplicate " + std::string(name) + " subrecords");
    }
    found = &subrecord;
  }
  if (required && found == nullptr) {
    throw EyesError("EYES is missing required " + std::string(name) + " subrecord");
  }
  return found;
}

bool replace_string_subrecord(Record& record,
                              Signature signature,
                              std::string_view value,
                              bool insert_before_full) {
  const auto encoded = zstring(value);
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (subrecord.data == encoded) return false;
    subrecord.data = encoded;
    return true;
  }

  Subrecord created;
  created.signature = signature;
  created.data = encoded;
  if (insert_before_full) {
    const auto position = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                       [](const Subrecord& subrecord) {
                                         return subrecord.signature == k_full;
                                       });
    record.subrecords.insert(position, std::move(created));
  } else {
    record.subrecords.push_back(std::move(created));
  }
  return true;
}

}  // namespace

Eyes decode_eyes(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) throw EyesError("EYES record not found: " + key.to_string());
  if (record->header.signature != k_eyes) {
    throw EyesError("record is not EYES: " + key.to_string());
  }

  Eyes result;
  result.key = key;
  result.record_flags = record->header.flags;

  if (const auto* edid = unique_subrecord(*record, sig_edid, "EDID", false)) {
    const auto decoded = decode_zstring(edid->data, false);
    if (!decoded.has_value()) throw EyesError("EYES EDID is not a single NUL-terminated string");
    result.editor_id = *decoded;
  }

  const auto* full = unique_subrecord(*record, k_full, "FULL", true);
  result.full_raw = full->data;
  result.full_text = decode_zstring(full->data, true);

  const auto* icon = unique_subrecord(*record, k_icon, "ICON", true);
  const auto decoded_icon = decode_zstring(icon->data, false);
  if (!decoded_icon.has_value()) throw EyesError("EYES ICON is not a single NUL-terminated string");
  result.icon = *decoded_icon;

  const auto* data = unique_subrecord(*record, sig_data, "DATA", true);
  if (data->data.size() != 1) throw EyesError("EYES DATA must be exactly one byte");
  result.data_flags = std::to_integer<std::uint8_t>(data->data.front());
  return result;
}

FormKey create_eyes(Plugin& plugin,
                    std::optional<std::string_view> editor_id,
                    std::string_view full_text,
                    std::string_view icon,
                    std::uint8_t data_flags,
                    RecordFlags record_flags) {
  if (record_flags.is_master() || record_flags.is_deleted() ||
      record_flags.is_compressed() || record_flags.is_small_file()) {
    throw EyesError(
        "new EYES records cannot be created with master, deleted, compressed, or small-file flags");
  }

  auto next = plugin;

  // Encode every caller-supplied string before committing the cloned plugin.
  // A bad embedded NUL therefore cannot leave an allocated but incomplete
  // record behind in the caller's Plugin object.
  std::optional<std::vector<std::byte>> encoded_edid;
  if (editor_id.has_value()) encoded_edid = zstring(stored_editor_id(*editor_id));
  const auto encoded_full = zstring(stored_inline_text(full_text, "EYES FULL"));
  const auto encoded_icon = zstring(stored_inline_text(icon, "EYES ICON"));

  const auto key = create_local_record(next, k_eyes);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw EyesError("newly allocated EYES record could not be resolved");
  }

  record->header.flags = record_flags;
  if (encoded_edid.has_value()) {
    record->subrecords.push_back(Subrecord{sig_edid, std::move(*encoded_edid), false});
  }
  record->subrecords.push_back(Subrecord{k_full, encoded_full, false});
  record->subrecords.push_back(Subrecord{k_icon, encoded_icon, false});
  record->subrecords.push_back(
      Subrecord{sig_data, std::vector<std::byte>{std::byte{data_flags}}, false});
  mark_modified(next, *record);

  for (const auto& issue : schema::preflight_authoring_record(next, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw EyesError("EYES authoring preflight failed: " + issue.message);
    }
  }

  plugin = std::move(next);
  return key;
}

bool set_eyes_text_fields(Plugin& plugin,
                          const FormKey& key,
                          std::optional<std::string_view> editor_id,
                          std::optional<std::string_view> full_text,
                          std::optional<std::string_view> icon) {
  assign_record_form_keys(plugin);
  const auto current = decode_eyes(plugin, key);
  if (full_text.has_value() && !current.full_text.has_value()) {
    throw EyesError(
        "EYES FULL is opaque/localized storage; refusing to reinterpret it as inline text");
  }

  const auto stored_edid = editor_id.has_value()
      ? std::optional<std::string>{stored_editor_id(*editor_id)}
      : std::nullopt;
  const auto stored_full = full_text.has_value()
      ? std::optional<std::string>{stored_inline_text(*full_text, "EYES FULL")}
      : std::nullopt;
  const auto stored_icon = icon.has_value()
      ? std::optional<std::string>{stored_inline_text(*icon, "EYES ICON")}
      : std::nullopt;
  bool needs_change = false;
  if (stored_edid.has_value() && current.editor_id != stored_edid) {
    needs_change = true;
  }
  if (stored_full.has_value() && current.full_text != stored_full) needs_change = true;
  if (stored_icon.has_value() && current.icon != *stored_icon) needs_change = true;
  if (!needs_change) return true;

  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) return false;
  bool changed = false;
  if (stored_edid.has_value()) {
    changed |= replace_string_subrecord(*record, sig_edid, *stored_edid, true);
  }
  if (stored_full.has_value()) {
    changed |= replace_string_subrecord(*record, k_full, *stored_full, false);
  }
  if (stored_icon.has_value()) {
    changed |= replace_string_subrecord(*record, k_icon, *stored_icon, false);
  }
  if (changed) mark_modified(plugin, *record);
  return true;
}

bool set_eyes_data_flags(Plugin& plugin,
                         const FormKey& key,
                         std::uint8_t flags) {
  assign_record_form_keys(plugin);
  const auto current = decode_eyes(plugin, key);
  if (current.data_flags == flags) return true;

  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) return false;
  for (auto& subrecord : record->subrecords) {
    if (subrecord.signature != sig_data) continue;
    if (subrecord.data.size() != 1) throw EyesError("EYES DATA must be exactly one byte");
    subrecord.data.front() = std::byte{flags};
    mark_modified(plugin, *record);
    return true;
  }
  throw EyesError("EYES is missing required DATA subrecord");
}

}  // namespace fo4::forms
