#include "fo4/forms/cell_relocate_nav.hpp"

#include "../plugin/child_groups.hpp"
#include "fo4/constants.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::forms {
namespace {

constexpr std::uint32_t k_nvnm_version = 15;
constexpr std::uint16_t k_nvnm_form_version_minimum = 128;
constexpr std::size_t k_nvnm_vertices_offset = 16;
constexpr std::size_t k_triangle_size = 21;
constexpr std::size_t k_triangle_edges_offset = 6;
constexpr std::size_t k_triangle_flags_offset = 17;
constexpr std::size_t k_link_size = 11;
constexpr std::size_t k_nvnm_grid_size = 36;
constexpr std::uint32_t k_nvnm_max_divisor = 12;
constexpr std::uint32_t k_link_type_portal = 0;
constexpr std::uint32_t k_link_type_two = 2;
constexpr std::uint16_t k_no_neighbour = 0xFFFF;
constexpr std::size_t k_nvmi_lists_offset = 24;
constexpr std::size_t k_nvmi_island_header = 24;
constexpr std::size_t k_nvmi_parent_size = 12;

template <typename T>
T read_at(const std::vector<std::byte>& data, std::size_t offset) {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

template <typename T>
void write_at(std::vector<std::byte>& data, std::size_t offset, T value) {
  std::memcpy(data.data() + offset, &value, sizeof(T));
}

struct Shift {
  float x{0};
  float y{0};
};

bool shift_xy(std::vector<std::byte>& data, std::size_t offset, Shift shift) {
  for (const auto& [axis, delta] : {std::pair{std::size_t{0}, shift.x}, std::pair{std::size_t{4}, shift.y}}) {
    if (delta == 0.0f) continue;
    if (offset + axis + sizeof(float) > data.size()) return false;
    const auto value = read_at<float>(data, offset + axis) + delta;
    if (!std::isfinite(value)) return false;
    write_at(data, offset + axis, value);
  }
  return true;
}

Subrecord* unique_subrecord(Record& record, Signature signature, bool& duplicate) {
  Subrecord* found = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) duplicate = true;
    found = &subrecord;
  }
  return found;
}

struct Counted {
  std::size_t first{0};
  std::size_t count{0};
};

std::optional<Counted> take_counted(const std::vector<std::byte>& data,
                                    std::size_t& cursor,
                                    std::size_t element_size) {
  if (data.size() < cursor || data.size() - cursor < 4) return std::nullopt;
  const auto count = read_at<std::uint32_t>(data, cursor);
  cursor += 4;
  if (count > (data.size() - cursor) / element_size) return std::nullopt;
  const Counted counted{cursor, count};
  cursor += static_cast<std::size_t>(count) * element_size;
  return counted;
}

struct Layout {
  std::size_t triangles_first{0};
  std::size_t triangle_count{0};
  std::size_t links_count_at{0};
  std::size_t link_count{0};
};

std::optional<Layout> parse_layout(const Record& record, const std::vector<std::byte>& data) {
  if (record.header.form_version < k_nvnm_form_version_minimum || data.size() < k_nvnm_vertices_offset) {
    return std::nullopt;
  }
  if (read_at<std::uint32_t>(data, 0) != k_nvnm_version || read_at<std::uint32_t>(data, 8) == 0) {
    return std::nullopt;
  }
  std::size_t cursor = k_nvnm_vertices_offset;
  if (!take_counted(data, cursor, 12)) return std::nullopt;
  const auto triangles = take_counted(data, cursor, k_triangle_size);
  if (!triangles) return std::nullopt;
  const auto links_count_at = cursor;
  const auto links = take_counted(data, cursor, k_link_size);
  if (!links || !take_counted(data, cursor, 10) || !take_counted(data, cursor, 8) ||
      !take_counted(data, cursor, 4) || !take_counted(data, cursor, 18) ||
      data.size() - cursor < k_nvnm_grid_size) {
    return std::nullopt;
  }
  const auto divisor = read_at<std::uint32_t>(data, cursor);
  if (divisor > k_nvnm_max_divisor) return std::nullopt;
  cursor += k_nvnm_grid_size;
  for (std::uint32_t cell = 0; cell < divisor * divisor; ++cell) {
    if (!take_counted(data, cursor, 2)) return std::nullopt;
  }
  if (cursor != data.size()) return std::nullopt;
  return Layout{triangles->first, triangles->count, links_count_at, links->count};
}

