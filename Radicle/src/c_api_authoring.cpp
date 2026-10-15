#include "fo4/c_api_authoring.h"

#include "c_api_env_internal.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/text/encoding.hpp"

#include <cstring>
#include <optional>
#include <string_view>

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

}  // extern "C"
