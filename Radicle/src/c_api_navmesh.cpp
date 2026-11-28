#include "c_api_env_internal.hpp"
#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/navmesh_structure.hpp"
#include "fo4/environment/navmesh_path.hpp"
#include "fo4/environment/navmesh_rules.hpp"
#include "fo4/environment/navmesh_walk.hpp"
#include "fo4/plugin/navmesh_writer.hpp"
#include "fo4/schema/scalar.hpp"

#include <memory>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct GeometryHolder {
  radicle_navmesh view{};
  std::vector<radicle_navmesh_vertex> vertices;
  std::vector<radicle_navmesh_triangle> triangles;
  std::vector<radicle_navmesh_edge_link> edges;
  std::vector<radicle_navmesh_door_link> doors;
};

struct WalkHolder {
  radicle_walk_view view{};
  std::vector<radicle_navmesh_vertex> vertices;
  std::vector<std::uint32_t> triangles;
  std::vector<std::uint32_t> triangle_class;
  std::vector<radicle_walk_link> links;
  std::vector<radicle_walk_door> doors;
  std::vector<std::uint32_t> door_triangles;
};

struct InfoLinks {
  std::vector<radicle_form_key> edges, preferred_edges;
  std::vector<radicle_navi_door_link> doors;
};

struct NaviHolder {
  radicle_navi view{};
  std::vector<radicle_navmesh_info> infos;
  std::vector<InfoLinks> links;
};

struct PathListHolder {
  radicle_navi_path_list view{};
  std::vector<std::vector<radicle_form_key>> keys;
  std::vector<radicle_navi_path> paths;
  std::vector<radicle_navi_anchor> anchors;
};

radicle_navmesh_parent parent(const radicle_env& env, const fo4::env::NavmeshParent& p) {
  return {p.crc, to_c_key(env, p.world), to_c_key(env, p.cell), p.grid_y, p.grid_x};
}

fo4::env::NavmeshTriangle to_triangle(const radicle_navmesh_triangle& value) {
  fo4::env::NavmeshTriangle t;
  for (std::size_t i = 0; i < 3; ++i) {
    t.vertices[i] = value.vertices[i];
    t.edges[i] = value.edges[i];
  }
  t.clearance = value.clearance; t.traversal = value.traversal; t.flags = value.flags;
  return t;
}

fo4::FormKey required_form_key(radicle_form_key key, const char* message) {
  const auto parsed = to_form_key(key);
  if (!parsed) throw fo4::schema::ScalarError(message);
  return *parsed;
}

fo4::FormKey optional_form_key(radicle_form_key key) {
  if (key.plugin == nullptr) return {};
  return required_form_key(key, "valid form key required");
}

fo4::env::NavmeshInfo to_navi_info(const radicle_navmesh_info& value) {
  fo4::env::NavmeshInfo info;
  info.navmesh = required_form_key(value.navmesh, "valid NAVM target required");
  info.flags = value.flags;
  info.location = {value.location.x, value.location.y, value.location.z};
  info.preferred = value.preferred;
  if (value.edge_count != 0 && value.edges == nullptr) throw fo4::schema::ScalarError("edges required when edge_count is non-zero");
  if (value.preferred_edge_count != 0 && value.preferred_edges == nullptr) {
    throw fo4::schema::ScalarError("preferred_edges required when preferred_edge_count is non-zero");
  }
  if (value.door_count != 0 && value.doors == nullptr) throw fo4::schema::ScalarError("doors required when door_count is non-zero");
  for (std::size_t i = 0; i < value.edge_count; ++i) {
    info.edges.push_back(required_form_key(value.edges[i], "valid NAVM target required"));
  }
  for (std::size_t i = 0; i < value.preferred_edge_count; ++i) {
    info.preferred_edges.push_back(required_form_key(value.preferred_edges[i], "valid NAVM target required"));
  }
  for (std::size_t i = 0; i < value.door_count; ++i) {
    info.doors.push_back({value.doors[i].crc, required_form_key(value.doors[i].door, "valid door key required")});
  }
  info.has_island = value.has_island != 0;
  info.island = std::span<const std::byte>(reinterpret_cast<const std::byte*>(value.island), value.island_size);
  info.parent.crc = value.parent.crc;
  info.parent.world = optional_form_key(value.parent.world);
  info.parent.cell = optional_form_key(value.parent.cell);
  info.parent.grid_y = value.parent.grid_y;
  info.parent.grid_x = value.parent.grid_x;
  return info;
}