std::uint32_t link_raw(const std::vector<std::byte>& data, const Layout& layout, std::size_t index) {
  return read_at<std::uint32_t>(data, layout.links_count_at + 4 + index * k_link_size + 4);
}

std::uint32_t link_type(const std::vector<std::byte>& data, const Layout& layout, std::size_t index) {
  return read_at<std::uint32_t>(data, layout.links_count_at + 4 + index * k_link_size);
}

FormKey key_of(std::uint32_t raw, const PluginFormIdContext& context) {
  return resolve_plugin_form_id(RawFormId{raw}, context);
}

// Drops the flagged links and resets the triangle slots that named them; false on an inconsistent slot.
bool strip_links(std::vector<std::byte>& data, const Layout& layout, const std::vector<bool>& remove) {
  std::vector<std::uint16_t> renumbered(layout.link_count, 0);
  std::uint16_t kept = 0;
  for (std::size_t index = 0; index < layout.link_count; ++index) {
    if (!remove[index]) renumbered[index] = kept++;
  }
  for (std::size_t triangle = 0; triangle < layout.triangle_count; ++triangle) {
    const auto base = layout.triangles_first + triangle * k_triangle_size;
    auto flags = read_at<std::uint32_t>(data, base + k_triangle_flags_offset);
    for (std::size_t slot = 0; slot < 3; ++slot) {
      if (((flags >> slot) & 1u) == 0) continue;
      const auto offset = base + k_triangle_edges_offset + slot * 2;
      const auto index = read_at<std::uint16_t>(data, offset);
      if (index >= layout.link_count) return false;
      if (!remove[index]) {
        write_at(data, offset, renumbered[index]);
        continue;
      }
      write_at(data, offset, k_no_neighbour);
      flags &= ~(1u << slot);
    }
    write_at(data, base + k_triangle_flags_offset, flags);
  }
  std::vector<std::byte> rebuilt(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(layout.links_count_at));
  rebuilt.resize(rebuilt.size() + 4);
  write_at<std::uint32_t>(rebuilt, layout.links_count_at, kept);
  const auto first = layout.links_count_at + 4;
  for (std::size_t index = 0; index < layout.link_count; ++index) {
    if (remove[index]) continue;
    const auto from = data.begin() + static_cast<std::ptrdiff_t>(first + index * k_link_size);
    rebuilt.insert(rebuilt.end(), from, from + k_link_size);
  }
  rebuilt.insert(rebuilt.end(), data.begin() + static_cast<std::ptrdiff_t>(first + layout.link_count * k_link_size),
                 data.end());
  data = std::move(rebuilt);
  return true;
}

struct Edit {
  bool ok{false};
  std::size_t removed{0};
  std::set<FormKey> kept_targets;
};

// Targets that the NVMI edge lists may still name: portal and type-two links to other navmeshes (Fallout4.esm survey, code-notes).
std::set<FormKey> listed_targets(const std::vector<std::byte>& data, const Layout& layout,
                                 const PluginFormIdContext& context, const FormKey& self) {
  std::set<FormKey> out;
  for (std::size_t index = 0; index < layout.link_count; ++index) {
    const auto type = link_type(data, layout, index);
    const auto raw = link_raw(data, layout, index);
    if ((type != k_link_type_portal && type != k_link_type_two) || raw == 0) continue;
    const auto target = key_of(raw, context);
    if (target != self) out.insert(target);
  }
  return out;
}

