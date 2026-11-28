#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exterior blocks require 32x32 cells and sub-blocks 8x8 cells, floored for negative coordinates. */
typedef struct radicle_exterior_cell_layout {
  int32_t cell_x;
  int32_t cell_y;
  int32_t block_x;
  int32_t block_y;
  int32_t subblock_x;
  int32_t subblock_y;
} radicle_exterior_cell_layout;

/* flags must keep bit 0 clear; it defaults to the shipped exterior value 0x0002. */
typedef struct radicle_exterior_cell_creation {
  radicle_form_key worldspace;
  int32_t x;
  int32_t y;
  const char* editor_id;
  uint16_t flags;
} radicle_exterior_cell_creation;

/* Created cells require an existing worldspace or an editor ID; the created key is returned in out_key. */
RADICLE_API radicle_status radicle_exterior_cell_layout_for(int32_t x,
                                                            int32_t y,
                                                            radicle_exterior_cell_layout* out_layout);
RADICLE_API radicle_status radicle_exterior_cell_coordinate_valid(int32_t coordinate,
                                                                  int32_t* out_valid);
RADICLE_API radicle_status radicle_plugin_create_exterior_cell(radicle_plugin* plugin,
                                                               const radicle_exterior_cell_creation* creation,
                                                               radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_create_interior_cell(radicle_plugin* plugin,
                                                               const char* editor_id,
                                                               radicle_form_key* out_key);
/* Requires: an existing WRLD in the plugin without a persistent CELL; the new CELL holds only a DATA of flags (bit 0 must be clear) and nothing is validated against vanilla shape. */
RADICLE_API radicle_status radicle_plugin_create_worldspace_persistent_cell(radicle_plugin* plugin,
                                                                            radicle_form_key worldspace,
                                                                            uint16_t flags,
                                                                            radicle_form_key* out_key);
/* Requires: signature is a placed reference type (REFR, ACHR, PGRE and the other projectile types) or LAND and the cell exists in the plugin; the record is created empty in the persistent (nonzero) or temporary group, LAND must not be persistent, local_id 0 takes the next free id and any other must be in range and unused, and form_version 0 keeps the plugin form version. Flags, position and subrecords stay with the caller. */
RADICLE_API radicle_status radicle_plugin_create_raw_cell_child(radicle_plugin* plugin,
                                                                radicle_form_key cell,
                                                                const char* signature,
                                                                int32_t persistent,
                                                                uint32_t local_id,
                                                                uint16_t form_version,
                                                                radicle_form_key* out_key);
/* A search requires the worldspace plugin to be loaded; an absent cell reports RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_find_exterior_cell(const radicle_env* env,
                                                          radicle_form_key worldspace,
                                                          int32_t x,
                                                          int32_t y,
                                                          const radicle_record** out_record);
RADICLE_API radicle_status radicle_env_find_persistent_exterior_cell(const radicle_env* env,
                                                                     radicle_form_key worldspace,
                                                                     const radicle_record** out_record);

typedef struct radicle_cell_lighting_tail {
  int32_t has_near_height;
  float near_height_mid;
  float near_height_range;
  int32_t has_high_fog_colors;
  radicle_byte_color fog_color_high_near;
  radicle_byte_color fog_color_high_far;
  int32_t has_high_density_scale;
  float high_density_scale;
  int32_t has_fog_scales;
  float fog_near_scale;
  float fog_far_scale;
  float fog_high_near_scale;
  float fog_high_far_scale;
  int32_t has_far_height;
  float far_height_mid;
  float far_height_range;
} radicle_cell_lighting_tail;

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
  radicle_form_key water;
  radicle_cell_lighting_tail lighting_tail;
  int32_t has_previs_timestamp;
  uint16_t previs_timestamp;
  int32_t has_precombined_timestamp;
  uint16_t precombined_timestamp;
  radicle_form_key acoustic_space;
  radicle_form_key music_type;
  radicle_form_key sky_weather_region;
  radicle_form_key god_rays;
  radicle_form_key lock_list;
  radicle_form_key owner;
  int32_t has_owner_rank;
  int32_t owner_rank;
  int32_t has_exterior_lod;
  radicle_form_key exterior_lod_worldspace;
  float exterior_lod_offset[3];
  int32_t has_water_current_count;
  uint32_t water_current_count;
  const char* water_environment_map;
} radicle_cell_info;

typedef struct radicle_water_current {
  float velocity[3];
  float unnamed;
} radicle_water_current;

typedef struct radicle_region_weather {
  radicle_form_key weather;
  radicle_form_key global;
  uint32_t chance;
  uint8_t override_value;
  uint8_t priority;
} radicle_region_weather;

typedef struct radicle_cell_child {
  const radicle_record* record;
  int32_t persistent;
} radicle_cell_child;

RADICLE_API radicle_status radicle_cell_get_info(const radicle_env* env,
                                                 const radicle_record* record,
                                                 radicle_cell_info* out_info);
/* XCLR regions; the caller must free the array with radicle_free_form_keys. */
RADICLE_API radicle_status radicle_cell_get_regions(const radicle_env* env,
                                                    const radicle_record* record,
                                                    radicle_form_key** out_keys,
                                                    size_t* out_count);
/* XPRI previs reference index; the caller must free the array with radicle_free_form_keys. */
RADICLE_API radicle_status radicle_cell_get_previs_references(const radicle_env* env,
                                                              const radicle_record* record,
                                                              radicle_form_key** out_keys,
                                                              size_t* out_count);
RADICLE_API void radicle_free_form_keys(radicle_form_key* keys);
/* Constraint: the caller must free the array with radicle_free_region_weathers; rows are every RDWT entry in stored order and global is the null key when unset. */
RADICLE_API radicle_status radicle_region_get_weathers(const radicle_env* env,
                                                       const radicle_record* record,
                                                       radicle_region_weather** out_weathers,
                                                       size_t* out_count);
RADICLE_API void radicle_free_region_weathers(radicle_region_weather* weathers);
/* Constraint: returns the winning REGN keys of the worldspace whose RPLD areas contain (x, y) in world units, weather-entry priority descending, ties with the later-loaded plugin first and record order within a plugin; free with radicle_free_form_keys, no hit or unknown worldspace is OK with NULL/0, a malformed REGN is RADICLE_STATUS_PARSE_ERROR. Containment and priority rules are UNVERIFIED. */
RADICLE_API radicle_status radicle_env_regions_at(const radicle_env* env,
                                                  radicle_form_key worldspace,
                                                  float x,
                                                  float y,
                                                  radicle_form_key** out_keys,
                                                  size_t* out_count);
/* XWCU water currents; the caller must free the array with radicle_free_water_currents. */
RADICLE_API radicle_status radicle_cell_get_water_currents(const radicle_env* env,
                                                           const radicle_record* record,
                                                           radicle_water_current** out_currents,
                                                           size_t* out_count);
RADICLE_API void radicle_free_water_currents(radicle_water_current* currents);

/* Persistent top CELL from the highest-priority plugin; an absent cell must report RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_worldspace_top_cell(const radicle_env* env,
                                                           radicle_form_key worldspace,
                                                           const radicle_record** out_record);
/* Block/sub-block CELLs, each from the highest plugin listing it; the ordering is ascending plugin then on-disk. */
RADICLE_API radicle_status radicle_env_worldspace_cells(const radicle_env* env,
                                                        radicle_form_key worldspace,
                                                        const radicle_record*** out_records,
                                                        size_t* out_count);
/* Constraint: returns the REFRs carrying an XMRK map marker in the top cell and every block cell of the worldspace, in cell order; an unknown worldspace is OK with NULL/0, and the array must be freed with radicle_free_record_array. */
RADICLE_API radicle_status radicle_env_worldspace_map_markers(const radicle_env* env,
                                                              radicle_form_key worldspace,
                                                              const radicle_record*** out_records,
                                                              size_t* out_count);
/* Winning CELLs; the winning version must sit in an interior sub-block. */
RADICLE_API radicle_status radicle_env_interior_cells(const radicle_env* env,
                                                      const radicle_record*** out_records,
                                                      size_t* out_count);
/* Children merged low to high priority; the caller must free them with radicle_free_cell_children. */
RADICLE_API radicle_status radicle_env_cell_children(const radicle_env* env,
                                                     radicle_form_key cell,
                                                     radicle_cell_child** out_children,
                                                     size_t* out_count);
RADICLE_API void radicle_free_cell_children(radicle_cell_child* children);
/* LAND child of the highest-priority CELL version holding one; a cell without one must report RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_cell_landscape(const radicle_env* env,
                                                      radicle_form_key cell,
                                                      const radicle_record** out_record);

typedef struct radicle_cell_view radicle_cell_view;

typedef struct radicle_cell_summary {
  radicle_form_key key;
  const char* editor_id;
  radicle_name_kind name_kind;
  const char* name_text;
  uint32_t name_string_id;
  int32_t interior;
  int32_t has_grid;
  int32_t grid_x;
  int32_t grid_y;
  int32_t has_worldspace;
  radicle_form_key worldspace;
  size_t winning_plugin;
  int32_t localized;
} radicle_cell_summary;

typedef struct radicle_scol_part {
  const char* model_path;
  const radicle_static_placement* placements;
  size_t placement_count;
} radicle_scol_part;

typedef struct radicle_base_object_info {
  radicle_form_key key;
  char signature[5];
  const char* editor_id;
  const char* type_name;
  const char* model_path;
  int32_t editor_marker;
  int32_t has_decal;
  const char* decal_diffuse;
  int32_t has_decal_width;
  float decal_width;
  int32_t has_decal_height;
  float decal_height;
  const radicle_scol_part* scol_parts;
  size_t scol_part_count;
  void* internal;
} radicle_base_object_info;

typedef struct radicle_cell_reference {
  radicle_form_key key;
  const char* record_type;
  const char* editor_id;
  int32_t has_base;
  radicle_form_key base;
  const radicle_base_object_info* base_info;
  float position[3];
  float rotation[3];
  float scale;
  int32_t persistent;
  int32_t initially_disabled;
  size_t winning_plugin;
} radicle_cell_reference;

RADICLE_API radicle_status radicle_env_resolve_cell(const radicle_env* env,
                                                    const char* id,
                                                    radicle_form_key* out_key);
RADICLE_API radicle_status radicle_env_resolve_worldspace(const radicle_env* env,
                                                          const char* id,
                                                          radicle_form_key* out_key);
RADICLE_API radicle_status radicle_env_find_cell_at_grid(const radicle_env* env,
                                                         radicle_form_key world,
                                                         int32_t x,
                                                         int32_t y,
                                                         radicle_form_key* out_key);
/* The view must be freed with radicle_cell_view_free and must not outlive env; its form_key.plugin pointers borrow from env. */
RADICLE_API radicle_status radicle_env_cell_view(const radicle_env* env,
                                                 radicle_form_key cell,
                                                 radicle_cell_view** out_view);
RADICLE_API radicle_status radicle_cell_view_summary(const radicle_cell_view* view,
                                                     radicle_cell_summary* out_summary);
RADICLE_API size_t radicle_cell_view_reference_count(const radicle_cell_view* view);
RADICLE_API radicle_status radicle_cell_view_reference(const radicle_cell_view* view,
                                                       size_t index,
                                                       radicle_cell_reference* out_reference);
RADICLE_API radicle_status radicle_cell_view_scol_part(const radicle_cell_view* view,
                                                       size_t reference_index,
                                                       size_t part_index,
                                                       radicle_scol_part* out_part);
RADICLE_API void radicle_cell_view_free(radicle_cell_view* view);
/* The info must be freed with radicle_base_object_info_free and must not outlive env; its form_key.plugin pointers borrow from env. */
RADICLE_API radicle_status radicle_env_base_object_info(const radicle_env* env,
                                                        radicle_form_key base,
                                                        radicle_base_object_info** out_info);
RADICLE_API void radicle_base_object_info_free(radicle_base_object_info* info);

typedef struct radicle_placed_ref_flag_row {
  uint32_t bit;
  const char* name;
} radicle_placed_ref_flag_row;

/* Constraint: rows are the record-flag bits a placed reference carries for this placed record signature and base-object signature, in ascending bit order; the same bit means different things per base type, so decode only with the base's signature. base_signature may be null or unknown and then selects the table xEdit uses when the base matches no group; an ACHR placed signature and the projectile types PARW, PBAR, PBEA, PCON, PFLA, PGRE, PHZD and PMIS each have their own table and ignore the base. Signatures compare exactly in upper case. A null placed_signature, out_rows or out_count is INVALID_ARGUMENT.
   Invariant: the rows are static, valid for the life of the process, and never freed; names are NUL-terminated ASCII. */
RADICLE_API radicle_status radicle_placed_ref_flag_table(const char* placed_signature,
                                                         const char* base_signature,
                                                         const radicle_placed_ref_flag_row** out_rows,
                                                         size_t* out_count);

/* Constraint: rows are the (bit, name) header flags of a base record of this signature from the xEdit pin, in ascending bit order; ACTI, DOOR, FURN and STAT have their own tables (bit 23 is Is Marker) and every other signature returns only the common rows Deleted (bit 5) and Ignored (bit 12). Signatures compare exactly in upper case. A null signature, out_rows or out_count is INVALID_ARGUMENT.
   Invariant: the rows are static, valid for the life of the process, and never freed; names are NUL-terminated ASCII. */
RADICLE_API radicle_status radicle_record_flag_table(const char* signature,
                                                     const radicle_placed_ref_flag_row** out_rows,
                                                     size_t* out_count);

/* Constraint: rows are the named bits of the Flags byte of an Enable Parent link (bit 0 Set Enable State to Opposite of Parent, bit 1 Pop In), not record-header bits. A null out_rows or out_count is INVALID_ARGUMENT.
   Invariant: the rows are static, valid for the life of the process, and never freed; names are NUL-terminated ASCII. */
RADICLE_API radicle_status radicle_enable_parent_flag_table(const radicle_placed_ref_flag_row** out_rows,
                                                            size_t* out_count);

#ifdef __cplusplus
}
#endif
