#include "c_api_env_internal.hpp"
#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh.hpp"
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

}