radicle_path_verdict to_c_verdict(fo4::env::PathVerdict verdict) {
  switch (verdict) {
    case fo4::env::PathVerdict::passable: return RADICLE_PATH_PASSABLE;
    case fo4::env::PathVerdict::needs_opening: return RADICLE_PATH_NEEDS_OPENING;
    case fo4::env::PathVerdict::blocked: return RADICLE_PATH_BLOCKED;
    case fo4::env::PathVerdict::unknown: break;
  }
  return RADICLE_PATH_UNKNOWN;
}

fo4::env::DoorState to_door_state(const radicle_door_state& state) {
  return {state.locked != 0, state.npc_has_key != 0};
}

radicle_status edit(radicle_plugin* plugin, radicle_form_key key, const void* value, auto&& write) {
  clear_error();
  const auto parsed = to_form_key(key);
  if (!plugin || !parsed || !value) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, key and value required");
  return guarded("unknown exception while editing navmesh", [&] {
    try {
      const auto result = write(plugin->value, *parsed);
      if (result == fo4::ScalarMutationStatus::record_missing || result == fo4::ScalarMutationStatus::subrecord_missing) {
        return fail(RADICLE_STATUS_NOT_FOUND, "navmesh element not found");
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}

extern "C" {

radicle_status radicle_plugin_set_navi_path_link(radicle_plugin* plugin, radicle_form_key key,
                                                 size_t path, size_t index, radicle_form_key target) {
  return edit(plugin, key, &target, [&](auto& p, const auto& k) {
    const auto parsed = to_form_key(target);
    if (!parsed) throw fo4::schema::ScalarError("valid NAVM target required");
    return fo4::set_navi_path_link(p, k, path, index, *parsed);
  });
}

radicle_status radicle_plugin_set_navi_link(radicle_plugin* plugin, radicle_form_key key, size_t info_index,
                                            radicle_navi_link_kind kind, size_t index, radicle_form_key target) {
  return edit(plugin, key, &target, [&](auto& p, const auto& k) {
    const auto parsed = to_form_key(target);
    if (!parsed) throw fo4::schema::ScalarError("valid NAVI target required");
    return fo4::set_navi_link(p, k, info_index, static_cast<fo4::NaviLinkKind>(kind), index, *parsed);
  });
}

radicle_status radicle_plugin_set_navmesh_edge_target(radicle_plugin* plugin, radicle_form_key key,
                                                     size_t index, radicle_form_key target) {
  return edit(plugin, key, &target, [&](auto& p, const auto& k) {
    const auto parsed = to_form_key(target);
    if (!parsed) throw fo4::schema::ScalarError("valid NAVM target required");
    return fo4::set_navmesh_edge_target(p, k, index, *parsed);
  });
}

radicle_status radicle_plugin_set_navmesh_vertex(radicle_plugin* plugin, radicle_form_key key,
                                                size_t index, const radicle_navmesh_vertex* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    return fo4::set_navmesh_vertex(p, k, index, {value->x, value->y, value->z});
  });
}

radicle_status radicle_plugin_set_navmesh_triangle(radicle_plugin* plugin, radicle_form_key key,
                                                  size_t index, const radicle_navmesh_triangle* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    fo4::env::NavmeshTriangle t;
    std::copy_n(value->vertices, 3, t.vertices.begin());
    std::copy_n(value->edges, 3, t.edges.begin());
    t.clearance = value->clearance; t.traversal = value->traversal; t.flags = value->flags;
    return fo4::set_navmesh_triangle(p, k, index, t);
  });
}

radicle_status radicle_plugin_set_navmesh_door(radicle_plugin* plugin, radicle_form_key key,
                                              size_t index, const radicle_navmesh_door_link* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    const auto door = to_form_key(value->door);
    if (!door) throw fo4::schema::ScalarError("valid door key required");
    return fo4::set_navmesh_door(p, k, index, {value->triangle, value->crc, *door});
  });
}

radicle_status radicle_plugin_add_navmesh_vertex(radicle_plugin* plugin, radicle_form_key key,
                                                 const radicle_navmesh_vertex* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    return fo4::add_navmesh_vertex(p, k, {value->x, value->y, value->z});
  });
}

radicle_status radicle_plugin_remove_navmesh_vertex(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_vertex(p, k, index);
  });
}