// remove_if names the link targets to drop.
template <typename Predicate>
Edit edit_navmesh(Plugin& plugin, const FormKey& key, Predicate remove_if) {
  auto located = fo4::detail::find_mutable_location(plugin, key);
  if (!located.has_value()) return {};
  Record& record = located->record();
  bool duplicate = false;
  auto* nvnm = unique_subrecord(record, sig_nvnm, duplicate);
  if (duplicate || nvnm == nullptr || record.undecodable) return {};
  const auto layout = parse_layout(record, nvnm->data);
  if (!layout) return {};
  const auto context = plugin_form_id_context(plugin);
  std::vector<bool> remove(layout->link_count, false);
  std::size_t removed = 0;
  for (std::size_t index = 0; index < layout->link_count; ++index) {
    const auto raw = link_raw(nvnm->data, *layout, index);
    if (raw == 0 || !remove_if(key_of(raw, context))) continue;
    remove[index] = true;
    ++removed;
  }
  if (removed != 0) {
    auto data = nvnm->data;
    if (!strip_links(data, *layout, remove)) return {};
    nvnm->data = std::move(data);
    fo4::detail::mark_record_modified(plugin, record);
  }
  const auto after = parse_layout(record, nvnm->data);
  if (!after) return {};
  return {true, removed, listed_targets(nvnm->data, *after, context, key)};
}

void collect_navmeshes(const Group& group, std::vector<FormKey>& out, bool& malformed) {
  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<Group>(&child)) {
      collect_navmeshes(*nested, out, malformed);
      continue;
    }
    const auto& record = std::get<Record>(child);
    if (record.header.signature != sig_navm || record.header.flags.is_deleted()) continue;
    if (record.undecodable) malformed = true;
    out.push_back(record.form_key);
  }
}

// Same containment rules as copy_placed_reference_as_override, for a NAVM in a temporary or persistent bucket.
bool copy_navmesh_as_override(Plugin& next, const Plugin& source, const FormKey& key) {
  const auto located = forms::detail::locate_record(source, key);
  if (located.record == nullptr || located.record->header.signature != sig_navm) return false;
  if (located.path.size() < 2 || located.path[located.path.size() - 2]->header.group_type !=
                                     static_cast<std::int32_t>(GroupType::cell_children)) {
    return false;
  }
  const auto source_context = plugin_form_id_context(source);
  const auto cell_index = located.path.size() - 2;
  const auto cell = key_of(located.path[cell_index]->header.label_as_u32(), source_context);
  const auto source_cell = forms::detail::locate_record(source, cell);
  if (source_cell.record == nullptr || source_cell.record->header.signature != sig_cell ||
      source_cell.path.size() != cell_index) {
    return false;
  }
  std::optional<FormKey> world;
  forms::detail::RecordLocation source_world;
  for (std::size_t index = 0; index < source_cell.path.size(); ++index) {
    if (source_cell.path[index]->header.group_type != static_cast<std::int32_t>(GroupType::world_children)) {
      continue;
    }
    world = key_of(source_cell.path[index]->header.label_as_u32(), source_context);
    source_world = forms::detail::locate_record(source, *world);
    if (source_world.record == nullptr || source_world.record->header.signature != sig_wrld ||
        source_world.path.size() != index) {
      return false;
    }
  }

  std::set<PluginKey> owners;
  for (const auto& owner : {key.owner(), cell.owner(), world.has_value() ? world->owner() : PluginKey{}}) {
    if (!owner.empty()) owners.insert(owner);
  }
  for (const auto* group : located.path) {
    forms::detail::collect_group_label_owner(*group, source_context, owners);
  }
  forms::detail::collect_record_link_owners(source, *located.record, owners);
  forms::detail::add_missing_masters(next, source, owners);

  const auto destination_context = plugin_form_id_context(next);
  if (world.has_value()) {
    forms::detail::validate_destination_record_path(next, *world, sig_wrld, source_world.path, source_context,
                                                    destination_context, "WRLD");
    auto* world_parent =
        forms::detail::ensure_group_path(next, source_world.path, source_context, destination_context);
    if (find_record(next, *world) == nullptr) {
      world_parent->children.emplace_back(forms::detail::copy_record_for_destination(
          *source_world.record, *world, source_context, destination_context));
    }
  }
  forms::detail::validate_destination_record_path(next, cell, sig_cell, source_cell.path, source_context,
                                                  destination_context, "CELL");
  auto* cell_parent =
      forms::detail::ensure_group_path(next, source_cell.path, source_context, destination_context);
  if (find_record(next, cell) == nullptr) {
    cell_parent->children.emplace_back(forms::detail::copy_record_for_destination(
        *source_cell.record, cell, source_context, destination_context));
  }
  auto* children = forms::detail::ensure_cell_children_group(
      *cell_parent, located.path[cell_index]->header, cell, source_context, destination_context);
  auto* bucket = forms::detail::ensure_reference_bucket(*children, located.path.back()->header, source_context,
                                                        destination_context);
  bucket->children.emplace_back(
      forms::detail::copy_record_for_destination(*located.record, key, source_context, destination_context));
  return true;
}

