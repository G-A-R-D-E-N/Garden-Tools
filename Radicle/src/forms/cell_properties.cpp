#include "fo4/forms/cell_edit.hpp"

#include "../plugin/record_edit.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

struct Refusal {
  CellEditStatus status;
};

constexpr Signature sig(const char (&text)[5]) {
  return Signature::from_string(text);
}

constexpr std::uint16_t k_ambient_specular_form_version = 30;
constexpr std::size_t k_lighting_base_size = 40;
constexpr std::size_t k_owner_size = 12;

constexpr std::initializer_list<const char*> k_cell_order = {
    "EDID", "FULL", "DATA", "VISI", "RVIS", "PCMB", "XCLC", "XCLL", "CNAM", "ZNAM", "TVDT", "MHDT", "LTMP",
    "XCLW", "XCLR", "XLCN", "XWCN", "XWCU", "XCWT", "XOWN", "XRNK", "XILL", "XILW", "XWEM", "XCCM", "XCAS",
    "XEZN", "XCMO", "XCIM", "XGDR", "XPRI", "XCRI"};

std::size_t rank_of(Signature field) {
  std::size_t rank = 0;
  for (const auto* text : k_cell_order) {
    if (field == Signature::from_string(text)) return rank;
    ++rank;
  }
  return rank;
}

std::vector<Subrecord>::iterator insertion_point(Record& record, Signature field) {
  const auto target = rank_of(field);
  return std::find_if(record.subrecords.begin(), record.subrecords.end(),
                      [&](const Subrecord& sub) { return rank_of(sub.signature) > target; });
}

Subrecord* unique_subrecord(Record& record, Signature field) {
  Subrecord* found = nullptr;
  for (auto& sub : record.subrecords) {
    if (sub.signature != field) continue;
    if (found != nullptr) throw Refusal{CellEditStatus::malformed};
    found = &sub;
  }
  return found;
}

void erase_all(Record& record, Signature field) {
  std::erase_if(record.subrecords, [&](const Subrecord& sub) { return sub.signature == field; });
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

template <class T>
void store(std::vector<std::byte>& data, std::size_t offset, T value) {
  std::memcpy(data.data() + offset, &value, sizeof(T));
}

void store_color(std::vector<std::byte>& data, std::size_t offset, const std::array<std::uint8_t, 3>& color) {
  for (std::size_t channel = 0; channel < color.size(); ++channel) {
    data[offset + channel] = std::byte{color[channel]};
  }
}

float require_finite(float value) {
  if (!std::isfinite(value)) throw Refusal{CellEditStatus::invalid_value};
  return value;
}

bool is_null(const FormKey& key) {
  return key.owner().empty() && key.local().value() == 0;
}

std::uint32_t link_raw(const Plugin& plugin,
                       const fo4::env::Environment& env,
                       const FormKey& key,
                       std::initializer_list<Signature> allowed) {
  if (key.owner().empty()) throw Refusal{CellEditStatus::target_missing};
  const auto& owner = key.owner();
  const bool known = owner == plugin.key ||
                     std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                                 [&](const auto& master) { return master.key == owner; });
  if (!known) throw Refusal{CellEditStatus::target_not_master};
  const Record* target = find_record(plugin, key);
  if (target == nullptr) {
    const auto* winner = env.winner(key);
    if (winner == nullptr) throw Refusal{CellEditStatus::target_missing};
    target = winner->record;
  }
  if (std::find(allowed.begin(), allowed.end(), target->header.signature) == allowed.end()) {
    throw Refusal{CellEditStatus::target_type_not_allowed};
  }
  return encode_plugin_form_id(key, plugin_form_id_context(plugin)).value();
}

void store_link(const Plugin& plugin,
                const fo4::env::Environment& env,
                Record& record,
                Signature field,
                const std::optional<FormKey>& key,
                std::initializer_list<Signature> allowed) {
  if (!key || is_null(*key)) {
    erase_all(record, field);
    return;
  }
  put(record, field, bytes_of(link_raw(plugin, env, *key, allowed)));
}

void store_owner(const Plugin& plugin,
                 const fo4::env::Environment& env,
                 Record& record,
                 const std::optional<FormKey>& owner) {
  if (!owner || is_null(*owner)) {
    erase_all(record, sig("XOWN"));
    return;
  }
  const auto raw = link_raw(plugin, env, *owner, {sig("FACT"), sig("NPC_")});
  auto* existing = unique_subrecord(record, sig("XOWN"));
  auto data = existing ? existing->data : std::vector<std::byte>(k_owner_size, std::byte{0});
  if (data.size() != k_owner_size) throw Refusal{CellEditStatus::malformed};
  store(data, 0, raw);
  put(record, sig("XOWN"), std::move(data));
}

