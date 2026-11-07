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

typedef struct radicle_static_placement {
  float position[3];
  float rotation[3];
  float scale;
} radicle_static_placement;

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

typedef struct radicle_reference_hit {
  radicle_form_key record;
  size_t plugin_index;
  char subrecord[5];
  size_t offset;
} radicle_reference_hit;

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
/* First version, highest priority first, whose own EDID matches case-insensitively; `signature` is NULL or four characters. */
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

/* Referencing records in load order, `plugin_index` the version holding the link; an unknown key is OK with NULL/0. Free with radicle_free_reference_hits. */
RADICLE_API radicle_status radicle_env_referenced_by(const radicle_env* env,
                                                     radicle_form_key key,
                                                     radicle_reference_hit** out_hits,
                                                     size_t* out_count);
/* Targets of the key's form links, one hit per link; `record` is the target. Empty results as radicle_env_referenced_by. */
RADICLE_API radicle_status radicle_env_references_from(const radicle_env* env,
                                                       radicle_form_key key,
                                                       radicle_reference_hit** out_hits,
                                                       size_t* out_count);
RADICLE_API void radicle_free_reference_hits(radicle_reference_hit* hits);

/* Requires radicle_free_string_array; one tab-separated finding per string: code, severity, plugin, record, subrecord, offset, target, message. NULL plugin checks every plugin, limit 0 is unlimited. */
RADICLE_API radicle_status radicle_env_check_errors(const radicle_env* env,
                                                    const char* plugin,
                                                    size_t limit,
                                                    char*** out_strings,
                                                    size_t* out_count);

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
  /* Stored NAM0/NAM9 values; Mutagen ObjectBoundsMin/Max divide these by 4096. */
  int32_t has_bounds_min;
  float bounds_min[2];
  int32_t has_bounds_max;
  float bounds_max[2];
  int32_t has_flags;
  uint8_t flags;
  radicle_form_key lod_water;
  int32_t has_lod_water_height;
  float lod_water_height;
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

/* XCLL members past Inherits; each group requires a longer XCLL payload, else its has_* stays 0. */
/* RADICLE_STATUS_PARSE_ERROR for a wrong signature or a malformed present subrecord. */
RADICLE_API radicle_status radicle_worldspace_get_info(const radicle_env* env,
                                                       const radicle_record* record,
                                                       radicle_worldspace_info* out_info);
/* Immediate group type (fo4 GroupType values) and the enclosing WRLD/CELL; absent keys have plugin == NULL. */
RADICLE_API radicle_status radicle_record_get_container(const radicle_env* env,
                                                        const radicle_record* record,
                                                        int32_t* out_group_type,
                                                        radicle_form_key* out_worldspace,
                                                        radicle_form_key* out_cell);

typedef struct radicle_linked_ref {
  radicle_form_key keyword;
  radicle_form_key reference;
} radicle_linked_ref;

typedef struct radicle_activate_parent {
  radicle_form_key reference;
  float delay;
} radicle_activate_parent;

typedef struct radicle_portal {
  radicle_form_key origin;
  radicle_form_key destination;
} radicle_portal;

typedef struct radicle_topic_ref {
  uint32_t type;
  radicle_form_key topic;
  uint32_t subtype;
} radicle_topic_ref;

typedef struct radicle_patrol {
  float idle_time;
  radicle_form_key idle;
  radicle_form_key topic;
  const radicle_topic_ref* topics;
  size_t topic_count;
} radicle_patrol;

typedef struct radicle_script_element {
  radicle_form_key object;
  int16_t alias;
  const char* text;
  int32_t integer;
  float real;
  int32_t boolean;
} radicle_script_element;

/* One struct member with its VMAD type and flags; `value` has only the field for `type` set. */
typedef struct radicle_script_struct_member {
  const char* name;
  uint8_t type;
  uint8_t flags;
  radicle_script_element value;
} radicle_script_struct_member;

/* Members in file order. `has_structs` is 1 only when every member of every struct is an object, string, int, float or bool; then `struct_count` is 1 for a struct and n for a struct array (0 allowed). Otherwise `has_structs` and `struct_count` are 0 and `structs` is NULL. */
typedef struct radicle_script_struct {
  const radicle_script_struct_member* members;
  size_t member_count;
} radicle_script_struct;

