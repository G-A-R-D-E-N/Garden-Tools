#include "fo4/c_api_dds.h"

#include "c_api_internal.hpp"
#include "fo4/texture/dds.hpp"

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

}

extern "C" {

radicle_status radicle_dds_decode_rgba(const uint8_t* bytes,
                                       size_t size,
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
        std::span<const std::uint8_t>(bytes, size), mip, reconstruct_z != 0, width, height);
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
    *out_bc5 = dxgi == 82 || dxgi == 83 || dxgi == 84 ? 1 : 0;
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

}
