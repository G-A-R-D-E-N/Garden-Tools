#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable, thread-safe load-order environment; returned strings and records are borrowed until radicle_env_destroy. */
typedef struct radicle_env radicle_env;
typedef struct radicle_record radicle_record;

typedef struct radicle_form_key {
  const char* plugin;
  uint32_t local_id;
} radicle_form_key;

typedef struct radicle_env_plugin_info {
  const char* filename;
  const char* path;
  radicle_plugin_kind kind;
  int32_t master_flag;
  int32_t localized;
  size_t master_count;
  size_t unresolved_master_count;
} radicle_env_plugin_info;

typedef enum radicle_name_kind {
  RADICLE_NAME_ABSENT = 0,
  RADICLE_NAME_INLINE = 1,
  RADICLE_NAME_STRING_ID = 2,
  RADICLE_NAME_MALFORMED = 3
} radicle_name_kind;

typedef struct radicle_record_info {
  const char* owner_plugin;
  uint32_t local_id;
  char signature[5];
  size_t plugin_index;
  uint32_t record_flags;
  int32_t deleted;
  const char* editor_id;
  radicle_name_kind name_kind;
  const char* name_text;
  uint32_t name_string_id;
  const char* model_path;
  int32_t has_bounds;
  int16_t bounds_min[3];
  int16_t bounds_max[3];
} radicle_record_info;

/* Paths lowest priority first; any unreadable, duplicate or self-conflicting plugin fails the load. */
RADICLE_API radicle_status radicle_env_load(const char* const* paths,
                                            size_t count,
                                            radicle_env** out_env);
RADICLE_API void radicle_env_destroy(radicle_env* env);

RADICLE_API size_t radicle_env_plugin_count(const radicle_env* env);
RADICLE_API radicle_status radicle_env_get_plugin_info(const radicle_env* env,
                                                       size_t index,
                                                       radicle_env_plugin_info* out_info);
/* `out_resolved` is 0 when the master does not load earlier than the plugin. */
RADICLE_API radicle_status radicle_env_plugin_master(const radicle_env* env,
                                                     size_t plugin_index,
                                                     size_t master_index,
                                                     const char** out_filename,
                                                     int32_t* out_resolved);
/* Case-insensitive; RADICLE_STATUS_NOT_FOUND when the plugin is not loaded. */
RADICLE_API radicle_status radicle_env_find_plugin(const radicle_env* env,
                                                   const char* filename,
                                                   size_t* out_index);

/* RADICLE_STATUS_NOT_FOUND when no plugin holds the record. */
RADICLE_API radicle_status radicle_env_winner(const radicle_env* env,
                                              radicle_form_key key,
                                              const radicle_record** out_record);
/* Winner last; an unknown record is OK with NULL/0. Free the array with radicle_free_record_array. */
RADICLE_API radicle_status radicle_env_versions(const radicle_env* env,
                                                radicle_form_key key,
                                                const radicle_record*** out_records,
                                                size_t* out_count);
/* Case-insensitive winning EDID, highest priority first; `signature` is NULL or four characters. */
RADICLE_API radicle_status radicle_env_find_editor_id(const radicle_env* env,
                                                      const char* editor_id,
                                                      const char* signature,
                                                      const radicle_record** out_record);
/* Winners grouped by highest-priority plugin, then on-disk order; empty results as radicle_env_versions. */
RADICLE_API radicle_status radicle_env_winners_of(const radicle_env* env,
                                                  const char* signature,
                                                  const radicle_record*** out_records,
                                                  size_t* out_count);
RADICLE_API void radicle_free_record_array(const radicle_record** records);

/* Absent or malformed EDID/MODL report NULL; name_kind separates inline, string-ID and malformed FULL. */
RADICLE_API radicle_status radicle_record_get_info(const radicle_env* env,
                                                   const radicle_record* record,
                                                   radicle_record_info* out_info);

