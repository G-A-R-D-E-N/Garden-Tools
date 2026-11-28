#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum radicle_esl_compaction_status {
  RADICLE_ESL_COMPACTED = 0,
  RADICLE_ESL_ALREADY_COMPATIBLE = 1,
  RADICLE_ESL_CAPACITY_EXCEEDED = 2,
  RADICLE_ESL_BLOCKED_NESTED_RECORDS = 3,
  RADICLE_ESL_REKEY_FAILED = 4
} radicle_esl_compaction_status;

typedef enum radicle_record_flag_status {
  RADICLE_RECORD_FLAGS_CHANGED = 0,
  RADICLE_RECORD_FLAGS_UNCHANGED = 1,
  RADICLE_RECORD_FLAGS_RECORD_MISSING = 2,
  RADICLE_RECORD_FLAGS_HEADER_RECORD = 3,
  RADICLE_RECORD_FLAGS_DELETED_BIT = 4,
  RADICLE_RECORD_FLAGS_TARGET_CELL_MISSING = 5
} radicle_record_flag_status;

typedef enum radicle_mark_deleted_status {
  RADICLE_MARK_DELETED_MARKED = 0,
  RADICLE_MARK_DELETED_ALREADY_DELETED = 1,
  RADICLE_MARK_DELETED_RECORD_MISSING = 2,
  RADICLE_MARK_DELETED_LOCAL_RECORD = 3,
  RADICLE_MARK_DELETED_HEADER_RECORD = 4
} radicle_mark_deleted_status;

typedef enum radicle_record_delete_status {
  RADICLE_RECORD_DELETE_DELETED = 0,
  RADICLE_RECORD_DELETE_RECORD_MISSING = 1,
  RADICLE_RECORD_DELETE_NOT_LOCAL = 2,
  RADICLE_RECORD_DELETE_REFERENCED = 3
} radicle_record_delete_status;

typedef enum radicle_undelete_status {
  RADICLE_UNDELETE_UNDELETED = 0,
  RADICLE_UNDELETE_NOT_DELETED = 1,
  RADICLE_UNDELETE_RECORD_MISSING = 2,
  RADICLE_UNDELETE_HEADER_RECORD = 3,
  RADICLE_UNDELETE_NOT_A_REFERENCE = 4,
  RADICLE_UNDELETE_INCOMPLETE_REFERENCE = 5,
  RADICLE_UNDELETE_INJECTED_RECORD = 6
} radicle_undelete_status;

typedef enum radicle_master_add_status {
  RADICLE_MASTER_ADDED = 0,
  RADICLE_MASTER_ALREADY_PRESENT = 1
} radicle_master_add_status;

typedef enum radicle_clean_masters_status {
  RADICLE_CLEAN_MASTERS_CLEANED = 0,
  RADICLE_CLEAN_MASTERS_NOTHING_UNUSED = 1,
  RADICLE_CLEAN_MASTERS_INCOMPLETE_USAGE = 2
} radicle_clean_masters_status;

RADICLE_API radicle_status radicle_plugin_set_hedr_next_form_id(radicle_plugin* plugin,
                                                                uint32_t next_form_id);

RADICLE_API radicle_status radicle_plugin_compact_to_esl_detailed(radicle_plugin* plugin,
                                                                  radicle_esl_compaction_status* out_status,
                                                                  size_t* out_remapped,
                                                                  uint32_t* out_next_form_id,
                                                                  char*** out_blockers,
                                                                  size_t* out_blocker_count);

RADICLE_API radicle_status radicle_plugin_set_record_flags(radicle_plugin* plugin,
                                                           radicle_form_key record,
                                                           uint32_t flags,
                                                           radicle_record_flag_status* out_status);

RADICLE_API radicle_status radicle_plugin_mark_override_deleted(radicle_plugin* plugin,
                                                                radicle_form_key record,
                                                                radicle_mark_deleted_status* out_status);

RADICLE_API radicle_status radicle_plugin_delete_local_record(radicle_plugin* plugin,
                                                              radicle_form_key record,
                                                              radicle_record_delete_status* out_status);

RADICLE_API radicle_status radicle_plugin_undelete_and_disable_reference(radicle_plugin* plugin,
                                                                         const radicle_env* env,
                                                                         radicle_form_key record,
                                                                         radicle_undelete_status* out_status);

RADICLE_API radicle_status radicle_plugin_add_master_detailed(radicle_plugin* plugin,
                                                     const char* filename,
                                                     radicle_plugin_kind kind,
                                                     radicle_master_add_status* out_status);

RADICLE_API radicle_status radicle_plugin_clean_unused_masters(radicle_plugin* plugin,
                                                               radicle_clean_masters_status* out_status,
                                                               char*** out_removed,
                                                               size_t* out_removed_count);

RADICLE_API radicle_status radicle_plugin_list_write(const char* path,
                                                     const char* const* names,
                                                     const int32_t* active,
                                                     size_t count);

#ifdef __cplusplus
}
#endif
