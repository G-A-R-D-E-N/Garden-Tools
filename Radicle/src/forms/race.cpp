#include "fo4/forms/race.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_race = Signature::from_string("RACE");
constexpr auto k_vtyp = Signature::from_string("VTYP");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_desc = Signature::from_string("DESC");
constexpr auto k_vtck = Signature::from_string("VTCK");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_unam = Signature::from_string("UNAM");
constexpr auto k_nam3 = Signature::from_string("NAM3");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr std::size_t k_current_race_data_size = 200;
constexpr std::uint16_t k_current_form_version = 131;

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw RaceError("RACE string fields cannot contain embedded NUL bytes");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) {
    data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  data.push_back(std::byte{0});
  return data;
}

std::optional<std::string> optional_zstring(const Record& record,
                                            Signature signature,
                                            std::string_view label) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw RaceError("RACE contains duplicate " + std::string(label) + " subrecords");
    }
    found = &subrecord;
  }
  if (found == nullptr) return std::nullopt;
  if (found->data.empty() || found->data.back() != std::byte{0} ||
      std::find(found->data.begin(), found->data.end() - 1, std::byte{0}) !=
          found->data.end() - 1) {
    throw RaceError(std::string(label) + " is not a single NUL-terminated string");
  }
  const auto* begin = reinterpret_cast<const char*>(found->data.data());
  return std::string(begin, begin + found->data.size() - 1);
}

std::optional<schema::LocalizedString> optional_localized(const Plugin& plugin,
                                                         const Record& record,
                                                         Signature signature) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw RaceError("RACE contains duplicate " + signature.to_string() + " subrecords");
    }
    found = &subrecord;
  }
  if (found == nullptr) return std::nullopt;
  try {
    return schema::read_localized_string(plugin, k_race, signature, *found);
  } catch (const std::exception& error) {
    throw RaceError(error.what());
  }
}

schema::LocalizedString required_localized(const Plugin& plugin,
                                           const Record& record,
                                           Signature signature) {
  auto value = optional_localized(plugin, record, signature);
  if (!value.has_value()) {
    throw RaceError("RACE is missing required " + signature.to_string());
  }
  return *value;
}


using fo4::detail::find_mutable_record;

const Subrecord* unique_subrecord(const Record& record,
                                  Signature signature,
                                  std::string_view label) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw RaceError("RACE contains duplicate " + std::string(label) + " subrecords");
    }
    found = &subrecord;
  }
  if (found == nullptr) {
    throw RaceError("RACE is missing required " + std::string(label) + " subrecord");
  }
  return found;
}

