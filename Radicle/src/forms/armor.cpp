#include "fo4/forms/armor.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_armo = Signature::from_string("ARMO");
constexpr auto k_desc = Signature::from_string("DESC");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_obte = Signature::from_string("OBTE");
constexpr auto k_arma = Signature::from_string("ARMA");
constexpr auto k_mod2 = Signature::from_string("MOD2");
constexpr auto k_mod3 = Signature::from_string("MOD3");
constexpr auto k_mod4 = Signature::from_string("MOD4");
constexpr auto k_mod5 = Signature::from_string("MOD5");
constexpr auto k_icon = Signature::from_string("ICON");
constexpr auto k_mico = Signature::from_string("MICO");
constexpr auto k_ico2 = Signature::from_string("ICO2");
constexpr auto k_mic2 = Signature::from_string("MIC2");
constexpr auto k_dnam = Signature::from_string("DNAM");

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw ArmorError("ARMO/ARMA string fields cannot contain embedded NUL bytes");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) {
    data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  data.push_back(std::byte{0});
  return data;
}

std::string decode_zstring(const Subrecord& subrecord,
                           std::string_view field_name) {
  if (subrecord.data.empty() || subrecord.data.back() != std::byte{0}) {
    throw ArmorError(std::string(field_name) +
                     " must be a single NUL-terminated string");
  }
  const auto embedded =
      std::find(subrecord.data.begin(), subrecord.data.end() - 1, std::byte{0});
  if (embedded != subrecord.data.end() - 1) {
    throw ArmorError(std::string(field_name) +
                     " contains an embedded NUL byte");
  }
  const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
  return std::string(begin, begin + subrecord.data.size() - 1);
}

const Subrecord* unique_subrecord(const Record& record,
                                  Signature signature,
                                  std::string_view field_name,
                                  bool required) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw ArmorError(std::string(field_name) + " appears more than once");
    }
    found = &subrecord;
  }
  if (required && found == nullptr) {
    throw ArmorError(std::string(field_name) + " is required");
  }
  return found;
}

// The template block starts at OBTE and emits its own FULL per combination,
// so the record's own name is the FULL before that boundary.
std::size_t template_boundary(const Record& record) {
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == k_obte) return i;
  }
  return record.subrecords.size();
}

std::optional<schema::LocalizedString> optional_localized_before(const Plugin& plugin,
                                                                const Record& record,
                                                                Signature record_signature,
                                                                Signature signature,
                                                                std::size_t boundary,
                                                                std::string_view field_name) {
  const Subrecord* found = nullptr;
  for (std::size_t i = 0; i < boundary && i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != signature) continue;
    if (found != nullptr) {
      throw ArmorError(std::string(field_name) + " appears more than once before the "
                       "object-template block");
    }
    found = &record.subrecords[i];
  }
  if (found == nullptr) return std::nullopt;
  try {
    return schema::read_localized_string(plugin, record_signature, signature, *found);
  } catch (const std::exception& error) {
    throw ArmorError(error.what());
  }
}

std::optional<schema::LocalizedString> optional_localized(const Plugin& plugin,
                                                         const Record& record,
                                                         Signature record_signature,
                                                         Signature signature,
                                                         std::string_view field_name) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw ArmorError(std::string(field_name) + " appears more than once");
    }
    found = &subrecord;
  }
  if (found == nullptr) return std::nullopt;
  try {
    return schema::read_localized_string(plugin, record_signature, signature, *found);
  } catch (const std::exception& error) {
    throw ArmorError(error.what());
  }
}

std::optional<std::string> optional_string(const Record& record,
                                           Signature signature,
                                           std::string_view field_name) {
  const auto* subrecord = unique_subrecord(record, signature, field_name, false);
  if (subrecord == nullptr) return std::nullopt;
  return decode_zstring(*subrecord, field_name);
}

using fo4::detail::find_mutable_record;

void append_optional_string(Record& record,
                            Signature signature,
                            const std::optional<std::vector<std::byte>>& encoded) {
  if (!encoded.has_value()) return;
  record.subrecords.push_back(Subrecord{signature, *encoded, false});
}

