#pragma once

#include "fo4/c_api_cell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A patch handle is changed in place and only on success; a refused call must leave its bytes unchanged. */

typedef enum radicle_placed_edit_status {
  RADICLE_PLACED_EDIT_EDITED = 0,
  RADICLE_PLACED_EDIT_REFERENCE_MISSING = 1,
  RADICLE_PLACED_EDIT_REFERENCE_DELETED = 2,
  RADICLE_PLACED_EDIT_TARGET_CELL_MISSING = 3,
  RADICLE_PLACED_EDIT_FIELD_ABSENT = 4
} radicle_placed_edit_status;

typedef enum radicle_duplicate_status {
  RADICLE_DUPLICATE_DUPLICATED = 0,
  RADICLE_DUPLICATE_REFERENCE_MISSING = 1,
  RADICLE_DUPLICATE_REFERENCE_DELETED = 2,
  RADICLE_DUPLICATE_PARENT_CELL_MISSING = 3,
  RADICLE_DUPLICATE_UNSUPPORTED_SIGNATURE = 4
} radicle_duplicate_status;

typedef enum radicle_delete_status {
  RADICLE_DELETE_DELETED = 0,
  RADICLE_DELETE_MARKED_DELETED = 1,
  RADICLE_DELETE_REFERENCED = 2,
  RADICLE_DELETE_REFERENCE_MISSING = 3,
  RADICLE_DELETE_ALREADY_DELETED = 4
} radicle_delete_status;

typedef enum radicle_cell_override_status {
  RADICLE_CELL_OVERRIDE_INSERTED = 0,
  RADICLE_CELL_OVERRIDE_ALREADY_EXISTS = 1,
  RADICLE_CELL_OVERRIDE_SOURCE_MISSING = 2,
  RADICLE_CELL_OVERRIDE_NOT_A_CELL = 3
} radicle_cell_override_status;

typedef enum radicle_cell_edit_status {
  RADICLE_CELL_EDIT_EDITED = 0,
  RADICLE_CELL_EDIT_CELL_MISSING = 1,
  RADICLE_CELL_EDIT_CELL_DELETED = 2,
  RADICLE_CELL_EDIT_NOT_A_CELL = 3,
  RADICLE_CELL_EDIT_TARGET_MISSING = 4,
  RADICLE_CELL_EDIT_TARGET_TYPE_NOT_ALLOWED = 5,
  RADICLE_CELL_EDIT_TARGET_NOT_MASTER = 6,
  RADICLE_CELL_EDIT_INTERIOR_FLAG_LOCKED = 7,
  RADICLE_CELL_EDIT_INVALID_VALUE = 8,
  RADICLE_CELL_EDIT_MALFORMED = 9
} radicle_cell_edit_status;

/* A link key with a NULL plugin must mean no link; a non-null plugin must name a valid FormKey. */
typedef struct radicle_linked_reference_edit {
  radicle_form_key keyword;
  radicle_form_key reference;
} radicle_linked_reference_edit;

/* A has_ flag must be set for its field to apply; with has_comments set, a NULL comments must clear the text. */
typedef struct radicle_placed_edit {
  int32_t has_transform;
  float position[3];
  float rotation[3];
  int32_t has_scale;
  int32_t has_scale_value;
  float scale;
  int32_t has_editor_id;
  const char* editor_id;
  int32_t has_record_flags;
  uint32_t record_flags;
  int32_t has_comments;
  const char* comments;
  int32_t has_location_reference;
  radicle_form_key location_reference;
  int32_t has_ownership;
  radicle_form_key owner;
  int32_t no_crime;
  int32_t has_faction_rank;
  int32_t faction_rank;
  int32_t has_enable_parent;
  radicle_form_key enable_parent;
  uint8_t enable_parent_flags;
  int32_t has_linked_references;
  const radicle_linked_reference_edit* linked_references;
  size_t linked_reference_count;
  int32_t has_teleport;
  radicle_form_key teleport_door;
  float teleport_position[3];
  float teleport_rotation[3];
  int32_t has_primitive;
  float primitive_bounds[3];
  uint32_t primitive_type;
} radicle_placed_edit;

