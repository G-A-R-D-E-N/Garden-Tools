#include "fo4/c_api_localization.h"

#include "c_api_forms_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct radicle_localization_tables {
  fo4::localization::LocalizationTables value;
  std::array<bool, 3> dirty{};
};

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;
using StringTableKind = fo4::localization::StringTableKind;

constexpr std::array<StringTableKind, 3> k_table_kinds{
    StringTableKind::strings, StringTableKind::dlstrings, StringTableKind::ilstrings};

bool valid_language(std::string_view language) {
  return !language.empty() && std::all_of(language.begin(), language.end(), [](char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
  });
}

std::size_t kind_index(StringTableKind kind) noexcept {
  return static_cast<std::size_t>(kind);
}

void commit_localized_tables(radicle_localization_tables& tables,
                             fo4::localization::LocalizationTables next) {
  for (const auto kind : k_table_kinds) {
    if (fo4::localization::table_for(next, kind).entries.size() !=
        fo4::localization::table_for(tables.value, kind).entries.size()) {
      tables.dirty[kind_index(kind)] = true;
    }
  }
  tables.value = std::move(next);
}

radicle_status localization_outcome(fo4::PluginLocalizationStatus status) {
  switch (status) {
    case fo4::PluginLocalizationStatus::converted:
      return RADICLE_STATUS_OK;
    case fo4::PluginLocalizationStatus::already_localized:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is already localized");
    case fo4::PluginLocalizationStatus::not_localized:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is not localized");
    case fo4::PluginLocalizationStatus::unconvertible_field:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a localized field payload is not one NUL-terminated string or a string ID");
    case fo4::PluginLocalizationStatus::unresolved_string_id:
      return fail(RADICLE_STATUS_NOT_FOUND, "a string ID is missing from the string tables");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown plugin localization status");
}

radicle_plugin_localization_status to_c_localization_status(fo4::PluginLocalizationStatus status) {
  switch (status) {
    case fo4::PluginLocalizationStatus::converted:
      return RADICLE_PLUGIN_LOCALIZATION_CONVERTED;
    case fo4::PluginLocalizationStatus::already_localized:
      return RADICLE_PLUGIN_LOCALIZATION_ALREADY_LOCALIZED;
    case fo4::PluginLocalizationStatus::not_localized:
      return RADICLE_PLUGIN_LOCALIZATION_NOT_LOCALIZED;
    case fo4::PluginLocalizationStatus::unconvertible_field:
      return RADICLE_PLUGIN_LOCALIZATION_UNCONVERTIBLE_FIELD;
    case fo4::PluginLocalizationStatus::unresolved_string_id:
      return RADICLE_PLUGIN_LOCALIZATION_UNRESOLVED_STRING_ID;
  }
  return RADICLE_PLUGIN_LOCALIZATION_UNRESOLVED_STRING_ID;
}

radicle_status report_localization(const fo4::PluginLocalizationResult& result,
                                   radicle_plugin_localization_result* out_result) {
  out_result->status = to_c_localization_status(result.status);
  out_result->converted = result.converted;
  out_result->empty_text = result.empty_text;
  return localization_outcome(result.status);
}

radicle_status report_localized_value(fo4::LocalizedValueStatus status,
                                      radicle_localized_value_status* out_status) {
  switch (status) {
    case fo4::LocalizedValueStatus::updated:
      *out_status = RADICLE_LOCALIZED_VALUE_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::LocalizedValueStatus::unchanged:
      *out_status = RADICLE_LOCALIZED_VALUE_UNCHANGED;
      return RADICLE_STATUS_OK;
    case fo4::LocalizedValueStatus::record_missing:
      *out_status = RADICLE_LOCALIZED_VALUE_RECORD_MISSING;
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::LocalizedValueStatus::subrecord_missing:
      *out_status = RADICLE_LOCALIZED_VALUE_SUBRECORD_MISSING;
      return fail(RADICLE_STATUS_NOT_FOUND, "record has no subrecord at that index");
    case fo4::LocalizedValueStatus::field_not_localized:
      *out_status = RADICLE_LOCALIZED_VALUE_FIELD_NOT_LOCALIZED;
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "subrecord is not a localized field");
    case fo4::LocalizedValueStatus::plugin_not_localized:
      *out_status = RADICLE_LOCALIZED_VALUE_PLUGIN_NOT_LOCALIZED;
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is not localized");
    case fo4::LocalizedValueStatus::payload_not_an_id:
      *out_status = RADICLE_LOCALIZED_VALUE_PAYLOAD_NOT_AN_ID;
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "localized field payload is not a string ID");
    case fo4::LocalizedValueStatus::empty_text:
      *out_status = RADICLE_LOCALIZED_VALUE_EMPTY_TEXT;
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "empty text has no string table entry");
    case fo4::LocalizedValueStatus::table_kind_mismatch:
      *out_status = RADICLE_LOCALIZED_VALUE_TABLE_KIND_MISMATCH;
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "string table is not the kind this field reads");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown localized value status");
}

}  // namespace