// NVMI: navmesh, flags, location (3 f32), preferred, A list, B list, doors, island, parent (Fallout4.esm survey in the code-notes).
struct NvmiLayout {
  std::size_t a_at{0};
  std::size_t a_count{0};
  std::size_t b_at{0};
  std::size_t b_count{0};
  std::size_t island_at{0};
  bool island{false};
  std::size_t tail_at{0};
};

std::optional<NvmiLayout> parse_nvmi(const std::vector<std::byte>& data) {
  if (data.size() < k_nvmi_lists_offset + 4) return std::nullopt;
  NvmiLayout layout;
  std::size_t cursor = k_nvmi_lists_offset;
  layout.a_at = cursor;
  const auto a = take_counted(data, cursor, 4);
  if (!a) return std::nullopt;
  layout.a_count = a->count;
  layout.b_at = cursor;
  const auto b = take_counted(data, cursor, 4);
  if (!b || !take_counted(data, cursor, 8) || data.size() - cursor < 1) return std::nullopt;
  layout.b_count = b->count;
  layout.island_at = cursor;
  layout.island = read_at<std::uint8_t>(data, cursor) != 0;
  ++cursor;
  if (layout.island) {
    if (data.size() - cursor < k_nvmi_island_header) return std::nullopt;
    cursor += k_nvmi_island_header;
    if (!take_counted(data, cursor, 6) || !take_counted(data, cursor, 12)) return std::nullopt;
  }
  layout.tail_at = cursor;
  if (data.size() - cursor != k_nvmi_parent_size) return std::nullopt;
  return layout;
}

struct NaviMove {
  std::int32_t old_x{0};
  std::int32_t old_y{0};
  std::int32_t new_x{0};
  std::int32_t new_y{0};
  Shift shift;
};

std::vector<std::byte> filtered_list(const std::vector<std::byte>& data, std::size_t at, std::size_t count,
                                     const PluginFormIdContext& context, const std::set<FormKey>& keep) {
  std::vector<std::byte> out(4);
  std::uint32_t kept = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const auto raw = read_at<std::uint32_t>(data, at + 4 + index * 4);
    if (raw != 0 && !keep.contains(key_of(raw, context))) continue;
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at + 4 + index * 4),
               data.begin() + static_cast<std::ptrdiff_t>(at + 8 + index * 4));
    ++kept;
  }
  write_at(out, 0, kept);
  return out;
}

