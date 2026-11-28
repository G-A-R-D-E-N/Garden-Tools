#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A report must be destroyed before its radicle_env; every string stays borrowed until then. */
typedef struct radicle_previs_report radicle_previs_report;

typedef struct radicle_previs_summary {
  size_t cells;
  size_t broken;
  size_t outdated;
  size_t skipped;
  size_t cdx_files;
  size_t cdx_rows;
  size_t csg_files;
  size_t csg_rows;
  size_t csg_chunks;
  size_t tome_files;
  size_t oc_nif_files;
  size_t problem_count;
} radicle_previs_summary;

typedef struct radicle_previs_cell {
  radicle_form_key cell;
  size_t plugin_index;
  int32_t broken;
  int32_t outdated;
  int32_t skip_unsupported;
  size_t issue_count;
  size_t outdated_issue_count;
  size_t skip_count;
} radicle_previs_cell;

/* Scan must cover the .cdx and CSG files, every General BA2 and the loose Vis and PreCombined trees. */
/* A missing or unreadable Data folder must return RADICLE_STATUS_IO_ERROR. */
RADICLE_API radicle_status radicle_previs_scan(const radicle_env* env,
                                               const char* data_dir,
                                               radicle_previs_report** out_report);
RADICLE_API void radicle_previs_report_destroy(radicle_previs_report* report);

RADICLE_API radicle_status radicle_previs_report_summary(const radicle_previs_report* report,
                                                         radicle_previs_summary* out_summary);
RADICLE_API size_t radicle_previs_report_cell_count(const radicle_previs_report* report);
/* The index must be inside the report; outside it returns RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_previs_report_cell(const radicle_previs_report* report,
                                                      size_t index,
                                                      radicle_previs_cell* out_cell);

/* Broken findings; the detail string must stay borrowed from the report. */
RADICLE_API radicle_status radicle_previs_report_issue(const radicle_previs_report* report,
                                                       size_t cell_index,
                                                       size_t issue_index,
                                                       int32_t* out_code,
                                                       const char** out_detail);
/* Staleness findings must stay outdated, never broken. */
RADICLE_API radicle_status radicle_previs_report_outdated_issue(
    const radicle_previs_report* report, size_t cell_index, size_t issue_index,
    int32_t* out_code, const char** out_detail);
/* An index that could not answer must report a skip reason here. */
RADICLE_API radicle_status radicle_previs_report_skip(const radicle_previs_report* report,
                                                      size_t cell_index,
                                                      size_t skip_index,
                                                      const char** out_detail);
/* Scan problems; a clean-mode .psg must make CSG fail closed. */
RADICLE_API radicle_status radicle_previs_report_problem(const radicle_previs_report* report,
                                                         size_t index,
                                                         const char** out_detail);

/* Stable token for an issue code; an unknown code must report "unknown". */
RADICLE_API const char* radicle_previs_issue_name(int32_t code);

#ifdef __cplusplus
}
#endif
