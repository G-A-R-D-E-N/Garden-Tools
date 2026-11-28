#include "fo4/forms/world_edit.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

// Constraint: vanilla WRLD order, 23 records in the shipped masters; LTMP, WCTR and XEZN placement is unproven.
constexpr std::initializer_list<const char*> k_world_order = {
    "EDID", "RNAM", "MHDT", "FULL", "WCTR", "LTMP", "XEZN", "XLCN", "WNAM", "PNAM", "CNAM",
    "NAM2", "NAM3", "NAM4", "DNAM", "ICON", "MNAM", "ONAM", "NAMA", "DATA", "NAM0", "NAM9",
    "ZNAM", "XWEM", "WLEV", "OFST", "CLSZ"};

struct Refusal {
  WorldEditStatus status;
};

std::optional<std::size_t> rank_of(Signature field) {
  std::size_t rank = 0;
  for (const auto* text : k_world_order) {
    if (field == Signature::from_string(text)) return rank;
    ++rank;
  }
  return std::nullopt;
}

Subrecord* unique_subrecord(Record& record, Signature field) {
  return fo4::unique_subrecord(record, field, [&] { throw Refusal{WorldEditStatus::malformed}; });
}

// Constraint: a new field goes after the last present field of lower rank; unranked fields never move it.
Subrecord* insertion_point(Record& record, Signature field) {
  const auto target = rank_of(field);
  Subrecord* at = record.subrecords.begin();
  if (!target) return record.subrecords.end();
  for (auto* sub = record.subrecords.begin(); sub != record.subrecords.end(); ++sub) {
    const auto rank = rank_of(sub->signature);
    if (rank && *rank < *target) at = sub + 1;
  }
  return at;
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

template <class T>
std::vector<std::byte> bytes_of(T value) {
  std::vector<std::byte> data(sizeof(T));
  std::memcpy(data.data(), &value, sizeof(T));
  return data;
}

void store(std::vector<std::byte>& data, std::size_t offset, float value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

float require_finite(float value) {
  if (!std::isfinite(value)) throw Refusal{WorldEditStatus::invalid_value};
  return value;
}

bool is_null(const FormKey& key) {
  return key.owner().empty() && key.local().value() == 0;
}

std::uint32_t link_raw(const Plugin& plugin,
                       const fo4::env::Environment& env,
                       const FormKey& key,
                       Signature allowed) {
  if (key.owner().empty()) throw Refusal{WorldEditStatus::target_missing};
  const auto& owner = key.owner();
  const bool known = owner == plugin.key ||
                     std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                                 [&](const auto& master) { return master.key == owner; });
  if (!known) throw Refusal{WorldEditStatus::target_not_master};
  const Record* target = find_record(plugin, key);
  if (target == nullptr) {
    const auto* winner = env.winner(key);
    if (winner == nullptr) throw Refusal{WorldEditStatus::target_missing};
    target = winner->record;
  }
  if (target->header.signature != allowed) throw Refusal{WorldEditStatus::target_type_not_allowed};
  return encode_plugin_form_id(key, plugin_form_id_context(plugin)).value();
}

void store_link(const Plugin& plugin,
                const fo4::env::Environment& env,
                Record& record,
                Signature field,
                const std::optional<FormKey>& key,
                Signature allowed) {
  if (!key || is_null(*key)) {
    erase_subrecords(record, field);
    return;
  }
  put(record, field, bytes_of(link_raw(plugin, env, *key, allowed)));
}

void store_parent(const Plugin& plugin,
                  const fo4::env::Environment& env,
                  Record& record,
                  const std::optional<FormKey>& parent,
                  std::optional<std::uint16_t> parent_flags) {
  store_link(plugin, env, record, sig("WNAM"), parent, sig("WRLD"));
  const bool linked = parent.has_value() && !is_null(*parent);
  if (parent_flags) {
    if (!linked) throw Refusal{WorldEditStatus::invalid_value};
    put(record, sig("PNAM"), bytes_of(*parent_flags));
    return;
  }
  if (!linked) erase_subrecords(record, sig("PNAM"));
}

void store_heights(Record& record, std::optional<float> land, std::optional<float> water) {
  if (!land && !water) return;
  auto* existing = unique_subrecord(record, sig("DNAM"));
  std::vector<std::byte> data(8, std::byte{0});
  if (existing != nullptr) {
    if (existing->data.size() != data.size()) throw Refusal{WorldEditStatus::malformed};
    data = existing->data;
  }
  if (land) store(data, 0, require_finite(*land));
  if (water) store(data, 4, require_finite(*water));
  put(record, sig("DNAM"), std::move(data));
}

void store_point(Record& record, Signature field, const std::array<float, 2>& point) {
  std::vector<std::byte> data(sizeof(float) * point.size());
  for (std::size_t index = 0; index < point.size(); ++index) {
    store(data, index * sizeof(float), require_finite(point[index]));
  }
  put(record, field, std::move(data));
}

void apply_edit(Record& record, const Plugin& plugin, const fo4::env::Environment& env,
                const WorldEdit& edit) {
  if (edit.flags) put(record, sig("DATA"), std::vector<std::byte>{std::byte{*edit.flags}});
  if (edit.parent) {
    store_parent(plugin, env, record, *edit.parent, edit.parent_flags);
  } else if (edit.parent_flags) {
    if (unique_subrecord(record, sig("WNAM")) == nullptr) throw Refusal{WorldEditStatus::invalid_value};
    put(record, sig("PNAM"), bytes_of(*edit.parent_flags));
  }
  if (edit.climate) store_link(plugin, env, record, sig("CNAM"), *edit.climate, sig("CLMT"));
  if (edit.water) store_link(plugin, env, record, sig("NAM2"), *edit.water, sig("WATR"));
  if (edit.lighting_template) {
    store_link(plugin, env, record, sig("LTMP"), *edit.lighting_template, sig("LGTM"));
  }
  store_heights(record, edit.default_land_height, edit.default_water_height);
  if (edit.bounds_min) store_point(record, sig("NAM0"), *edit.bounds_min);
  if (edit.bounds_max) store_point(record, sig("NAM9"), *edit.bounds_max);
  if (edit.lod_water) store_link(plugin, env, record, sig("NAM3"), *edit.lod_water, sig("WATR"));
  if (edit.lod_water_height) put(record, sig("NAM4"), bytes_of(require_finite(*edit.lod_water_height)));
}

std::set<PluginKey> required_owners(const Plugin& source, const Record& source_world) {
  std::set<PluginKey> owners;
  if (!source.key.filename().empty()) owners.insert(source.key);
  detail::collect_record_link_owners(source, source_world, owners);
  return owners;
}

WorldEditStatus copy_world_into_patch(Plugin& next,
                                      const Plugin& source,
                                      const FormKey& world,
                                      const Record& source_world) {
  const auto source_context = plugin_form_id_context(source);
  const auto located = detail::locate_record(source, world);
  if (located.record == nullptr) return WorldEditStatus::world_missing;

  detail::add_missing_masters(next, source, required_owners(source, source_world));
  // Constraint: adding masters rewrites the form ID context, so the copy must encode against a fresh one.
  const auto destination_context = plugin_form_id_context(next);
  auto* parent = detail::ensure_group_path(next, located.path, source_context, destination_context);
  parent->children.emplace_back(detail::copy_record_for_destination(
      source_world, world, source_context, destination_context));
  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  return WorldEditStatus::edited;
}

}  // namespace

