#pragma once

#include "c_api_env_internal.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/text/encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace radicle_c_forms_detail {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

// Returned c_str() pointers must survive later insertions, so the store is a deque.
struct TextStore {
  std::deque<std::string> strings;

  const char* keep(std::string_view stored) {
    strings.emplace_back(fo4::text::windows1252_to_utf8(stored));
    return strings.back().c_str();
  }

  const char* keep(const std::optional<std::string>& stored) {
    return stored ? keep(std::string_view(*stored)) : nullptr;
  }

  void keep(const std::optional<fo4::schema::LocalizedString>& value,
            radicle_name_kind& kind,
            const char*& text,
            uint32_t& string_id) {
    kind = RADICLE_NAME_ABSENT;
    text = nullptr;
    string_id = 0;
    if (!value) return;
    if (value->id) {
      kind = RADICLE_NAME_STRING_ID;
      string_id = *value->id;
      return;
    }
    if (value->raw_id) {
      kind = RADICLE_NAME_MALFORMED;
      return;
    }
    if (value->text) {
      kind = RADICLE_NAME_INLINE;
      text = keep(value->text);
    }
  }
};

inline std::optional<std::string_view> optional_view(const char* text) {
  if (text == nullptr) return std::nullopt;
  return std::string_view(text);
}

inline bool text_representable(const char* text) {
  if (text == nullptr) return true;
  return fo4::text::utf8_to_windows1252(text).has_value();
}

inline bool texts_representable(std::initializer_list<const char*> values) {
  for (const auto* value : values) {
    if (!text_representable(value)) return false;
  }
  return true;
}

inline radicle_status bad_text() {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT,
              "text must be valid UTF-8 representable in Windows-1252");
}

inline radicle_status require_env_record(const radicle_env* env,
                                         const radicle_record* record,
                                         const void* out) {
  if (env == nullptr || record == nullptr || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and an output are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return RADICLE_STATUS_OK;
}

inline radicle_status require_plugin_record(const radicle_plugin* plugin,
                                            radicle_form_key key,
                                            std::optional<fo4::FormKey>& out_key) {
  if (plugin == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  out_key = to_form_key(key);
  if (!out_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record key is required");
  if (fo4::find_record(plugin->value, *out_key) == nullptr) {
    return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
  }
  return RADICLE_STATUS_OK;
}

}  // namespace radicle_c_forms_detail
