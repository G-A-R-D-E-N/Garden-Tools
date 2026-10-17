#include "fo4/c_api_loadorder.h"

#include "c_api_internal.hpp"
#include "fo4/loadorder/load_order_files.hpp"

#include <cstdlib>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using fo4::loadorder::AssembledLoadOrder;
using fo4::loadorder::OfficialTable;
using fo4::loadorder::PluginList;

OfficialTable to_table(radicle_official_table table) {
  return table == RADICLE_OFFICIAL_TABLE_AE ? OfficialTable::ae : OfficialTable::og_ng;
}

std::string flag_text(bool value) {
  return value ? "1" : "0";
}

template <class Fn>
radicle_status guarded_io(const char* what, Fn&& fn) {
  try {
    return fn();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

void free_partial(char** strings, std::size_t count) {
  if (strings == nullptr) return;
  for (std::size_t i = 0; i < count; ++i) std::free(strings[i]);
  std::free(strings);
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_list_read(const char* path, char*** out_rows, size_t* out_count) {
  clear_error();
  if (path == nullptr || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path, out_rows and out_count are required");
  }
  *out_rows = nullptr;
  *out_count = 0;
  return guarded_io("unknown exception while reading the plugin list", [&] {
    const auto list = fo4::loadorder::read_plugin_list(utf8_path(path));
    std::vector<std::string> rows;
    for (const auto& entry : list.entries) {
      rows.push_back(entry.name + '\t' + flag_text(entry.active) + '\t' + std::to_string(entry.line));
    }
    return copy_strings(rows, out_rows, out_count);
  });
}

radicle_status radicle_ccc_read(const char* path, char*** out_names, size_t* out_count) {
  clear_error();
  if (path == nullptr || out_names == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path, out_names and out_count are required");
  }
  *out_names = nullptr;
  *out_count = 0;
  return guarded_io("unknown exception while reading Fallout4.ccc", [&] {
    return copy_strings(fo4::loadorder::read_ccc(utf8_path(path)), out_names, out_count);
  });
}

radicle_status radicle_load_order_assemble(const char* data_dir,
                                           const char* plugin_list_path,
                                           const char* ccc_path,
                                           radicle_official_table table,
                                           char*** out_rows,
                                           size_t* out_row_count,
                                           char*** out_findings,
                                           size_t* out_finding_count,
                                           int32_t* out_failed) {
  clear_error();
  if (data_dir == nullptr || out_rows == nullptr || out_row_count == nullptr || out_findings == nullptr ||
      out_finding_count == nullptr || out_failed == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "data_dir and all outputs are required");
  }
  *out_rows = nullptr;
  *out_row_count = 0;
  *out_findings = nullptr;
  *out_finding_count = 0;
  *out_failed = 0;
  if (table != RADICLE_OFFICIAL_TABLE_OG_NG && table != RADICLE_OFFICIAL_TABLE_AE) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown official table");
  }
  return guarded_io("unknown exception while assembling the load order", [&] {
    const auto dir = utf8_path(data_dir);
    if (!std::filesystem::is_directory(dir)) {
      return fail(RADICLE_STATUS_IO_ERROR, "data_dir is not a directory");
    }
    const PluginList list = plugin_list_path == nullptr
                                ? PluginList{}
                                : fo4::loadorder::read_plugin_list(utf8_path(plugin_list_path));
    const auto ccc = ccc_path == nullptr ? std::vector<std::string>{} : fo4::loadorder::read_ccc(utf8_path(ccc_path));
    const AssembledLoadOrder assembled = fo4::loadorder::assemble_load_order(dir, list, ccc, to_table(table));

    std::vector<std::string> rows;
    for (const auto& entry : assembled.entries) {
      rows.push_back(entry.name + '\t' + flag_text(entry.checked) + '\t' + flag_text(entry.master_flag) + '\t' +
                     flag_text(entry.light));
    }
    std::vector<std::string> findings;
    for (const auto& finding : assembled.findings) {
      findings.push_back(finding.code + '\t' + finding.plugin + '\t' + finding.detail);
    }

    auto status = copy_strings(rows, out_rows, out_row_count);
    if (status == RADICLE_STATUS_OK) status = copy_strings(findings, out_findings, out_finding_count);
    if (status != RADICLE_STATUS_OK) {
      free_partial(*out_rows, *out_row_count);
      *out_rows = nullptr;
      *out_row_count = 0;
      return status;
    }
    *out_failed = assembled.failed ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