radicle_status radicle_plugin_add_navmesh_triangle(radicle_plugin* plugin, radicle_form_key key,
                                                   const radicle_navmesh_triangle* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    return fo4::add_navmesh_triangle(p, k, to_triangle(*value));
  });
}

radicle_status radicle_plugin_remove_navmesh_triangle(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_triangle(p, k, index);
  });
}

radicle_status radicle_plugin_add_navmesh_edge_link(radicle_plugin* plugin, radicle_form_key key,
                                                    const radicle_navmesh_edge_link* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    const auto navmesh = to_form_key(value->navmesh);
    if (!navmesh) throw fo4::schema::ScalarError("valid NAVM target required");
    return fo4::add_navmesh_edge_link(p, k, {value->type, *navmesh, value->triangle, value->edge});
  });
}

radicle_status radicle_plugin_remove_navmesh_edge_link(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_edge_link(p, k, index);
  });
}

radicle_status radicle_plugin_add_navmesh_door(radicle_plugin* plugin, radicle_form_key key,
                                               const radicle_navmesh_door_link* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    const auto door = to_form_key(value->door);
    if (!door) throw fo4::schema::ScalarError("valid door key required");
    return fo4::add_navmesh_door(p, k, {value->triangle, value->crc, *door});
  });
}

radicle_status radicle_plugin_remove_navmesh_door(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_door(p, k, index);
  });
}

radicle_status radicle_plugin_add_navmesh_cover(radicle_plugin* plugin, radicle_form_key key,
                                                const radicle_navmesh_cover* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    fo4::env::NavmeshCover cover;
    for (std::size_t i = 0; i < 2; ++i) cover.vertices[i] = value->vertices[i];
    for (std::size_t i = 0; i < 4; ++i) cover.data[i] = value->data[i];
    return fo4::add_navmesh_cover(p, k, cover);
  });
}

radicle_status radicle_plugin_remove_navmesh_cover(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_cover(p, k, index);
  });
}

radicle_status radicle_plugin_add_navmesh_cover_map(radicle_plugin* plugin, radicle_form_key key,
                                                    const radicle_navmesh_cover_map* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    fo4::env::NavmeshCoverMap mapping;
    mapping.cover = value->cover;
    mapping.triangle = value->triangle;
    return fo4::add_navmesh_cover_map(p, k, mapping);
  });
}

radicle_status radicle_plugin_remove_navmesh_cover_map(radicle_plugin* plugin, radicle_form_key key,
                                                       size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_cover_map(p, k, index);
  });
}

radicle_status radicle_plugin_add_navmesh_waypoint(radicle_plugin* plugin, radicle_form_key key,
                                                   const radicle_navmesh_waypoint* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    fo4::env::NavmeshWaypoint waypoint;
    for (std::size_t i = 0; i < 3; ++i) waypoint.position[i] = value->position[i];
    waypoint.triangle = value->triangle; waypoint.flags = value->flags;
    return fo4::add_navmesh_waypoint(p, k, waypoint);
  });
}

radicle_status radicle_plugin_remove_navmesh_waypoint(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navmesh_waypoint(p, k, index);
  });
}

radicle_status radicle_plugin_add_navi_info(radicle_plugin* plugin, radicle_form_key key, const radicle_navmesh_info* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    return fo4::add_navi_info(p, k, to_navi_info(*value));
  });
}

radicle_status radicle_plugin_remove_navi_info(radicle_plugin* plugin, radicle_form_key key, size_t info_index) {
  return edit(plugin, key, &info_index, [&](auto& p, const auto& k) {
    return fo4::remove_navi_info(p, k, info_index);
  });
}

radicle_status radicle_plugin_add_navi_link(radicle_plugin* plugin, radicle_form_key key, size_t info_index,
                                            radicle_navi_link_kind kind, radicle_form_key target) {
  return edit(plugin, key, &target, [&](auto& p, const auto& k) {
    return fo4::add_navi_link(p, k, info_index, static_cast<fo4::NaviLinkKind>(kind),
                              required_form_key(target, "valid NAVM target required"));
  });
}

radicle_status radicle_plugin_remove_navi_link(radicle_plugin* plugin, radicle_form_key key, size_t info_index,
                                               radicle_navi_link_kind kind, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navi_link(p, k, info_index, static_cast<fo4::NaviLinkKind>(kind), index);
  });
}

