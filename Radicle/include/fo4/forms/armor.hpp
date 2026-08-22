#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fo4::forms {

class ArmorError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ArmorCreation {
  std::optional<std::string_view> editor_id;
  std::optional<std::string_view> male_world_model;
  std::optional<std::string_view> male_icon;
  std::optional<std::string_view> male_message_icon;
  std::optional<std::string_view> female_world_model;
  std::optional<std::string_view> female_icon;
  std::optional<std::string_view> female_message_icon;
  // Inline text only. Authoring into a localized plugin is refused: minting a
  // string ID would mean owning the .STRINGS tables, which this path does not.
  std::optional<std::string_view> description;
  std::int32_t value{0};
  float weight{0.0f};
  std::uint32_t health{0};
};

struct Armor {
  FormKey key{};
  std::optional<std::string> editor_id;
  std::optional<std::string> male_world_model;
  std::optional<std::string> male_icon;
  std::optional<std::string> male_message_icon;
  std::optional<std::string> female_world_model;
  std::optional<std::string> female_icon;
  std::optional<std::string> female_message_icon;
  // DESC is localized: a string ID in a localized plugin, inline text
  // otherwise. Exactly one per record across all 688 shipped ARMO.
  //
  // The record's own name. FULL is not unique in ARMO -- the OBTE template
  // block emits [OBTF] FULL OBTS per combination, up to 20 in one record --
  // but the record's own FULL always precedes OBTE. Verified across 5,792
  // template-bearing ARMO/WEAP/NPC_ records: never more than one FULL before
  // the boundary, zero exceptions.
  std::optional<schema::LocalizedString> full_name;
  std::optional<schema::LocalizedString> description;
  std::int32_t value{0};
  float weight{0.0f};
  std::uint32_t health{0};
};

struct ArmorAddonCreation {
  std::optional<std::string_view> editor_id;
  std::optional<std::string_view> male_biped_model;
  std::optional<std::string_view> female_biped_model;
  std::optional<std::string_view> male_first_person_model;
  std::optional<std::string_view> female_first_person_model;
  std::uint8_t male_priority{0};
  std::uint8_t female_priority{0};
  std::uint8_t male_weight_slider{0};
  std::uint8_t female_weight_slider{0};
  std::array<std::byte, 2> unknown_before_detection{};
  std::uint8_t detection_sound_value{0};
  std::byte unknown_before_weapon_adjust{};
  float weapon_adjust{0.0f};
};

struct ArmorAddon {
  FormKey key{};
  std::optional<std::string> editor_id;
  std::optional<std::string> male_biped_model;
  std::optional<std::string> female_biped_model;
  std::optional<std::string> male_first_person_model;
  std::optional<std::string> female_first_person_model;
  std::uint8_t male_priority{0};
  std::uint8_t female_priority{0};
  std::uint8_t male_weight_slider{0};
  std::uint8_t female_weight_slider{0};
  std::array<std::byte, 2> unknown_before_detection{};
  std::uint8_t detection_sound_value{0};
  std::byte unknown_before_weapon_adjust{};
  float weapon_adjust{0.0f};
};

Armor decode_armor(const Plugin& plugin, const FormKey& key);
ArmorAddon decode_armor_addon(const Plugin& plugin, const FormKey& key);

FormKey create_armor(Plugin& plugin, const ArmorCreation& creation);
FormKey create_armor_addon(Plugin& plugin, const ArmorAddonCreation& creation);

bool set_armor_model_fields(
    Plugin& plugin,
    const FormKey& key,
    std::optional<std::string_view> male_world_model,
    std::optional<std::string_view> male_icon,
    std::optional<std::string_view> male_message_icon,
    std::optional<std::string_view> female_world_model,
    std::optional<std::string_view> female_icon,
    std::optional<std::string_view> female_message_icon);

bool set_armor_addon_model_fields(
    Plugin& plugin,
    const FormKey& key,
    std::optional<std::string_view> male_biped_model,
    std::optional<std::string_view> female_biped_model,
    std::optional<std::string_view> male_first_person_model,
    std::optional<std::string_view> female_first_person_model);

}  // namespace fo4::forms