typedef struct radicle_script_property {
  const char* name;
  uint8_t type;
  uint8_t flags;
  radicle_form_key object;
  int32_t has_alias;
  int16_t alias;
  const char* text;
  size_t text_size; /* Byte length of text; text may hold embedded NUL bytes. */
  int32_t has_integer;
  int32_t integer;
  int32_t has_real;
  float real;
  int32_t has_boolean;
  int32_t boolean;
  int32_t has_element_count;
  uint32_t element_count;
  int32_t has_elements;
  const radicle_script_element* elements;
  size_t element_value_count;
  uint8_t has_structs;
  const radicle_script_struct* structs;
  size_t struct_count;
} radicle_script_property;

typedef struct radicle_script {
  const char* name;
  uint8_t flags;
  const radicle_script_property* properties;
  size_t property_count;
} radicle_script;

typedef struct radicle_placed_ref {
  char signature[5];
  radicle_form_key base;
  int32_t has_placement;
  float position[3];
  float rotation[3];
  int32_t has_scale;
  float scale;
  radicle_form_key layer;
  radicle_form_key encounter_zone;
  radicle_form_key location_reference;
  radicle_form_key attach_ref;
  const radicle_form_key* location_ref_types;
  size_t location_ref_type_count;
  int32_t has_enable_parent;
  radicle_form_key enable_parent;
  int32_t has_enable_parent_flags;
  uint8_t enable_parent_flags;
  int32_t has_ownership;
  radicle_form_key owner;
  int32_t has_no_crime;
  int32_t no_crime;
  int32_t has_faction_rank;
  int32_t faction_rank;
  const radicle_linked_ref* linked_refs;
  size_t linked_ref_count;
  int32_t has_activate_parent_flags;
  uint8_t activate_parent_flags;
  const radicle_activate_parent* activate_parents;
  size_t activate_parent_count;
  const char* comments;
  int32_t ignored_by_sandbox;
  int32_t open_by_default;
  int32_t has_map_marker;
  int32_t has_map_marker_flags;
  uint8_t map_marker_flags;
  radicle_name_kind map_marker_name_kind;
  const char* map_marker_name_text;
  uint32_t map_marker_name_string_id;
  int32_t has_map_marker_type;
  uint8_t map_marker_type;
  int32_t has_teleport;
  radicle_form_key teleport_door;
  float teleport_position[3];
  float teleport_rotation[3];
  uint32_t teleport_flags;
  radicle_form_key teleport_transition_interior;
  /* Stored XPRM bounds; Mutagen's PlacedPrimitive.Bounds doubles them. */
  int32_t has_primitive;
  float primitive_bounds[3];
  float primitive_color[4];
  uint32_t primitive_type;
  int32_t has_spline;
  float spline_slack;
  float spline_thickness;
  float spline_half_extents[3];
  int32_t has_spline_wind_detached_end;
  int32_t spline_wind_detached_end;
  const radicle_portal* portals;
  size_t portal_count;
  int32_t has_room_portal;
  float room_portal_size[2];
  float room_portal_position[3];
  float room_portal_rotation[4];
  int32_t has_room;
  uint8_t room_linked_room_count;
  uint8_t room_flags;
  radicle_form_key room_lighting_template;
  radicle_form_key room_image_space;
  const radicle_form_key* linked_rooms;
  size_t linked_room_count;
  const radicle_patrol* patrols;
  size_t patrol_count;
  int32_t has_count;
  int32_t count;
  int32_t has_level_modifier;
  int32_t level_modifier;
  int32_t has_scripts;
  int16_t script_version;
  int16_t script_object_format;
  int32_t scripts_unsupported;
  const radicle_script* scripts;
  size_t script_count;
  void* internal;
  int32_t has_emittance;
  radicle_form_key emittance;
  int32_t has_radius;
  float radius;
  int32_t has_light_data;
  float light_fov;
  float light_fade;
  float light_end_distance_cap;
  float light_shadow_depth_bias;
  int32_t has_light_near_clip;
  float light_near_clip;
  int32_t has_light_volumetric_intensity;
  float light_volumetric_intensity;
} radicle_placed_ref;