extern "C" {

radicle_status radicle_localization_tables_create(radicle_localization_tables** out_tables) {
  clear_error();
  if (out_tables == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_tables is required");
  *out_tables = nullptr;
  return guarded("unknown exception while creating localization tables", [&] {
    *out_tables = new radicle_localization_tables{};
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_localization_tables_load(const char* data_dir,
                                                const char* plugin_file_name,
                                                const char* language,
                                                radicle_localization_tables** out_tables) {
  clear_error();
  if (data_dir == nullptr || plugin_file_name == nullptr || language == nullptr || out_tables == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "data_dir, plugin_file_name, language and out_tables are required");
  }
  *out_tables = nullptr;
  if (!valid_language(language)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "language must be ASCII letters only");
  }
  return guarded("unknown exception while loading localization tables", [&] {
    auto loaded = std::make_unique<radicle_localization_tables>();
    const auto directory = utf8_path(data_dir);
    for (const auto kind : k_table_kinds) {
      const auto path = directory / fo4::localization::string_table_path(plugin_file_name, kind, language);
      if (!std::filesystem::exists(path)) continue;
      fo4::localization::table_for(loaded->value, kind) = fo4::localization::read_string_table_file(path, kind);
    }
    *out_tables = loaded.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_localization_tables_write(const radicle_localization_tables* tables,
                                                 const char* data_dir,
                                                 const char* plugin_file_name,
                                                 const char* language) {
  clear_error();
  if (tables == nullptr || data_dir == nullptr || plugin_file_name == nullptr || language == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tables, data_dir, plugin_file_name and language are required");
  }
  if (!valid_language(language)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "language must be ASCII letters only");
  }
  try {
    const auto directory = utf8_path(data_dir);
    std::vector<fo4::localization::StringTableWrite> writes;
    for (const auto kind : k_table_kinds) {
      const auto& table = fo4::localization::table_for(tables->value, kind);
      if (table.entries.empty() && !tables->dirty[kind_index(kind)]) continue;
      auto path = directory / fo4::localization::string_table_path(plugin_file_name, kind, language);
      std::filesystem::create_directories(path.parent_path());
      writes.push_back({&table, std::move(path)});
    }
    fo4::localization::write_string_table_files(writes);
    return RADICLE_STATUS_OK;
  } catch (const std::filesystem::filesystem_error& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while writing localization tables");
  }
}

void radicle_localization_tables_destroy(radicle_localization_tables* tables) {
  clear_error();
  delete tables;
}

radicle_status radicle_plugin_localize(radicle_plugin* plugin,
                                       radicle_localization_tables* tables,
                                       radicle_plugin_localization_result* out_result) {
  clear_error();
  if (plugin == nullptr || tables == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, tables and out_result are required");
  }
  *out_result = radicle_plugin_localization_result{};
  return guarded("unknown exception while localizing the plugin", [&] {
    try {
      auto next_plugin = plugin->value;
      auto next_tables = tables->value;
      const auto result = fo4::localize_plugin(next_plugin, next_tables);
      if (result.status == fo4::PluginLocalizationStatus::converted) {
        plugin->value = std::move(next_plugin);
        commit_localized_tables(*tables, std::move(next_tables));
      }
      return report_localization(result, out_result);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_delocalize(radicle_plugin* plugin,
                                         radicle_localization_tables* tables,
                                         radicle_plugin_localization_result* out_result) {
  clear_error();
  if (plugin == nullptr || tables == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, tables and out_result are required");
  }
  *out_result = radicle_plugin_localization_result{};
  return guarded("unknown exception while delocalizing the plugin", [&] {
    try {
      auto next_plugin = plugin->value;
      auto next_tables = tables->value;
      const auto result = fo4::delocalize_plugin(next_plugin, next_tables);
      if (result.status == fo4::PluginLocalizationStatus::converted) {
        plugin->value = std::move(next_plugin);
        commit_localized_tables(*tables, std::move(next_tables));
      }
      return report_localization(result, out_result);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_localized_string(radicle_plugin* plugin,
                                                   radicle_form_key record,
                                                   const char* subrecord_signature,
                                                   size_t index,
                                                   const char* text,
                                                   radicle_localization_tables* tables,
                                                   radicle_localized_value_status* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  const auto signature = to_signature(subrecord_signature);
  if (plugin == nullptr || tables == nullptr || out_status == nullptr || !key || !signature || text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, a valid record key, a four-character subrecord signature, text, tables and out_status are required");
  }
  *out_status = RADICLE_LOCALIZED_VALUE_RECORD_MISSING;
  const auto encoded = fo4::text::utf8_to_windows1252(text);
  if (!encoded) return bad_text();
  return guarded("unknown exception while writing a localized string", [&] {
    const auto* found = fo4::find_record(plugin->value, *key);
    if (found == nullptr) return report_localized_value(fo4::LocalizedValueStatus::record_missing, out_status);
    const auto kind = fo4::localization::string_table_kind(found->header.signature, *signature);
    auto& table = fo4::localization::table_for(tables->value, kind);
    const auto status = fo4::add_record_localized_string(plugin->value, *key, *signature, index, *encoded, table);
    if (status == fo4::LocalizedValueStatus::updated) tables->dirty[kind_index(kind)] = true;
    return report_localized_value(status, out_status);
  });
}

}  // extern "C"
