#include "fo4/plugin/navmesh_structure.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/scalar.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using Row = std::vector<std::byte>;
using Rows = std::vector<Row>;

constexpr auto k_nvnm = Signature::from_string("NVNM");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_navm = Signature::from_string("NAVM");
constexpr auto k_navmesh_link = Signature::from_string("NAVM");
constexpr auto k_refr = Signature::from_string("REFR");

enum Block : std::size_t {
  vertex_block,
  triangle_block,
  edge_block,
  door_block,
  cover_block,
  cover_map_block,
  waypoint_block,
  block_count,
};

constexpr std::array<std::size_t, block_count> k_widths{12, 21, 11, 10, 8, 4, 18};
constexpr std::size_t k_head_size = 16;
constexpr std::size_t k_grid_header_size = 36;
constexpr const char* k_referenced = "referenced navmesh element cannot be removed";
constexpr const char* k_inbound_link =
    "an edge link in this plugin points at this triangle or a later one; remove the link first";
constexpr const char* k_inbound_unreadable =
    "cannot remove a triangle while another navmesh in this plugin cannot be read";
constexpr const char* k_lists_present = "navmesh vertex lists must be empty before removing a vertex";
constexpr const char* k_cover_present = "navmesh covers must be removed before removing a vertex";
constexpr const char* k_map_present = "navmesh triangle map must be empty before removing a triangle";
constexpr const char* k_cover_map_present = "navmesh cover map must be empty before this removal";
constexpr const char* k_grid_present = "navmesh grid cells must be empty before removing a triangle";
constexpr const char* k_vertex_cap = "navmesh vertex count is limited to 65535 by u16 references";
constexpr const char* k_triangle_cap = "navmesh triangle count is limited to 32767 by i16 references";
constexpr const char* k_edge_cap = "navmesh edge link count is limited to 32767 by i16 references";
constexpr std::size_t k_max_vertices = 65535;
constexpr std::size_t k_max_indexed = 32767;

struct Geometry {
  std::array<std::byte, k_head_size> head{};
  std::array<Rows, block_count> blocks{};
  std::array<std::byte, k_grid_header_size> grid{};
  std::vector<std::vector<std::uint16_t>> cells;
};

struct Working {
  Geometry geometry;
  bool vertex_lists{false};
  bool triangle_map{false};
};

template <class T> T load(const Row& row, std::size_t at) {
  T value{};
  std::memcpy(&value, row.data() + at, sizeof value);
  return value;
}

template <class T> void store(Row& row, std::size_t at, const T& value) {
  std::memcpy(row.data() + at, &value, sizeof value);
}

void require_finite(float value) {
  if (!std::isfinite(value)) throw schema::ScalarError("navmesh values must be finite");
}

void require_triangle_index(std::int64_t triangle, std::size_t count) {
  if (triangle < 0 || triangle >= static_cast<std::int64_t>(count)) {
    throw schema::ScalarError("navmesh triangle index is out of range");
  }
}

void require_target(Plugin& plugin, const FormKey& target, Signature expected, const char* message) {
  if (target.owner().empty() || target.local().value() == 0) throw schema::ScalarError(message);
  if (target.owner() == plugin.key) {
    const auto* found = find_record(plugin, target);
    if (!found || found->header.signature != expected) throw schema::ScalarError(message);
  }
}

std::uint32_t raw_link(Plugin& plugin, const FormKey& target) {
  return static_cast<std::uint32_t>(encode_plugin_form_id(target, plugin_form_id_context(plugin)).value());
}

bool triangle_edge_is_extra(const Row& row, std::size_t edge) {
  return ((load<std::uint32_t>(row, 17) >> edge) & 1u) != 0;
}

template <class T> void drop_ref(T& value, std::size_t removed, const char* message) {
  const auto current = static_cast<std::int64_t>(value);
  const auto gone = static_cast<std::int64_t>(removed);
  if (current == gone) throw schema::ScalarError(message);
  if (current > gone) value = static_cast<T>(current - 1);
}

template <class T> void drop_ref_at(Row& row, std::size_t at, std::size_t removed, const char* message) {
  auto value = load<T>(row, at);
  drop_ref(value, removed, message);
  store(row, at, value);
}

