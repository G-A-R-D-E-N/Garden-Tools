#include "fo4/forms/land_edit.hpp"

#include "fo4/environment/cell_view.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/cell_edit.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"
#include "land_normals.hpp"
#include "placed_reference_copy_internal.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr std::initializer_list<const char*> k_land_order = {
    "DATA", "VNML", "VHGT", "VCLR", "BTXT", "ATXT", "VTXT"};

struct Refusal {
  LandEditStatus status;
};

std::size_t rank_of(Signature field) {
  std::size_t rank = 0;
  for (const auto* text : k_land_order) {
    if (field == Signature::from_string(text)) return rank;
    ++rank;
  }
  return rank;
}

Subrecord* unique_subrecord(Record& record, Signature field) {
  return fo4::unique_subrecord(record, field, [&] { throw Refusal{LandEditStatus::malformed}; });
}

Subrecord* insertion_point(Record& record, Signature field) {
  const auto target = rank_of(field);
  return std::find_if(record.subrecords.begin(), record.subrecords.end(),
                      [&](const Subrecord& sub) { return rank_of(sub.signature) > target; });
}

void put(Record& record, Signature field, std::vector<std::byte> data) {
  if (auto* existing = unique_subrecord(record, field)) {
    existing->data = std::move(data);
    return;
  }
  Subrecord sub;
  sub.signature = field;
  sub.data = std::move(data);
  record.subrecords.insert(insertion_point(record, field), std::move(sub));
}

std::array<std::byte, 4> label_bytes(std::uint32_t value) {
  return {
      std::byte{static_cast<std::uint8_t>(value)},
      std::byte{static_cast<std::uint8_t>(value >> 8)},
      std::byte{static_cast<std::uint8_t>(value >> 16)},
      std::byte{static_cast<std::uint8_t>(value >> 24)},
  };
}

Group* find_bucket(Group& parent, GroupType type, std::uint32_t label) {
  const auto wanted = static_cast<std::int32_t>(type);
  for (auto& child : parent.children) {
    auto* group = std::get_if<Group>(&child);
    if (group == nullptr || group->header.group_type != wanted) continue;
    if (group->header.label_as_u32() == label) return group;
  }
  return nullptr;
}

Group* ensure_bucket(Group& parent, GroupType type, std::uint32_t label) {
  if (auto* existing = find_bucket(parent, type, label)) return existing;
  Group created;
  created.header.group_type = static_cast<std::int32_t>(type);
  created.header.label = label_bytes(label);
  auto position = parent.children.end();
  return &std::get<Group>(*parent.children.insert(position, std::move(created)));
}

// A CELL must be followed by its own children group, so the group is inserted, never appended.
// Precondition: a copied override can carry its source label, so the label is re-keyed to the patch id.
Group* ensure_children_group_after(Group& parent, std::size_t record_index, std::uint32_t label) {
  if (record_index + 1 < parent.children.size()) {
    if (auto* next = std::get_if<Group>(&parent.children[record_index + 1])) {
      if (next->header.group_type == static_cast<std::int32_t>(GroupType::cell_children)) {
        next->header.label = label_bytes(label);
        return next;
      }
    }
  }
  Group created;
  created.header.group_type = static_cast<std::int32_t>(GroupType::cell_children);
  created.header.label = label_bytes(label);
  auto position = parent.children.begin() + static_cast<std::ptrdiff_t>(record_index + 1);
  auto inserted = parent.children.insert(position, std::move(created));
  return &std::get<Group>(*inserted);
}

struct CellLocation {
  Group* parent{nullptr};
  std::size_t index{0};
};

std::optional<CellLocation> find_cell_in(Group& group, const FormKey& cell) {
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    if (auto* record = std::get_if<Record>(&group.children[index])) {
      if (record->header.signature == sig_cell && record->form_key == cell) {
        return CellLocation{&group, index};
      }
      continue;
    }
    if (auto found = find_cell_in(std::get<Group>(group.children[index]), cell)) return found;
  }
  return std::nullopt;
}

// Constraint: the walk matches the signature too, so a LAND that reuses a cell FormKey is never taken for the CELL.
std::optional<CellLocation> find_cell(Plugin& plugin, const FormKey& cell) {
  for (auto& group : plugin.groups) {
    if (auto found = find_cell_in(group, cell)) return found;
  }
  return std::nullopt;
}

