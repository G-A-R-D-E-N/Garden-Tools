#include "fo4/c_api_lod.h"

#include "c_api_internal.hpp"
#include "fo4/lod/lod.hpp"

#include <cstddef>
#include <new>
#include <span>

extern "C" {

radicle_status radicle_lod_settings_read(const uint8_t* bytes,
                                         size_t size,
                                         int32_t* out_southwest_x,
                                         int32_t* out_southwest_y,
                                         int32_t* out_stride,
                                         int32_t* out_level_min,
                                         int32_t* out_level_max) {
  using namespace radicle_c_detail;
  clear_error();
  if (out_southwest_x) *out_southwest_x = 0;
  if (out_southwest_y) *out_southwest_y = 0;
  if (out_stride) *out_stride = 0;
  if (out_level_min) *out_level_min = 0;
  if (out_level_max) *out_level_max = 0;
  if (!bytes || size == 0 || !out_southwest_x || !out_southwest_y || !out_stride || !out_level_min || !out_level_max) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and every output are required");
  }
  try {
    const auto settings =
        fo4::lod::parse_lod_settings(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
    *out_southwest_x = settings.southwest_x;
    *out_southwest_y = settings.southwest_y;
    *out_stride = settings.stride;
    *out_level_min = settings.level_min;
    *out_level_max = settings.level_max;
    return RADICLE_STATUS_OK;
  } catch (const fo4::lod::LodError& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception");
  }
}

}
