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

/* Invariant: tag_index is the flat radicle_swf_tag_at index of a top-level DefineEditText; a tag inside a DefineSprite is refused. *out_text is malloc'd with a trailing NUL, release with radicle_free_bytes. */
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

/* Must report the raw MATRIX fixed-point fields, so the panel can round-trip them through SetTransform. The name must stay clear of SwfPlacementMatrix, which already names the managed codec record. */
typedef struct radicle_swf_placement_matrix_fields {
  uint8_t has_scale;
  int32_t scale_x;             /* must be 16.16 fixed, like the tag bytes */
  int32_t scale_y;             /* must be 16.16 fixed, like the tag bytes */
  uint8_t has_rotate;
  int32_t rotate0;             /* must be 16.16 fixed, like the tag bytes */
  int32_t rotate1;             /* must be 16.16 fixed, like the tag bytes */
  int32_t translate_x;         /* must be twips, like the tag bytes */
  int32_t translate_y;         /* must be twips, like the tag bytes */
} radicle_swf_placement_matrix_fields;

/* Invariant: the JSON holds every PlaceObject tag as {tag_index, depth, character_id, inside_sprite}; character_id is null when the tag moves without one. *out_text is malloc'd with a trailing NUL, release with radicle_free_bytes. */
RADICLE_API radicle_status radicle_swf_placements_json(const radicle_swf_history* history,
                                                        uint8_t** out_text, size_t* out_size);

/* Invariant: tag_index is the flat radicle_swf_tag_at index of a PlaceObject, PlaceObject2 or PlaceObject3; any other tag must be refused. */
RADICLE_API radicle_status radicle_swf_placement_matrix_at(const radicle_swf_history* history, size_t tag_index,
                                                           radicle_swf_placement_matrix_fields* out_matrix);

/* Requires tag_index to name a flat placement index; a root placement must move its creating instance through the native depth verb, so a refused lookup moves no history state. */
RADICLE_API radicle_status radicle_swf_set_placement_depth(radicle_swf_history* history, size_t tag_index,
                                                           uint16_t depth, radicle_swf_plan_result* out_result);

/* Requires tag_index to name a flat placement index and every field to fit 31 bits; the ints mirror SwfInterop.SetTransform, so translate is twips and scale/rotate are 16.16 fixed. A root placement whose MATRIX the tag authors must move through the native transform verb; a sprite placement takes the payload write. */
RADICLE_API radicle_status radicle_swf_set_placement_transform(radicle_swf_history* history, size_t tag_index,
                                                               int32_t x, int32_t y, int32_t scale_x,
                                                               int32_t scale_y, int32_t rotate0, int32_t rotate1,
                                                               radicle_swf_plan_result* out_result);

/* Requires tag_index to name a flat PlaceObject2 index and frame to name a reached frame; a root placement must insert through the native keyframe verb and a sprite placement must splice a move child ahead of that frame, so a refused frame moves no history state. */
RADICLE_API radicle_status radicle_swf_insert_placement_keyframe(radicle_swf_history* history, size_t tag_index,
                                                                 uint32_t frame,
                                                                 radicle_swf_plan_result* out_result);

#ifdef __cplusplus
}
#endif