Record* find_land_in(Group& group) {
  for (auto& child : group.children) {
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->header.signature == sig_land) return record;
      continue;
    }
    if (auto* found = find_land_in(std::get<Group>(child))) return found;
  }
  return nullptr;
}

// Constraint: LAND is a child record of the CELL, so it is found through the cell children group.
Record* find_land(Plugin& plugin, const FormKey& cell) {
  const auto location = find_cell(plugin, cell);
  if (!location.has_value() || location->index + 1 >= location->parent->children.size()) {
    return nullptr;
  }
  auto& parent = *location->parent;
  auto* children = std::get_if<Group>(&parent.children[location->index + 1]);
  if (children == nullptr ||
      children->header.group_type != static_cast<std::int32_t>(GroupType::cell_children)) {
    return nullptr;
  }
  return find_land_in(*children);
}

const Subrecord* subrecord_of(const Record& record, Signature field) {
  const Subrecord* found = nullptr;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != field) continue;
    if (found != nullptr) throw Refusal{LandEditStatus::malformed};
    found = &sub;
  }
  return found;
}

std::optional<land::LandHeightmap> heightmap_of(const Record& land) {
  const auto* vhgt = subrecord_of(land, sig_vhgt);
  if (vhgt == nullptr || vhgt->data.size() != land::k_vhgt_size) return std::nullopt;
  try {
    return land::decode_land_heightmap(vhgt->data);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::set<PluginKey> required_owners(const Plugin& source, const Record& source_land) {
  std::set<PluginKey> owners;
  if (!source.key.filename().empty()) owners.insert(source.key);
  detail::collect_record_link_owners(source, source_land, owners);
  return owners;
}

// Constraint: a copied LAND must sit in the patch CELL's temporary children, the same bucket the codec reads.
LandEditStatus copy_land_into_patch(Plugin& next,
                                    const env::Environment& environment,
                                    const FormKey& cell,
                                    const Record& source_land) {
  const auto* source_land_record = environment.cell_landscape(cell);
  if (source_land_record == nullptr) return LandEditStatus::land_missing;
  const auto& source = environment.plugin(source_land_record->plugin);
  const auto source_context = plugin_form_id_context(source);

  if (!find_cell(next, cell).has_value()) {
    const env::RecordRef* source_cell = nullptr;
    for (const auto& version : environment.versions(cell)) {
      if (version.record->header.signature == sig_cell) source_cell = &version;
    }
    if (source_cell == nullptr) return LandEditStatus::land_missing;
    if (copy_cell_as_override(next, environment.plugin(source_cell->plugin), cell) !=
        CellOverrideStatus::inserted) {
      return LandEditStatus::land_missing;
    }
    assign_record_form_keys(next);
  }

  const auto location = find_cell(next, cell);
  if (!location.has_value()) return LandEditStatus::malformed;
  auto& parent = *location->parent;
  const auto local_id = std::get<Record>(parent.children[location->index]).header.form_id.value();
  auto* children = ensure_children_group_after(parent, location->index, local_id);
  if (children == nullptr) return LandEditStatus::malformed;
  auto* bucket = ensure_bucket(*children, GroupType::cell_temporary_children, local_id);
  if (bucket == nullptr) return LandEditStatus::malformed;

  if (find_land_in(*bucket) != nullptr) return LandEditStatus::edited;

  detail::add_missing_masters(next, source, required_owners(source, source_land));
  // Constraint: adding masters rewrites the form ID context, so the copy must encode against a fresh one.
  const auto rebuilt_context = plugin_form_id_context(next);
  bucket->children.emplace_back(detail::copy_record_for_destination(
      source_land, source_land.form_key, source_context, rebuilt_context));
  return LandEditStatus::edited;
}

// Precondition: no status means the winning version is a live CELL.
std::optional<LandEditStatus> cell_status(const fo4::env::Environment& environment, const FormKey& cell) {
  const auto versions = environment.versions(cell);
  const env::RecordRef* winner = nullptr;
  for (const auto& version : versions) {
    if (version.record->header.signature == sig_cell) winner = &version;
  }
  if (winner == nullptr) {
    return versions.empty() ? LandEditStatus::cell_missing : LandEditStatus::not_a_cell;
  }
  if (winner->record->header.flags.is_deleted()) return LandEditStatus::cell_deleted;
  return std::nullopt;
}

struct NeighbourLand {
  FormKey cell;
  land::LandHeightmap heightmap;
  land::HeightGrid heights{};
  std::optional<std::vector<std::byte>> vnml;
};

using Ring = std::array<std::optional<NeighbourLand>, 9>;
using GridRing = std::array<const land::HeightGrid*, 9>;

constexpr std::size_t ring_index(int dx, int dy) {
  return static_cast<std::size_t>((dy + 1) * 3 + dx + 1);
}

std::optional<NeighbourLand> neighbour_land(Plugin& patch,
                                            const env::Environment& environment,
                                            const FormKey& world,
                                            std::int32_t x,
                                            std::int32_t y) {
  try {
    const auto key = env::find_cell_at_grid(environment, world, x, y);
    if (!key.has_value()) return std::nullopt;
    const Record* land = find_land(patch, *key);
    if (land == nullptr) {
      const auto* source = environment.cell_landscape(*key);
      if (source == nullptr || source->record->header.flags.is_deleted()) return std::nullopt;
      land = source->record;
    }
    const auto heightmap = heightmap_of(*land);
    if (!heightmap.has_value()) return std::nullopt;
    NeighbourLand out{*key, *heightmap, land::land_heights(*heightmap), std::nullopt};
    const auto* vnml = subrecord_of(*land, sig_vnml);
    if (vnml != nullptr && vnml->data.size() == land::k_normals_size) out.vnml = vnml->data;
    return out;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Constraint: the grid comes from the patch CELL when it has one, else from the winning CELL.
Ring locate_ring(Plugin& patch, const env::Environment& environment, const FormKey& cell) {
  Ring ring;
  try {
    const env::RecordRef* winner = nullptr;
    for (const auto& version : environment.versions(cell)) {
      if (version.record->header.signature == sig_cell) winner = &version;
    }
    if (winner == nullptr) throw std::runtime_error("no winning CELL");
    const auto data = env::decode_cell(environment, *winner);
    std::optional<env::CellGrid> grid = data.grid;
    if (const auto location = find_cell(patch, cell)) {
      const auto patched = cell_grid(std::get<Record>(location->parent->children[location->index]));
      if (patched.has_value()) grid = env::CellGrid{patched->first, patched->second, std::nullopt};
    }
    if (grid.has_value() && data.worldspace.has_value()) {
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) continue;
          ring[ring_index(dx, dy)] = neighbour_land(patch, environment, *data.worldspace, grid->x + dx, grid->y + dy);
        }
      }
    }
  } catch (const std::exception&) {
    ring = Ring{};
  }
  return ring;
}

const land::HeightGrid* grid_in(const GridRing& grids, int dx, int dy) {
  if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return nullptr;
  return grids[ring_index(dx, dy)];
}

land::BorderHeights borders_at(const GridRing& grids, int dx, int dy) {
  return {grid_in(grids, dx + 1, dy),     grid_in(grids, dx - 1, dy),     grid_in(grids, dx, dy + 1),     grid_in(grids, dx, dy - 1),
          grid_in(grids, dx + 1, dy + 1), grid_in(grids, dx - 1, dy + 1), grid_in(grids, dx + 1, dy - 1), grid_in(grids, dx - 1, dy - 1)};
}

// Constraint: a vertex whose triangles read no changed padded height keeps its shipped normal, so only edited terrain is recomputed.
std::vector<std::byte> rebuilt_normals(const Ring& ring,
                                       std::span<const float> heights,
                                       const land::HeightGrid& previous,
                                       const Subrecord* shipped) {
  GridRing grids{};
  for (std::size_t index = 0; index < ring.size(); ++index) {
    if (ring[index]) grids[index] = &ring[index]->heights;
  }
  const auto borders = borders_at(grids, 0, 0);
  const auto computed = land::compute_land_normals(heights, borders);
  const bool keep_shipped = shipped != nullptr && shipped->data.size() == land::k_normals_size;
  const auto changed = land::changed_normal_vertices(previous, borders, heights, borders);
  std::vector<std::byte> out(land::k_normals_size);
  for (std::size_t vertex = 0; vertex < land::k_heightmap_vertices; ++vertex) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto at = vertex * 3 + axis;
      out[at] = (changed[vertex] || !keep_shipped)
                    ? static_cast<std::byte>(static_cast<std::uint8_t>(computed[at]))
                    : shipped->data[at];
    }
  }
  return out;
}

