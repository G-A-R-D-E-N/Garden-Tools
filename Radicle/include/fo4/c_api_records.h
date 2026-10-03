#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_record_payload_info(
    const radicle_env* env, const radicle_record* record,
    uint16_t* out_form_version, size_t* out_subrecord_count);
RADICLE_API radicle_status radicle_record_copy_subrecord(
    const radicle_env* env, const radicle_record* record, size_t index,
    uint8_t* out_signature, uint8_t** out_bytes, size_t* out_size,
    int32_t* out_extended_size);
RADICLE_API radicle_status radicle_record_resolve_form_id(
    const radicle_env* env, const radicle_record* record, uint32_t raw_form_id,
    radicle_form_key* out_key);

#ifdef __cplusplus
}
#endif
