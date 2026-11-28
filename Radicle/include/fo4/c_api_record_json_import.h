#pragma once

#include "fo4/c_api_env.h"

#define RADICLE_RECORD_JSON_ALLOW_RAW 1u
#define RADICLE_RECORD_JSON_IGNORE_FORM_KEY 2u
#define RADICLE_RECORD_JSON_SKIP_UNKNOWN 4u

#ifdef __cplusplus
extern "C" {
#endif

/* Applies all rows or none. out_results holds one row per field and is returned on a refused import
   too (the rows say which field was refused), so free it with radicle_free_string_array whatever
   the status. */
RADICLE_API radicle_status radicle_plugin_apply_record_json(radicle_plugin* plugin,
                                                            radicle_form_key form_key,
                                                            const char* json_utf8,
                                                            uint32_t options,
                                                            char*** out_results,
                                                            size_t* out_count);

#ifdef __cplusplus
}
#endif
