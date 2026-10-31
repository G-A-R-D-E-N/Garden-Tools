#include "c_api_env_internal.hpp"

#include "fo4/c_api_previs.h"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/previs_data.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct radicle_previs_report {
  std::vector<fo4::previs::CellDiagnosis> cells;
  fo4::previs::PrevisDataSummary summary;
  std::size_t broken{0};
  std::size_t outdated{0};
  std::size_t skipped{0};
};

namespace {

using namespace radicle_c_detail;

radicle_status check_report(const radicle_previs_report* report) {
  if (report == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "previs report is null");
  return RADICLE_STATUS_OK;
}

radicle_status check_cell_index(const radicle_previs_report* report, size_t cell_index) {
  const auto status = check_report(report);
  if (status != RADICLE_STATUS_OK) return status;
  if (cell_index >= report->cells.size()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "previs cell index is outside the report");
  }
  return RADICLE_STATUS_OK;
}

radicle_form_key to_c_form_key(const fo4::FormKey& key) {
  return radicle_form_key{key.owner().filename().data(), key.local().value()};
}

}  // namespace

extern "C" {

radicle_status radicle_previs_scan(const radicle_env* env, const char* data_dir,
                                   radicle_previs_report** out_report) {
  clear_error();
  if (out_report == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_report is null");
  }
  *out_report = nullptr;
  if (env == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "environment is null");
  if (data_dir == nullptr || *data_dir == '\0') {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "data_dir is empty");
  }
  try {
    const auto directory = utf8_path(data_dir);
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
      return fail(RADICLE_STATUS_IO_ERROR, "data_dir is not a readable directory");
    }
    const std::filesystem::path roots[]{directory};
    const auto indexes = fo4::previs::PrevisData::load(roots, env->value);
    auto report = std::make_unique<radicle_previs_report>();
    report->summary = indexes.summary();
    report->cells = fo4::previs::diagnose_load_order(env->value, indexes.indexes());
    for (const auto& cell : report->cells) {
      if (cell.status.broken) ++report->broken;
      if (cell.status.outdated) ++report->outdated;
      if (cell.status.skip_unsupported) ++report->skipped;
    }
    *out_report = report.release();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc&) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "previs scan ran out of memory");
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  }
}

void radicle_previs_report_destroy(radicle_previs_report* report) { delete report; }

radicle_status radicle_previs_report_summary(const radicle_previs_report* report,
                                             radicle_previs_summary* out_summary) {
  clear_error();
  if (out_summary == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_summary is null");
  }
  const auto status = check_report(report);
  if (status != RADICLE_STATUS_OK) return status;
  *out_summary = radicle_previs_summary{
      report->cells.size(),        report->broken,
      report->outdated,            report->skipped,
      report->summary.cdx_files,   report->summary.cdx_rows,
      report->summary.csg_files,   report->summary.csg_rows,
      report->summary.csg_chunks,  report->summary.tome_files,
      report->summary.oc_nif_files, report->summary.problems.size()};
  return RADICLE_STATUS_OK;
}

size_t radicle_previs_report_cell_count(const radicle_previs_report* report) {
  return report == nullptr ? 0 : report->cells.size();
}

radicle_status radicle_previs_report_cell(const radicle_previs_report* report, size_t index,
                                          radicle_previs_cell* out_cell) {
  clear_error();
  if (out_cell == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_cell is null");
  const auto status = check_cell_index(report, index);
  if (status != RADICLE_STATUS_OK) return status;
  const auto& cell = report->cells[index];
  *out_cell = radicle_previs_cell{
      to_c_form_key(cell.cell),   cell.plugin,
      cell.status.broken ? 1 : 0, cell.status.outdated ? 1 : 0,
      cell.status.skip_unsupported ? 1 : 0, cell.status.issues.size(),
      cell.status.outdated_issues.size(),   cell.status.skips.size()};
  return RADICLE_STATUS_OK;
}

radicle_status radicle_previs_report_issue(const radicle_previs_report* report, size_t cell_index,
                                           size_t issue_index, int32_t* out_code,
                                           const char** out_detail) {
  clear_error();
  if (out_code == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_code is null");
  const auto status = check_cell_index(report, cell_index);
  if (status != RADICLE_STATUS_OK) return status;
  const auto& issues = report->cells[cell_index].status.issues;
  if (issue_index >= issues.size()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "previs issue index is outside the cell");
  }
  *out_code = static_cast<int32_t>(issues[issue_index].code);
  if (out_detail != nullptr) *out_detail = issues[issue_index].detail.c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_previs_report_outdated_issue(const radicle_previs_report* report,
                                                    size_t cell_index, size_t issue_index,
                                                    int32_t* out_code, const char** out_detail) {
  clear_error();
  if (out_code == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_code is null");
  const auto status = check_cell_index(report, cell_index);
  if (status != RADICLE_STATUS_OK) return status;
  const auto& issues = report->cells[cell_index].status.outdated_issues;
  if (issue_index >= issues.size()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "previs outdated issue index is outside the cell");
  }
  *out_code = static_cast<int32_t>(issues[issue_index].code);
  if (out_detail != nullptr) *out_detail = issues[issue_index].detail.c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_previs_report_skip(const radicle_previs_report* report, size_t cell_index,
                                          size_t skip_index, const char** out_detail) {
  clear_error();
  if (out_detail == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_detail is null");
  const auto status = check_cell_index(report, cell_index);
  if (status != RADICLE_STATUS_OK) return status;
  const auto& skips = report->cells[cell_index].status.skips;
  if (skip_index >= skips.size()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "previs skip index is outside the cell");
  }
  *out_detail = skips[skip_index].c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_previs_report_problem(const radicle_previs_report* report, size_t index,
                                             const char** out_detail) {
  clear_error();
  if (out_detail == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_detail is null");
  const auto status = check_report(report);
  if (status != RADICLE_STATUS_OK) return status;
  if (index >= report->summary.problems.size()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "previs problem index is outside the report");
  }
  *out_detail = report->summary.problems[index].c_str();
  return RADICLE_STATUS_OK;
}

const char* radicle_previs_issue_name(int32_t code) {
  // Range invariant: an out-of-range code must not wrap to a real enumerator and report its name.
  if (code < 0 || code >= fo4::previs::k_previs_issue_count) return "unknown";
  return fo4::previs::to_string(static_cast<fo4::previs::PrevisIssue>(code)).data();
}

}  // extern "C"