void store_regions(const Plugin& plugin,
                   const fo4::env::Environment& env,
                   Record& record,
                   const std::vector<FormKey>& regions) {
  if (regions.empty()) {
    erase_all(record, sig("XCLR"));
    return;
  }
  std::vector<std::byte> data;
  for (const auto& region : regions) {
    const auto encoded = bytes_of(link_raw(plugin, env, region, {sig("REGN")}));
    data.insert(data.end(), encoded.begin(), encoded.end());
  }
  put(record, sig("XCLR"), std::move(data));
}

void store_flags(Record& record, std::uint16_t flags) {
  const auto* data = unique_subrecord(record, sig("DATA"));
  if (data != nullptr && (data->data.empty() || data->data.size() > 2)) {
    throw Refusal{CellEditStatus::malformed};
  }
  const bool interior_now =
      data != nullptr && (std::to_integer<std::uint8_t>(data->data[0]) & 1u) != 0;
  if (interior_now != ((flags & 1u) != 0)) throw Refusal{CellEditStatus::interior_flag_locked};
  put(record, sig("DATA"), bytes_of(flags));
}

void store_lighting(Record& record, const CellLightingEdit& edit) {
  const std::size_t ambient_size =
      record.header.form_version >= k_ambient_specular_form_version ? 32 : 24;
  const std::size_t required = k_lighting_base_size + ambient_size;
  auto* existing = unique_subrecord(record, sig("XCLL"));
  auto data = existing ? existing->data : std::vector<std::byte>(required, std::byte{0});
  if (data.size() < required) throw Refusal{CellEditStatus::malformed};
  if (edit.ambient) store_color(data, 0, *edit.ambient);
  if (edit.directional) store_color(data, 4, *edit.directional);
  if (edit.fog_near_color) store_color(data, 8, *edit.fog_near_color);
  if (edit.fog_near) store(data, 12, require_finite(*edit.fog_near));
  if (edit.fog_far) store(data, 16, require_finite(*edit.fog_far));
  if (edit.directional_rotation_xy) store(data, 20, *edit.directional_rotation_xy);
  if (edit.directional_rotation_z) store(data, 24, *edit.directional_rotation_z);
  if (edit.directional_fade) store(data, 28, require_finite(*edit.directional_fade));
  if (edit.fog_clip_distance) store(data, 32, require_finite(*edit.fog_clip_distance));
  if (edit.fog_power) store(data, 36, require_finite(*edit.fog_power));
  put(record, sig("XCLL"), std::move(data));
}

void apply_edit(const Plugin& plugin, const fo4::env::Environment& env, Record& record, const CellEdit& edit) {
  if (edit.flags) store_flags(record, *edit.flags);
  if (edit.lighting) store_lighting(record, *edit.lighting);
  if (edit.lighting_template) {
    store_link(plugin, env, record, sig("LTMP"), *edit.lighting_template, {sig("LGTM")});
  }
  if (edit.water_height) put(record, sig("XCLW"), bytes_of(require_finite(*edit.water_height)));
  if (edit.regions) store_regions(plugin, env, record, *edit.regions);
  if (edit.water) store_link(plugin, env, record, sig("XCWT"), *edit.water, {sig("WATR")});
  if (edit.location) store_link(plugin, env, record, sig("XLCN"), *edit.location, {sig("LCTN")});
  if (edit.image_space) store_link(plugin, env, record, sig("XCIM"), *edit.image_space, {sig("IMGS")});
  if (edit.encounter_zone) {
    store_link(plugin, env, record, sig("XEZN"), *edit.encounter_zone, {sig("ECZN")});
  }
  if (edit.acoustic_space) {
    store_link(plugin, env, record, sig("XCAS"), *edit.acoustic_space, {sig("ASPC")});
  }
  if (edit.music_type) store_link(plugin, env, record, sig("XCMO"), *edit.music_type, {sig("MUSC")});
  if (edit.sky_weather_region) {
    store_link(plugin, env, record, sig("XCCM"), *edit.sky_weather_region, {sig("REGN")});
  }
  if (edit.god_rays) store_link(plugin, env, record, sig("XGDR"), *edit.god_rays, {sig("GDRY")});
  if (edit.lock_list) {
    store_link(plugin, env, record, sig("XILL"), *edit.lock_list, {sig("FLST"), sig("NPC_")});
  }
  if (edit.owner) store_owner(plugin, env, record, *edit.owner);
  if (edit.owner_rank) put(record, sig("XRNK"), bytes_of(*edit.owner_rank));
}

}  // namespace

