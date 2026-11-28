#include "fo4/c_api_authoring.h"

#include "c_api_env_internal.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/text/encoding.hpp"

#include <cstring>
#include <filesystem>
#include <iterator>
#include <span>
#include <system_error>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

std::optional<fo4::Signature> parse_signature(const char* text) {
  const auto parsed = to_signature(text);
  if (!parsed) return std::nullopt;
  for (int index = 0; index < 4; ++index) {
    if (text[index] < 0x20 || text[index] > 0x7E) return std::nullopt;
  }
  return parsed;
}

constexpr fo4::localization::StringTableKind k_table_kinds[] = {fo4::localization::StringTableKind::strings,
                                                              fo4::localization::StringTableKind::dlstrings,
                                                              fo4::localization::StringTableKind::ilstrings};

// Constraint: a missing table file is an empty table, but an existing file that cannot be read is an error so it is never overwritten.
void load_existing_tables(fo4::localization::LocalizationTables& tables, std::string_view plugin_file,
                          std::string_view language, const std::filesystem::path& data_dir) {
  for (const auto kind : k_table_kinds) {
    const auto path = data_dir / fo4::localization::string_table_path(plugin_file, kind, language);
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) continue;
    if (ec) throw fo4::localization::StringTableError("could not inspect localized string file");
    fo4::localization::table_for(tables, kind) = fo4::localization::read_string_table_file(path, kind);
  }
}

radicle_status bad_signature() {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature must be four printable ASCII characters");
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_add_subrecord(radicle_plugin* plugin,
                                            radicle_form_key key,
                                            const char* signature) {
  clear_error();
  return guarded("unknown exception while adding a subrecord", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
    }
    const auto parsed = parse_signature(signature);
    if (!parsed) return bad_signature();
    using Status = fo4::SubrecordAddStatus;
    switch (fo4::add_record_subrecord(plugin->value, *form_key, *parsed)) {
      case Status::added:
      case Status::already_present:
        return RADICLE_STATUS_OK;
      case Status::record_missing:
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      case Status::not_authorable:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "subrecord is not authorable for this record type");
      case Status::unknown_order:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                    "record layout is not fully described by the authoring order, so the insertion point is unknown");
      case Status::no_default:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "subrecord has no registered default payload");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown subrecord add status");
  });
}

radicle_status radicle_plugin_remove_subrecord(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               const char* signature,
                                               size_t occurrence) {
  clear_error();
  return guarded("unknown exception while removing a subrecord", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
    }
    const auto parsed = parse_signature(signature);
    if (!parsed) return bad_signature();
    using Status = fo4::SubrecordRemoveStatus;
    switch (fo4::remove_record_subrecord(plugin->value, *form_key, *parsed, occurrence)) {
      case Status::removed:
        return RADICLE_STATUS_OK;
      case Status::record_missing:
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      case Status::absent:
        return fail(RADICLE_STATUS_NOT_FOUND, "record has no such subrecord occurrence");
      case Status::below_minimum:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "removal would leave fewer subrecords than the schema requires");
      case Status::not_authorable:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "subrecord is not authorable for this record type");
      case Status::unknown_order:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                    "record layout is not fully described by the authoring order, so the removal cannot be proven safe");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown subrecord remove status");
  });
}

radicle_status radicle_plugin_create_record(radicle_plugin* plugin,
                                            const char* signature,
                                            const char* editor_id_utf8,
                                            radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || editor_id_utf8 == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, editor ID and out_key are required");
  }
  const auto parsed = parse_signature(signature);
  if (!parsed) return bad_signature();
  return guarded("unknown exception while creating a record", [&] {
    const auto stored = fo4::text::utf8_to_windows1252(editor_id_utf8);
    if (!stored) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID is not valid UTF-8 representable in Windows-1252");
    }
    using Status = fo4::RecordCreateStatus;
    const auto result = fo4::create_record(plugin->value, *parsed, *stored);
    switch (result.status) {
      case Status::created:
        *out_key = {plugin->value.key.filename().data(), result.key.local().value()};
        return RADICLE_STATUS_OK;
      case Status::not_authorable:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type has no authoring schema");
      case Status::no_default:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a required subrecord has no registered default payload");
      case Status::invalid_editor_id:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID cannot be stored for this record type");
      case Status::preflight_failed:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the default record failed authoring preflight");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown record create status");
  });
}

radicle_status radicle_plugin_create_raw_record(radicle_plugin* plugin,
                                                const char* signature,
                                                uint16_t form_version,
                                                radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_key are required");
  }
  const auto parsed = parse_signature(signature);
  if (!parsed) return bad_signature();
  return guarded("unknown exception while creating a raw record", [&] {
    const auto result = fo4::create_raw_record(plugin->value, *parsed, form_version);
    switch (result.status) {
      case fo4::RawRecordStatus::created:
        *out_key = {plugin->value.key.filename().data(), result.key.local().value()};
        return RADICLE_STATUS_OK;
      case fo4::RawRecordStatus::not_creatable:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature cannot name a record");
      case fo4::RawRecordStatus::nested_placement:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type is nested in a parent group and cannot be created at top level");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown raw record status");
  });
}