// move is set for navmeshes that changed grid; the edge lists of every touched navmesh are narrowed to keep.
std::optional<std::vector<std::byte>> rewrite_nvmi(const std::vector<std::byte>& original,
                                                  const PluginFormIdContext& context,
                                                  const std::set<FormKey>& keep,
                                                  const NaviMove* move) {
  const auto layout = parse_nvmi(original);
  if (!layout) return std::nullopt;
  auto data = original;
  if (move != nullptr) {
    const auto parent = layout->tail_at;
    if (read_at<std::uint32_t>(data, parent + 4) == 0 ||
        read_at<std::int16_t>(data, parent + 8) != static_cast<std::int16_t>(move->old_y) ||
        read_at<std::int16_t>(data, parent + 10) != static_cast<std::int16_t>(move->old_x)) {
      return std::nullopt;
    }
    write_at(data, parent + 8, static_cast<std::int16_t>(move->new_y));
    write_at(data, parent + 10, static_cast<std::int16_t>(move->new_x));
    if (!shift_xy(data, 8, move->shift)) return std::nullopt;
    if (layout->island) {
      const auto island = layout->island_at + 1;
      if (!shift_xy(data, island, move->shift) || !shift_xy(data, island + 12, move->shift)) return std::nullopt;
      const auto triangles = read_at<std::uint32_t>(data, island + k_nvmi_island_header);
      const auto vertices = island + k_nvmi_island_header + 4 + static_cast<std::size_t>(triangles) * 6;
      const auto count = read_at<std::uint32_t>(data, vertices);
      for (std::size_t index = 0; index < count; ++index) {
        if (!shift_xy(data, vertices + 4 + index * 12, move->shift)) return std::nullopt;
      }
    }
  }
  std::vector<std::byte> out(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(layout->a_at));
  const auto a = filtered_list(data, layout->a_at, layout->a_count, context, keep);
  const auto b = filtered_list(data, layout->b_at, layout->b_count, context, keep);
  out.insert(out.end(), a.begin(), a.end());
  out.insert(out.end(), b.begin(), b.end());
  const auto rest = layout->b_at + 4 + layout->b_count * 4;
  out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(rest), data.end());
  return out;
}

std::optional<FormKey> nvmi_key(const Subrecord& subrecord, const PluginFormIdContext& context) {
  if (subrecord.signature != sig_nvmi || subrecord.data.size() < 4) return std::nullopt;
  const auto raw = read_at<std::uint32_t>(subrecord.data, 0);
  if (raw == 0) return std::nullopt;
  return key_of(raw, context);
}

const Subrecord* find_nvmi(const Record& record, const PluginFormIdContext& context, const FormKey& key) {
  for (const auto& subrecord : record.subrecords) {
    const auto found = nvmi_key(subrecord, context);
    if (found.has_value() && *found == key) return &subrecord;
  }
  return nullptr;
}

std::size_t stale_paths(const Record& record, const PluginFormIdContext& context, const std::set<FormKey>& moved) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig_nvpp) continue;
    const auto& data = subrecord.data;
    if (data.size() < 4) return 0;
    const auto lists = read_at<std::uint32_t>(data, 0);
    std::size_t cursor = 4;
    std::size_t stale = 0;
    for (std::uint32_t list = 0; list < lists; ++list) {
      const auto entries = take_counted(data, cursor, 4);
      if (!entries) return stale;
      for (std::size_t entry = 0; entry < entries->count; ++entry) {
        const auto raw = read_at<std::uint32_t>(data, entries->first + entry * 4);
        if (raw != 0 && moved.contains(key_of(raw, context))) {
          ++stale;
          break;
        }
      }
    }
    return stale;
  }
  return 0;
}

Record navi_stub(const Record& source, const FormKey& navi, std::initializer_list<const Subrecord*> parts) {
  Record stub;
  stub.header = source.header;
  stub.form_key = navi;
  std::vector<Subrecord> items;
  for (const auto* part : parts) {
    if (part != nullptr) items.push_back(*part);
  }
  stub.subrecords = SubrecordList(std::move(items));
  return stub;
}