radicle_status radicle_plugin_add_navi_door(radicle_plugin* plugin, radicle_form_key key, size_t info_index,
                                            const radicle_navi_door_link* value) {
  return edit(plugin, key, value, [&](auto& p, const auto& k) {
    fo4::env::NaviDoorLink door;
    door.crc = value->crc;
    door.door = required_form_key(value->door, "valid door key required");
    return fo4::add_navi_door(p, k, info_index, door);
  });
}

radicle_status radicle_plugin_add_navi_deleted(radicle_plugin* plugin, radicle_form_key key, radicle_form_key target) {
  return edit(plugin, key, &target, [&](auto& p, const auto& k) {
    return fo4::add_navi_deleted(p, k, required_form_key(target, "valid NAVM target required"));
  });
}

radicle_status radicle_plugin_remove_navi_deleted(radicle_plugin* plugin, radicle_form_key key, size_t index) {
  return edit(plugin, key, &index, [&](auto& p, const auto& k) {
    return fo4::remove_navi_deleted(p, k, index);
  });
}

radicle_status radicle_plugin_add_navi_path(radicle_plugin* plugin, radicle_form_key key,
                                            const radicle_form_key* links, size_t count) {
  return edit(plugin, key, links, [&](auto& p, const auto& k) {
    std::vector<fo4::FormKey> path;
    path.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      path.push_back(required_form_key(links[i], "valid NAVM link required"));
    }
    return fo4::add_navi_path(p, k, path);
  });
}

radicle_status radicle_plugin_remove_navi_path(radicle_plugin* plugin, radicle_form_key key, size_t path) {
  return edit(plugin, key, &path, [&](auto& p, const auto& k) {
    return fo4::remove_navi_path(p, k, path);
  });
}

