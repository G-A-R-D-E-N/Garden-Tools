#pragma once

#include "fo4/c_api_env.h"

#define RADICLE_NAVMESH_FLAG_EXTRA_EDGE_MASK 0x7u
#define RADICLE_NAVMESH_FLAG_DOOR 0x400u
#define RADICLE_NAVMESH_FLAG_VISITED 0x800u
#define RADICLE_NAVMESH_PATH_ALLOW_UNKNOWN_LINKS 0x1u
#define RADICLE_NAVMESH_PATH_ALLOW_UNKNOWN_DOORS 0x2u
#define RADICLE_NAVMESH_PATH_NODE_VIA_LINK 0x1u
#define RADICLE_NAVMESH_PATH_NODE_NEEDS_OPENING 0x2u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_navmesh_vertex { float x; float y; float z; } radicle_navmesh_vertex;

typedef struct radicle_navmesh_parent {
  uint32_t crc;
  radicle_form_key world;
  radicle_form_key cell;
  int16_t grid_y;
  int16_t grid_x;
} radicle_navmesh_parent;

typedef struct radicle_navmesh_triangle {
  uint16_t vertices[3];
  int16_t edges[3];
  float clearance;
  uint8_t traversal;
  uint32_t flags;
} radicle_navmesh_triangle;

typedef struct radicle_navmesh_edge_link {
  uint32_t type;
  radicle_form_key navmesh;
  int16_t triangle;
  uint8_t edge;
} radicle_navmesh_edge_link;

typedef struct radicle_navmesh_door_link {
  int16_t triangle;
  uint32_t crc;
  radicle_form_key door;
} radicle_navmesh_door_link;

typedef struct radicle_navmesh_cover_map {
  uint16_t cover;
  uint16_t triangle;
} radicle_navmesh_cover_map;

typedef struct radicle_navmesh_cover {
  uint16_t vertices[2];
  uint8_t data[4];
} radicle_navmesh_cover;

typedef struct radicle_navmesh_waypoint {
  float position[3];
  int16_t triangle;
  uint32_t flags;
} radicle_navmesh_waypoint;

typedef struct radicle_navmesh {
  int32_t has_geometry;
  uint32_t version;
  radicle_navmesh_parent parent;
  const radicle_navmesh_vertex* vertices;
  size_t vertex_count;
  const radicle_navmesh_triangle* triangles;
  size_t triangle_count;
  const radicle_navmesh_edge_link* edges;
  size_t edge_count;
  const radicle_navmesh_door_link* doors;
  size_t door_count;
  const uint8_t* opaque_tail;
  size_t opaque_tail_size;
  void* internal;
} radicle_navmesh;

typedef struct radicle_navi_door_link {
  uint32_t crc;
  radicle_form_key door;
} radicle_navi_door_link;

typedef struct radicle_navmesh_info {
  radicle_form_key navmesh;
  uint32_t flags;
  radicle_navmesh_vertex location;
  float preferred;
  const radicle_form_key* edges;
  size_t edge_count;
  const radicle_form_key* preferred_edges;
  size_t preferred_edge_count;
  const radicle_navi_door_link* doors;
  size_t door_count;
  int32_t has_island;
  const uint8_t* island;
  size_t island_size;
  radicle_navmesh_parent parent;
} radicle_navmesh_info;

typedef struct radicle_navi {
  const radicle_navmesh_info* infos;
  size_t info_count;
  void* internal;
} radicle_navi;

typedef struct radicle_navi_path {
  const radicle_form_key* navmeshes;
  size_t navmesh_count;
} radicle_navi_path;

typedef struct radicle_navi_anchor {
  radicle_form_key navmesh;
  uint32_t value;
} radicle_navi_anchor;

typedef struct radicle_navi_path_list {
  const radicle_navi_path* paths;
  size_t path_count;
  const radicle_navi_anchor* anchors;
  size_t anchor_count;
  void* internal;
} radicle_navi_path_list;

typedef enum radicle_path_verdict {
  RADICLE_PATH_UNKNOWN = 0,
  RADICLE_PATH_PASSABLE = 1,
  RADICLE_PATH_NEEDS_OPENING = 2,
  RADICLE_PATH_BLOCKED = 3
} radicle_path_verdict;