const Subrecord* first_of(const Record& record, Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

// Walks only the top-level groups of each plugin, never its records.
std::optional<FormKey> navi_key_of(const env::Environment& env) {
  std::optional<FormKey> found;
  for (std::size_t index = 0; index < env.plugins().size(); ++index) {
    for (const auto& group : env.plugin(index).groups) {
      if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
          group.header.top_label() != sig_navi) {
        continue;
      }
      for (const auto& child : group.children) {
        const auto* record = std::get_if<Record>(&child);
        if (record == nullptr) continue;
        if (found.has_value() && *found != record->form_key) return std::nullopt;
        found = record->form_key;
      }
    }
  }
  return found;
}

struct NaviWork {
  std::map<FormKey, std::set<FormKey>> keep;
  std::set<FormKey> moved;
  NaviMove move;
};

struct NaviSource {
  const Plugin* plugin{nullptr};
  const Record* record{nullptr};
  const Subrecord* nvmi{nullptr};
};

// False when a touched NVMI entry does not match the surveyed layout.
bool update_navi(Plugin& next, const env::Environment& env, const NaviWork& work, CellNavigationResult& result) {
  const auto navi_found = navi_key_of(env);
  const auto* winner_ref = navi_found.has_value() ? env.winner(*navi_found) : nullptr;
  if (winner_ref == nullptr) {
    result.navi_entries_absent = work.keep.size();
    return true;
  }
  const auto navi_key = *navi_found;
  const auto versions = env.versions(navi_key);

  std::map<FormKey, NaviSource> staged;
  const auto* patch_navi = find_record(next, navi_key);
  if (patch_navi != nullptr) require_decodable(*patch_navi);
  const auto patch_context = plugin_form_id_context(next);
  for (auto version = versions.rbegin(); version != versions.rend() && staged.size() < work.keep.size(); ++version) {
    const auto context = plugin_form_id_context(env.plugin(version->plugin));
    for (const auto& [key, keep] : work.keep) {
      if (staged.contains(key) || (patch_navi != nullptr && find_nvmi(*patch_navi, patch_context, key) != nullptr)) {
        continue;
      }
      if (const auto* found = find_nvmi(*version->record, context, key)) {
        staged[key] = {&env.plugin(version->plugin), version->record, found};
      }
    }
  }
  std::size_t present = staged.size();
  for (const auto& [key, keep] : work.keep) {
    if (patch_navi != nullptr && find_nvmi(*patch_navi, patch_context, key) != nullptr) ++present;
  }
  result.navi_entries_absent = work.keep.size() - present;
  if (present == 0) return true;

  const auto& winner = *winner_ref;
  const auto& winner_plugin = env.plugin(winner.plugin);
  const auto* nver = first_of(*winner.record, sig_nver);
  const auto* nvpp = first_of(*winner.record, sig_nvpp);
  std::set<PluginKey> owners;
  for (const auto& [key, source] : staged) {
    owners.clear();
    forms::detail::collect_record_link_owners(*source.plugin, navi_stub(*source.record, navi_key, {nver, source.nvmi}), owners);
    if (!navi_key.owner().empty()) owners.insert(navi_key.owner());
    forms::detail::add_missing_masters(next, *source.plugin, owners);
  }
  if (patch_navi == nullptr) {
    owners.clear();
    forms::detail::collect_record_link_owners(winner_plugin, navi_stub(*winner.record, navi_key, {nver, nvpp}), owners);
    if (!navi_key.owner().empty()) owners.insert(navi_key.owner());
    forms::detail::add_missing_masters(next, winner_plugin, owners);
    const auto located = forms::detail::locate_record(winner_plugin, navi_key);
    const auto source_context = plugin_form_id_context(winner_plugin);
    const auto destination_context = plugin_form_id_context(next);
    auto* parent = forms::detail::ensure_group_path(next, located.path, source_context, destination_context);
    parent->children.emplace_back(forms::detail::copy_record_for_destination(
        navi_stub(*winner.record, navi_key, {nver, nvpp}), navi_key, source_context, destination_context));
  }

  const auto destination_context = plugin_form_id_context(next);
  auto located = fo4::detail::find_mutable_location(next, navi_key);
  if (!located.has_value()) return false;
  Record& navi = located->record();
  for (const auto& [key, keep] : work.keep) {
    std::vector<std::byte> bytes;
    Subrecord* existing = nullptr;
    for (auto& subrecord : navi.subrecords) {
      const auto found = nvmi_key(subrecord, destination_context);
      if (found.has_value() && *found == key) existing = &subrecord;
    }
    if (existing != nullptr) {
      bytes = existing->data;
    } else if (const auto source = staged.find(key); source != staged.end()) {
      const auto copied = forms::detail::copy_record_for_destination(
          navi_stub(*source->second.record, navi_key, {nver, source->second.nvmi}), navi_key,
          plugin_form_id_context(*source->second.plugin), destination_context);
      bytes = copied.subrecords.back().data;
    } else {
      continue;
    }
    const auto rewritten =
        rewrite_nvmi(bytes, destination_context, keep, work.moved.contains(key) ? &work.move : nullptr);
    if (!rewritten) return false;
    if (existing != nullptr) {
      existing->data = *rewritten;
    } else {
      auto position = navi.subrecords.end();
      for (auto it = navi.subrecords.begin(); it != navi.subrecords.end(); ++it) {
        if (it->signature == sig_nvpp) position = it;
      }
      navi.subrecords.insert(position, Subrecord{sig_nvmi, *rewritten, false});
    }
    ++result.navi_entries_updated;
  }
  fo4::detail::mark_record_modified(next, navi);
  result.stale_preferred_paths = stale_paths(navi, destination_context, work.moved);
  return true;
}

}  // namespace

