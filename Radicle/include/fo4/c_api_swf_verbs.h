#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_swf_edit_text_info {
  uint16_t character_id;
  radicle_swf_rect bounds;     /* twips */
  uint8_t has_color;
  uint32_t color;              /* 0xAARRGGBB; read only when has_color is 1 */
} radicle_swf_edit_text_info;

typedef struct radicle_swf_edit_text_fields {
  uint8_t has_text;
  const uint8_t* text;         /* must be non-null when has_text is 1 and text_size is not zero */
  size_t text_size;
  uint8_t has_color;
  uint32_t color;
  uint8_t has_bounds;
  radicle_swf_rect bounds;
} radicle_swf_edit_text_fields;

/* Invariant: tag_index is a top-level tag that must decode as DefineEditText; *out_text is malloc'd with a trailing NUL, release with radicle_free_bytes. */
RADICLE_API radicle_status radicle_swf_edit_text_read(const radicle_swf_history* history, size_t tag_index,
                                                      radicle_swf_edit_text_info* out_info, uint8_t** out_text,
                                                      size_t* out_text_size);
RADICLE_API radicle_status radicle_swf_edit_text_apply(radicle_swf_history* history, size_t tag_index,
                                                       const radicle_swf_edit_text_fields* fields,
                                                       radicle_swf_plan_result* out_result);

/* Invariant: rows are "label_index\tframe\tname" and release with radicle_free_string_array. */
RADICLE_API radicle_status radicle_swf_frame_label_rows(const radicle_swf_history* history, uint16_t timeline_id,
                                                        char*** out_rows, size_t* out_count);
RADICLE_API radicle_status radicle_swf_frame_label_add(radicle_swf_history* history, uint16_t timeline_id,
                                                       uint32_t frame, const char* name,
                                                       radicle_swf_plan_result* out_result);
RADICLE_API radicle_status radicle_swf_frame_label_rename(radicle_swf_history* history, uint16_t timeline_id,
                                                          size_t label_index, const char* name,
                                                          radicle_swf_plan_result* out_result);
RADICLE_API radicle_status radicle_swf_frame_label_remove(radicle_swf_history* history, uint16_t timeline_id,
                                                          size_t label_index, radicle_swf_plan_result* out_result);

/* Invariant: the JSON is a placement array, *out_text is malloc'd with a trailing NUL, release with radicle_free_bytes. */
RADICLE_API radicle_status radicle_swf_placement_list_json(const radicle_swf_history* history, uint16_t timeline_id,
                                                           uint8_t** out_text, size_t* out_size);

typedef struct radicle_swf_instance_transform {
  uint32_t instance_id;
  radicle_swf_transform transform;
} radicle_swf_instance_transform;

/* Requires root_frame to name a rendered root frame; a batch is one history command. */
RADICLE_API radicle_status radicle_swf_apply_instance_transforms(radicle_swf_history* history, uint32_t root_frame,
                                                                 const radicle_swf_instance_transform* edits,
                                                                 size_t edit_count,
                                                                 radicle_swf_plan_result* out_result);

/* Requires the instance to sit on the root timeline; a sprite-internal instance must be refused. */
RADICLE_API radicle_status radicle_swf_reassign_instance_depth(radicle_swf_history* history, uint32_t root_frame,
                                                               uint32_t instance_id, uint16_t destination_depth,
                                                               uint32_t* out_instance_id,
                                                               radicle_swf_plan_result* out_result);

/* Requires the instance to sit on the root timeline; a NULL transform keeps the authored one. */
RADICLE_API radicle_status radicle_swf_insert_instance_transform_keyframe(radicle_swf_history* history,
                                                                          uint32_t root_frame,
                                                                          uint32_t instance_id,
                                                                          const radicle_swf_transform* transform,
                                                                          radicle_swf_plan_result* out_result);

#ifdef __cplusplus
}
#endif
