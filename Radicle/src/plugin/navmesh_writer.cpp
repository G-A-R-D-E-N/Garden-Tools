#include "fo4/plugin/navmesh_writer.hpp"

#include "record_edit.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/binary/cursor.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fo4 {
namespace {

template<class T> void store(std::span<std::byte> data, std::size_t at, const T& value) {
  std::memcpy(data.data() + at, &value, sizeof(value));
}

void finite(float value) {
  if (!std::isfinite(value)) throw schema::ScalarError("navmesh values must be finite");
}

ScalarMutationStatus edit(Plugin& plugin, const FormKey& key, std::size_t block,
                          std::size_t index, auto&& patch) {
  auto* record = detail::find_mutable_record(plugin, key);
  if (!record) {
    assign_record_form_keys(plugin);
    record = detail::find_mutable_record(plugin, key);
  }
  if (!record) return ScalarMutationStatus::record_missing;
  if (record->header.signature != Signature::from_string("NAVM")) {
    throw schema::ScalarError("navmesh edits require NAVM");
  }
  Subrecord* geometry = nullptr;
  for (auto& sub : record->subrecords) {
    if (sub.signature != Signature::from_string("NVNM")) continue;
    if (geometry) throw schema::ScalarError("duplicate NVNM geometry");
    geometry = &sub;
  }
  if (!geometry) return ScalarMutationStatus::subrecord_missing;
  binary::Cursor cursor(geometry->data);
  if (record->header.form_version < 128 || cursor.remaining() < 4 || cursor.read_u32() != 15 ||
      !navmesh_mixed_formid_offsets(*record, *geometry)) {
    throw schema::ScalarError("unsupported navmesh geometry layout");
  }
  cursor.read_bytes(12);
  const std::size_t widths[] = {12, 21, 11, 10};
  std::array<std::size_t, 4> counts{};
  std::array<std::size_t, 4> starts{};
  counts[0] = cursor.read_u32();
  starts[0] = cursor.position();
  for (std::size_t b = 0; b < 3; ++b) {
    cursor.read_bytes(counts[b] * widths[b]);
    counts[b + 1] = cursor.read_u32();
    starts[b + 1] = cursor.position();
  }
  if (index >= counts[block]) return ScalarMutationStatus::subrecord_missing;
  const auto at = starts[block] + index * widths[block];
  std::vector<std::byte> candidate(geometry->data.begin() + at,
                                 geometry->data.begin() + at + widths[block]);
  patch(std::span{candidate}, counts);
  if (std::equal(candidate.begin(), candidate.end(), geometry->data.begin() + at)) {
    return ScalarMutationStatus::unchanged;
  }
  std::copy(candidate.begin(), candidate.end(), geometry->data.begin() + at);
  detail::mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

}

ScalarMutationStatus set_navmesh_vertex(Plugin& plugin, const FormKey& key, std::size_t index,
                                      const std::array<float, 3>& value) {
  return edit(plugin, key, 0, index, [&](auto data, auto) {
    for (std::size_t i = 0; i < value.size(); ++i) { finite(value[i]); store(data, i * 4, value[i]); }
  });
}

ScalarMutationStatus set_navmesh_triangle(Plugin& plugin, const FormKey& key, std::size_t index,
                                        const env::NavmeshTriangle& value) {
  return edit(plugin, key, 1, index, [&](auto data, auto counts) {
    finite(value.clearance);
    if (std::to_integer<std::uint8_t>(data[16]) != value.traversal) {
      throw schema::ScalarError("unknown triangle byte must be preserved");
    }
    std::uint32_t old_flags = 0;
    std::memcpy(&old_flags, data.data() + 17, 4);
    if (old_flags != value.flags) {
      throw schema::ScalarError("triangle and cover flags must be preserved");
    }
    for (std::size_t i = 0; i < 3; ++i) {
      std::uint16_t old_vertex = 0;
      std::int16_t old_edge = 0;
      std::memcpy(&old_vertex, data.data() + i * 2, sizeof old_vertex);
      std::memcpy(&old_edge, data.data() + 6 + i * 2, sizeof old_edge);
      if (value.vertices[i] != old_vertex && value.vertices[i] >= counts[0]) throw schema::ScalarError("triangle vertex is out of range");
      const auto limit = static_cast<std::int64_t>(((value.flags >> i) & 1) ? counts[2] : counts[1]);
      const auto low = ((value.flags >> i) & 1) ? 0 : -1;
      if (value.edges[i] != old_edge && (value.edges[i] < low || value.edges[i] >= limit)) throw schema::ScalarError("triangle edge is out of range");
      store(data, i * 2, value.vertices[i]); store(data, 6 + i * 2, value.edges[i]);
    }
    store(data, 12, value.clearance);
  });
}

ScalarMutationStatus set_navmesh_door(Plugin& plugin, const FormKey& key, std::size_t index,
                                    const env::NavmeshDoorLink& value) {
  return edit(plugin, key, 3, index, [&](auto data, auto counts) {
    if (value.triangle < 0 || value.triangle >= static_cast<std::int64_t>(counts[1])) throw schema::ScalarError("door triangle is out of range");
    if (value.door.owner().empty() || value.door.local().value() == 0) throw schema::ScalarError("door reference cannot be null");
    if (value.door.owner() == plugin.key) {
      const auto* target = find_record(plugin, value.door);
      if (!target || target->header.signature != Signature::from_string("REFR")) {
        throw schema::ScalarError("local door target must be REFR");
      }
    }
    const auto raw = encode_plugin_form_id(value.door, plugin_form_id_context(plugin)).value();
    store(data, 0, value.triangle); store(data, 2, value.crc); store(data, 6, raw);
  });
}

}