typedef struct radicle_door_state {
  uint8_t locked;
  uint8_t npc_has_key;
} radicle_door_state;

typedef struct radicle_teleport_leg {
  radicle_form_key destination_door;
  radicle_form_key destination_cell;
  int32_t has_navmesh;
  radicle_form_key navmesh;
  int16_t triangle;
  int32_t has_approach;
  radicle_navmesh_vertex approach;
  radicle_path_verdict verdict;
} radicle_teleport_leg;

typedef struct radicle_walk_link {
  radicle_navmesh_vertex start;
  radicle_navmesh_vertex end;
  uint32_t source_triangle;
  uint32_t source_edge;
  radicle_form_key target_navmesh;
  uint32_t target_triangle;
  uint32_t target_edge;
  radicle_path_verdict verdict;
} radicle_walk_link;

typedef struct radicle_walk_door {
  radicle_form_key door;
  size_t first_triangle;
  size_t triangle_count;
} radicle_walk_door;

typedef struct radicle_walk_view {
  int32_t has_geometry;
  radicle_form_key navmesh;
  int32_t has_grid;
  int16_t grid_x;
  int16_t grid_y;
  float metres_per_unit;
  const radicle_navmesh_vertex* vertices;
  size_t vertex_count;
  const uint32_t* triangles;
  size_t triangle_count;
  const uint32_t* triangle_class;
  size_t skipped_triangle_count;
  const radicle_walk_link* links;
  size_t link_count;
  const radicle_walk_door* doors;
  size_t door_count;
  const uint32_t* door_triangles;
  size_t door_triangle_count;
  void* internal;
} radicle_walk_view;

typedef struct radicle_agent_profile {
  float radius;
  float height;
  int32_t large;
  uint32_t forbidden_class;
} radicle_agent_profile;

typedef struct radicle_navmesh_path_door {
  radicle_form_key door;
  radicle_door_state state;
} radicle_navmesh_path_door;

typedef struct radicle_navmesh_path_options {
  uint32_t flags;
  uint32_t forbidden_class;
  const radicle_navmesh_path_door* doors;
  size_t door_count;
} radicle_navmesh_path_options;

typedef struct radicle_navmesh_path_node {
  radicle_form_key navmesh;
  uint32_t triangle;
  uint32_t flags;
} radicle_navmesh_path_node;

RADICLE_API radicle_status radicle_navmesh_decode(const radicle_env* env, const radicle_record* record, const radicle_navmesh** out_navmesh);
RADICLE_API void radicle_navmesh_free(const radicle_navmesh* navmesh);
RADICLE_API radicle_status radicle_navmesh_triangle_edge_is_extra(const radicle_navmesh_triangle* triangle, size_t edge, int32_t* out_is_extra);
RADICLE_API radicle_status radicle_navi_decode(const radicle_env* env, const radicle_record* record, const radicle_navi** out_navi);
RADICLE_API void radicle_navi_free(const radicle_navi* navi);
RADICLE_API radicle_status radicle_navi_path_list_decode(const radicle_env* env, const radicle_record* record, const radicle_navi_path_list** out_list);
RADICLE_API void radicle_navi_path_list_free(const radicle_navi_path_list* list);
typedef enum radicle_navi_link_kind {
  RADICLE_NAVI_EDGE = 0,
  RADICLE_NAVI_PREFERRED_EDGE = 1,
  RADICLE_NAVI_DOOR = 2
} radicle_navi_link_kind;

