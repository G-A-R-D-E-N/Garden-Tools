#include "c_api_env_internal.hpp"
#include "fo4/environment/strings.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct radicle_strings {
  const radicle_env* env{nullptr};
  fo4::env::StringTables value;
  std::vector<std::array<std::string, 3>> paths;
};

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

std::optional<fo4::env::StringSource> to_source(radicle_string_source source) {
  switch (source) {
    case RADICLE_STRING_SOURCE_NORMAL:
      return fo4::env::StringSource::normal;
    case RADICLE_STRING_SOURCE_DL:
      return fo4::env::StringSource::dl;
    case RADICLE_STRING_SOURCE_IL:
      return fo4::env::StringSource::il;
  }
  return std::nullopt;
}

radicle_status found_text(std::optional<std::string_view> text, const char** out_text, size_t* out_size) {
  if (!text) return fail(RADICLE_STATUS_NOT_FOUND, "string not found");
  *out_text = text->data();
  if (out_size != nullptr) *out_size = text->size();
  return RADICLE_STATUS_OK;
}

}  // namespace

namespace radicle_c_env_detail {

radicle_status load_strings(const radicle_env* env,
                            const char* strings_dir,
                            const char* language,
                            const fo4::archive::AssetResolver* assets,
                            radicle_strings** out_strings) {
  clear_error();
  if (env == nullptr || strings_dir == nullptr || language == nullptr || out_strings == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, strings_dir, language and out_strings are required");
  }
  *out_strings = nullptr;
  return guarded("unknown exception while loading string tables", [&] {
    auto strings = std::make_unique<radicle_strings>();
    strings->env = env;
    strings->value = fo4::env::StringTables::load(env->value, utf8_path(strings_dir), language, assets);
    strings->paths.resize(env->value.plugins().size());
    for (std::size_t plugin = 0; plugin < strings->paths.size(); ++plugin) {
      for (const auto source : {fo4::env::StringSource::normal, fo4::env::StringSource::dl,
                                fo4::env::StringSource::il}) {
        if (const auto* path = strings->value.table_path(plugin, source)) {
          const auto utf8 = path->u8string();
          strings->paths[plugin][static_cast<std::size_t>(source)].assign(utf8.begin(), utf8.end());
        }
      }
    }
    *out_strings = strings.release();
    return RADICLE_STATUS_OK;
  });
}

}  // namespace radicle_c_env_detail

extern "C" {

radicle_status radicle_string_source_for(const char* record_signature,
                                         const char* subrecord_signature,
                                         radicle_string_source* out_source) {
  clear_error();
  const auto record = to_signature(record_signature);
  const auto subrecord = to_signature(subrecord_signature);
  if (!record || !subrecord || out_source == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "two four-character signatures and out_source are required");
  }
  *out_source = static_cast<radicle_string_source>(fo4::env::string_source(*record, *subrecord));
  return RADICLE_STATUS_OK;
}

radicle_status radicle_strings_load(const radicle_env* env,
                                    const char* strings_dir,
                                    const char* language,
                                    radicle_strings** out_strings) {
  return load_strings(env, strings_dir, language, nullptr, out_strings);
}

void radicle_strings_destroy(radicle_strings* strings) {
  clear_error();
  delete strings;
}

radicle_status radicle_strings_table_path(const radicle_strings* strings,
                                          size_t plugin_index,
                                          radicle_string_source source,
                                          const char** out_path) {
  clear_error();
  const auto kind = to_source(source);
  if (strings == nullptr || !kind || out_path == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "strings, a valid source and out_path are required");
  }
  *out_path = nullptr;
  if (plugin_index >= strings->paths.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin index is out of range");
  }
  const auto& path = strings->paths[plugin_index][static_cast<std::size_t>(*kind)];
  if (path.empty()) return fail(RADICLE_STATUS_NOT_FOUND, "no string table for this plugin and source");
  *out_path = path.c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_strings_lookup(const radicle_strings* strings,
                                      size_t plugin_index,
                                      radicle_string_source source,
                                      uint32_t string_id,
                                      const char** out_text,
                                      size_t* out_size) {
  clear_error();
  const auto kind = to_source(source);
  if (strings == nullptr || !kind || out_text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "strings, a valid source and out_text are required");
  }
  *out_text = nullptr;
  if (out_size != nullptr) *out_size = 0;
  if (plugin_index >= strings->paths.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin index is out of range");
  }
  return found_text(strings->value.lookup(plugin_index, *kind, string_id), out_text, out_size);
}

radicle_status radicle_strings_resolve_name(const radicle_strings* strings,
                                            const radicle_record* record,
                                            const char** out_text,
                                            size_t* out_size) {
  clear_error();
  if (strings == nullptr || record == nullptr || out_text == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "strings, record and out_text are required");
  }
  if (!belongs_to_env(strings->env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_text = nullptr;
  if (out_size != nullptr) *out_size = 0;
  return guarded("unknown exception while resolving a record name", [&] {
    return found_text(strings->value.resolve_name(strings->env->value, *to_ref(record)), out_text, out_size);
  });
}

}  // extern "C"
