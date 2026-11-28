#include "fo4/c_api_duplicate_formid.h"

#include "c_api_internal.hpp"
#include "fo4/checks/duplicate_formid.hpp"

#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct radicle_duplicate_formid_scanner {
  fo4::DuplicateFormIdCache cache;
};

namespace {

using namespace radicle_c_detail;

radicle_status marshal_result(const fo4::DuplicateFormIdScanResult& result,
                              char*** out_rows,
                              size_t* out_count) {
  if (result.error.has_value()) {
    const auto status = result.is_io_error ? RADICLE_STATUS_IO_ERROR : RADICLE_STATUS_PARSE_ERROR;
    return fail(status, result.error->c_str());
  }
  std::vector<std::string> rows;
  rows.reserve(result.duplicates.size());
  for (const auto& entry : result.duplicates) {
    char hex_buf[16]{};
    std::snprintf(hex_buf, sizeof(hex_buf), "%08X", entry.raw_form_id);
    std::string row = std::string(hex_buf) + '\t' + std::to_string(entry.count) + '\t';
    for (std::size_t i = 0; i < entry.record_types.size(); ++i) {
      if (i > 0) {
        row.push_back(',');
      }
      row += entry.record_types[i];
    }
    rows.push_back(std::move(row));
  }
  return copy_strings(rows, out_rows, out_count);
}

}  // namespace

extern "C" {

radicle_status radicle_duplicate_formid_scanner_create(
    radicle_duplicate_formid_scanner** out_scanner) {
  clear_error();
  if (!out_scanner) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_scanner is required");
  }
  *out_scanner = nullptr;
  return guard_exceptions("unknown exception while creating duplicate formid scanner", [&] {
    auto scanner = std::make_unique<radicle_duplicate_formid_scanner>();
    *out_scanner = scanner.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_duplicate_formid_scanner_destroy(radicle_duplicate_formid_scanner* scanner) {
  delete scanner;
}

radicle_status radicle_duplicate_formid_scanner_invalidate(
    radicle_duplicate_formid_scanner* scanner,
    const char* path) {
  clear_error();
  if (!scanner || !path) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scanner and path are required");
  }
  return guard_io_exceptions("unknown exception while invalidating duplicate formid scanner", [&] {
    scanner->cache.invalidate(utf8_path(path));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_duplicate_formid_scanner_scan(radicle_duplicate_formid_scanner* scanner,
                                                     const char* path,
                                                     char*** out_rows,
                                                     size_t* out_count) {
  clear_error();
  if (!scanner || !path || !out_rows || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "scanner, path, out_rows and out_count are required");
  }
  *out_rows = nullptr;
  *out_count = 0;
  return guard_io_exceptions("unknown exception while scanning duplicate formids", [&] {
    const auto result = scanner->cache.scan(utf8_path(path));
    return marshal_result(result, out_rows, out_count);
  });
}

radicle_status radicle_duplicate_formid_scan_file(const char* path,
                                                  char*** out_rows,
                                                  size_t* out_count) {
  clear_error();
  if (!path || !out_rows || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path, out_rows and out_count are required");
  }
  *out_rows = nullptr;
  *out_count = 0;
  return guard_io_exceptions("unknown exception while scanning duplicate formids", [&] {
    const auto result = fo4::scan_duplicate_form_ids_file(utf8_path(path));
    return marshal_result(result, out_rows, out_count);
  });
}

radicle_status radicle_duplicate_formid_scan_bytes(const uint8_t* bytes,
                                                   size_t size,
                                                   char*** out_rows,
                                                   size_t* out_count) {
  clear_error();
  if ((!bytes && size > 0) || !out_rows || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, out_rows and out_count are required");
  }
  *out_rows = nullptr;
  *out_count = 0;
  return guard_exceptions("unknown exception while scanning duplicate formid bytes", [&] {
    const std::span<const std::byte> span(
        reinterpret_cast<const std::byte*>(bytes), size);
    const auto result = fo4::scan_duplicate_form_ids(span);
    return marshal_result(result, out_rows, out_count);
  });
}

}  // extern "C"
