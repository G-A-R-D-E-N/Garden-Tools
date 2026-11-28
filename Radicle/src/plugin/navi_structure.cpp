#include "fo4/plugin/navmesh_structure.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/scalar.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_navi = Signature::from_string("NAVI");
constexpr auto k_nver = Signature::from_string("NVER");
constexpr auto k_nvmi = Signature::from_string("NVMI");
constexpr auto k_nvsi = Signature::from_string("NVSI");
constexpr auto k_nvpp = Signature::from_string("NVPP");
constexpr auto k_navm = Signature::from_string("NAVM");
constexpr auto k_wrld = Signature::from_string("WRLD");
constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_refr = Signature::from_string("REFR");

constexpr std::size_t k_none = static_cast<std::size_t>(-1);
constexpr std::size_t k_nvmi_scalars = 24;
constexpr std::uint32_t k_anchor_flag = 0x40;
constexpr const char* k_anchor_refused =
    "navi anchor infos cannot be added or removed; their count sets the NVPP list index";
constexpr const char* k_island_refused = "navi info island data cannot be authored yet";
constexpr const char* k_duplicate_info = "navi info already exists for this navmesh";
constexpr const char* k_duplicate_nvsi = "navi deleted-navmesh list must be a single NVSI subrecord";
constexpr const char* k_duplicate_nvpp = "navi path list must be a single NVPP subrecord";
constexpr const char* k_empty_path = "navi path needs at least one navmesh";
constexpr const char* k_path_layout = "unsupported NVPP layout";

template <class T> T load(const Bytes& data, std::size_t at) {
  T value{};
  std::memcpy(&value, data.data() + at, sizeof value);
  return value;
}

template <class T> void store(Bytes& data, std::size_t at, const T& value) {
  std::memcpy(data.data() + at, &value, sizeof value);
}

template <class T> void append(Bytes& out, const T& value) {
  const auto* begin = reinterpret_cast<const std::byte*>(&value);
  out.insert(out.end(), begin, begin + sizeof value);
}

template <class T> void insert_value(Bytes& data, std::size_t at, const T& value) {
  Bytes bytes;
  append(bytes, value);
  data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin(), bytes.end());
}

void erase_bytes(Bytes& data, std::size_t at, std::size_t count) {
  data.erase(data.begin() + static_cast<std::ptrdiff_t>(at),
             data.begin() + static_cast<std::ptrdiff_t>(at + count));
}

void require_size(const Bytes& data, std::size_t size) {
  if (data.size() < size) throw schema::ScalarError("unsupported NAVI layout");
}

void require_finite(float value) {
  if (!std::isfinite(value)) throw schema::ScalarError("navi values must be finite");
}

void require_target(Plugin& plugin, const FormKey& target, Signature expected, const char* message) {
  if (target.owner().empty() || target.local().value() == 0) throw schema::ScalarError(message);
  if (target.owner() == plugin.key) {
    const auto* found = find_record(plugin, target);
    if (!found || found->header.signature != expected) throw schema::ScalarError(message);
  }
}

bool is_null(const FormKey& key) {
  return key.owner().empty() || key.local().value() == 0;
}

std::uint32_t raw_link(Plugin& plugin, const FormKey& target) {
  return static_cast<std::uint32_t>(encode_plugin_form_id(target, plugin_form_id_context(plugin)).value());
}

void require_navi_version(const Record& record) {
  const Subrecord* version = nullptr;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != k_nver) continue;
    if (version) throw schema::ScalarError("duplicate NVER");
    version = &sub;
  }
  if (!version || version->data.size() != 4 || load<std::uint32_t>(version->data, 0) != 15) {
    throw schema::ScalarError("NAVI version 15 required");
  }
}

std::size_t nvmi_position(const Record& record, std::size_t index) {
  std::size_t seen = 0;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != k_nvmi) continue;
    if (seen++ == index) return i;
  }
  return k_none;
}