CellNavigationResult relocate_cell_navigation(Plugin& patch,
                                              const env::Environment& env,
                                              const FormKey& cell,
                                              std::int32_t old_x,
                                              std::int32_t old_y,
                                              std::int32_t new_x,
                                              std::int32_t new_y) {
  const auto fail = [](CellNavigationStatus status, std::string message = {}) {
    CellNavigationResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
  };
  try {
    auto next = patch;
    assign_record_form_keys(next);
    auto located = fo4::detail::find_mutable_location(next, cell);
    if (!located.has_value()) return fail(CellNavigationStatus::cell_missing);
    const Record& record = located->record();
    if (record.header.signature != sig_cell) return fail(CellNavigationStatus::not_a_cell);
    const auto& path = located->path;
    if (path.size() != 4 || path[1]->header.group_type != static_cast<std::int32_t>(GroupType::world_children)) {
      return fail(CellNavigationStatus::not_exterior);
    }
    const auto grid = cell_grid(record);
    if (!grid.has_value() || grid->first != new_x || grid->second != new_y) {
      return fail(CellNavigationStatus::not_relocated, "the cell is not at the new grid");
    }
    if (old_x == new_x && old_y == new_y) return fail(CellNavigationStatus::unchanged);

    std::vector<FormKey> moved_keys;
    bool malformed = false;
    if (const auto* children = owned_child_group(*path.back(), located->index);
        children != nullptr && children->header.group_type == static_cast<std::int32_t>(GroupType::cell_children)) {
      collect_navmeshes(*children, moved_keys, malformed);
    }
    if (malformed) return fail(CellNavigationStatus::malformed, "undecodable navmesh under the cell");
    if (moved_keys.empty()) return fail(CellNavigationStatus::unchanged);
    const std::set<FormKey> moved(moved_keys.begin(), moved_keys.end());

    std::set<FormKey> neighbours;
    const auto context = plugin_form_id_context(next);
    for (const auto& key : moved_keys) {
      const auto* navm = find_record(next, key);
      const Subrecord* nvnm = nullptr;
      for (const auto& subrecord : navm->subrecords) {
        if (subrecord.signature != sig_nvnm) continue;
        if (nvnm != nullptr) return fail(CellNavigationStatus::malformed, "duplicate NVNM");
        nvnm = &subrecord;
      }
      if (nvnm == nullptr) return fail(CellNavigationStatus::malformed, "navmesh without geometry");
      const auto layout = parse_layout(*navm, nvnm->data);
      if (!layout) return fail(CellNavigationStatus::malformed, "unsupported navmesh geometry");
      if (read_at<std::int16_t>(nvnm->data, 12) != static_cast<std::int16_t>(new_y) ||
          read_at<std::int16_t>(nvnm->data, 14) != static_cast<std::int16_t>(new_x)) {
        return fail(CellNavigationStatus::not_relocated, "a navmesh still names the old grid");
      }
      for (std::size_t index = 0; index < layout->link_count; ++index) {
        const auto raw = link_raw(nvnm->data, *layout, index);
        if (raw == 0) continue;
        const auto target = key_of(raw, context);
        if (!moved.contains(target)) neighbours.insert(target);
      }
    }

    CellNavigationResult result;
    std::set<FormKey> copied;
    for (const auto& key : neighbours) {
      if (find_record(next, key) != nullptr) continue;
      const auto* winner = env.winner(key);
      if (winner == nullptr) return fail(CellNavigationStatus::neighbour_missing, key.to_string());
      if (winner->record->header.flags.is_deleted()) continue;
      if (winner->record->header.signature != sig_navm) return fail(CellNavigationStatus::malformed, key.to_string());
      const auto& source = env.plugin(winner->plugin);
      const auto source_context = plugin_form_id_context(source);
      const auto* nvnm = first_of(*winner->record, sig_nvnm);
      const auto layout = nvnm == nullptr ? std::nullopt : parse_layout(*winner->record, nvnm->data);
      if (!layout) return fail(CellNavigationStatus::malformed, key.to_string());
      bool links_back = false;
      for (std::size_t index = 0; index < layout->link_count && !links_back; ++index) {
        const auto raw = link_raw(nvnm->data, *layout, index);
        links_back = raw != 0 && moved.contains(key_of(raw, source_context));
      }
      if (!links_back) continue;
      if (!copy_navmesh_as_override(next, source, key)) return fail(CellNavigationStatus::copy_failed, key.to_string());
      copied.insert(key);
      result.neighbour_overrides.push_back(key);
    }

    NaviWork work;
    work.moved = moved;
    work.move = {old_x, old_y, new_x, new_y,
                 Shift{static_cast<float>(new_x - old_x) * k_cell_world_units_f32, static_cast<float>(new_y - old_y) * k_cell_world_units_f32}};
    for (const auto& key : moved_keys) {
      const auto edit = edit_navmesh(next, key, [&](const FormKey& target) { return !moved.contains(target); });
      if (!edit.ok) return fail(CellNavigationStatus::malformed, key.to_string());
      result.links_removed += edit.removed;
      work.keep[key] = edit.kept_targets;
    }
    for (const auto& key : neighbours) {
      if (find_record(next, key) == nullptr) continue;
      const auto edit = edit_navmesh(next, key, [&](const FormKey& target) { return moved.contains(target); });
      if (!edit.ok) return fail(CellNavigationStatus::malformed, key.to_string());
      result.back_links_removed += edit.removed;
      if (edit.removed != 0) work.keep[key] = edit.kept_targets;
    }
    if (!update_navi(next, env, work, result)) return fail(CellNavigationStatus::navi_unsupported);

    const bool changed = result.links_removed + result.back_links_removed + result.navi_entries_updated != 0 ||
                         !result.neighbour_overrides.empty();
    if (!changed) {
      result.status = CellNavigationStatus::unchanged;
      return result;
    }
    next.original_range.clear();
    next.original_structure_fingerprint = 0;
    patch = std::move(next);
    result.status = CellNavigationStatus::updated;
    return result;
  } catch (const std::exception& error) {
    return fail(CellNavigationStatus::malformed, error.what());
  }
}

}  // namespace fo4::forms