RADICLE_API radicle_status radicle_plugin_set_navi_path_link(radicle_plugin* plugin, radicle_form_key key, size_t path, size_t index, radicle_form_key target);
RADICLE_API radicle_status radicle_plugin_set_navi_link(radicle_plugin* plugin, radicle_form_key key, size_t info_index, radicle_navi_link_kind kind, size_t index, radicle_form_key target);
RADICLE_API radicle_status radicle_plugin_set_navmesh_edge_target(radicle_plugin* plugin, radicle_form_key key, size_t index, radicle_form_key target);
RADICLE_API radicle_status radicle_plugin_set_navmesh_vertex(radicle_plugin* plugin, radicle_form_key key, size_t index, const radicle_navmesh_vertex* value);
RADICLE_API radicle_status radicle_plugin_set_navmesh_triangle(radicle_plugin* plugin, radicle_form_key key, size_t index, const radicle_navmesh_triangle* value);
RADICLE_API radicle_status radicle_plugin_set_navmesh_door(radicle_plugin* plugin, radicle_form_key key, size_t index, const radicle_navmesh_door_link* value);
RADICLE_API radicle_status radicle_plugin_add_navmesh_vertex(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_vertex* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_vertex(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navmesh_triangle(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_triangle* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_triangle(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navmesh_edge_link(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_edge_link* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_edge_link(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navmesh_door(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_door_link* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_door(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navmesh_cover(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_cover* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_cover(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navmesh_cover_map(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_cover_map* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_cover_map(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navmesh_waypoint(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_waypoint* value);
RADICLE_API radicle_status radicle_plugin_remove_navmesh_waypoint(radicle_plugin* plugin, radicle_form_key key, size_t index);

RADICLE_API radicle_status radicle_plugin_add_navi_info(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_info* value);
RADICLE_API radicle_status radicle_plugin_remove_navi_info(radicle_plugin* plugin, radicle_form_key key, size_t info_index);
RADICLE_API radicle_status radicle_plugin_add_navi_link(radicle_plugin* plugin, radicle_form_key key, size_t info_index, radicle_navi_link_kind kind, radicle_form_key target);
RADICLE_API radicle_status radicle_plugin_remove_navi_link(radicle_plugin* plugin, radicle_form_key key, size_t info_index, radicle_navi_link_kind kind, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navi_door(radicle_plugin* plugin, radicle_form_key key, size_t info_index, const radicle_navi_door_link* value);
RADICLE_API radicle_status radicle_plugin_add_navi_deleted(radicle_plugin* plugin, radicle_form_key key, radicle_form_key target);
RADICLE_API radicle_status radicle_plugin_remove_navi_deleted(radicle_plugin* plugin, radicle_form_key key, size_t index);
RADICLE_API radicle_status radicle_plugin_add_navi_path(radicle_plugin* plugin, radicle_form_key key, const radicle_form_key* links, size_t count);
RADICLE_API radicle_status radicle_plugin_remove_navi_path(radicle_plugin* plugin, radicle_form_key key, size_t path);
RADICLE_API radicle_status radicle_plugin_create_navmesh(radicle_plugin* plugin, radicle_form_key cell, radicle_form_key* out_navmesh);
RADICLE_API radicle_status radicle_env_cell_navmeshes(const radicle_env* env, radicle_form_key cell, const radicle_record*** out_records, size_t* out_count);
RADICLE_API radicle_status radicle_door_passable(const radicle_env* env, radicle_form_key npc_key, radicle_form_key door_ref, const radicle_door_state* door_state, radicle_path_verdict* out_verdict);
RADICLE_API radicle_status radicle_navmesh_edge_link_usable(const radicle_env* env, uint32_t link_type, radicle_form_key npc_key, radicle_path_verdict* out_verdict);
RADICLE_API radicle_status radicle_door_teleport_leg(const radicle_env* env, radicle_form_key load_door_ref, const radicle_door_state* destination_door_state, radicle_teleport_leg* out_leg);
RADICLE_API radicle_status radicle_navmesh_walk_view(const radicle_env* env, const radicle_record* record, float link_inset_metres, const radicle_walk_view** out_view);
RADICLE_API void radicle_navmesh_walk_view_free(const radicle_walk_view* view);
/* Requires: options may be NULL for defaults; out_count, out_found and out_cost are always set; out_nodes may be NULL, and otherwise up to capacity nodes are written start first and out_count is the full node count; no path is RADICLE_STATUS_OK with out_found 0, and an unresolved start or goal navmesh or triangle is RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_navmesh_find_path(const radicle_env* env, radicle_form_key start_navmesh, uint32_t start_triangle, radicle_form_key goal_navmesh, uint32_t goal_triangle, const radicle_navmesh_path_options* options, radicle_navmesh_path_node* out_nodes, size_t capacity, size_t* out_count, int32_t* out_found, float* out_cost);
RADICLE_API radicle_status radicle_walk_agent_profile(const radicle_env* env, radicle_form_key race, radicle_agent_profile* out_profile);

#ifdef __cplusplus
}
#endif
