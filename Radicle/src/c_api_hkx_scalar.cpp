#include "fo4/c_api_hkx.h"

#include "c_api_internal.hpp"
#include "fo4/hkx/scalar_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct radicle_hkx_scalar_list {
  std::vector<fo4::hkx::HkxScalarMember> rows;
};

namespace {

using namespace radicle_c_detail;
using fo4::hkx::HkxScalarStatus;

constexpr CopyMessages k_string{"out_bytes and out_size are required", "failed to allocate an HKX scalar string"};

radicle_status refuse(HkxScalarStatus status, const std::string& why) {
  radicle_status code = RADICLE_STATUS_INVALID_ARGUMENT;
  switch (status) {
    case HkxScalarStatus::Ok:
      return RADICLE_STATUS_OK;
    case HkxScalarStatus::BadPackfile:
      code = RADICLE_STATUS_PARSE_ERROR;
      break;
    case HkxScalarStatus::NoObject:
    case HkxScalarStatus::NoMember:
      code = RADICLE_STATUS_NOT_FOUND;
      break;
    case HkxScalarStatus::VerifyFailed:
      code = RADICLE_STATUS_INTERNAL_ERROR;
      break;
    default:
      break;
  }
  const std::string message = std::string(fo4::hkx::hkx_scalar_status_name(status)) + ": " + why;
  return fail(code, message.c_str());
}

const fo4::hkx::HkxScalarMember* row_at(const radicle_hkx_scalar_list* list, size_t index, radicle_status& status) {
  if (!list) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "list is required");
    return nullptr;
  }
  if (index >= list->rows.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "scalar member index is out of range");
    return nullptr;
  }
  return &list->rows[index];
}

}  // namespace

extern "C" {

radicle_status radicle_hkx_scalar_list_read(const uint8_t* bytes,
                                            size_t size,
                                            size_t object_index,
                                            radicle_hkx_scalar_list** out_list) {
  clear_error();
  if (out_list) *out_list = nullptr;
  if (!bytes || size == 0 || !out_list) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_list are required");
  }
  return guard_exceptions("unknown exception while listing HKX scalar members", [&] {
    auto list = std::make_unique<radicle_hkx_scalar_list>();
    std::string why;
    const auto status = fo4::hkx::list_scalar_members(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size), object_index, list->rows, why);
    if (status != HkxScalarStatus::Ok) return refuse(status, why);
    *out_list = list.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_hkx_scalar_list_destroy(radicle_hkx_scalar_list* list) {
  clear_error();
  delete list;
}

radicle_status radicle_hkx_scalar_list_count(const radicle_hkx_scalar_list* list, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (!list || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "list and out_count are required");
  *out_count = list->rows.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_scalar_list_read_info(const radicle_hkx_scalar_list* list,
                                                 size_t index,
                                                 radicle_hkx_scalar_info* out_info) {
  clear_error();
  if (out_info) *out_info = radicle_hkx_scalar_info{};
  radicle_status status = RADICLE_STATUS_OK;
  const auto* row = row_at(list, index, status);
  if (!row) return status;
  if (!out_info) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  radicle_hkx_scalar_info info{};
  info.kind = static_cast<int32_t>(row->kind);
  info.is_enum = row->is_enum ? 1 : 0;
  info.is_flags = row->is_flags ? 1 : 0;
  info.offset = row->offset;
  info.size = row->size;
  *out_info = info;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_hkx_scalar_list_text(const radicle_hkx_scalar_list* list,
                                            size_t index,
                                            int32_t which,
                                            uint8_t** out_bytes,
                                            size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  radicle_status status = RADICLE_STATUS_OK;
  const auto* row = row_at(list, index, status);
  if (!row) return status;
  const std::string* text = nullptr;
  switch (which) {
    case 0: text = &row->name; break;
    case 1: text = &row->type; break;
    case 2: text = &row->value; break;
    case 3: text = &row->enum_name; break;
    default: return fail(RADICLE_STATUS_INVALID_ARGUMENT, "which must be 0 to 3");
  }
  return copy_out(*text, out_bytes, out_size, k_string);
}

radicle_status radicle_hkx_set_scalar(const uint8_t* bytes,
                                      size_t size,
                                      size_t object_index,
                                      const char* member,
                                      const char* value,
                                      uint8_t** out_bytes,
                                      size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (!bytes || size == 0 || !member || !value || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size, member, value, out_bytes and out_size are required");
  }
  return guard_exceptions("unknown exception while writing an HKX scalar member", [&] {
    std::vector<std::byte> produced;
    std::string why;
    const auto status = fo4::hkx::set_scalar_member(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size), object_index, member, value,
        produced, why);
    if (status != HkxScalarStatus::Ok) return refuse(status, why);
    return copy_out(produced, out_bytes, out_size, k_string);
  });
}

}  // extern "C"
