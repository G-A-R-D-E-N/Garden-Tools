#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invariant: a larger source is refused before it is read. */
#define RADICLE_PAPYRUS_SOURCE_MAX_BYTES (4u * 1024u * 1024u)

/* Invariant: a parse failure is a row, not an error status. */
/* Requires radicle_free_string_array. */
RADICLE_API radicle_status radicle_papyrus_source_summary(const uint8_t* source,
                                                          size_t size,
                                                          char*** out_rows,
                                                          size_t* out_count);

typedef struct radicle_event_catalog radicle_event_catalog;

typedef enum radicle_event_owner_kind {
  RADICLE_EVENT_OWNER_OTHER = 0,
  RADICLE_EVENT_OWNER_ACTOR = 1,
  RADICLE_EVENT_OWNER_OBJECT_REFERENCE = 2,
  RADICLE_EVENT_OWNER_QUEST = 3,
  RADICLE_EVENT_OWNER_ALIAS = 4
} radicle_event_owner_kind;

/* Invariant: state is "" for an event declared outside every state; pointers stay valid until radicle_papyrus_event_catalog_destroy. */
typedef struct radicle_event_info {
  const char* name;
  const char* state;
  const char* declaring_script;
  radicle_event_owner_kind owner_kind;
  int32_t is_custom;
  size_t param_count;
} radicle_event_info;

typedef struct radicle_event_param {
  const char* name;
  const char* type;
  int32_t is_array;
} radicle_event_param;

/* Invariant: owner_kind comes from the declaring script name alone: Actor, ObjectReference, Quest, Alias and ReferenceAlias; any other script is OTHER. */
/* Invariant: events are listed script level first, then each state in source order, each scope's functions before its custom events. */
/* Invariant: a parse failure is PARSE_ERROR with the reason in radicle_last_error and *out_catalog NULL; one script file per call. */
RADICLE_API radicle_status radicle_papyrus_event_catalog_parse(const uint8_t* source,
                                                               size_t size,
                                                               radicle_event_catalog** out_catalog);
RADICLE_API void radicle_papyrus_event_catalog_destroy(radicle_event_catalog* catalog);
RADICLE_API size_t radicle_papyrus_event_catalog_count(const radicle_event_catalog* catalog);
RADICLE_API radicle_status radicle_papyrus_event_catalog_get(const radicle_event_catalog* catalog,
                                                             size_t index,
                                                             radicle_event_info* out_info);
RADICLE_API radicle_status radicle_papyrus_event_catalog_param(const radicle_event_catalog* catalog,
                                                               size_t event_index,
                                                               size_t param_index,
                                                               radicle_event_param* out_param);

#ifdef __cplusplus
}
#endif