std::size_t nvsi_position(const Record& record) {
  std::size_t found = k_none;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != k_nvsi) continue;
    if (found != k_none) throw schema::ScalarError(k_duplicate_nvsi);
    found = i;
  }
  return found;
}

std::size_t nvpp_position(const Record& record) {
  std::size_t found = k_none;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != k_nvpp) continue;
    if (found != k_none) throw schema::ScalarError(k_duplicate_nvpp);
    found = i;
  }
  return found;
}

struct PathLayout {
  std::uint32_t paths;
  std::size_t anchors_at;
  std::vector<std::size_t> offsets;
};

PathLayout scan_paths(const Bytes& data) {
  require_size(data, 4);
  PathLayout out{};
  out.paths = load<std::uint32_t>(data, 0);
  std::size_t at = 4;
  out.offsets.reserve(out.paths);
  for (std::uint32_t i = 0; i < out.paths; ++i) {
    require_size(data, at + 4);
    out.offsets.push_back(at);
    const auto length = load<std::uint32_t>(data, at);
    at += 4;
    if (length > (data.size() - at) / 4) throw schema::ScalarError(k_path_layout);
    at += static_cast<std::size_t>(length) * 4;
  }
  require_size(data, at + 4);
  out.anchors_at = at;
  const auto anchors = load<std::uint32_t>(data, at);
  if (anchors > (data.size() - at - 4) / 8) throw schema::ScalarError(k_path_layout);
  if (at + 4 + static_cast<std::size_t>(anchors) * 8 != data.size()) {
    throw schema::ScalarError(k_path_layout);
  }
  return out;
}

struct ListSpan {
  std::size_t count_at;
  std::size_t width;
  std::uint32_t count;
};

std::size_t list_index(NaviLinkKind kind) {
  switch (kind) {
    case NaviLinkKind::edge: return 0;
    case NaviLinkKind::preferred_edge: return 1;
    case NaviLinkKind::door: return 2;
  }
  throw schema::ScalarError("invalid NAVI link kind");
}

ListSpan locate_list(const Bytes& data, std::size_t which) {
  require_size(data, k_nvmi_scalars);
  std::size_t at = k_nvmi_scalars;
  for (std::size_t list = 0; list <= which; ++list) {
    const std::size_t width = list == 2 ? 8 : 4;
    require_size(data, at + 4);
    const auto count = load<std::uint32_t>(data, at);
    if (count > (data.size() - at - 4) / width) throw schema::ScalarError("unsupported NAVI layout");
    if (list == which) return {at, width, count};
    at += 4 + static_cast<std::size_t>(count) * width;
  }
  throw schema::ScalarError("unsupported NAVI layout");
}

void append_parent(Bytes& out, Plugin& plugin, const NavmeshParent& parent) {
  append(out, parent.crc);
  if (is_null(parent.world)) {
    require_target(plugin, parent.cell, k_cell, "navi parent CELL must be a CELL");
    append(out, std::uint32_t{0});
    append(out, raw_link(plugin, parent.cell));
    return;
  }
  require_target(plugin, parent.world, k_wrld, "navi parent world must be a WRLD");
  append(out, raw_link(plugin, parent.world));
  append(out, parent.grid_y);
  append(out, parent.grid_x);
}

Bytes build_info(Plugin& plugin, const NavmeshInfo& value) {
  if (value.has_island || !value.island.empty()) throw schema::ScalarError(k_island_refused);
  for (const auto component : value.location) require_finite(component);
  require_finite(value.preferred);
  require_target(plugin, value.navmesh, k_navm, "navi navmesh must be a NAVM");
  if ((value.flags & k_anchor_flag) != 0) throw schema::ScalarError(k_anchor_refused);
  Bytes out;
  append(out, raw_link(plugin, value.navmesh));
  append(out, value.flags);
  for (const auto component : value.location) append(out, component);
  append(out, value.preferred);
  for (const auto* list : {&value.edges, &value.preferred_edges}) {
    append(out, static_cast<std::uint32_t>(list->size()));
    for (const auto& target : *list) {
      require_target(plugin, target, k_navm, "navi edge link must be a NAVM");
      append(out, raw_link(plugin, target));
    }
  }
  append(out, static_cast<std::uint32_t>(value.doors.size()));
  for (const auto& door : value.doors) {
    require_target(plugin, door.door, k_refr, "navi door link must be a REFR");
    append(out, door.crc);
    append(out, raw_link(plugin, door.door));
  }
  append(out, std::uint8_t{0});
  append_parent(out, plugin, value.parent);
  return out;
}

