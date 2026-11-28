#include "fo4/c_api_pet_write.h"

#include "c_api_env_internal.hpp"
#include "fo4/environment/strings.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/field_access.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_flags.hpp"
#include "fo4/schema/scalar.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status map_status(fo4::FieldStatus status) {
  using Status = fo4::FieldStatus;
  switch (status) {
    case Status::ok:
      return RADICLE_STATUS_OK;
    case Status::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case Status::record_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path does not apply to this record type");
    case Status::unknown_path:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown field path");
    case Status::wrong_kind:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "this call does not handle that kind of field path");
    case Status::occurrence_missing:
      return fail(RADICLE_STATUS_NOT_FOUND,
                  "record has no such field occurrence, or the field is outside its supported payload layout");
    case Status::index_out_of_range:
      return fail(RADICLE_STATUS_NOT_FOUND, "index is out of range");
    case Status::target_type_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "target record type is not allowed in this list");
    case Status::boundary_unavailable:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "this list cannot be inserted into here");
    case Status::would_empty:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "removing the last element would empty the array");
    case Status::next_alias_id_too_low:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "QUST Next Alias ID must stay above every existing alias ID");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown field status");
}

template <class Fn>
radicle_status checked(const char* what, Fn&& fn) {
  return guarded(what, [&] {
    try {
      return fn();
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::FormLinkListError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::CountedArrayError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::SubrecordGroupError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::localization::StringTableError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_field_kind to_c_kind(fo4::FieldKind kind) {
  switch (kind) {
    case fo4::FieldKind::scalar: return RADICLE_FIELD_SCALAR;
    case fo4::FieldKind::string: return RADICLE_FIELD_STRING;
    case fo4::FieldKind::formlink: return RADICLE_FIELD_FORMLINK;
    case fo4::FieldKind::formlink_list: return RADICLE_FIELD_FORMLINK_LIST;
    case fo4::FieldKind::counted_array: return RADICLE_FIELD_COUNTED_ARRAY;
    case fo4::FieldKind::subrecord_group: return RADICLE_FIELD_SUBRECORD_GROUP;
  }
  return RADICLE_FIELD_SCALAR;
}

char* copy_text(const std::string& text) {
  auto* buffer = static_cast<char*>(std::malloc(text.size() + 1));
  if (buffer != nullptr) std::memcpy(buffer, text.c_str(), text.size() + 1);
  return buffer;
}

fo4::FieldStatus readable_path(const char* path, fo4::Signature& signature) {
  const std::string_view text(path);
  if (text.size() < 6 || text[4] != '\\') return fo4::FieldStatus::unknown_path;
  signature = fo4::Signature::from_string(text.substr(0, 4));
  for (const auto& row : fo4::record_field_schema_rows(signature)) {
    if (row.path != path) continue;
    return fo4::FieldStatus::ok;
  }
  return fo4::FieldStatus::unknown_path;
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_list_size(const radicle_plugin* plugin,
                                        radicle_form_key key,
                                        const char* path,
                                        size_t* out_size) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || path == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and out_size are required");
  }
  *out_size = 0;
  return checked("unknown exception while sizing a list", [&] {
    return map_status(fo4::record_list_size(plugin->value, *form_key, path, *out_size));
  });
}

radicle_status radicle_plugin_list_insert(radicle_plugin* plugin,
                                          radicle_form_key key,
                                          const char* path,
                                          size_t at,
                                          const radicle_form_key* target) {
  clear_error();
  const auto form_key = to_form_key(key);
  const auto target_key = target == nullptr ? std::nullopt : to_form_key(*target);
  if (plugin == nullptr || !form_key || path == nullptr || (target != nullptr && !target_key)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, a valid record key, path and a valid target or NULL are required");
  }
  return checked("unknown exception while inserting a list element", [&] {
    return map_status(fo4::insert_record_list_element(plugin->value, *form_key, path, at,
                                                      target_key ? &*target_key : nullptr));
  });
}

radicle_status radicle_plugin_list_remove(radicle_plugin* plugin,
                                          radicle_form_key key,
                                          const char* path,
                                          size_t at) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || path == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and path are required");
  }
  return checked("unknown exception while removing a list element", [&] {
    return map_status(fo4::remove_record_list_element(plugin->value, *form_key, path, at));
  });
}

radicle_status radicle_record_get_field(const radicle_env* env,
                                        const radicle_record* record,
                                        const char* path,
                                        size_t index,
                                        char** out_text) {
  clear_error();
  if (out_text != nullptr) *out_text = nullptr;
  if (env == nullptr || record == nullptr || path == nullptr || out_text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, path and out_text are required");
  }
  return checked("unknown exception while reading a field", [&] {
    const auto& ref = *to_ref(record);
    if (!belongs_to_env(env, &ref)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
    }
    std::string text;
    const auto status = fo4::read_record_field(env->value.plugin(ref.plugin), *ref.record, path, index, text);
    if (status != fo4::FieldStatus::ok) return map_status(status);
    auto* buffer = static_cast<char*>(std::malloc(text.size() + 1));
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the field text");
    std::memcpy(buffer, text.c_str(), text.size() + 1);
    *out_text = buffer;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_list_size(const radicle_env* env,
                                        const radicle_record* record,
                                        const char* path,
                                        size_t* out_size) {
  clear_error();
  if (out_size != nullptr) *out_size = 0;
  if (env == nullptr || record == nullptr || path == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, path and out_size are required");
  }
  return checked("unknown exception while sizing a list", [&] {
    const auto& ref = *to_ref(record);
    if (!belongs_to_env(env, &ref)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
    }
    std::size_t size = 0;
    const auto status = fo4::record_list_size(env->value.plugin(ref.plugin), ref.record->form_key, path, size);
    if (status == fo4::FieldStatus::ok) *out_size = size;
    return map_status(status);
  });
}

radicle_status radicle_record_get_flag(const radicle_env* env,
                                       const radicle_record* record,
                                       const char* path,
                                       int32_t* out_value) {
  clear_error();
  if (out_value != nullptr) *out_value = 0;
  if (env == nullptr || record == nullptr || path == nullptr || out_value == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, path and out_value are required");
  }
  return checked("unknown exception while reading a record flag", [&] {
    const auto& ref = *to_ref(record);
    if (!belongs_to_env(env, &ref)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
    }
    const auto* descriptor = fo4::schema::record_flag_descriptor(path);
    if (descriptor == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown record flag path");
    if (ref.record->header.signature != descriptor->signature) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the record flag row belongs to another record type");
    }
    *out_value = fo4::schema::record_flag_set(*ref.record, *descriptor) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_record_field(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               const char* path,
                                               size_t index,
                                               const char* text) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || path == nullptr || text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and text are required");
  }
  return checked("unknown exception while setting a field", [&] {
    return map_status(fo4::write_record_field(plugin->value, *form_key, path, index, text,
                                              plugin->tables_loaded ? &plugin->tables : nullptr));
  });
}

namespace {

bool valid_language(const char* language) {
  if (language == nullptr || *language == 0) return false;
  for (const char* c = language; *c != 0; ++c) {
    if (std::isalnum(static_cast<unsigned char>(*c)) == 0) return false;
  }
  return true;
}

}  // namespace

radicle_status radicle_plugin_strings_load(radicle_plugin* plugin, const char* strings_dir, const char* language) {
  clear_error();
  if (plugin == nullptr || strings_dir == nullptr || !valid_language(language)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, strings_dir and an alphanumeric language are required");
  }
  if (!plugin->value.header.tes4_flags.is_localized()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is not localized");
  }
  return checked("unknown exception while loading string tables", [&] {
    const std::filesystem::path dirs[] = {utf8_path(strings_dir)};
    fo4::localization::LocalizationTables loaded;
    std::array<std::filesystem::path, 3> files;
    for (auto& found : fo4::env::load_plugin_string_tables(dirs, plugin->value, language)) {
      files[static_cast<std::size_t>(found.table.kind)] = found.path.filename();
      fo4::localization::table_for(loaded, found.table.kind) = std::move(found.table);
    }
    plugin->table_files = std::move(files);
    plugin->tables_language = language;
    plugin->tables = std::move(loaded);
    plugin->tables_loaded = true;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_strings_save(const radicle_plugin* plugin,
                                           const char* plugin_file_name,
                                           const char* strings_dir,
                                           const char* language) {
  clear_error();
  if (plugin == nullptr || strings_dir == nullptr || !valid_language(language)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, strings_dir and an alphanumeric language are required");
  }
  if (!plugin->tables_loaded) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "string tables are not loaded; call radicle_plugin_strings_load first");
  }
  return checked("unknown exception while saving string tables", [&] {
    const std::string name = plugin_file_name != nullptr ? std::string(plugin_file_name) : std::string(plugin->value.key.filename());
    const auto directory = utf8_path(strings_dir);
    std::filesystem::create_directories(directory);
    for (const auto kind : {fo4::localization::StringTableKind::strings, fo4::localization::StringTableKind::dlstrings,
                            fo4::localization::StringTableKind::ilstrings}) {
      const auto& loaded_name = plugin->table_files[static_cast<std::size_t>(kind)];
      const auto file = plugin_file_name == nullptr && !loaded_name.empty() && plugin->tables_language == language
                            ? loaded_name
                            : fo4::localization::string_table_path(name, kind, language).filename();
      fo4::localization::write_string_table_file(fo4::localization::table_for(plugin->tables, kind), directory / file);
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_field_schema(const char* signature,
                                           radicle_field_schema_row** out_rows,
                                           size_t* out_count) {
  clear_error();
  if (out_rows != nullptr) *out_rows = nullptr;
  if (out_count != nullptr) *out_count = 0;
  const auto record = to_signature(signature);
  if (!record || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a four-letter signature, out_rows and out_count are required");
  }
  return guarded("unknown exception while listing field paths", [&] {
    const auto rows = fo4::record_field_schema_rows(*record);
    if (rows.empty()) return RADICLE_STATUS_OK;
    size_t bytes = rows.size() * sizeof(radicle_field_schema_row);
    for (const auto& row : rows) bytes += row.path.size() + 1;
    auto* block = static_cast<char*>(std::malloc(bytes));
    if (block == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the field schema");
    auto* out = reinterpret_cast<radicle_field_schema_row*>(block);
    auto* text = block + rows.size() * sizeof(radicle_field_schema_row);
    for (size_t i = 0; i < rows.size(); ++i) {
      std::memcpy(text, rows[i].path.c_str(), rows[i].path.size() + 1);
      out[i] = {text, to_c_kind(rows[i].kind), rows[i].can_insert ? 1 : 0, rows[i].repeated ? 1 : 0};
      text += rows[i].path.size() + 1;
    }
    *out_rows = out;
    *out_count = rows.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_field_schema_free(radicle_field_schema_row* rows) {
  clear_error();
  std::free(rows);
}

radicle_status radicle_record_field_schema_ex(const char* signature,
                                              radicle_field_schema_ex_row** out_rows,
                                              size_t* out_count) {
  clear_error();
  if (out_rows != nullptr) *out_rows = nullptr;
  if (out_count != nullptr) *out_count = 0;
  const auto record = to_signature(signature);
  if (!record || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a four-letter signature, out_rows and out_count are required");
  }
  return guarded("unknown exception while listing extended field paths", [&] {
    const auto rows = fo4::record_field_schema_ex_rows(*record);
    if (rows.empty()) return RADICLE_STATUS_OK;
    size_t string_bytes = 0;
    size_t name_bytes = 0;
    for (const auto& row : rows) {
      string_bytes += row.path.size() + 1;
      string_bytes += row.subrecord.size() + 1;
      string_bytes += row.count_subrecord.size() + 1;
      name_bytes += row.names.size() * sizeof(radicle_field_schema_name);
      for (const auto& name : row.names) string_bytes += name.name.size() + 1;
    }
    const size_t rows_bytes = rows.size() * sizeof(radicle_field_schema_ex_row);
    const size_t name_table_offset = (rows_bytes + string_bytes + 7) & ~size_t(7);
    const size_t bytes = name_table_offset + name_bytes;
    auto* block = static_cast<char*>(std::malloc(bytes));
    if (block == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the extended field schema");
    auto* out = reinterpret_cast<radicle_field_schema_ex_row*>(block);
    char* text = block + rows_bytes;
    auto* name_tables = reinterpret_cast<radicle_field_schema_name*>(block + name_table_offset);
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto& row = rows[i];
      std::memcpy(text, row.path.c_str(), row.path.size() + 1);
      const char* path = text;
      text += row.path.size() + 1;
      std::memcpy(text, row.subrecord.c_str(), row.subrecord.size() + 1);
      const char* subrecord = text;
      text += row.subrecord.size() + 1;
      std::memcpy(text, row.count_subrecord.c_str(), row.count_subrecord.size() + 1);
      const char* count_subrecord = text;
      text += row.count_subrecord.size() + 1;
      const radicle_field_schema_name* names = nullptr;
      const size_t name_count = row.names.size();
      if (name_count != 0) {
        names = name_tables;
        for (const auto& name : row.names) {
          std::memcpy(text, name.name.c_str(), name.name.size() + 1);
          *name_tables = {text, name.value};
          text += name.name.size() + 1;
          ++name_tables;
        }
      }
      out[i] = {path,
                to_c_kind(row.kind),
                row.can_insert ? 1 : 0,
                row.repeated ? 1 : 0,
                subrecord,
                count_subrecord,
                row.storage,
                row.presentation,
                row.embedded ? 1 : 0,
                row.localized ? 1 : 0,
                row.offset,
                row.payload_size,
                row.stride,
                row.minimum,
                row.maximum,
                row.min_form_version,
                row.max_form_version,
                names,
                name_count};
    }
    *out_rows = out;
    *out_count = rows.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_field_schema_ex_free(radicle_field_schema_ex_row* rows) {
  clear_error();
  std::free(rows);
}

radicle_status radicle_schema_record_signatures(char*** out_signatures, size_t* out_count) {
  clear_error();
  if (out_signatures != nullptr) *out_signatures = nullptr;
  if (out_count != nullptr) *out_count = 0;
  if (out_signatures == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_signatures and out_count are required");
  }
  return guarded("unknown exception while listing schema record signatures", [&] {
    const auto records = fo4::schema::record_signatures_with_schema();
    if (records.empty()) return RADICLE_STATUS_OK;
    const size_t bytes = records.size() * sizeof(char*) + records.size() * 5;
    auto* block = static_cast<char*>(std::malloc(bytes));
    if (block == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate schema record signatures");
    auto* out = reinterpret_cast<char**>(block);
    char* text = block + records.size() * sizeof(char*);
    for (size_t i = 0; i < records.size(); ++i) {
      const auto signature = records[i].to_string();
      std::memcpy(text, signature.c_str(), signature.size() + 1);
      out[i] = text;
      text += 5;
    }
    *out_signatures = out;
    *out_count = records.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_schema_record_signatures_free(char** signatures) {
  clear_error();
  std::free(signatures);
}

radicle_status radicle_record_get_text(const radicle_env* env,
                                       const radicle_strings* strings,
                                       const radicle_record* record,
                                       const char* path,
                                       size_t index,
                                       char** out_text) {
  clear_error();
  if (out_text != nullptr) *out_text = nullptr;
  if (env == nullptr || record == nullptr || path == nullptr || out_text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, path and out_text are required");
  }
  bool mismatch = false;
  const auto* tables = bound_string_tables(strings, env, mismatch);
  if (mismatch) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "string tables were loaded for a different environment");
  }
  return checked("unknown exception while reading a field", [&] {
    const auto& ref = *to_ref(record);
    if (!belongs_to_env(env, &ref)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
    }
    std::string text;
    const auto status = fo4::read_record_field(env->value.plugin(ref.plugin), *ref.record, path, index, text,
                                               tables, ref.plugin);
    if (status != fo4::FieldStatus::ok) return map_status(status);
    auto* buffer = copy_text(text);
    if (buffer == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the field text");
    *out_text = buffer;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_read_winners(const radicle_env* env,
                                        const radicle_strings* strings,
                                        const char* path,
                                        size_t index,
                                        radicle_winner_field** out_rows,
                                        size_t* out_count) {
  clear_error();
  if (out_rows != nullptr) *out_rows = nullptr;
  if (out_count != nullptr) *out_count = 0;
  if (env == nullptr || path == nullptr || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, path, out_rows and out_count are required");
  }
  bool mismatch = false;
  const auto* tables = bound_string_tables(strings, env, mismatch);
  if (mismatch) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "string tables were loaded for a different environment");
  }
  return checked("unknown exception while reading winning fields", [&] {
    fo4::Signature signature{};
    const auto kind = readable_path(path, signature);
    if (kind != fo4::FieldStatus::ok) return map_status(kind);
    struct Held {
      std::vector<radicle_winner_field> rows;
      ~Held() {
        for (auto& row : rows) std::free(row.text);
      }
    } held;
    for (const auto* ref : env->value.winners_of(signature)) {
      held.rows.push_back({});
      auto& row = held.rows.back();
      const auto* owner = env->value.stable_owner(ref->record->form_key);
      row.key = radicle_form_key{owner == nullptr ? nullptr : owner->filename().data(),
                                 ref->record->form_key.local().value()};
      try {
        std::string text;
        const auto status = fo4::read_record_field(env->value.plugin(ref->plugin), *ref->record, path, index, text,
                                                   tables, ref->plugin);
        if (status == fo4::FieldStatus::occurrence_missing || status == fo4::FieldStatus::index_out_of_range
            || status != fo4::FieldStatus::ok) {
          row.text = nullptr;
        } else {
          row.text = copy_text(text);
          if (row.text == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the field text");
        }
      } catch (const std::exception&) {
        row.text = nullptr;
      }
    }
    if (held.rows.empty()) return RADICLE_STATUS_OK;
    auto* rows = static_cast<radicle_winner_field*>(std::malloc(held.rows.size() * sizeof(radicle_winner_field)));
    if (rows == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate winner rows");
    std::memcpy(rows, held.rows.data(), held.rows.size() * sizeof(radicle_winner_field));
    for (auto& row : held.rows) row.text = nullptr;
    *out_rows = rows;
    *out_count = held.rows.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_env_read_winners_free(radicle_winner_field* rows, size_t count) {
  clear_error();
  if (rows == nullptr) return;
  for (size_t index = 0; index < count; ++index) std::free(rows[index].text);
  std::free(rows);
}

radicle_status radicle_env_read_fields(const radicle_env* env,
                                       const radicle_strings* strings,
                                       const char* signature,
                                       const char* const* paths,
                                       size_t path_count,
                                       size_t index,
                                       radicle_winner_fields_row** out_rows,
                                       size_t* out_count) {
  clear_error();
  if (out_rows != nullptr) *out_rows = nullptr;
  if (out_count != nullptr) *out_count = 0;
  const auto record = to_signature(signature);
  if (env == nullptr || !record || paths == nullptr || path_count == 0 || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "env, signature, paths, path_count, out_rows and out_count are required");
  }
  for (size_t path_index = 0; path_index < path_count; ++path_index) {
    if (paths[path_index] == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "every path is required");
    }
    fo4::Signature path_record{};
    if (readable_path(paths[path_index], path_record) != fo4::FieldStatus::ok || path_record != *record) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown or mismatched field path");
    }
  }
  bool mismatch = false;
  const auto* tables = bound_string_tables(strings, env, mismatch);
  if (mismatch) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "string tables were loaded for a different environment");
  }
  return checked("unknown exception while reading winner fields", [&] {
    struct HeldRow {
      radicle_winner_fields_row row{};
      std::vector<char*> texts;
    };
    struct Held {
      std::vector<HeldRow> rows;
      ~Held() {
        for (auto& row : rows) {
          for (auto* text : row.texts) std::free(text);
        }
      }
    } held;
    for (const auto* ref : env->value.winners_of(*record)) {
      held.rows.push_back({});
      auto& held_row = held.rows.back();
      const auto* owner = env->value.stable_owner(ref->record->form_key);
      held_row.row.key = radicle_form_key{owner == nullptr ? nullptr : owner->filename().data(),
                                         ref->record->form_key.local().value()};
      held_row.texts.assign(path_count, nullptr);
      for (size_t path_index = 0; path_index < path_count; ++path_index) {
        try {
          std::string text;
          const auto status = fo4::read_record_field(env->value.plugin(ref->plugin), *ref->record, paths[path_index],
                                                     index, text, tables, ref->plugin);
          if (status == fo4::FieldStatus::occurrence_missing || status == fo4::FieldStatus::index_out_of_range
              || status != fo4::FieldStatus::ok) {
            held_row.texts[path_index] = nullptr;
          } else {
            held_row.texts[path_index] = copy_text(text);
            if (held_row.texts[path_index] == nullptr) {
              return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the field text");
            }
          }
        } catch (const std::exception&) {
          held_row.texts[path_index] = nullptr;
        }
      }
    }
    if (held.rows.empty()) return RADICLE_STATUS_OK;
    const size_t bytes = held.rows.size() * sizeof(radicle_winner_fields_row) +
                         held.rows.size() * path_count * sizeof(char*);
    auto* block = static_cast<char*>(std::malloc(bytes));
    if (block == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate winner field rows");
    auto* out = reinterpret_cast<radicle_winner_fields_row*>(block);
    auto** text_table = reinterpret_cast<char**>(block + held.rows.size() * sizeof(radicle_winner_fields_row));
    for (size_t row_index = 0; row_index < held.rows.size(); ++row_index) {
      out[row_index].key = held.rows[row_index].row.key;
      out[row_index].texts = text_table + row_index * path_count;
      for (size_t path_index = 0; path_index < path_count; ++path_index) {
        out[row_index].texts[path_index] = held.rows[row_index].texts[path_index];
        held.rows[row_index].texts[path_index] = nullptr;
      }
    }
    *out_rows = out;
    *out_count = held.rows.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_env_read_fields_free(radicle_winner_fields_row* rows, size_t count, size_t path_count) {
  clear_error();
  if (rows == nullptr || path_count == 0) return;
  for (size_t row_index = 0; row_index < count; ++row_index) {
    if (rows[row_index].texts == nullptr) continue;
    for (size_t path_index = 0; path_index < path_count; ++path_index) std::free(rows[row_index].texts[path_index]);
  }
  std::free(rows);
}

}
