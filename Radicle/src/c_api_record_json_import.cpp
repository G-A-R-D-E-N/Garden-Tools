#include "fo4/c_api_record_json_import.h"

#include "c_api_env_internal.hpp"
#include "fo4/plugin/record_json_import.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status status_for(fo4::RecordJsonImportStatus status) {
  switch (status) {
    case fo4::RecordJsonImportStatus::committed:
      return RADICLE_STATUS_OK;
    case fo4::RecordJsonImportStatus::malformed_json:
      return fail(RADICLE_STATUS_PARSE_ERROR, "record JSON is malformed");
    case fo4::RecordJsonImportStatus::form_key_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record JSON formKey differs from the target record");
    case fo4::RecordJsonImportStatus::signature_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record JSON signature differs from the target record");
    case fo4::RecordJsonImportStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::RecordJsonImportStatus::record_undecodable:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is undecodable; edits are refused");
    case fo4::RecordJsonImportStatus::refused:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record JSON import refused; the plugin is unchanged");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown record JSON import status");
}

std::vector<std::string> result_rows(const fo4::RecordJsonImportResult& result) {
  std::vector<std::string> rows;
  rows.reserve(result.entries.size());
  for (const auto& entry : result.entries) {
    rows.push_back(entry.path + "\t" + std::string(fo4::to_string(entry.status)));
  }
  return rows;
}

}

extern "C" {

radicle_status radicle_plugin_apply_record_json(radicle_plugin* plugin,
                                                radicle_form_key form_key,
                                                const char* json_utf8,
                                                uint32_t options,
                                                char*** out_results,
                                                size_t* out_count) {
  clear_error();
  if (out_results == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_results and out_count are required");
  }
  *out_results = nullptr;
  *out_count = 0;
  return guarded("unknown exception while applying record JSON", [&] {
    const auto key = to_form_key(form_key);
    if (plugin == nullptr || !key || json_utf8 == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and json_utf8 are required");
    }
    const fo4::RecordJsonImportOptions import_options{
        .allow_raw = (options & RADICLE_RECORD_JSON_ALLOW_RAW) != 0,
        .ignore_form_key = (options & RADICLE_RECORD_JSON_IGNORE_FORM_KEY) != 0,
        .skip_unknown = (options & RADICLE_RECORD_JSON_SKIP_UNKNOWN) != 0,
    };
    const auto result = fo4::record_apply_json(plugin->value, *key, json_utf8, import_options);
    const auto status = status_for(result.status);
    const auto copied = copy_strings(result_rows(result), out_results, out_count);
    return status != RADICLE_STATUS_OK ? status : copied;
  });
}

}