void write_u32(std::vector<std::byte>& data,
               std::size_t offset,
               std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::uint32_t read_u32(const std::vector<std::byte>& data,
                       std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_i32(std::vector<std::byte>& data,
               std::size_t offset,
               std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::int32_t read_i32(const std::vector<std::byte>& data,
                      std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_i16(std::vector<std::byte>& data,
               std::size_t offset,
               std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::int16_t read_i16(const std::vector<std::byte>& data,
                      std::size_t offset) {
  std::int16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_f32(std::vector<std::byte>& data,
               std::size_t offset,
               float value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

float read_f32(const std::vector<std::byte>& data,
               std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void require_finite(float value, std::string_view field) {
  if (!std::isfinite(value)) {
    throw RaceError("RACE " + std::string(field) + " must be finite");
  }
}

void validate_creation(const Plugin& plugin, const RaceCreation& creation) {
  require_finite(creation.male_height, "male height");
  require_finite(creation.female_height, "female height");
  for (const auto value : creation.male_default_weight) {
    require_finite(value, "male default weight");
  }
  for (const auto value : creation.female_default_weight) {
    require_finite(value, "female default weight");
  }
  require_finite(creation.acceleration_rate, "acceleration rate");
  require_finite(creation.deceleration_rate, "deceleration rate");
  require_finite(creation.injured_health_pct, "injured health percentage");
  require_finite(creation.aim_angle_tolerance, "aim angle tolerance");
  require_finite(creation.flight_radius, "flight radius");
  require_finite(creation.angular_acceleration_rate, "angular acceleration rate");
  require_finite(creation.angular_tolerance, "angular tolerance");
  require_finite(creation.facegen_main_clamp, "FaceGen main clamp");
  require_finite(creation.facegen_face_clamp, "FaceGen face clamp");
  if (creation.size > 3) {
    throw RaceError("RACE size must be 0..3");
  }

  for (const auto* voice : {&creation.male_voice, &creation.female_voice}) {
    if (!voice->has_value() || (*voice)->owner() != plugin.key) continue;
    const auto* target = find_record(plugin, **voice);
    if (target == nullptr || target->header.signature != k_vtyp) {
      throw RaceError(
          "local RACE voice FormKey must resolve to an existing VTYP record: " +
          (*voice)->to_string());
    }
  }
}

Subrecord encode_current_data(const Plugin& plugin, const RaceCreation& creation) {
  validate_creation(plugin, creation);

  Subrecord data;
  data.signature = sig_data;
  data.data.resize(k_current_race_data_size);

  write_f32(data.data, 0, creation.male_height);
  write_f32(data.data, 4, creation.female_height);
  write_f32(data.data, 8, creation.male_default_weight[0]);
  write_f32(data.data, 12, creation.male_default_weight[1]);
  write_f32(data.data, 16, creation.male_default_weight[2]);
  write_f32(data.data, 20, creation.female_default_weight[0]);
  write_f32(data.data, 24, creation.female_default_weight[1]);
  write_f32(data.data, 28, creation.female_default_weight[2]);
  write_u32(data.data, 32, creation.flags);
  write_f32(data.data, 36, creation.acceleration_rate);
  write_f32(data.data, 40, creation.deceleration_rate);
  write_u32(data.data, 44, creation.size);
  // 48..55: two four-byte unknown fields, preserved as zero for new records.
  write_f32(data.data, 56, creation.injured_health_pct);
  write_i32(data.data, 60, creation.shield_biped_object);
  write_i32(data.data, 64, creation.beard_biped_object);
  write_i32(data.data, 68, creation.body_biped_object);
  write_f32(data.data, 72, creation.aim_angle_tolerance);
  write_f32(data.data, 76, creation.flight_radius);
  write_f32(data.data, 80, creation.angular_acceleration_rate);
  write_f32(data.data, 84, creation.angular_tolerance);
  write_u32(data.data, 88, creation.flags2);
  // 92..127: current-format unknown float/byte fields, zero on new records.
  write_i32(data.data, 128, creation.pipboy_biped_object);
  write_i16(data.data, 132, creation.xp_value);
  // 134..199: debris, impact, OnCripple, subsegment and orientation fields.
  // Their FormIDs/counts/scales are all valid null/zero defaults for the
  // minimal #65 creator and will be modeled separately before editable use.

  return data;
}

Subrecord encode_voices(const Plugin& plugin, const RaceCreation& creation) {
  Subrecord voices;
  voices.signature = k_vtck;
  voices.data.resize(8);
  const auto context = plugin_form_id_context(plugin);
  if (creation.male_voice.has_value()) {
    write_u32(voices.data, 0,
              encode_plugin_form_id(*creation.male_voice, context).value());
  }
  if (creation.female_voice.has_value()) {
    write_u32(voices.data, 4,
              encode_plugin_form_id(*creation.female_voice, context).value());
  }
  return voices;
}

Subrecord float_subrecord(Signature signature, float value) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.resize(4);
  write_f32(subrecord.data, 0, value);
  return subrecord;
}

Subrecord empty_marker(Signature signature) {
  Subrecord subrecord;
  subrecord.signature = signature;
  return subrecord;
}

std::optional<FormKey> decode_voice(const PluginFormIdContext& context,
                                    const std::vector<std::byte>& data,
                                    std::size_t offset) {
  const auto raw = RawFormId{read_u32(data, offset)};
  if (raw.is_null()) return std::nullopt;
  return resolve_plugin_form_id(raw, context);
}

void require_authoring_preflight(const Plugin& plugin, const FormKey& key) {
  for (const auto& issue : schema::preflight_authoring_record(plugin, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw RaceError("RACE authoring preflight failed: " + issue.message);
    }
  }
}

}  // namespace

Race decode_race(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) throw RaceError("RACE record not found: " + key.to_string());
  if (record->header.signature != k_race) {
    throw RaceError("record is not RACE: " + key.to_string());
  }
  if (record->header.form_version != k_current_form_version) {
    throw RaceError("RACE typed decode currently supports form version 131 only");
  }

  Race result;
  result.key = key;
  result.editor_id = optional_zstring(*record, sig_edid, "EDID");
  result.full_name = optional_localized(plugin, *record, k_full);
  result.description = required_localized(plugin, *record, k_desc);

  const auto* data = unique_subrecord(*record, sig_data, "DATA");
  if (data->data.size() != k_current_race_data_size) {
    throw RaceError("form-version 131 RACE DATA must be exactly 200 bytes");
  }
  result.male_height = read_f32(data->data, 0);
  result.female_height = read_f32(data->data, 4);
  result.male_default_weight = {
      read_f32(data->data, 8), read_f32(data->data, 12), read_f32(data->data, 16)};
  result.female_default_weight = {
      read_f32(data->data, 20), read_f32(data->data, 24), read_f32(data->data, 28)};
  result.flags = read_u32(data->data, 32);
  result.acceleration_rate = read_f32(data->data, 36);
  result.deceleration_rate = read_f32(data->data, 40);
  result.size = read_u32(data->data, 44);
  result.injured_health_pct = read_f32(data->data, 56);
  result.shield_biped_object = read_i32(data->data, 60);
  result.beard_biped_object = read_i32(data->data, 64);
  result.body_biped_object = read_i32(data->data, 68);
  result.aim_angle_tolerance = read_f32(data->data, 72);
  result.flight_radius = read_f32(data->data, 76);
  result.angular_acceleration_rate = read_f32(data->data, 80);
  result.angular_tolerance = read_f32(data->data, 84);
  result.flags2 = read_u32(data->data, 88);
  result.pipboy_biped_object = read_i32(data->data, 128);
  result.xp_value = read_i16(data->data, 132);

  const auto* voices = unique_subrecord(*record, k_vtck, "VTCK");
  if (voices->data.size() != 8) {
    throw RaceError("RACE VTCK must contain exactly male and female FormIDs");
  }
  const auto context = plugin_form_id_context(plugin);
  result.male_voice = decode_voice(context, voices->data, 0);
  result.female_voice = decode_voice(context, voices->data, 4);

  const auto* main_clamp = unique_subrecord(*record, k_pnam, "PNAM");
  const auto* face_clamp = unique_subrecord(*record, k_unam, "UNAM");
  if (main_clamp->data.size() != 4 || face_clamp->data.size() != 4) {
    throw RaceError("RACE PNAM and UNAM FaceGen clamps must each be four bytes");
  }
  result.facegen_main_clamp = read_f32(main_clamp->data, 0);
  result.facegen_face_clamp = read_f32(face_clamp->data, 0);
  result.body_data = decode_race_body_data(plugin, key);

  const auto* behavior_marker = unique_subrecord(*record, k_nam3, "NAM3");
  if (!behavior_marker->data.empty()) {
    throw RaceError("RACE required NAM3 behavior marker must be empty");
  }
  return result;
}

FormKey create_race(Plugin& plugin, const RaceCreation& creation) {
  if (plugin.header.form_version != k_current_form_version) {
    throw RaceError("RACE creation currently supports Fallout 4 form version 131 only");
  }
  if (plugin.header.tes4_flags.is_localized()) {
    throw RaceError(
        "RACE creation in localized plugins is disabled until required DESC string-table authoring is implemented");
  }

  const auto editor = creation.editor_id.has_value()
      ? std::optional<std::vector<std::byte>>{zstring(stored_editor_id(*creation.editor_id))}
      : std::nullopt;
  const auto full = creation.full_name.has_value()
      ? std::optional<std::vector<std::byte>>{zstring(stored_inline_text(*creation.full_name, "full name"))}
      : std::nullopt;
  const auto desc = zstring(stored_inline_text(creation.description, "description"));
  const auto data = encode_current_data(plugin, creation);
  const auto voices = encode_voices(plugin, creation);
  const auto main_clamp = float_subrecord(k_pnam, creation.facegen_main_clamp);
  const auto face_clamp = float_subrecord(k_unam, creation.facegen_face_clamp);

  auto next = plugin;
  const auto key = create_local_record(next, k_race);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw RaceError("newly allocated RACE record could not be resolved");
  }

  if (editor.has_value()) {
    record->subrecords.push_back(Subrecord{sig_edid, *editor, false});
  }
  if (full.has_value()) {
    record->subrecords.push_back(Subrecord{k_full, *full, false});
  }
  record->subrecords.push_back(Subrecord{k_desc, desc, false});
  record->subrecords.push_back(data);
  record->subrecords.push_back(voices);
  record->subrecords.push_back(main_clamp);
  record->subrecords.push_back(face_clamp);

  if (!set_race_body_data(next, key, creation.body_data.male, creation.body_data.female)) {
    throw RaceError("newly allocated RACE record could not accept BodyData");
  }

  record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw RaceError("newly allocated RACE record disappeared after BodyData creation");
  }
  // xEdit marks NAM3 and both behavior-graph containers required. The male
  // marker itself is optional, but emitting both empty gender markers gives
  // the new record an explicit, deterministic empty behavior-graph shape.
  record->subrecords.push_back(empty_marker(k_nam3));
  record->subrecords.push_back(empty_marker(k_mnam));
  record->subrecords.push_back(empty_marker(k_fnam));

  require_authoring_preflight(next, key);
  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
