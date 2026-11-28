#include "fo4/c_api_nif.h"

#include "c_api_internal.hpp"
#include "fo4/nif/header.hpp"

#include <cstddef>
#include <new>
#include <span>
#include <string>

extern "C" {

radicle_status radicle_nif_header_read(const uint8_t* bytes,
                                       size_t size,
                                       uint32_t* out_file_version,
                                       uint32_t* out_user_version,
                                       uint32_t* out_block_count,
                                       int32_t* out_readable,
                                       uint8_t** out_type_names,
                                       size_t* out_type_names_size) {
  using namespace radicle_c_detail;
  clear_error();
  if (out_file_version) *out_file_version = 0;
  if (out_user_version) *out_user_version = 0;
  if (out_block_count) *out_block_count = 0;
  if (out_readable) *out_readable = 0;
  if (out_type_names) *out_type_names = nullptr;
  if (out_type_names_size) *out_type_names_size = 0;
  if (!bytes || size == 0 || !out_file_version || !out_user_version || !out_block_count || !out_readable ||
      !out_type_names || !out_type_names_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and every output are required");
  }
  try {
    const auto info = fo4::nif::read_nif_header(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
    std::string names;
    for (std::size_t i = 0; i < info.block_types.size(); ++i) {
      if (i != 0) names.push_back('\0');
      names += info.block_types[i];
    }
    constexpr CopyMessages messages{"out_type_names and out_type_names_size are required",
                                    "failed to allocate the NIF block type names"};
    const auto status = copy_out(names, out_type_names, out_type_names_size, messages);
    if (status != RADICLE_STATUS_OK) return status;
    *out_file_version = info.file_version;
    *out_user_version = info.user_version;
    *out_block_count = info.block_count;
    *out_readable = info.readable ? 1 : 0;
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception");
  }
}

radicle_status radicle_nif_skin_scan(const uint8_t* bytes, size_t size, int32_t* out_scan) {
  using namespace radicle_c_detail;
  clear_error();
  if (out_scan) *out_scan = 2;
  if (!bytes || size == 0 || !out_scan) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_scan are required");
  *out_scan = static_cast<int32_t>(
      fo4::nif::scan_nif_skin(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size)));
  return RADICLE_STATUS_OK;
}

}
