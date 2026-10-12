#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_official_table {
  RADICLE_OFFICIAL_TABLE_OG_NG = 0,
  RADICLE_OFFICIAL_TABLE_AE = 1
} radicle_official_table;

/* Rows are "name<TAB>active(0|1)<TAB>line". Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_plugin_list_read(const char* path,
                                                    char*** out_rows,
                                                    size_t* out_count);

/* Names in file order. Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_ccc_read(const char* path,
                                            char*** out_names,
                                            size_t* out_count);

/* Rows are "name<TAB>checked(0|1)<TAB>master_flag(0|1)<TAB>light(0|1)" in load order; findings are "code<TAB>plugin<TAB>detail". */
/* plugin_list_path and ccc_path may be NULL; *out_failed is 1 when the assembly aborted. Both arrays require radicle_free_string_array. */
RADICLE_API radicle_status radicle_load_order_assemble(const char* data_dir,
                                                       const char* plugin_list_path,
                                                       const char* ccc_path,
                                                       radicle_official_table table,
                                                       char*** out_rows,
                                                       size_t* out_row_count,
                                                       char*** out_findings,
                                                       size_t* out_finding_count,
                                                       int32_t* out_failed);

#ifdef __cplusplus
}
#endif