std::optional<std::vector<std::byte>> encode_optional(
    std::optional<std::string_view> value) {
  if (!value.has_value()) return std::nullopt;
  return zstring(stored_inline_text(*value, "ARMO/ARMA string field"));
}

std::optional<std::vector<std::byte>> encode_editor_id(
    std::optional<std::string_view> value) {
  if (!value.has_value()) return std::nullopt;
  return zstring(stored_editor_id(*value));
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

Subrecord armor_data(const ArmorCreation& creation) {
  if (!std::isfinite(creation.weight)) {
    throw ArmorError("ARMO weight must be finite");
  }
  Subrecord data;
  data.signature = sig_data;
  data.data.resize(12);
  write_u32(data.data, 0, static_cast<std::uint32_t>(creation.value));
  write_f32(data.data, 4, creation.weight);
  write_u32(data.data, 8, creation.health);
  return data;
}

Subrecord armor_addon_data(const ArmorAddonCreation& creation) {
  if ((creation.male_weight_slider & ~0x03u) != 0 ||
      (creation.female_weight_slider & ~0x03u) != 0) {
    throw ArmorError("ARMA weight-slider flags may use only bits 0x01 and 0x02");
  }
  if (!std::isfinite(creation.weapon_adjust)) {
    throw ArmorError("ARMA weapon adjust must be finite");
  }

  Subrecord data;
  data.signature = k_dnam;
  data.data.resize(12);
  data.data[0] = std::byte{creation.male_priority};
  data.data[1] = std::byte{creation.female_priority};
  data.data[2] = std::byte{creation.male_weight_slider};
  data.data[3] = std::byte{creation.female_weight_slider};
  data.data[4] = creation.unknown_before_detection[0];
  data.data[5] = creation.unknown_before_detection[1];
  data.data[6] = std::byte{creation.detection_sound_value};
  data.data[7] = creation.unknown_before_weapon_adjust;
  write_f32(data.data, 8, creation.weapon_adjust);
  return data;
}

void require_authoring_preflight(const Plugin& plugin,
                                 const FormKey& key,
                                 std::string_view record_name) {
  for (const auto& issue : schema::preflight_authoring_record(plugin, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw ArmorError(std::string(record_name) +
                       " authoring preflight failed: " + issue.message);
    }
  }
}

}  // namespace

Armor decode_armor(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) throw ArmorError("ARMO record not found: " + key.to_string());
  if (record->header.signature != k_armo) {
    throw ArmorError("record is not ARMO: " + key.to_string());
  }

  Armor result;
  result.key = key;
  result.editor_id = optional_string(*record, sig_edid, "ARMO EDID");
  result.male_world_model = optional_string(*record, k_mod2, "ARMO male MOD2");
  result.male_icon = optional_string(*record, k_icon, "ARMO male ICON");
  result.male_message_icon = optional_string(*record, k_mico, "ARMO male MICO");
  result.female_world_model = optional_string(*record, k_mod4, "ARMO female MOD4");
  result.female_icon = optional_string(*record, k_ico2, "ARMO female ICO2");
  result.female_message_icon = optional_string(*record, k_mic2, "ARMO female MIC2");
  const auto boundary = template_boundary(*record);
  result.full_name =
      optional_localized_before(plugin, *record, k_armo, k_full, boundary, "ARMO FULL");
  result.description = optional_localized(plugin, *record, k_armo, k_desc, "ARMO DESC");

  const auto* data = unique_subrecord(*record, sig_data, "ARMO DATA", true);
  if (data->data.size() != 12) {
    throw ArmorError("ARMO DATA must be exactly 12 bytes");
  }
  result.value = static_cast<std::int32_t>(read_u32(data->data, 0));
  result.weight = read_f32(data->data, 4);
  result.health = read_u32(data->data, 8);
  return result;
}

