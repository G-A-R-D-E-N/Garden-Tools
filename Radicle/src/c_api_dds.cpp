#include "fo4/c_api_dds.h"

#include "c_api_internal.hpp"
#include "fo4/texture/dds.hpp"
#include "fo4/texture/png.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace radicle_c_detail;

radicle_status map_dds_error(const fo4::texture::dds_error& error) {
  const auto status = error.type() == "TooLarge" ? RADICLE_STATUS_INVALID_ARGUMENT : RADICLE_STATUS_PARSE_ERROR;
  return fail(status, error.what());
}

radicle_status decode_face(const uint8_t* bytes,
                           size_t size,
                           uint32_t face,
                           uint32_t mip,
                           uint8_t reconstruct_z,
                           uint8_t** out_rgba,
                           uint32_t* out_width,
                           uint32_t* out_height) {
  clear_error();
  if (out_rgba) *out_rgba = nullptr;
  if (out_width) *out_width = 0;
  if (out_height) *out_height = 0;
  if (!bytes || size == 0 || !out_rgba || !out_width || !out_height) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size, out_rgba, out_width and out_height are required");
  }

  try {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const auto rgba = fo4::texture::decode_dds_rgba(
        std::span<const std::uint8_t>(bytes, size), face, mip, reconstruct_z != 0, width, height);
    if (rgba.empty()) return RADICLE_STATUS_OK;
    auto* buffer = static_cast<uint8_t*>(std::malloc(rgba.size()));
    if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the decoded pixels");
    std::memcpy(buffer, rgba.data(), rgba.size());
    *out_rgba = buffer;
    *out_width = width;
    *out_height = height;
    return RADICLE_STATUS_OK;
  } catch (const fo4::texture::dds_error& error) {
    return map_dds_error(error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while decoding a DDS");
  }
}

}

extern "C" {

radicle_status radicle_dds_decode_rgba(const uint8_t* bytes,
                                       size_t size,
                                       uint32_t mip,
                                       uint8_t reconstruct_z,
                                       uint8_t** out_rgba,
                                       uint32_t* out_width,
                                       uint32_t* out_height) {
  return decode_face(bytes, size, 0, mip, reconstruct_z, out_rgba, out_width, out_height);
}

radicle_status radicle_dds_decode_rgba_face(const uint8_t* bytes,
                                            size_t size,
                                            uint32_t face,
                                            uint32_t mip,
                                            uint8_t reconstruct_z,
                                            uint8_t** out_rgba,
                                            uint32_t* out_width,
                                            uint32_t* out_height) {
  return decode_face(bytes, size, face, mip, reconstruct_z, out_rgba, out_width, out_height);
}

radicle_status radicle_dds_info(const uint8_t* bytes,
                                size_t size,
                                uint32_t* out_format,
                                uint32_t* out_mip_count,
                                uint32_t* out_width,
                                uint32_t* out_height,
                                uint64_t* out_payload_size,
                                int32_t* out_bc5) {
  clear_error();
  if (out_format) *out_format = 0;
  if (out_mip_count) *out_mip_count = 0;
  if (out_width) *out_width = 0;
  if (out_height) *out_height = 0;
  if (out_payload_size) *out_payload_size = 0;
  if (out_bc5) *out_bc5 = 0;
  if (!bytes || size == 0 || !out_format || !out_mip_count || !out_width || !out_height || !out_payload_size ||
      !out_bc5) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and every output are required");
  }

  try {
    const auto info = fo4::texture::read_dds(std::span<const std::uint8_t>(bytes, size));
    *out_format = info.dxgi_format();
    *out_mip_count = static_cast<std::uint32_t>(info.mip_count);
    *out_width = static_cast<std::uint32_t>(info.width);
    *out_height = static_cast<std::uint32_t>(info.height);
    *out_payload_size = static_cast<std::uint64_t>(info.payload_size());
    const auto dxgi = info.dxgi_format();
    *out_bc5 = dxgi == 82 || dxgi == 83 ? 1 : 0;
    return RADICLE_STATUS_OK;
  } catch (const fo4::texture::dds_error& error) {
    return map_dds_error(error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading a DDS header");
  }
}

radicle_status radicle_dds_layout(const uint8_t* bytes,
                                  size_t size,
                                  uint32_t* out_data_offset,
                                  uint32_t* out_face_count,
                                  int32_t* out_is_cube_map) {
  clear_error();
  if (out_data_offset) *out_data_offset = 0;
  if (out_face_count) *out_face_count = 0;
  if (out_is_cube_map) *out_is_cube_map = 0;
  if (!bytes || size == 0 || !out_data_offset || !out_face_count || !out_is_cube_map) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "bytes, size, out_data_offset, out_face_count and out_is_cube_map are required");
  }

  try {
    const auto info = fo4::texture::read_dds(std::span<const std::uint8_t>(bytes, size));
    *out_data_offset = static_cast<std::uint32_t>(info.data_offset);
    *out_face_count = static_cast<std::uint32_t>(info.face_count());
    *out_is_cube_map = info.is_cube_map ? 1 : 0;
    return RADICLE_STATUS_OK;
  } catch (const fo4::texture::dds_error& error) {
    return map_dds_error(error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading a DDS header");
  }
}