struct NeighbourCounts {
  std::size_t updated{0};
  std::size_t skipped{0};
};

// Constraint: a neighbour reads the edited cell through the two rows and columns nearest the shared edge or corner.
bool neighbour_depends_on_change(std::span<const float> heights, const land::HeightGrid& previous, int dx, int dy) {
  constexpr std::size_t side = land::k_heightmap_side;
  const auto range = [](int d) { return d < 0 ? std::pair<std::size_t, std::size_t>{0, 1}
                                              : d > 0 ? std::pair<std::size_t, std::size_t>{side - 2, side - 1}
                                                      : std::pair<std::size_t, std::size_t>{0, side - 1}; };
  const auto [row_low, row_high] = range(dy);
  const auto [column_low, column_high] = range(dx);
  for (auto row = row_low; row <= row_high; ++row) {
    for (auto column = column_low; column <= column_high; ++column) {
      if (heights[row * side + column] != previous[row * side + column]) return true;
    }
  }
  return false;
}

struct PendingNeighbour {
  bool relevant{false};
  land::HeightGrid after{};
  std::optional<land::LandHeightmap> heightmap;
};

// Constraint: a neighbour's shared vertices take the edited heights that changed; only changed normals and VHGT bytes are written.
NeighbourCounts update_neighbours(Plugin& next,
                                  const env::Environment& environment,
                                  const Ring& ring,
                                  std::span<const float> heights,
                                  const land::HeightGrid& previous) {
  constexpr std::ptrdiff_t side = static_cast<std::ptrdiff_t>(land::k_heightmap_side);
  NeighbourCounts counts;
  land::HeightGrid edited_after{};
  std::copy(heights.begin(), heights.end(), edited_after.begin());
  std::array<PendingNeighbour, 9> pending;
  GridRing before{};
  GridRing after{};
  before[ring_index(0, 0)] = &previous;
  after[ring_index(0, 0)] = &edited_after;

  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      const auto index = ring_index(dx, dy);
      const bool depends = neighbour_depends_on_change(heights, previous, dx, dy);
      if (!ring[index]) {
        if (depends) ++counts.skipped;
        continue;
      }
      const auto& land = *ring[index];
      auto& item = pending[index];
      item.after = land.heights;
      before[index] = &land.heights;
      after[index] = &item.after;
      if (!depends) continue;
      item.relevant = true;
      bool shared_changed = false;
      for (std::ptrdiff_t row = 0; row < side; ++row) {
        for (std::ptrdiff_t column = 0; column < side; ++column) {
          const auto edited_row = row + (side - 1) * dy;
          const auto edited_column = column + (side - 1) * dx;
          if (edited_row < 0 || edited_row >= side || edited_column < 0 || edited_column >= side) continue;
          const auto from = static_cast<std::size_t>(edited_row * side + edited_column);
          if (heights[from] == previous[from]) continue;
          item.after[static_cast<std::size_t>(row * side + column)] = heights[from];
          shared_changed = true;
        }
      }
      if (shared_changed) {
        item.heightmap = land::make_land_heightmap(item.after, land.heightmap);
        if (!item.heightmap.has_value()) {
          item.after = land.heights;
          item.relevant = false;
          ++counts.skipped;
        }
      }
    }
  }

  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const auto index = ring_index(dx, dy);
      if (!pending[index].relevant) continue;
      const auto& land = *ring[index];
      const auto& item = pending[index];
      std::optional<std::vector<std::byte>> vnml;
      if (land.vnml) {
        const auto after_borders = borders_at(after, dx, dy);
        const auto changed = land::changed_normal_vertices(land.heights, borders_at(before, dx, dy), item.after, after_borders);
        const auto computed = land::compute_land_normals(item.after, after_borders);
        vnml = *land.vnml;
        for (std::size_t vertex = 0; vertex < land::k_heightmap_vertices; ++vertex) {
          if (!changed[vertex]) continue;
          for (std::size_t axis = 0; axis < 3; ++axis) {
            (*vnml)[vertex * 3 + axis] = static_cast<std::byte>(static_cast<std::uint8_t>(computed[vertex * 3 + axis]));
          }
        }
      }
      const bool vnml_differs = vnml.has_value() && *vnml != *land.vnml;
      const bool vhgt_differs = item.heightmap.has_value() && *item.heightmap != land.heightmap;
      if (!vnml_differs && !vhgt_differs) continue;

      auto trial = next;
      try {
        if (find_land(trial, land.cell) == nullptr) {
          const auto* source = environment.cell_landscape(land.cell);
          if (source == nullptr || source->record->header.flags.is_deleted() ||
              copy_land_into_patch(trial, environment, land.cell, *source->record) != LandEditStatus::edited) {
            ++counts.skipped;
            continue;
          }
          assign_record_form_keys(trial);
        }
        auto* record = find_land(trial, land.cell);
        if (record == nullptr) {
          ++counts.skipped;
          continue;
        }
        if (vnml_differs) put(*record, sig_vnml, *vnml);
        if (vhgt_differs) {
          const auto bytes = land::encode_land_heightmap(*item.heightmap);
          put(*record, sig_vhgt, std::vector<std::byte>(bytes.begin(), bytes.end()));
        }
        fo4::detail::mark_record_modified(trial, *record);
        next = std::move(trial);
        ++counts.updated;
      } catch (const Refusal&) {
        ++counts.skipped;
      } catch (const std::exception&) {
        ++counts.skipped;
      }
    }
  }
  return counts;
}

}  // namespace

