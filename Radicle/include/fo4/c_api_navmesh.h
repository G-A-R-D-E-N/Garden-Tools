#pragma once

#include "fo4/c_api_env.h"

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

RADICLE_API radicle_status radicle_navmesh_decode(const radicle_env* env, const radicle_record* record, const radicle_navmesh** out_navmesh);
RADICLE_API void radicle_navmesh_free(const radicle_navmesh* navmesh);
RADICLE_API radicle_status radicle_navi_decode(const radicle_env* env, const radicle_record* record, const radicle_navi** out_navi);
RADICLE_API void radicle_navi_free(const radicle_navi* navi);

#ifdef __cplusplus
}
#endif
