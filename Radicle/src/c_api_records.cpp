#include "c_api_env_internal.hpp"
#include "fo4/c_api_records.h"

using namespace radicle_c_env_detail;

extern "C" {

radicle_status radicle_record_payload_info(
    const radicle_env* env, const radicle_record* record,
    uint16_t* out_form_version, size_t* out_subrecord_count) {
  clear_error();
  if (out_form_version != nullptr) *out_form_version = 0;
  if (out_subrecord_count != nullptr) *out_subrecord_count = 0;
  if (env == nullptr || record == nullptr || out_form_version == nullptr || out_subrecord_count == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and outputs are required");
  const auto& value = *to_ref(record)->record;
  *out_form_version = value.header.form_version;
  *out_subrecord_count = value.subrecords.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_record_copy_subrecord(
    const radicle_env* env, const radicle_record* record, size_t index,
    uint8_t* out_signature, uint8_t** out_bytes, size_t* out_size,
    int32_t* out_extended_size) {
  clear_error();
  if (out_bytes != nullptr) *out_bytes = nullptr;
  if (out_size != nullptr) *out_size = 0;
  if (out_extended_size != nullptr) *out_extended_size = 0;
  if (out_signature != nullptr) std::memset(out_signature, 0, 4);
  if (env == nullptr || record == nullptr || out_signature == nullptr || out_bytes == nullptr ||
      out_size == nullptr || out_extended_size == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and outputs are required");
  const auto& subs = to_ref(record)->record->subrecords;
  if (index >= subs.size()) return fail(RADICLE_STATUS_NOT_FOUND, "subrecord index is out of range");
  const auto& sub = subs[index];
  if (!sub.data.empty()) {
    auto* copy = static_cast<uint8_t*>(std::malloc(sub.data.size()));
    if (copy == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to copy subrecord bytes");
    std::memcpy(copy, sub.data.data(), sub.data.size());
    *out_bytes = copy;
  }
  std::memcpy(out_signature, sub.signature.bytes().data(), 4);
  *out_size = sub.data.size();
  *out_extended_size = sub.extended_size ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_record_resolve_form_id(
    const radicle_env* env, const radicle_record* record, uint32_t raw_form_id,
    radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (env == nullptr || record == nullptr || out_key == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_key are required");
  return guarded("unknown exception while resolving record FormID", [&] {
    const fo4::RawFormId raw{raw_form_id};
    if (raw.is_none()) return RADICLE_STATUS_OK;
    const auto& ref = *to_ref(record);
    // Invalid indices must be rejected before coercion (source:src/formid.cpp:resolve_plugin_link).
    if (raw.is_created() || raw.index_byte() > env->value.plugins().at(ref.plugin).masters.size())
      return fail(RADICLE_STATUS_PARSE_ERROR, "FormID index is outside the record plugin's master table");
    *out_key = to_c_key(*env, env->value.resolve_link(ref, raw));
    return RADICLE_STATUS_OK;
  });
}

}