ArmorAddon decode_armor_addon(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) throw ArmorError("ARMA record not found: " + key.to_string());
  if (record->header.signature != k_arma) {
    throw ArmorError("record is not ARMA: " + key.to_string());
  }

  ArmorAddon result;
  result.key = key;
  result.editor_id = optional_string(*record, sig_edid, "ARMA EDID");
  result.male_biped_model = optional_string(*record, k_mod2, "ARMA male biped MOD2");
  result.female_biped_model = optional_string(*record, k_mod3, "ARMA female biped MOD3");
  result.male_first_person_model = optional_string(*record, k_mod4, "ARMA male first-person MOD4");
  result.female_first_person_model = optional_string(*record, k_mod5, "ARMA female first-person MOD5");

  const auto* data = unique_subrecord(*record, k_dnam, "ARMA DNAM", true);
  if (data->data.size() != 12) {
    throw ArmorError("ARMA DNAM must be exactly 12 bytes");
  }
  result.male_priority = std::to_integer<std::uint8_t>(data->data[0]);
  result.female_priority = std::to_integer<std::uint8_t>(data->data[1]);
  result.male_weight_slider = std::to_integer<std::uint8_t>(data->data[2]);
  result.female_weight_slider = std::to_integer<std::uint8_t>(data->data[3]);
  result.unknown_before_detection = {data->data[4], data->data[5]};
  result.detection_sound_value = std::to_integer<std::uint8_t>(data->data[6]);
  result.unknown_before_weapon_adjust = data->data[7];
  result.weapon_adjust = read_f32(data->data, 8);
  return result;
}

FormKey create_armor(Plugin& plugin, const ArmorCreation& creation) {
  auto next = plugin;

  // Authoring inline text into a localized plugin would produce a DESC the
  // engine reads as a four-byte string ID. Minting a real ID means owning the
  // .STRINGS tables, which this path does not, so fail closed.
  if (creation.description.has_value() && next.header.tes4_flags.is_localized()) {
    throw ArmorError(
        "ARMO DESC cannot be authored as inline text into a localized plugin");
  }

  const auto editor = encode_editor_id(creation.editor_id);
  const auto male_model = encode_optional(creation.male_world_model);
  const auto male_icon = encode_optional(creation.male_icon);
  const auto male_message_icon = encode_optional(creation.male_message_icon);
  const auto female_model = encode_optional(creation.female_world_model);
  const auto female_icon = encode_optional(creation.female_icon);
  const auto female_message_icon = encode_optional(creation.female_message_icon);
  const auto encoded_description = encode_optional(creation.description);
  const auto data = armor_data(creation);

  const auto key = create_local_record(next, k_armo);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw ArmorError("newly allocated ARMO record could not be resolved");
  }

  append_optional_string(*record, sig_edid, editor);
  append_optional_string(*record, k_mod2, male_model);
  append_optional_string(*record, k_icon, male_icon);
  append_optional_string(*record, k_mico, male_message_icon);
  append_optional_string(*record, k_mod4, female_model);
  append_optional_string(*record, k_ico2, female_icon);
  append_optional_string(*record, k_mic2, female_message_icon);
  if (encoded_description.has_value()) {
    record->subrecords.push_back(Subrecord{k_desc, *encoded_description, false});
  }
  record->subrecords.push_back(data);

  require_authoring_preflight(next, key, "ARMO");
  plugin = std::move(next);
  return key;
}

FormKey create_armor_addon(Plugin& plugin, const ArmorAddonCreation& creation) {
  auto next = plugin;

  const auto editor = encode_editor_id(creation.editor_id);
  const auto male_biped = encode_optional(creation.male_biped_model);
  const auto female_biped = encode_optional(creation.female_biped_model);
  const auto male_first = encode_optional(creation.male_first_person_model);
  const auto female_first = encode_optional(creation.female_first_person_model);
  const auto data = armor_addon_data(creation);

  const auto key = create_local_record(next, k_arma);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw ArmorError("newly allocated ARMA record could not be resolved");
  }

  append_optional_string(*record, sig_edid, editor);
  record->subrecords.push_back(data);
  append_optional_string(*record, k_mod2, male_biped);
  append_optional_string(*record, k_mod3, female_biped);
  append_optional_string(*record, k_mod4, male_first);
  append_optional_string(*record, k_mod5, female_first);

  require_authoring_preflight(next, key, "ARMA");
  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