void erase_row(Rows& rows, std::size_t index) {
  rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(index));
}

Geometry parse_geometry(std::span<const std::byte> data) {
  Geometry g;
  std::size_t cursor = 0;
  const auto read = [&](std::byte* out, std::size_t bytes) {
    if (cursor > data.size() || data.size() - cursor < bytes) {
      throw schema::ScalarError("truncated navmesh geometry");
    }
    if (bytes != 0) std::memcpy(out, data.data() + cursor, bytes);
    cursor += bytes;
  };
  const auto read_count = [&]() {
    std::array<std::byte, 4> raw{};
    read(raw.data(), raw.size());
    std::uint32_t count = 0;
    std::memcpy(&count, raw.data(), raw.size());
    return static_cast<std::size_t>(count);
  };
  read(g.head.data(), g.head.size());
  for (std::size_t b = 0; b < block_count; ++b) {
    const auto count = read_count();
    const auto width = k_widths[b];
    if (count > (data.size() - cursor) / width) throw schema::ScalarError("truncated navmesh geometry");
    auto& rows = g.blocks[b];
    rows.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      Row row(width);
      read(row.data(), width);
      rows.push_back(std::move(row));
    }
  }
  read(g.grid.data(), g.grid.size());
  std::uint32_t divisor = 0;
  std::memcpy(&divisor, g.grid.data(), sizeof divisor);
  const auto cell_count = static_cast<std::size_t>(divisor) * divisor;
  for (std::size_t index = 0; index < cell_count; ++index) {
    const auto count = read_count();
    if (count > (data.size() - cursor) / 2) throw schema::ScalarError("truncated navmesh geometry");
    std::vector<std::uint16_t> cell(count);
    if (count != 0) read(reinterpret_cast<std::byte*>(cell.data()), count * 2);
    g.cells.push_back(std::move(cell));
  }
  if (cursor != data.size()) throw schema::ScalarError("trailing navmesh geometry bytes");
  return g;
}

void append_bytes(std::vector<std::byte>& out, const void* source, std::size_t bytes) {
  const auto* begin = static_cast<const std::byte*>(source);
  out.insert(out.end(), begin, begin + bytes);
}

std::vector<std::byte> serialize_geometry(const Geometry& g) {
  std::vector<std::byte> out;
  append_bytes(out, g.head.data(), g.head.size());
  for (const auto& rows : g.blocks) {
    const auto count = static_cast<std::uint32_t>(rows.size());
    append_bytes(out, &count, sizeof count);
    for (const auto& row : rows) out.insert(out.end(), row.begin(), row.end());
  }
  append_bytes(out, g.grid.data(), g.grid.size());
  for (const auto& cell : g.cells) {
    const auto count = static_cast<std::uint32_t>(cell.size());
    append_bytes(out, &count, sizeof count);
    if (!cell.empty()) append_bytes(out, cell.data(), cell.size() * 2);
  }
  return out;
}

// Constraint: every NAVM in this plugin must parse, because an unreadable one may hold an inbound link.
void refuse_inbound_edge_links(Plugin& plugin, const FormKey& key, std::size_t index) {
  const auto target = raw_link(plugin, key);
  visit_records(plugin, [&](const Record& record) {
    if (record.header.signature != k_navm) return;
    if (record.undecodable) throw schema::ScalarError(k_inbound_unreadable);
    for (const auto& sub : record.subrecords) {
      if (sub.signature != k_nvnm) continue;
      Geometry geometry;
      try {
        geometry = parse_geometry(sub.data);
      } catch (const schema::ScalarError&) {
        throw schema::ScalarError(k_inbound_unreadable);
      }
      for (const auto& row : geometry.blocks[edge_block]) {
        if (load<std::uint32_t>(row, 4) != target) continue;
        const auto triangle = load<std::int16_t>(row, 8);
        if (triangle >= 0 && static_cast<std::size_t>(triangle) >= index) {
          throw schema::ScalarError(k_inbound_link);
        }
      }
    }
  });
}

