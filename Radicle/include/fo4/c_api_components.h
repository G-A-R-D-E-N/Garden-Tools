#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A component entry requires a resolved component record key and a count; count defaults to 1 on authoring. */
typedef struct radicle_component_entry {
  radicle_form_key component;
  uint32_t count;
} radicle_component_entry;

/* An empty array requires a record type that carries components; the write reports RADICLE_STATUS_NOT_FOUND for a missing record. */
RADICLE_API radicle_status radicle_plugin_set_record_components(radicle_plugin* plugin,
                                                                radicle_form_key key,
                                                                const radicle_component_entry* entries,
                                                                size_t count);

#ifdef __cplusplus
}
#endif