template <class Op>
ScalarMutationStatus mutate_navi(Plugin& plugin, const FormKey& key, Op&& op) {
  auto* record = detail::find_mutable_record(plugin, key);
  if (!record) {
    assign_record_form_keys(plugin);
    record = detail::find_mutable_record(plugin, key);
  }
  if (!record) return ScalarMutationStatus::record_missing;
  if (record->header.signature != k_navi) throw schema::ScalarError("NAVI record required");
  require_navi_version(*record);
  if (record->header.form_version < 128) throw schema::ScalarError("unsupported NAVI layout");
  Record candidate = *record;
  const auto status = op(candidate);
  if (status != ScalarMutationStatus::updated) return status;
  for (const auto& sub : candidate.subrecords) {
    if ((sub.signature == k_nvmi || sub.signature == k_nvsi) && !navmesh_mixed_formid_offsets(candidate, sub)) {
      throw schema::ScalarError("navi edit would produce an invalid layout");
    }
  }
  *record = std::move(candidate);
  detail::mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

}

ScalarMutationStatus add_navi_info(Plugin& plugin, const FormKey& key, const NavmeshInfo& value) {
  return mutate_navi(plugin, key, [&](Record& record) {
    const auto payload = build_info(plugin, value);
    const auto navmesh = raw_link(plugin, value.navmesh);
    for (const auto& sub : record.subrecords) {
      if (sub.signature == k_nvmi && sub.data.size() >= 4 && load<std::uint32_t>(sub.data, 0) == navmesh) {
        throw schema::ScalarError(k_duplicate_info);
      }
    }
    std::size_t at = 0;
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      const auto signature = record.subrecords[i].signature;
      if (signature == k_nver || signature == k_nvmi) at = i + 1;
    }
    record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(at),
                             Subrecord{k_nvmi, payload, false});
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus remove_navi_info(Plugin& plugin, const FormKey& key, std::size_t info_index) {
  return mutate_navi(plugin, key, [&](Record& record) {
    const auto pos = nvmi_position(record, info_index);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    const auto& data = record.subrecords[pos].data;
    require_size(data, 8);
    if ((load<std::uint32_t>(data, 4) & k_anchor_flag) != 0) throw schema::ScalarError(k_anchor_refused);
    record.subrecords.erase(record.subrecords.begin() + static_cast<std::ptrdiff_t>(pos));
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus add_navi_link(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                   NaviLinkKind kind, const FormKey& navmesh) {
  if (kind == NaviLinkKind::door) throw schema::ScalarError("navi door links use add_navi_door");
  return mutate_navi(plugin, key, [&](Record& record) {
    require_target(plugin, navmesh, k_navm, "navi edge link must be a NAVM");
    const auto pos = nvmi_position(record, info_index);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    auto& data = record.subrecords[pos].data;
    const auto list = locate_list(data, list_index(kind));
    insert_value(data, list.count_at + 4 + static_cast<std::size_t>(list.count) * list.width,
                 raw_link(plugin, navmesh));
    store(data, list.count_at, static_cast<std::uint32_t>(list.count + 1));
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus add_navi_door(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                   const NaviDoorLink& door) {
  return mutate_navi(plugin, key, [&](Record& record) {
    require_target(plugin, door.door, k_refr, "navi door link must be a REFR");
    const auto pos = nvmi_position(record, info_index);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    auto& data = record.subrecords[pos].data;
    const auto list = locate_list(data, list_index(NaviLinkKind::door));
    const auto at = list.count_at + 4 + static_cast<std::size_t>(list.count) * list.width;
    insert_value(data, at, door.crc);
    insert_value(data, at + 4, raw_link(plugin, door.door));
    store(data, list.count_at, static_cast<std::uint32_t>(list.count + 1));
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus remove_navi_link(Plugin& plugin, const FormKey& key, std::size_t info_index,
                                      NaviLinkKind kind, std::size_t index) {
  return mutate_navi(plugin, key, [&](Record& record) {
    const auto pos = nvmi_position(record, info_index);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    auto& data = record.subrecords[pos].data;
    const auto list = locate_list(data, list_index(kind));
    if (index >= static_cast<std::size_t>(list.count)) return ScalarMutationStatus::subrecord_missing;
    erase_bytes(data, list.count_at + 4 + index * list.width, list.width);
    store(data, list.count_at, static_cast<std::uint32_t>(list.count - 1));
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus add_navi_deleted(Plugin& plugin, const FormKey& key, const FormKey& navmesh) {
  return mutate_navi(plugin, key, [&](Record& record) {
    require_target(plugin, navmesh, k_navm, "navi deleted navmesh must be a NAVM");
    Bytes entry(4);
    store(entry, 0, raw_link(plugin, navmesh));
    const auto pos = nvsi_position(record);
    if (pos == k_none) {
      record.subrecords.push_back(Subrecord{k_nvsi, std::move(entry), false});
    } else {
      auto& data = record.subrecords[pos].data;
      data.insert(data.end(), entry.begin(), entry.end());
    }
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus remove_navi_deleted(Plugin& plugin, const FormKey& key, std::size_t index) {
  return mutate_navi(plugin, key, [&](Record& record) {
    const auto pos = nvsi_position(record);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    auto& data = record.subrecords[pos].data;
    if (index >= data.size() / 4) return ScalarMutationStatus::subrecord_missing;
    erase_bytes(data, index * 4, 4);
    if (data.empty()) record.subrecords.erase(record.subrecords.begin() + static_cast<std::ptrdiff_t>(pos));
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus add_navi_path(Plugin& plugin, const FormKey& key, const std::vector<FormKey>& links) {
  if (links.empty()) throw schema::ScalarError(k_empty_path);
  return mutate_navi(plugin, key, [&](Record& record) {
    for (const auto& link : links) require_target(plugin, link, k_navm, "navi path link must be a NAVM");
    const auto pos = nvpp_position(record);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    auto& data = record.subrecords[pos].data;
    const auto layout = scan_paths(data);
    Bytes entry;
    entry.reserve(4 + links.size() * 4);
    append(entry, static_cast<std::uint32_t>(links.size()));
    for (const auto& link : links) append(entry, raw_link(plugin, link));
    data.insert(data.begin() + static_cast<std::ptrdiff_t>(layout.anchors_at), entry.begin(), entry.end());
    store(data, 0, static_cast<std::uint32_t>(layout.paths + 1));
    return ScalarMutationStatus::updated;
  });
}

ScalarMutationStatus remove_navi_path(Plugin& plugin, const FormKey& key, std::size_t path) {
  return mutate_navi(plugin, key, [&](Record& record) {
    const auto pos = nvpp_position(record);
    if (pos == k_none) return ScalarMutationStatus::subrecord_missing;
    auto& data = record.subrecords[pos].data;
    const auto layout = scan_paths(data);
    if (path >= layout.offsets.size()) return ScalarMutationStatus::subrecord_missing;
    const auto begin = layout.offsets[path];
    const auto end = path + 1 < layout.offsets.size() ? layout.offsets[path + 1] : layout.anchors_at;
    erase_bytes(data, begin, end - begin);
    store(data, 0, static_cast<std::uint32_t>(layout.paths - 1));
    return ScalarMutationStatus::updated;
  });
}

}