template <class Op>
ScalarMutationStatus mutate(Plugin& plugin, const FormKey& key, Op&& op) {
  auto* record = detail::find_mutable_record(plugin, key);
  if (!record) {
    assign_record_form_keys(plugin);
    record = detail::find_mutable_record(plugin, key);
  }
  if (!record) return ScalarMutationStatus::record_missing;
  if (record->header.signature != k_navm) throw schema::ScalarError("navmesh edits require NAVM");
  Subrecord* geometry = nullptr;
  bool vertex_lists = false;
  bool triangle_map = false;
  for (auto& sub : record->subrecords) {
    if (sub.signature == k_nvnm) {
      if (geometry != nullptr) throw schema::ScalarError("duplicate NVNM geometry");
      geometry = &sub;
    } else if (sub.signature == k_pnam || sub.signature == k_nnam) {
      vertex_lists = vertex_lists || !sub.data.empty();
    } else if (sub.signature == k_mnam) {
      triangle_map = triangle_map || !sub.data.empty();
    }
  }
  if (!geometry) return ScalarMutationStatus::subrecord_missing;
  if (record->header.form_version < 128 || !navmesh_mixed_formid_offsets(*record, *geometry)) {
    throw schema::ScalarError("unsupported navmesh geometry layout");
  }
  Working work{parse_geometry(geometry->data), vertex_lists, triangle_map};
  const auto status = op(work);
  if (status != ScalarMutationStatus::updated) return status;
  auto bytes = serialize_geometry(work.geometry);
  Subrecord probe = *geometry;
  probe.data = bytes;
  if (!navmesh_mixed_formid_offsets(*record, probe)) {
    throw schema::ScalarError("navmesh edit would produce an invalid layout");
  }
  geometry->data = std::move(bytes);
  detail::mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_vertex(Working& work, const std::array<float, 3>& value) {
  for (const auto component : value) require_finite(component);
  if (work.geometry.blocks[vertex_block].size() >= k_max_vertices) throw schema::ScalarError(k_vertex_cap);
  Row row(k_widths[vertex_block]);
  for (std::size_t i = 0; i < value.size(); ++i) store(row, i * 4, value[i]);
  work.geometry.blocks[vertex_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_vertex(Working& work, std::size_t index) {
  auto& g = work.geometry;
  if (index >= g.blocks[vertex_block].size()) return ScalarMutationStatus::subrecord_missing;
  if (work.vertex_lists) throw schema::ScalarError(k_lists_present);
  if (!g.blocks[cover_block].empty()) throw schema::ScalarError(k_cover_present);
  for (auto& row : g.blocks[triangle_block]) {
    for (std::size_t i = 0; i < 3; ++i) drop_ref_at<std::uint16_t>(row, i * 2, index, k_referenced);
  }
  erase_row(g.blocks[vertex_block], index);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_triangle(Working& work, const NavmeshTriangle& value) {
  require_finite(value.clearance);
  const auto& g = work.geometry;
  const auto vertex_count = g.blocks[vertex_block].size();
  const auto triangle_count = g.blocks[triangle_block].size();
  const auto edge_count = g.blocks[edge_block].size();
  if (triangle_count >= k_max_indexed) throw schema::ScalarError(k_triangle_cap);
  for (const auto vertex : value.vertices) {
    if (static_cast<std::size_t>(vertex) >= vertex_count) throw schema::ScalarError("triangle vertex is out of range");
  }
  for (std::size_t i = 0; i < 3; ++i) {
    const bool extra = value.edge_is_extra(i);
    const auto limit = static_cast<std::int64_t>(extra ? edge_count : triangle_count);
    const std::int64_t low = extra ? 0 : -1;
    const auto edge = static_cast<std::int64_t>(value.edges[i]);
    if (edge < low || edge >= limit) throw schema::ScalarError("triangle edge is out of range");
  }
  Row row(k_widths[triangle_block]);
  for (std::size_t i = 0; i < 3; ++i) {
    store(row, i * 2, value.vertices[i]);
    store(row, 6 + i * 2, value.edges[i]);
  }
  store(row, 12, value.clearance);
  store(row, 16, static_cast<std::uint8_t>(value.traversal));
  store(row, 17, value.flags);
  work.geometry.blocks[triangle_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_triangle(Working& work, std::size_t index) {
  auto& g = work.geometry;
  auto& triangles = g.blocks[triangle_block];
  if (index >= triangles.size()) return ScalarMutationStatus::subrecord_missing;
  if (work.triangle_map) throw schema::ScalarError(k_map_present);
  if (!g.blocks[cover_map_block].empty()) throw schema::ScalarError(k_cover_map_present);
  for (const auto& cell : g.cells) {
    if (!cell.empty()) throw schema::ScalarError(k_grid_present);
  }
  for (std::size_t t = 0; t < triangles.size(); ++t) {
    if (t == index) continue;
    for (std::size_t i = 0; i < 3; ++i) {
      if (!triangle_edge_is_extra(triangles[t], i)) drop_ref_at<std::int16_t>(triangles[t], 6 + i * 2, index, k_referenced);
    }
  }
  for (auto& row : g.blocks[door_block]) drop_ref_at<std::int16_t>(row, 0, index, k_referenced);
  for (auto& row : g.blocks[waypoint_block]) drop_ref_at<std::int16_t>(row, 12, index, k_referenced);
  erase_row(triangles, index);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_edge_link(Working& work, Plugin& plugin, const NavmeshEdgeLink& value) {
  if (value.type > 2) throw schema::ScalarError("navmesh edge link type must be 0, 1 or 2");
  if (value.edge > 2) throw schema::ScalarError("navmesh edge link edge must be 0, 1 or 2");
  if (value.triangle < 0) throw schema::ScalarError("navmesh edge link triangle must not be negative");
  if (work.geometry.blocks[edge_block].size() >= k_max_indexed) throw schema::ScalarError(k_edge_cap);
  require_target(plugin, value.navmesh, k_navmesh_link, "navmesh edge link target must be a NAVM");
  Row row(k_widths[edge_block]);
  store(row, 0, value.type);
  store(row, 4, raw_link(plugin, value.navmesh));
  store(row, 8, value.triangle);
  store(row, 10, value.edge);
  work.geometry.blocks[edge_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_edge_link(Working& work, std::size_t index) {
  auto& g = work.geometry;
  if (index >= g.blocks[edge_block].size()) return ScalarMutationStatus::subrecord_missing;
  for (auto& row : g.blocks[triangle_block]) {
    for (std::size_t i = 0; i < 3; ++i) {
      if (triangle_edge_is_extra(row, i)) drop_ref_at<std::int16_t>(row, 6 + i * 2, index, k_referenced);
    }
  }
  erase_row(g.blocks[edge_block], index);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_door(Working& work, Plugin& plugin, const NavmeshDoorLink& value) {
  require_triangle_index(value.triangle, work.geometry.blocks[triangle_block].size());
  require_target(plugin, value.door, k_refr, "navmesh door target must be a REFR");
  Row row(k_widths[door_block]);
  store(row, 0, value.triangle);
  store(row, 2, value.crc);
  store(row, 6, raw_link(plugin, value.door));
  work.geometry.blocks[door_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_door(Working& work, std::size_t index) {
  auto& doors = work.geometry.blocks[door_block];
  if (index >= doors.size()) return ScalarMutationStatus::subrecord_missing;
  erase_row(doors, index);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_cover(Working& work, const NavmeshCover& value) {
  const auto vertex_count = work.geometry.blocks[vertex_block].size();
  for (const auto vertex : value.vertices) {
    if (static_cast<std::size_t>(vertex) >= vertex_count) throw schema::ScalarError("navmesh cover vertex is out of range");
  }
  Row row(k_widths[cover_block]);
  store(row, 0, value.vertices[0]);
  store(row, 2, value.vertices[1]);
  for (std::size_t i = 0; i < value.data.size(); ++i) row[4 + i] = std::byte{value.data[i]};
  work.geometry.blocks[cover_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_cover(Working& work, std::size_t index) {
  auto& g = work.geometry;
  if (index >= g.blocks[cover_block].size()) return ScalarMutationStatus::subrecord_missing;
  if (!g.blocks[cover_map_block].empty()) throw schema::ScalarError(k_cover_map_present);
  erase_row(g.blocks[cover_block], index);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_cover_map(Working& work, const NavmeshCoverMap& value) {
  const auto& g = work.geometry;
  if (static_cast<std::size_t>(value.cover) >= g.blocks[cover_block].size()) {
    throw schema::ScalarError("navmesh cover map cover is out of range");
  }
  if (static_cast<std::size_t>(value.triangle) >= g.blocks[triangle_block].size()) {
    throw schema::ScalarError("navmesh cover map triangle is out of range");
  }
  Row row(k_widths[cover_map_block]);
  store(row, 0, value.cover);
  store(row, 2, value.triangle);
  work.geometry.blocks[cover_map_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_cover_map(Working& work, std::size_t index) {
  auto& g = work.geometry;
  if (index >= g.blocks[cover_map_block].size()) return ScalarMutationStatus::subrecord_missing;
  erase_row(g.blocks[cover_map_block], index);
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus add_waypoint(Working& work, const NavmeshWaypoint& value) {
  for (const auto component : value.position) require_finite(component);
  require_triangle_index(value.triangle, work.geometry.blocks[triangle_block].size());
  Row row(k_widths[waypoint_block]);
  for (std::size_t i = 0; i < value.position.size(); ++i) store(row, i * 4, value.position[i]);
  store(row, 12, value.triangle);
  store(row, 14, value.flags);
  work.geometry.blocks[waypoint_block].push_back(std::move(row));
  return ScalarMutationStatus::updated;
}

ScalarMutationStatus remove_waypoint(Working& work, std::size_t index) {
  auto& waypoints = work.geometry.blocks[waypoint_block];
  if (index >= waypoints.size()) return ScalarMutationStatus::subrecord_missing;
  erase_row(waypoints, index);
  return ScalarMutationStatus::updated;
}

}

ScalarMutationStatus add_navmesh_vertex(Plugin& plugin, const FormKey& key, const std::array<float, 3>& value) {
  return mutate(plugin, key, [&](Working& work) { return add_vertex(work, value); });
}

ScalarMutationStatus remove_navmesh_vertex(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) { return remove_vertex(work, index); });
}

ScalarMutationStatus add_navmesh_triangle(Plugin& plugin, const FormKey& key, const NavmeshTriangle& value) {
  return mutate(plugin, key, [&](Working& work) { return add_triangle(work, value); });
}

ScalarMutationStatus remove_navmesh_triangle(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) {
    if (index < work.geometry.blocks[triangle_block].size()) refuse_inbound_edge_links(plugin, key, index);
    return remove_triangle(work, index);
  });
}

ScalarMutationStatus add_navmesh_edge_link(Plugin& plugin, const FormKey& key, const NavmeshEdgeLink& value) {
  return mutate(plugin, key, [&](Working& work) { return add_edge_link(work, plugin, value); });
}

ScalarMutationStatus remove_navmesh_edge_link(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) { return remove_edge_link(work, index); });
}

ScalarMutationStatus add_navmesh_door(Plugin& plugin, const FormKey& key, const NavmeshDoorLink& value) {
  return mutate(plugin, key, [&](Working& work) { return add_door(work, plugin, value); });
}

ScalarMutationStatus remove_navmesh_door(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) { return remove_door(work, index); });
}

ScalarMutationStatus add_navmesh_cover(Plugin& plugin, const FormKey& key, const NavmeshCover& value) {
  return mutate(plugin, key, [&](Working& work) { return add_cover(work, value); });
}

ScalarMutationStatus remove_navmesh_cover(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) { return remove_cover(work, index); });
}

ScalarMutationStatus add_navmesh_cover_map(Plugin& plugin, const FormKey& key, const NavmeshCoverMap& value) {
  return mutate(plugin, key, [&](Working& work) { return add_cover_map(work, value); });
}

ScalarMutationStatus remove_navmesh_cover_map(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) { return remove_cover_map(work, index); });
}

ScalarMutationStatus add_navmesh_waypoint(Plugin& plugin, const FormKey& key, const NavmeshWaypoint& value) {
  return mutate(plugin, key, [&](Working& work) { return add_waypoint(work, value); });
}

ScalarMutationStatus remove_navmesh_waypoint(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate(plugin, key, [&](Working& work) { return remove_waypoint(work, index); });
}

}