/* RADICLE_STATUS_PARSE_ERROR for a non-placed record or a malformed present subrecord. */
RADICLE_API radicle_status radicle_placed_ref_decode(const radicle_env* env,
                                                     const radicle_record* record,
                                                     radicle_placed_ref** out_ref);
RADICLE_API void radicle_placed_ref_free(radicle_placed_ref* ref);

/* ModelPathResolver.PrimaryModelPath parity; RADICLE_STATUS_NOT_FOUND when the record has no model. */
RADICLE_API radicle_status radicle_record_primary_model_path(const radicle_env* env,
                                                             const radicle_record* record,
                                                             const char** out_path);

typedef struct radicle_land_layer {
  int32_t is_base;
  radicle_form_key texture;
  uint8_t quadrant;
  uint16_t layer;
  const uint8_t* alpha;
  size_t alpha_size;
} radicle_land_layer;

typedef struct radicle_landscape {
  int32_t has_flags;
  uint32_t flags;
  int32_t has_heights;
  float height_offset;
  const uint8_t* heights;
  const uint8_t* normals;
  const uint8_t* colors;
  const radicle_land_layer* layers;
  size_t layer_count;
  void* internal;
} radicle_landscape;

typedef struct radicle_texture_set_info {
  const char* textures[8];
  const char* material;
  int32_t has_flags;
  uint16_t flags;
  int32_t has_decal;
  float decal[6];
  size_t decal_count;
} radicle_texture_set_info;

RADICLE_API radicle_status radicle_landscape_decode(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_landscape** out_landscape);
RADICLE_API void radicle_landscape_free(radicle_landscape* landscape);
RADICLE_API radicle_status radicle_landscape_texture_get_set(const radicle_env* env,
                                                             const radicle_record* record,
                                                             radicle_form_key* out_texture_set);
RADICLE_API radicle_status radicle_texture_set_get_info(const radicle_env* env,
                                                        const radicle_record* record,
                                                        radicle_texture_set_info* out_info);
RADICLE_API radicle_status radicle_env_landscape_texture_diffuse(const radicle_env* env,
                                                                 radicle_form_key landscape_texture,
                                                                 const char** out_path);

typedef struct radicle_strings radicle_strings;

typedef enum radicle_string_source {
  RADICLE_STRING_SOURCE_NORMAL = 0,
  RADICLE_STRING_SOURCE_DL = 1,
  RADICLE_STRING_SOURCE_IL = 2
} radicle_string_source;

RADICLE_API radicle_status radicle_string_source_for(const char* record_signature,
                                                     const char* subrecord_signature,
                                                     radicle_string_source* out_source);
RADICLE_API radicle_status radicle_strings_load(const radicle_env* env,
                                                const char* strings_dir,
                                                const char* language,
                                                radicle_strings** out_strings);
RADICLE_API void radicle_strings_destroy(radicle_strings* strings);
RADICLE_API radicle_status radicle_strings_table_path(const radicle_strings* strings,
                                                      size_t plugin_index,
                                                      radicle_string_source source,
                                                      const char** out_path);
RADICLE_API radicle_status radicle_strings_lookup(const radicle_strings* strings,
                                                  size_t plugin_index,
                                                  radicle_string_source source,
                                                  uint32_t string_id,
                                                  const char** out_text,
                                                  size_t* out_size);
RADICLE_API radicle_status radicle_strings_resolve_name(const radicle_strings* strings,
                                                        const radicle_record* record,
                                                        const char** out_text,
                                                        size_t* out_size);

/* Index-only BA2 reader; entries are read on demand and const calls are thread-safe. */
typedef struct radicle_ba2 radicle_ba2;
typedef struct radicle_assets radicle_assets;

typedef enum radicle_ba2_kind {
  RADICLE_BA2_GENERAL = 0,
  RADICLE_BA2_TEXTURE = 1
} radicle_ba2_kind;

typedef struct radicle_ba2_info {
  radicle_ba2_kind kind;
  uint32_t version;
  int32_t has_name_table;
  size_t entry_count;
} radicle_ba2_info;

