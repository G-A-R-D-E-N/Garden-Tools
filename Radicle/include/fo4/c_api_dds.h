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

RADICLE_API radicle_status radicle_dds_decode_rgba_face(const uint8_t* bytes,
                                                        size_t size,
                                                        uint32_t face,
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

RADICLE_API radicle_status radicle_dds_layout(const uint8_t* bytes,
                                              size_t size,
                                              uint32_t* out_data_offset,
                                              uint32_t* out_face_count,
                                              int32_t* out_is_cube_map);

/* Requires a block-compressed DDS and mip_count > 0; *out_bytes is the stored blocks of mips [first_mip, first_mip + mip_count) of one face, malloc'd, release with radicle_free_bytes. */
/* Invariant: unsigned_rgtc nonzero rewrites signed BC4 and BC5 blocks to unsigned; a file shorter than the requested mips returns RADICLE_STATUS_PARSE_ERROR. */
RADICLE_API radicle_status radicle_dds_block_chain(const uint8_t* bytes,
                                                   size_t size,
                                                   uint32_t face,
                                                   uint32_t first_mip,
                                                   uint32_t mip_count,
                                                   uint8_t unsigned_rgtc,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);

/* Requires a block-compressed DDS and 1 <= mip_count <= mips_per_face <= the full chain of the larger side; face n starts at n * (the bytes of mips_per_face mips) after the header, so a cube with no cube flag slices like a flagged one. */
/* Invariant: *out_bytes is the first mip_count mips of that face, malloc'd, release with radicle_free_bytes; a file that ends inside them returns RADICLE_STATUS_PARSE_ERROR; unsigned_rgtc acts as in radicle_dds_block_chain. */
RADICLE_API radicle_status radicle_dds_face_blocks(const uint8_t* bytes,
                                                   size_t size,
                                                   uint32_t face,
                                                   uint32_t mips_per_face,
                                                   uint32_t mip_count,
                                                   uint8_t unsigned_rgtc,
                                                   uint8_t** out_bytes,
                                                   size_t* out_size);

/* Requires rgba_size == width * height * 4 and nonzero width and height; *out_png is malloc'd, release with radicle_free_bytes. */
RADICLE_API radicle_status radicle_png_encode_rgba(const uint8_t* rgba,
                                                   size_t rgba_size,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint8_t** out_png,
                                                   size_t* out_size);

#ifdef __cplusplus
}
#endif
