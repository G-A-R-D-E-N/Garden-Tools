#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

RADICLE_API radicle_status radicle_dds_decode_rgba(const uint8_t* bytes,
                                                   size_t size,
                                                   uint32_t mip,
                                                   uint8_t reconstruct_z,
                                                   uint8_t** out_rgba,
                                                   uint32_t* out_width,
                                                   uint32_t* out_height);

RADICLE_API radicle_status radicle_dds_info(const uint8_t* bytes,
                                            size_t size,
                                            uint32_t* out_format,
                                            uint32_t* out_mip_count,
                                            uint32_t* out_width,
                                            uint32_t* out_height,
                                            uint64_t* out_payload_size,
                                            int32_t* out_bc5);

#ifdef __cplusplus
}
#endif