typedef struct radicle_ba2_entry_info {
  const char* path;
  uint32_t name_hash;
  uint32_t extension_hash;
  uint32_t directory_hash;
  uint64_t unpacked_size;
  size_t chunk_count;
  uint16_t width;
  uint16_t height;
  uint8_t mip_count;
  uint8_t dxgi_format;
  uint8_t texture_flags;
  uint8_t tile_mode;
} radicle_ba2_entry_info;

typedef struct radicle_ba2_file {
  const char* path;
  const uint8_t* data;
  size_t size;
} radicle_ba2_file;

typedef enum radicle_asset_origin {
  RADICLE_ASSET_LOOSE = 0,
  RADICLE_ASSET_ARCHIVE = 1
} radicle_asset_origin;

typedef struct radicle_asset_location {
  radicle_asset_origin origin;
  size_t archive_index;
  size_t entry_index;
} radicle_asset_location;

/* Entry paths must stay valid until radicle_asset_list_destroy. */
typedef struct radicle_asset_list radicle_asset_list;

typedef struct radicle_asset_list_entry {
  const char* path;
  radicle_asset_location location;
  uint64_t size;
  int32_t shadowed;
} radicle_asset_list_entry;

RADICLE_API radicle_status radicle_ba2_open(const char* path, radicle_ba2** out_ba2);
RADICLE_API void radicle_ba2_close(radicle_ba2* ba2);
RADICLE_API radicle_status radicle_ba2_get_info(const radicle_ba2* ba2, radicle_ba2_info* out_info);
RADICLE_API radicle_status radicle_ba2_get_entry(const radicle_ba2* ba2,
                                                 size_t index,
                                                 radicle_ba2_entry_info* out_entry);
RADICLE_API radicle_status radicle_ba2_find(const radicle_ba2* ba2, const char* path, size_t* out_index);
RADICLE_API radicle_status radicle_ba2_extract(const radicle_ba2* ba2,
                                               size_t index,
                                               uint8_t** out_bytes,
                                               size_t* out_size);

RADICLE_API radicle_status radicle_ba2_write_general(const radicle_ba2_file* files,
                                                     size_t file_count,
                                                     uint32_t version,
                                                     int32_t compress,
                                                     uint8_t** out_bytes,
                                                     size_t* out_size);

RADICLE_API radicle_status radicle_archive_load_order(const char* data_dir,
                                                      const char* const* ini_archives,
                                                      size_t ini_count,
                                                      const char* const* plugins,
                                                      size_t plugin_count,
                                                      char*** out_paths,
                                                      size_t* out_count);
RADICLE_API radicle_status radicle_assets_open(const char* data_dir,
                                               const char* const* archive_paths,
                                               size_t archive_count,
                                               radicle_assets** out_assets);
/* roots must be ordered high to low priority; empty root entries are skipped. */
RADICLE_API radicle_status radicle_assets_open_roots(const char* const* root_paths,
                                                     size_t root_count,
                                                     const char* const* archive_paths,
                                                     size_t archive_count,
                                                     radicle_assets** out_assets);
RADICLE_API void radicle_assets_destroy(radicle_assets* assets);
RADICLE_API size_t radicle_assets_archive_count(const radicle_assets* assets);
/* Invariant: empty and duplicate entries are dropped, so this can be fewer than the count radicle_assets_open_roots was given. */
RADICLE_API size_t radicle_assets_root_count(const radicle_assets* assets);
RADICLE_API radicle_status radicle_assets_get_archive(const radicle_assets* assets,
                                                      size_t index,
                                                      const radicle_ba2** out_ba2);
RADICLE_API radicle_status radicle_assets_locate(const radicle_assets* assets,
                                                 const char* path,
                                                 radicle_asset_location* out_location);
