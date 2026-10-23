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

/* May pass NULL plugin_list_path or ccc_path. Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_load_order_assemble(const char* data_dir,
                                                       const char* plugin_list_path,
                                                       const char* ccc_path,
                                                       radicle_official_table table,
                                                       char*** out_rows,
                                                       size_t* out_row_count,
                                                       char*** out_findings,
                                                       size_t* out_finding_count,
                                                       int32_t* out_failed);

/* Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_mo2_instance_read(const char* instance_dir,
                                                     char*** out_fields,
                                                     size_t* out_count);

/* NULL profile selects the instance profile; "" is invalid. Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_mo2_profile_read(const char* instance_dir,
                                                    const char* profile,
                                                    char*** out_mod_rows,
                                                    size_t* out_mod_count,
                                                    char*** out_plugin_rows,
                                                    size_t* out_plugin_count);

/* Invariant: roots are LOW to HIGH; callers must reverse for open_roots (HIGH to LOW). Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_mo2_asset_roots(const char* instance_dir,
                                                   const char* profile,
                                                   char*** out_roots,
                                                   size_t* out_count);

/* Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_mo2_plugin_paths(const char* instance_dir,
                                                    const char* profile,
                                                    char*** out_rows,
                                                    size_t* out_count);

#ifdef __cplusplus
}
#endif