radicle_status radicle_plugin_set_subrecord_bytes(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  const char* signature,
                                                  size_t occurrence,
                                                  const uint8_t* bytes,
                                                  size_t size) {
  clear_error();
  return guarded("unknown exception while setting subrecord bytes", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
    }
    if (bytes == nullptr && size != 0) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes must not be null when size is nonzero");
    }
    const auto parsed = parse_signature(signature);
    if (!parsed) return bad_signature();
    const std::span<const std::byte> payload{reinterpret_cast<const std::byte*>(bytes), size};
    switch (fo4::set_record_subrecord_bytes(plugin->value, *form_key, *parsed, occurrence, payload)) {
      case fo4::SubrecordBytesStatus::replaced:
      case fo4::SubrecordBytesStatus::appended:
        return RADICLE_STATUS_OK;
      case fo4::SubrecordBytesStatus::record_missing:
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      case fo4::SubrecordBytesStatus::occurrence_out_of_range:
        return fail(RADICLE_STATUS_NOT_FOUND, "record has fewer subrecord occurrences than requested");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown subrecord bytes status");
  });
}

radicle_status radicle_plugin_set_record_raw_compressed_payload(radicle_plugin* plugin,
                                                                radicle_form_key key,
                                                                uint32_t declared_size,
                                                                const uint8_t* bytes,
                                                                size_t size) {
  clear_error();
  return guarded("unknown exception while setting a raw compressed payload", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
    }
    if (bytes == nullptr && size != 0) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes must not be null when size is nonzero");
    }
    const std::span<const std::byte> payload{reinterpret_cast<const std::byte*>(bytes), size};
    if (!fo4::set_record_raw_compressed_payload(plugin->value, *form_key, declared_size, payload)) {
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_create_record_with_string_rows(radicle_plugin* plugin,
                                                             const char* signature,
                                                             const char* editor_id_utf8,
                                                             const char* const* strings,
                                                             size_t string_count,
                                                             const char* data_dir_utf8,
                                                             const char* language_utf8,
                                                             radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || editor_id_utf8 == nullptr || out_key == nullptr || data_dir_utf8 == nullptr ||
      language_utf8 == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, editor ID, data directory, language and out_key are required");
  }
  if (string_count != 0 && strings == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a string array with a nonzero count must not be null");
  }
  const auto parsed = parse_signature(signature);
  if (!parsed) return bad_signature();
  if (!plugin->value.header.tes4_flags.is_localized()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record string rows require a localized plugin");
  }
  return guarded("unknown exception while creating a record with string rows", [&] {
    const auto stored = fo4::text::utf8_to_windows1252(editor_id_utf8);
    if (!stored) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID is not valid UTF-8 representable in Windows-1252");
    }
    // Constraint: the record is created on a copy, so a row-count mismatch leaves the plugin unchanged.
    fo4::Plugin working = plugin->value;
    const auto data_dir = utf8_path(data_dir_utf8);
    fo4::localization::LocalizationTables tables;
    try {
      load_existing_tables(tables, working.key.filename(), language_utf8, data_dir);
    } catch (const fo4::localization::StringTableError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
    const std::size_t existing_rows[] = {tables.strings.entries.size(), tables.dlstrings.entries.size(),
                                         tables.ilstrings.entries.size()};
    const auto result = fo4::create_record_with_string_rows(working, *parsed, *stored, tables);
    using Status = fo4::RecordCreateStatus;
    switch (result.status) {
      case Status::created: break;
      case Status::not_authorable:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type has no authoring schema");
      case Status::no_default:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a required subrecord has no registered default payload");
      case Status::invalid_editor_id:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID cannot be stored for this record type");
      case Status::preflight_failed:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the default record failed authoring preflight");
      default:
        return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown record create status");
    }
    // Constraint: rows fill in resource order, so caller strings map to STRINGS then DLSTRINGS then ILSTRINGS.
    std::vector<std::pair<fo4::localization::StringTableKind, std::uint32_t>> rows;
    for (std::size_t kind = 0; kind < std::size(k_table_kinds); ++kind) {
      const auto& table = fo4::localization::table_for(tables, k_table_kinds[kind]);
      for (std::size_t at = existing_rows[kind]; at < table.entries.size(); ++at) {
        rows.emplace_back(table.kind, table.entries[at].id);
      }
    }
    if (rows.size() != string_count) {
      const auto message = std::string("string_count must equal the number of reserved localized rows (") +
                           std::to_string(rows.size()) + ")";
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    for (std::size_t index = 0; index < string_count; ++index) {
      const auto text = fo4::text::utf8_to_windows1252(strings[index]);
      if (!text) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "row text is not valid UTF-8 representable in Windows-1252");
      }
      fo4::localization::set_string(fo4::localization::table_for(tables, rows[index].first), rows[index].second,
                                    *text);
    }
    fo4::localization::write_localization_tables(tables, working.key.filename(), language_utf8, data_dir);
    plugin->value = std::move(working);
    *out_key = {plugin->value.key.filename().data(), result.key.local().value()};
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
