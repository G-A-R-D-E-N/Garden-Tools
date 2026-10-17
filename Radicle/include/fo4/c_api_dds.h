#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DDS container header information and one-mip RGBA pixel decode. `mip` is a
 * zero-based mip index; a file with no mip count in its header is treated as
 * one mip. `reconstruct_z` rebuilds the blue channel from red and green, which
 * BC5 normal maps need, and is ignored for other formats.
 *
 * The returned RGBA buffer is width * height * 4 bytes and is released with
 * radicle_free_bytes. Errors: "InvalidDds" and "UnsupportedFormat" as
 * RADICLE_STATUS_PARSE_ERROR, and "TooLarge" as
 * RADICLE_STATUS_INVALID_ARGUMENT, with the reason in radicle_last_error.
 */
RADICLE_API radicle_status radicle_dds_decode_rgba(const uint8_t* bytes,
                                                   size_t size,
                                                   uint32_t mip,
                                                   uint8_t reconstruct_z,
                                                   uint8_t** out_rgba,
                                                   uint32_t* out_width,
                                                   uint32_t* out_height);

/*
 * Header information without decoding: the DXGI format code (71 is BC1_UNORM,
 * 83 BC5_UNORM, 98 BC7_UNORM and so on), the mip count, the base width and
 * height, the byte size of every mip together, and whether the format is a BC5
 * normal map.
 */
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