WorldEditResult edit_world(Plugin& patch,
                           const fo4::env::Environment& env,
                           const FormKey& world,
                           const WorldEdit& edit) {
  auto next = patch;
  assign_record_form_keys(next);
  std::string copied_from;
  if (find_record(next, world) == nullptr) {
    const auto* winner = env.winner(world);
    if (winner == nullptr) return {WorldEditStatus::world_missing, {}};
    if (winner->record->header.flags.is_deleted()) return {WorldEditStatus::world_deleted, {}};
    if (winner->record->header.signature != sig_wrld) return {WorldEditStatus::not_a_world, {}};
    const auto status = copy_world_into_patch(next, env.plugin(winner->plugin), world, *winner->record);
    if (status != WorldEditStatus::edited) return {status, {}};
    copied_from = std::string(env.plugin(winner->plugin).key.filename());
  }

  Record* record = nullptr;
  try {
    record = fo4::detail::locate_mutable_record(next, world);
  } catch (const std::exception&) {
    return {WorldEditStatus::malformed, {}};
  }
  if (record == nullptr || record->header.signature != sig_wrld) {
    return {WorldEditStatus::not_a_world, {}};
  }
  if (record->header.flags.is_deleted()) return {WorldEditStatus::world_deleted, {}};
  try {
    apply_edit(*record, next, env, edit);
  } catch (const Refusal& refusal) {
    return {refusal.status, {}};
  } catch (const FormIdError&) {
    return {WorldEditStatus::invalid_value, {}};
  } catch (const std::exception&) {
    return {WorldEditStatus::malformed, {}};
  }
  fo4::detail::mark_record_modified(next, *record);
  patch = std::move(next);
  return {WorldEditStatus::edited, std::move(copied_from)};
}

std::vector<WorldPropertyDescriptor> world_property_descriptors() {
  using Kind = WorldPropertyKind;
  constexpr double float_min = -std::numeric_limits<float>::max();
  constexpr double float_max = std::numeric_limits<float>::max();
  return {
      {"Parent", "Parent worldspace", Kind::link, 0, 0, {"WRLD"}},
      {"ParentFlags", "Parent flags", Kind::flags, 0, 65535, {}},
      {"Climate", "Climate", Kind::link, 0, 0, {"CLMT"}},
      {"Water", "Water", Kind::link, 0, 0, {"WATR"}},
      {"LightingTemplate", "Lighting template", Kind::link, 0, 0, {"LGTM"}},
      {"DefaultLandHeight", "Default land height", Kind::scalar, float_min, float_max, {}},
      {"DefaultWaterHeight", "Default water height", Kind::scalar, float_min, float_max, {}},
      {"BoundsMin", "Bounds min", Kind::scalar, float_min, float_max, {}},
      {"BoundsMax", "Bounds max", Kind::scalar, float_min, float_max, {}},
      {"Flags", "Flags", Kind::flags, 0, 255, {}},
      {"LodWater", "LOD water", Kind::link, 0, 0, {"WATR"}},
      {"LodWaterHeight", "LOD water height", Kind::scalar, float_min, float_max, {}},
  };
}

}  // namespace fo4::forms