/* Sorted by path; a winner must precede its shadowed copies; NULL or empty prefix and extension mean no filter; a prefix that normalizes to nothing is INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_assets_list(const radicle_assets* assets,
                                               const char* prefix,
                                               const char* extension,
                                               int32_t include_shadowed,
                                               radicle_asset_list** out_list);
RADICLE_API size_t radicle_asset_list_count(const radicle_asset_list* list);
RADICLE_API radicle_status radicle_asset_list_get(const radicle_asset_list* list,
                                                  size_t index,
                                                  radicle_asset_list_entry* out_entry);
RADICLE_API void radicle_asset_list_destroy(radicle_asset_list* list);
RADICLE_API radicle_status radicle_assets_read(const radicle_assets* assets,
                                               const char* path,
                                               uint8_t** out_bytes,
                                               size_t* out_size,
                                               radicle_asset_location* out_location);
RADICLE_API radicle_status radicle_strings_load_with_assets(const radicle_env* env,
                                                            const char* strings_dir,
                                                            const char* language,
                                                            const radicle_assets* assets,
                                                            radicle_strings** out_strings);

/* Reference writeback into a caller-owned radicle_plugin (see c_api.h); a plugin handle is not thread-safe. */
typedef enum radicle_placed_copy_status {
  RADICLE_PLACED_COPY_INSERTED = 0,
  RADICLE_PLACED_COPY_REPLACED = 1,
  RADICLE_PLACED_COPY_ALREADY_EXISTS = 2
} radicle_placed_copy_status;

typedef enum radicle_placed_move_status {
  RADICLE_PLACED_MOVE_MOVED = 0,
  RADICLE_PLACED_MOVE_RELOCATED = 1
} radicle_placed_move_status;

typedef enum radicle_record_copy_status {
  RADICLE_RECORD_COPY_INSERTED = 0,
  RADICLE_RECORD_COPY_REPLACED = 1,
  RADICLE_RECORD_COPY_ALREADY_EXISTS = 2
} radicle_record_copy_status;

RADICLE_API radicle_status radicle_plugin_create(const char* filename, radicle_plugin** out_plugin);
RADICLE_API radicle_status radicle_plugin_copy_placed_ref_override(radicle_plugin* plugin,
                                                                   const radicle_env* env,
                                                                   const radicle_record* record,
                                                                   int32_t overwrite,
                                                                   radicle_placed_copy_status* out_status);