CellEditResult edit_cell(Plugin& patch,
                         const fo4::env::Environment& env,
                         const FormKey& cell,
                         const CellEdit& edit) {
  auto next = patch;
  std::string copied_from;
  if (find_record(next, cell) == nullptr) {
    const auto* winner = env.winner(cell);
    if (winner == nullptr) return {CellEditStatus::cell_missing, {}};
    if (winner->record->header.flags.is_deleted()) return {CellEditStatus::cell_deleted, {}};
    if (winner->record->header.signature != sig("CELL")) return {CellEditStatus::not_a_cell, {}};
    const auto& source = env.plugin(winner->plugin);
    if (copy_cell_as_override(next, source, cell) != CellOverrideStatus::inserted) {
      return {CellEditStatus::cell_missing, {}};
    }
    copied_from = std::string(source.key.filename());
  }

  auto* record = detail::locate_mutable_record(next, cell);
  if (record == nullptr || record->header.signature != sig("CELL")) {
    return {CellEditStatus::not_a_cell, {}};
  }
  try {
    apply_edit(next, env, *record, edit);
  } catch (const Refusal& refusal) {
    return {refusal.status, {}};
  }
  detail::mark_record_modified(next, *record);
  patch = std::move(next);
  return {CellEditStatus::edited, copied_from};
}

std::vector<CellPropertyDescriptor> cell_property_descriptors() {
  using Kind = CellPropertyKind;
  constexpr double float_min = -std::numeric_limits<float>::max();
  constexpr double float_max = std::numeric_limits<float>::max();
  constexpr double int_min = std::numeric_limits<std::int32_t>::min();
  constexpr double int_max = std::numeric_limits<std::int32_t>::max();
  return {
      {"Flags", "Flags", Kind::flags, 0, 65535, false, {}},
      {"LightingAmbient", "Ambient color", Kind::colour, 0, 255, false, {}},
      {"LightingDirectional", "Directional color", Kind::colour, 0, 255, false, {}},
      {"LightingFogNearColor", "Fog color near", Kind::colour, 0, 255, false, {}},
      {"LightingFogNear", "Fog near", Kind::scalar, float_min, float_max, false, {}},
      {"LightingFogFar", "Fog far", Kind::scalar, float_min, float_max, false, {}},
      {"LightingDirectionalRotationXY", "Directional rotation XY", Kind::scalar, int_min, int_max, false, {}},
      {"LightingDirectionalRotationZ", "Directional rotation Z", Kind::scalar, int_min, int_max, false, {}},
      {"LightingDirectionalFade", "Directional fade", Kind::scalar, float_min, float_max, false, {}},
      {"LightingFogClipDistance", "Fog clip distance", Kind::scalar, float_min, float_max, false, {}},
      {"LightingFogPower", "Fog power", Kind::scalar, float_min, float_max, false, {}},
      {"LightingTemplate", "Lighting template", Kind::link, 0, 0, false, {"LGTM"}},
      {"WaterHeight", "Water height", Kind::scalar, float_min, float_max, false, {}},
      {"Water", "Water", Kind::link, 0, 0, false, {"WATR"}},
      {"ImageSpace", "Image space", Kind::link, 0, 0, false, {"IMGS"}},
      {"EncounterZone", "Encounter zone", Kind::link, 0, 0, false, {"ECZN"}},
      {"Location", "Location", Kind::link, 0, 0, false, {"LCTN"}},
      {"AcousticSpace", "Acoustic space", Kind::link, 0, 0, false, {"ASPC"}},
      {"MusicType", "Music type", Kind::link, 0, 0, false, {"MUSC"}},
      {"SkyWeatherRegion", "Sky/weather from region", Kind::link, 0, 0, false, {"REGN"}},
      {"GodRays", "God rays", Kind::link, 0, 0, false, {"GDRY"}},
      {"LockList", "Lock list", Kind::link, 0, 0, false, {"FLST", "NPC_"}},
      {"Owner", "Owner", Kind::link, 0, 0, false, {"FACT", "NPC_"}},
      {"OwnerRank", "Faction rank", Kind::scalar, int_min, int_max, false, {}},
      {"Regions", "Regions", Kind::link, 0, 0, true, {"REGN"}},
  };
}

}  // namespace fo4::forms