radicle_status radicle_dds_block_chain(const uint8_t* bytes,
                                       size_t size,
                                       uint32_t face,
                                       uint32_t first_mip,
                                       uint32_t mip_count,
                                       uint8_t unsigned_rgtc,
                                       uint8_t** out_bytes,
                                       size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (!bytes || size == 0 || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size, out_bytes and out_size are required");
  }

  try {
    const auto blocks = fo4::texture::read_dds_block_chain(std::span<const std::uint8_t>(bytes, size), face,
                                                           first_mip, mip_count, unsigned_rgtc != 0);
    auto* buffer = static_cast<uint8_t*>(std::malloc(blocks.size()));
    if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the DDS blocks");
    std::memcpy(buffer, blocks.data(), blocks.size());
    *out_bytes = buffer;
    *out_size = blocks.size();
    return RADICLE_STATUS_OK;
  } catch (const fo4::texture::dds_error& error) {
    return map_dds_error(error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading DDS blocks");
  }
}

radicle_status radicle_dds_face_blocks(const uint8_t* bytes,
                                       size_t size,
                                       uint32_t face,
                                       uint32_t mips_per_face,
                                       uint32_t mip_count,
                                       uint8_t unsigned_rgtc,
                                       uint8_t** out_bytes,
                                       size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (!bytes || size == 0 || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size, out_bytes and out_size are required");
  }

  try {
    const auto blocks = fo4::texture::read_dds_face_blocks(std::span<const std::uint8_t>(bytes, size), face,
                                                           mips_per_face, mip_count, unsigned_rgtc != 0);
    auto* buffer = static_cast<uint8_t*>(std::malloc(blocks.size()));
    if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the DDS face blocks");
    std::memcpy(buffer, blocks.data(), blocks.size());
    *out_bytes = buffer;
    *out_size = blocks.size();
    return RADICLE_STATUS_OK;
  } catch (const fo4::texture::dds_error& error) {
    return map_dds_error(error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading DDS face blocks");
  }
}

radicle_status radicle_png_encode_rgba(const uint8_t* rgba,
                                       size_t rgba_size,
                                       uint32_t width,
                                       uint32_t height,
                                       uint8_t** out_png,
                                       size_t* out_size) {
  clear_error();
  if (out_png) *out_png = nullptr;
  if (out_size) *out_size = 0;
  if (!rgba || rgba_size == 0 || !out_png || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "rgba, rgba_size, out_png and out_size are required");
  }

  try {
    const auto png = fo4::texture::encode_png_rgba(std::span<const std::uint8_t>(rgba, rgba_size), width, height);
    auto* buffer = static_cast<uint8_t*>(std::malloc(png.size()));
    if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the PNG bytes");
    std::memcpy(buffer, png.data(), png.size());
    *out_png = buffer;
    *out_size = png.size();
    return RADICLE_STATUS_OK;
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while encoding a PNG");
  }
}

}