RADICLE_API radicle_status radicle_plugin_copy_placed_ref_new(radicle_plugin* plugin,
                                                              const radicle_env* env,
                                                              const radicle_record* record,
                                                              radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_copy_record_as_new(radicle_plugin* destination,
                                                             const radicle_plugin* source,
                                                             radicle_form_key record,
                                                             const char* editor_id,
                                                             const char* remove_prefix,
                                                             const char* remove_suffix,
                                                             const char* prefix,
                                                             const char* suffix,
                                                             radicle_form_key* out_key);
/* The key must be owned by source; overwrite replaces an existing copy in destination. */
RADICLE_API radicle_status radicle_plugin_copy_record_override(radicle_plugin* destination,
                                                               const radicle_plugin* source,
                                                               radicle_form_key record,
                                                               int32_t overwrite,
                                                               radicle_record_copy_status* out_status);
RADICLE_API radicle_status radicle_plugin_set_placed_transform(radicle_plugin* plugin,
                                                               radicle_form_key reference,
                                                               const float position[3],
                                                               const float rotation[3]);
RADICLE_API radicle_status radicle_plugin_move_placed_ref(radicle_plugin* plugin,
                                                          radicle_form_key reference,
                                                          const float position[3],
                                                          const float rotation[3],
                                                          radicle_placed_move_status* out_status);
RADICLE_API radicle_status radicle_plugin_set_placed_scale(radicle_plugin* plugin,
                                                           radicle_form_key reference,
                                                           int32_t has_scale,
                                                           float scale);
RADICLE_API radicle_status radicle_plugin_sort_masters(radicle_plugin* plugin, const radicle_env* env);

RADICLE_API radicle_status radicle_plugin_set_placed_editor_id(radicle_plugin* plugin,
                                                               radicle_form_key reference,
                                                               const char* editor_id);
RADICLE_API radicle_status radicle_plugin_set_placed_record_flags(radicle_plugin* plugin,
                                                                  radicle_form_key reference,
                                                                  uint32_t flags);
RADICLE_API radicle_status radicle_plugin_set_placed_comments(radicle_plugin* plugin,
                                                              radicle_form_key reference,
                                                              const char* comments);
RADICLE_API radicle_status radicle_plugin_set_placed_location_reference(radicle_plugin* plugin,
                                                                        const radicle_env* env,
                                                                        radicle_form_key reference,
                                                                        radicle_form_key location);
RADICLE_API radicle_status radicle_plugin_set_placed_ownership(radicle_plugin* plugin,
                                                               const radicle_env* env,
                                                               radicle_form_key reference,
                                                               radicle_form_key owner,
                                                               int32_t no_crime,
                                                               int32_t has_rank,
                                                               int32_t faction_rank);
RADICLE_API radicle_status radicle_plugin_set_placed_enable_parent(radicle_plugin* plugin,
                                                                   const radicle_env* env,
                                                                   radicle_form_key reference,
                                                                   radicle_form_key parent,
                                                                   uint8_t flags);
RADICLE_API radicle_status radicle_plugin_set_placed_linked_refs(radicle_plugin* plugin,
                                                                 const radicle_env* env,
                                                                 radicle_form_key reference,
                                                                 const radicle_linked_ref* links,
                                                                 size_t link_count);
RADICLE_API radicle_status radicle_plugin_set_placed_teleport(radicle_plugin* plugin,
                                                              const radicle_env* env,
                                                              radicle_form_key reference,
                                                              radicle_form_key door,
                                                              const float position[3],
                                                              const float rotation[3]);
RADICLE_API radicle_status radicle_plugin_set_placed_primitive(radicle_plugin* plugin,
                                                               radicle_form_key reference,
                                                               const float bounds[3],
                                                               uint32_t type);
RADICLE_API radicle_status radicle_plugin_set_placed_activate_parent_delay(radicle_plugin* plugin,
                                                                           radicle_form_key reference,
                                                                           size_t index,
                                                                           float delay);
RADICLE_API radicle_status radicle_plugin_set_record_scalar(radicle_plugin* plugin,
                                                            radicle_form_key key,
                                                            const char* path,
                                                            const char* value);
RADICLE_API radicle_status radicle_plugin_set_record_scalar_at(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               const char* path,
                                                               size_t index,
                                                               const char* value);
RADICLE_API radicle_status radicle_plugin_set_record_formlink(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              const char* path,
                                                              size_t index,
                                                              const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_set_record_string(radicle_plugin* plugin,
                                                            radicle_form_key key,
                                                            const char* path,
                                                            size_t index,
                                                            const char* utf8);
RADICLE_API radicle_status radicle_plugin_record_field_count(const radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             const char* path,
                                                             size_t* out_count);
RADICLE_API radicle_status radicle_plugin_formlink_list_size(const radicle_plugin* plugin,
                                                             radicle_form_key key,
                                                             const char* path,
                                                             size_t* out_count);
RADICLE_API radicle_status radicle_plugin_formlink_list_insert(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               const char* path,
                                                               size_t at,
                                                               const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_formlink_list_remove(radicle_plugin* plugin,
                                                               radicle_form_key key,
                                                               const char* path,
                                                               size_t at);
RADICLE_API radicle_status radicle_plugin_formlink_list_set(radicle_plugin* plugin,
                                                            radicle_form_key key,
                                                            const char* path,
                                                            size_t at,
                                                            const radicle_form_key* target);
RADICLE_API radicle_status radicle_plugin_formlink_list_clear(radicle_plugin* plugin,
                                                              radicle_form_key key,
                                                              const char* path);

/* Constraint: sets (present != 0) or clears one optional group member; group_path is
   "<signature>\\<path>" and member must be the four-byte subrecord signature. */
RADICLE_API radicle_status radicle_plugin_subrecord_group_member_set(radicle_plugin* plugin,
                                                                    radicle_form_key key,
                                                                    const char* group_path,
                                                                    size_t index,
                                                                    const char* member,
                                                                    int32_t present);

RADICLE_API radicle_status radicle_env_plugin_records(const radicle_env* env,
                                                      size_t plugin_index,
                                                      const radicle_record*** out_records,
                                                      size_t* out_count);

#include "fo4/c_api_records.h"
#include "fo4/c_api_cell.h"

#ifdef __cplusplus
}
#endif