/* Links use plugin == NULL for an absent or null FormID; has_* flags mark absent subrecords. */
typedef struct radicle_worldspace_info {
  radicle_form_key parent;
  int32_t has_parent_flags;
  uint16_t parent_flags;
  radicle_form_key climate;
  radicle_form_key water;
  radicle_form_key lighting_template;
  int32_t has_land_defaults;
  float default_land_height;
  float default_water_height;
  int32_t has_bounds_min;
  float bounds_min[2];
  int32_t has_bounds_max;
  float bounds_max[2];
  int32_t has_flags;
  uint8_t flags;
} radicle_worldspace_info;

typedef struct radicle_byte_color {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} radicle_byte_color;

typedef struct radicle_cell_lighting {
  radicle_byte_color ambient;
  radicle_byte_color directional;
  radicle_byte_color fog_near_color;
  float fog_near;
  float fog_far;
  int32_t directional_rotation_xy;
  int32_t directional_rotation_z;
  float directional_fade;
  float fog_clip_distance;
  float fog_power;
  int32_t has_fog_far_color;
  radicle_byte_color fog_far_color;
  int32_t has_fog_max;
  float fog_max;
  int32_t has_light_fade;
  float light_fade_begin;
  float light_fade_end;
  int32_t has_inherits;
  uint32_t inherits;
} radicle_cell_lighting;

typedef struct radicle_cell_info {
  int32_t has_flags;
  uint16_t flags;
  int32_t has_grid;
  int32_t grid_x;
  int32_t grid_y;
  int32_t has_land_flags;
  uint8_t land_flags;
  int32_t has_lighting;
  radicle_cell_lighting lighting;
  radicle_form_key lighting_template;
  int32_t has_water_height;
  float water_height;
  radicle_form_key image_space;
  radicle_form_key encounter_zone;
  radicle_form_key location;
  radicle_form_key worldspace;
} radicle_cell_info;

typedef struct radicle_cell_child {
  const radicle_record* record;
  int32_t persistent;
} radicle_cell_child;

/* RADICLE_STATUS_PARSE_ERROR for a wrong signature or a malformed present subrecord. */
RADICLE_API radicle_status radicle_worldspace_get_info(const radicle_env* env,
                                                       const radicle_record* record,
                                                       radicle_worldspace_info* out_info);
RADICLE_API radicle_status radicle_cell_get_info(const radicle_env* env,
                                                 const radicle_record* record,
                                                 radicle_cell_info* out_info);

/* Immediate group type (fo4 GroupType values) and the enclosing WRLD/CELL; absent keys have plugin == NULL. */
RADICLE_API radicle_status radicle_record_get_container(const radicle_env* env,
                                                        const radicle_record* record,
                                                        int32_t* out_group_type,
                                                        radicle_form_key* out_worldspace,
                                                        radicle_form_key* out_cell);

/* Persistent top CELL from the highest-priority plugin; RADICLE_STATUS_NOT_FOUND when absent. */
RADICLE_API radicle_status radicle_env_worldspace_top_cell(const radicle_env* env,
                                                           radicle_form_key worldspace,
                                                           const radicle_record** out_record);
/* Block/sub-block CELLs, each from the highest plugin listing it; ascending plugin, then on-disk. */
RADICLE_API radicle_status radicle_env_worldspace_cells(const radicle_env* env,
                                                        radicle_form_key worldspace,
                                                        const radicle_record*** out_records,
                                                        size_t* out_count);
/* Winning CELLs whose winning version sits in an interior sub-block. */
RADICLE_API radicle_status radicle_env_interior_cells(const radicle_env* env,
                                                      const radicle_record*** out_records,
                                                      size_t* out_count);
/* Children merged low to high priority; free with radicle_free_cell_children. */
RADICLE_API radicle_status radicle_env_cell_children(const radicle_env* env,
                                                     radicle_form_key cell,
                                                     radicle_cell_child** out_children,
                                                     size_t* out_count);
RADICLE_API void radicle_free_cell_children(radicle_cell_child* children);
/* LAND child of the highest-priority CELL version holding one; RADICLE_STATUS_NOT_FOUND otherwise. */
RADICLE_API radicle_status radicle_env_cell_landscape(const radicle_env* env,
                                                      radicle_form_key cell,
                                                      const radicle_record** out_record);

#ifdef __cplusplus
}
#endif