namespace {

LandEditResult write_land(Plugin& patch,
                          const fo4::env::Environment& environment,
                          const FormKey& cell,
                          const land::LandHeightmap& heightmap,
                          std::span<const float> heights) {
  if (!std::isfinite(heightmap.offset)) return {LandEditStatus::invalid_value, {}};
  if (const auto status = cell_status(environment, cell)) return {*status, {}};

  auto next = patch;
  assign_record_form_keys(next);
  std::string copied_from;
  NeighbourCounts counts;

  try {
    if (find_land(next, cell) == nullptr) {
      const auto* source_land = environment.cell_landscape(cell);
      if (source_land == nullptr || source_land->record->header.flags.is_deleted()) {
        return {LandEditStatus::land_missing, {}};
      }
      copied_from = std::string(environment.plugin(source_land->plugin).key.filename());
      const auto status = copy_land_into_patch(next, environment, cell, *source_land->record);
      if (status != LandEditStatus::edited) return {status, {}};
      assign_record_form_keys(next);
    }

    auto* land = find_land(next, cell);
    if (land == nullptr) return {LandEditStatus::malformed, {}};
    const auto bytes = land::encode_land_heightmap(heightmap);
    land::HeightGrid previous{};
    Ring ring;
    if (!heights.empty()) {
      previous = land::land_heights(heightmap_of(*land).value_or(heightmap));
      ring = locate_ring(next, environment, cell);
      put(*land, sig_vnml, rebuilt_normals(ring, heights, previous, unique_subrecord(*land, sig_vnml)));
    }
    put(*land, sig_vhgt, std::vector<std::byte>(bytes.begin(), bytes.end()));
    fo4::detail::mark_record_modified(next, *land);
    if (!heights.empty()) counts = update_neighbours(next, environment, ring, heights, previous);
  } catch (const Refusal& refusal) {
    return {refusal.status, {}};
  } catch (const std::exception&) {
    return {LandEditStatus::malformed, {}};
  }

  patch = std::move(next);
  return {LandEditStatus::edited, std::move(copied_from), counts.updated, counts.skipped};
}

}  // namespace