radicle_status radicle_plugin_create_navmesh(radicle_plugin* plugin, radicle_form_key cell, radicle_form_key* out_navmesh) {
  clear_error();
  if (out_navmesh != nullptr) *out_navmesh = {nullptr, 0};
  const auto parsed = to_form_key(cell);
  if (plugin == nullptr || !parsed || out_navmesh == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, cell and out_navmesh are required");
  }
  return guarded("unknown exception while creating NAVM", [&] {
    try {
      const auto key = fo4::create_navmesh(plugin->value, *parsed);
      *out_navmesh = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_navmesh_decode(const radicle_env* env, const radicle_record* record,
                                      const radicle_navmesh** out_navmesh) {
  clear_error();
  if (!env || !record || !out_navmesh) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required");
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_navmesh = nullptr;
  return guarded("unknown exception while decoding NAVM", [&] {
    const auto data = fo4::env::decode_navmesh(env->value, *to_ref(record));
    auto h = std::make_unique<GeometryHolder>();
    auto& v = h->view;
    if (data) {
      v.has_geometry = 1; v.version = data->version; v.parent = parent(*env, data->parent);
      for (const auto& p : data->vertices) h->vertices.push_back({p[0], p[1], p[2]});
      for (const auto& t : data->triangles) {
        radicle_navmesh_triangle row{};
        std::copy(t.vertices.begin(), t.vertices.end(), row.vertices);
        std::copy(t.edges.begin(), t.edges.end(), row.edges);
        row.clearance = t.clearance; row.traversal = t.traversal; row.flags = t.flags;
        h->triangles.push_back(row);
      }
      for (const auto& e : data->edges) h->edges.push_back({e.type, to_c_key(*env, e.navmesh), e.triangle, e.edge});
      for (const auto& d : data->doors) h->doors.push_back({d.triangle, d.crc, to_c_key(*env, d.door)});
      v.opaque_tail = reinterpret_cast<const std::uint8_t*>(data->opaque_tail.data());
      v.opaque_tail_size = data->opaque_tail.size();
    }
    v.vertices = h->vertices.data(); v.vertex_count = h->vertices.size();
    v.triangles = h->triangles.data(); v.triangle_count = h->triangles.size();
    v.edges = h->edges.data(); v.edge_count = h->edges.size();
    v.doors = h->doors.data(); v.door_count = h->doors.size();
    v.internal = h.get(); *out_navmesh = &h.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_navmesh_free(const radicle_navmesh* navmesh) {
  if (navmesh) delete static_cast<GeometryHolder*>(navmesh->internal);
}

radicle_status radicle_navmesh_triangle_edge_is_extra(const radicle_navmesh_triangle* triangle, size_t edge,
                                                      int32_t* out_is_extra) {
  clear_error();
  if (!triangle || !out_is_extra) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "triangle and output required");
  if (edge >= 3) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "edge must be 0, 1 or 2");
  fo4::env::NavmeshTriangle t;
  t.flags = triangle->flags;
  *out_is_extra = t.edge_is_extra(edge) ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_navi_decode(const radicle_env* env, const radicle_record* record,
                                  const radicle_navi** out_navi) {
  clear_error();
  if (!env || !record || !out_navi) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required");
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_navi = nullptr;
  return guarded("unknown exception while decoding NAVI", [&] {
    const auto data = fo4::env::decode_navmesh_info(env->value, *to_ref(record));
    auto h = std::make_unique<NaviHolder>();
    h->links.resize(data.size()); h->infos.resize(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
      const auto& d = data[i]; auto& v = h->infos[i]; auto& links = h->links[i];
      v.navmesh = to_c_key(*env, d.navmesh); v.flags = d.flags;
      v.location = {d.location[0], d.location[1], d.location[2]}; v.preferred = d.preferred;
      for (const auto& key : d.edges) links.edges.push_back(to_c_key(*env, key));
      for (const auto& key : d.preferred_edges) links.preferred_edges.push_back(to_c_key(*env, key));
      for (const auto& door : d.doors) links.doors.push_back({door.crc, to_c_key(*env, door.door)});
      v.edges = links.edges.data(); v.edge_count = links.edges.size();
      v.preferred_edges = links.preferred_edges.data(); v.preferred_edge_count = links.preferred_edges.size();
      v.doors = links.doors.data(); v.door_count = links.doors.size();
      v.has_island = d.has_island ? 1 : 0;
      v.island = reinterpret_cast<const std::uint8_t*>(d.island.data()); v.island_size = d.island.size();
      v.parent = parent(*env, d.parent);
    }
    h->view.infos = h->infos.data(); h->view.info_count = h->infos.size();
    h->view.internal = h.get(); *out_navi = &h.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_navi_free(const radicle_navi* navi) {
  if (navi) delete static_cast<NaviHolder*>(navi->internal);
}

radicle_status radicle_navi_path_list_decode(const radicle_env* env, const radicle_record* record,
                                             const radicle_navi_path_list** out_list) {
  clear_error();
  if (!env || !record || !out_list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required");
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_list = nullptr;
  return guarded("unknown exception while decoding NAVI paths", [&] {
    const auto data = fo4::env::decode_navmesh_paths(env->value, *to_ref(record));
    auto h = std::make_unique<PathListHolder>();
    h->keys.resize(data.paths.size());
    for (std::size_t i = 0; i < data.paths.size(); ++i) {
      for (const auto& key : data.paths[i]) h->keys[i].push_back(to_c_key(*env, key));
    }
    for (const auto& path : h->keys) h->paths.push_back({path.data(), path.size()});
    for (const auto& [key, value] : data.anchors) h->anchors.push_back({to_c_key(*env, key), value});
    h->view.paths = h->paths.data(); h->view.path_count = h->paths.size();
    h->view.anchors = h->anchors.data(); h->view.anchor_count = h->anchors.size();
    h->view.internal = h.get(); *out_list = &h.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_navi_path_list_free(const radicle_navi_path_list* list) {
  if (list) delete static_cast<PathListHolder*>(list->internal);
}

radicle_status radicle_env_cell_navmeshes(const radicle_env* env, radicle_form_key cell,
                                          const radicle_record*** out_records, size_t* out_count) {
  clear_error();
  if (!env || !out_records || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and outputs required");
  const auto key = to_form_key(cell);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "cell key required");
  *out_records = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing cell navmeshes", [&] {
    std::vector<const fo4::env::RecordRef*> refs;
    for (const auto& child : env->value.cell_children(*key)) {
      if (child.ref->record->header.signature == fo4::Signature::from_string("NAVM")) refs.push_back(child.ref);
    }
    return copy_records(refs, out_records, out_count);
  });
}

radicle_status radicle_door_passable(const radicle_env* env, radicle_form_key npc_key, radicle_form_key door_ref,
                                     const radicle_door_state* door_state, radicle_path_verdict* out_verdict) {
  clear_error();
  (void)npc_key;  // reserved: owner and faction rules are UNVERIFIED (docs/NAVM_NAVI.md)
  if (!env || !door_state || !out_verdict) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, door state and output required");
  const auto key = to_form_key(door_ref);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "door reference key required");
  *out_verdict = RADICLE_PATH_UNKNOWN;
  return guarded("unknown exception while evaluating door", [&] {
    const auto* ref = env->value.winner(*key);
    if (ref == nullptr || !fo4::env::is_placed_reference_signature(ref->record->header.signature)) {
      return fail(RADICLE_STATUS_NOT_FOUND, "door reference not found");
    }
    const auto placed = fo4::env::decode_placed_reference(env->value, *ref);
    *out_verdict = to_c_verdict(fo4::env::door_verdict(placed.lock, to_door_state(*door_state)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_navmesh_edge_link_usable(const radicle_env* env, uint32_t link_type, radicle_form_key npc_key,
                                                radicle_path_verdict* out_verdict) {
  clear_error();
  (void)npc_key;  // reserved: no NPC-dependent edge rule is proven (docs/NAVM_NAVI.md)
  if (!env || !out_verdict) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and output required");
  *out_verdict = to_c_verdict(fo4::env::edge_link_verdict(link_type));
  return RADICLE_STATUS_OK;
}

radicle_status radicle_door_teleport_leg(const radicle_env* env, radicle_form_key load_door_ref,
                                         const radicle_door_state* destination_door_state, radicle_teleport_leg* out_leg) {
  clear_error();
  if (!env || !destination_door_state || !out_leg) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, destination door state and output required");
  }
  const auto key = to_form_key(load_door_ref);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "load door reference key required");
  *out_leg = radicle_teleport_leg{};
  return guarded("unknown exception while planning teleport leg", [&] {
    const auto* ref = env->value.winner(*key);
    if (ref == nullptr || !fo4::env::is_placed_reference_signature(ref->record->header.signature)) {
      return fail(RADICLE_STATUS_NOT_FOUND, "load door reference not found");
    }
    const auto leg = fo4::env::teleport_leg(env->value, *ref, to_door_state(*destination_door_state));
    if (!leg) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "reference has no teleport");
    out_leg->destination_door = to_c_key(*env, leg->destination_door);
    out_leg->destination_cell = to_c_key(*env, leg->destination_cell);
    out_leg->has_navmesh = leg->navmesh ? 1 : 0;
    out_leg->navmesh = to_c_key(*env, leg->navmesh);
    out_leg->triangle = leg->triangle;
    if (leg->approach) {
      out_leg->has_approach = 1;
      out_leg->approach = {(*leg->approach)[0], (*leg->approach)[1], (*leg->approach)[2]};
    }
    out_leg->verdict = to_c_verdict(leg->verdict);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_navmesh_walk_view(const radicle_env* env, const radicle_record* record, float link_inset_metres,
                                         const radicle_walk_view** out_view) {
  clear_error();
  if (!env || !record || !out_view) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required");
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  if (!(link_inset_metres >= 0.0f)) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "link inset must not be negative");
  *out_view = nullptr;
  return guarded("unknown exception while building navmesh walk view", [&] {
    const auto data = fo4::env::build_walk_view(env->value, *to_ref(record), link_inset_metres);
    auto h = std::make_unique<WalkHolder>();
    auto& v = h->view;
    if (data) {
      v.has_geometry = 1;
      v.navmesh = to_c_key(*env, data->navmesh);
      v.has_grid = data->has_grid ? 1 : 0;
      v.grid_x = data->grid_x;
      v.grid_y = data->grid_y;
      v.skipped_triangle_count = data->skipped_triangles;
      for (const auto& p : data->vertices) h->vertices.push_back({p[0], p[1], p[2]});
      for (const auto& t : data->triangles) h->triangles.insert(h->triangles.end(), t.begin(), t.end());
      h->triangle_class = data->triangle_class;
      for (const auto& l : data->links) {
        h->links.push_back({{l.start[0], l.start[1], l.start[2]}, {l.end[0], l.end[1], l.end[2]}, l.source_triangle,
                            l.source_edge, to_c_key(*env, l.target_navmesh), l.target_triangle, l.target_edge,
                            to_c_verdict(l.verdict)});
      }
      for (const auto& d : data->doors) {
        h->doors.push_back({to_c_key(*env, d.door), h->door_triangles.size(), d.triangles.size()});
        h->door_triangles.insert(h->door_triangles.end(), d.triangles.begin(), d.triangles.end());
      }
    }
    v.metres_per_unit = fo4::env::k_walk_metres_per_unit;
    v.vertices = h->vertices.data(); v.vertex_count = h->vertices.size();
    v.triangles = h->triangles.data(); v.triangle_count = h->triangle_class.size();
    v.triangle_class = h->triangle_class.data();
    v.links = h->links.data(); v.link_count = h->links.size();
    v.doors = h->doors.data(); v.door_count = h->doors.size();
    v.door_triangles = h->door_triangles.data(); v.door_triangle_count = h->door_triangles.size();
    v.internal = h.get(); *out_view = &h.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_navmesh_walk_view_free(const radicle_walk_view* view) {
  if (view) delete static_cast<WalkHolder*>(view->internal);
}

radicle_status radicle_navmesh_find_path(const radicle_env* env, radicle_form_key start_navmesh, uint32_t start_triangle,
                                         radicle_form_key goal_navmesh, uint32_t goal_triangle,
                                         const radicle_navmesh_path_options* options, radicle_navmesh_path_node* out_nodes,
                                         size_t capacity, size_t* out_count, int32_t* out_found, float* out_cost) {
  clear_error();
  if (!env || !out_count || !out_found || !out_cost) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_count, out_found and out_cost required");
  }
  *out_count = 0;
  *out_found = 0;
  *out_cost = 0.0f;
  if (out_nodes == nullptr && capacity != 0) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_nodes required when capacity is non-zero");
  for (size_t i = 0; i < capacity; ++i) out_nodes[i] = radicle_navmesh_path_node{};
  const auto start = to_form_key(start_navmesh);
  const auto goal = to_form_key(goal_navmesh);
  if (!start || !goal) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "valid start and goal navmesh keys required");
  fo4::env::NavmeshPathOptions request;
  if (options != nullptr) {
    if (options->door_count != 0 && options->doors == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "doors required when door_count is non-zero");
    }
    request.forbidden_class = options->forbidden_class;
    request.allow_unknown_links = (options->flags & RADICLE_NAVMESH_PATH_ALLOW_UNKNOWN_LINKS) != 0;
    request.allow_unknown_doors = (options->flags & RADICLE_NAVMESH_PATH_ALLOW_UNKNOWN_DOORS) != 0;
    for (size_t i = 0; i < options->door_count; ++i) {
      const auto door = to_form_key(options->doors[i].door);
      if (!door) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "valid door key required");
      request.door_states.emplace_back(*door, to_door_state(options->doors[i].state));
    }
  }
  return guarded("unknown exception while searching navmesh path", [&] {
    const auto path = fo4::env::find_navmesh_path(env->value, *start, start_triangle, *goal, goal_triangle, request);
    if (path.status == fo4::env::NavmeshPathStatus::invalid_endpoint) {
      return fail(RADICLE_STATUS_NOT_FOUND, "start or goal navmesh triangle not found");
    }
    if (path.status == fo4::env::NavmeshPathStatus::no_path) return RADICLE_STATUS_OK;
    for (size_t i = 0; i < path.nodes.size() && i < capacity; ++i) {
      const auto& node = path.nodes[i];
      out_nodes[i] = {to_c_key(*env, node.navmesh), node.triangle,
                      (node.via_link ? RADICLE_NAVMESH_PATH_NODE_VIA_LINK : 0u) |
                          (node.needs_opening ? RADICLE_NAVMESH_PATH_NODE_NEEDS_OPENING : 0u)};
    }
    *out_count = path.nodes.size();
    *out_found = 1;
    *out_cost = path.cost;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_walk_agent_profile(const radicle_env* env, radicle_form_key race, radicle_agent_profile* out_profile) {
  clear_error();
  if (!env || !out_profile) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and output required");
  *out_profile = radicle_agent_profile{};
  std::optional<fo4::FormKey> key;
  if (race.plugin != nullptr && *race.plugin != '\0') {
    key = to_form_key(race);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "race key is not a valid form key");
  }
  return guarded("unknown exception while reading agent profile", [&] {
    const auto profile = fo4::env::agent_profile(env->value, key);
    if (!profile) return fail(RADICLE_STATUS_NOT_FOUND, "race not found");
    *out_profile = {profile->radius, profile->height, profile->large ? 1 : 0, profile->forbidden_class};
    return RADICLE_STATUS_OK;
  });
}

}