/* copied_from must borrow from the env until radicle_env_destroy; NULL when the patch already held the reference. */
typedef struct radicle_placed_edit_result {
  radicle_placed_edit_status status;
  const char* copied_from;
  int32_t relocated;
} radicle_placed_edit_result;

typedef struct radicle_duplicate_options {
  float offset_position[3];
  float offset_rotation[3];
  int32_t keep_editor_id;
} radicle_duplicate_options;

typedef struct radicle_cell_lighting_edit {
  int32_t has_ambient;
  radicle_byte_color ambient;
  int32_t has_directional;
  radicle_byte_color directional;
  int32_t has_fog_near_color;
  radicle_byte_color fog_near_color;
  int32_t has_fog_near;
  float fog_near;
  int32_t has_fog_far;
  float fog_far;
  int32_t has_directional_rotation_xy;
  int32_t directional_rotation_xy;
  int32_t has_directional_rotation_z;
  int32_t directional_rotation_z;
  int32_t has_directional_fade;
  float directional_fade;
  int32_t has_fog_clip_distance;
  float fog_clip_distance;
  int32_t has_fog_power;
  float fog_power;
} radicle_cell_lighting_edit;

/* A has_ flag must be set for its field to apply; a link key with a NULL plugin must clear the link. */
typedef struct radicle_cell_edit {
  int32_t has_flags;
  uint16_t flags;
  int32_t has_lighting;
  radicle_cell_lighting_edit lighting;
  int32_t has_lighting_template;
  radicle_form_key lighting_template;
  int32_t has_water_height;
  float water_height;
  int32_t has_water;
  radicle_form_key water;
  int32_t has_image_space;
  radicle_form_key image_space;
  int32_t has_encounter_zone;
  radicle_form_key encounter_zone;
  int32_t has_location;
  radicle_form_key location;
  int32_t has_acoustic_space;
  radicle_form_key acoustic_space;
  int32_t has_music_type;
  radicle_form_key music_type;
  int32_t has_sky_weather_region;
  radicle_form_key sky_weather_region;
  int32_t has_god_rays;
  radicle_form_key god_rays;
  int32_t has_lock_list;
  radicle_form_key lock_list;
  int32_t has_owner;
  radicle_form_key owner;
  int32_t has_owner_rank;
  int32_t owner_rank;
  int32_t has_regions;
  const radicle_form_key* regions;
  size_t region_count;
} radicle_cell_edit;

/* Status pointers must be written only once the arguments validate; a refusal must return an error status with radicle_last_error text. */
RADICLE_API radicle_status radicle_plugin_edit_placed_reference(radicle_plugin* patch,
                                                                const radicle_env* env,
                                                                radicle_form_key reference,
                                                                const radicle_placed_edit* edit,
                                                                radicle_placed_edit_result* out_result);
/* options may be NULL and must then mean a zero offset; out_new must be set only when the status is DUPLICATED. */
RADICLE_API radicle_status radicle_plugin_duplicate_placed_reference(radicle_plugin* patch,
                                                                     const radicle_env* env,
                                                                     radicle_form_key reference,
                                                                     const radicle_duplicate_options* options,
                                                                     radicle_duplicate_status* out_status,
                                                                     radicle_form_key* out_new);
RADICLE_API radicle_status radicle_plugin_delete_placed_reference(radicle_plugin* patch,
                                                                  const radicle_env* env,
                                                                  radicle_form_key reference,
                                                                  radicle_delete_status* out_status);
/* The source must be a different handle from patch; an already present CELL reports ALREADY_EXISTS with OK. */
RADICLE_API radicle_status radicle_plugin_copy_cell_override(radicle_plugin* patch,
                                                             const radicle_plugin* source,
                                                             radicle_form_key cell,
                                                             radicle_cell_override_status* out_status);
RADICLE_API radicle_status radicle_plugin_edit_cell(radicle_plugin* patch,
                                                    const radicle_env* env,
                                                    radicle_form_key cell,
                                                    const radicle_cell_edit* edit,
                                                    radicle_cell_edit_status* out_status);

#ifdef __cplusplus
}
#endif