LandEditResult edit_land_heightmap(Plugin& patch,
                                   const fo4::env::Environment& environment,
                                   const FormKey& cell,
                                   const land::LandHeightmap& heightmap) {
  return write_land(patch, environment, cell, heightmap, {});
}

LandEditResult edit_land_heights(Plugin& patch,
                                 const fo4::env::Environment& environment,
                                 const FormKey& cell,
                                 std::span<const float> heights) {
  if (heights.size() != land::k_heightmap_vertices) return {LandEditStatus::invalid_value, {}};

  if (const auto status = cell_status(environment, cell)) return {*status, {}};

  std::optional<land::LandHeightmap> original;
  try {
    if (const auto* land = find_land(patch, cell)) {
      original = heightmap_of(*land);
      if (!original.has_value()) return {LandEditStatus::malformed, {}};
    } else {
      const auto* source_land = environment.cell_landscape(cell);
      if (source_land == nullptr || source_land->record->header.flags.is_deleted()) {
        return {LandEditStatus::land_missing, {}};
      }
      original = heightmap_of(*source_land->record);
      if (!original.has_value()) return {LandEditStatus::malformed, {}};
    }
  } catch (const Refusal& refusal) {
    return {refusal.status, {}};
  }

  const auto rebuilt = land::make_land_heightmap(heights, *original);
  if (!rebuilt.has_value()) return {LandEditStatus::invalid_value, {}};
  return write_land(patch, environment, cell, *rebuilt, heights);
}

}  // namespace fo4::forms
