#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {

class PerkError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class PerkEffectType : std::uint8_t {
  quest_stage = 0,
  ability = 1,
  entry_point = 2,
};

enum class PerkFunctionType : std::uint8_t {
  none = 0,
  single_float = 1,
  float_pair = 2,
  leveled_item = 3,
  labeled_spell = 4,
  spell = 5,
  text = 6,
  localized_text = 7,
  actor_value = 8,
};

struct PerkFunctionParameters {
  std::optional<std::uint8_t> type;
  std::optional<std::uint16_t> entry_id;
  std::optional<schema::LocalizedString> button_label;
  std::optional<std::uint16_t> script_flags;
  std::optional<std::vector<std::byte>> data;
};

struct PerkConditionTab {
  std::int8_t run_on{0};
  std::vector<CtdaCondition> conditions;
};

struct PerkEffectEntry {
  PerkEffectType type{PerkEffectType::quest_stage};
  std::uint8_t rank{0};
  std::uint8_t priority{0};
  FormKey quest{};
  std::uint16_t quest_stage{0};
  FormKey ability{};
  std::uint8_t entry_point{0};
  std::uint8_t function{0};
  std::uint8_t condition_tab_count{0};
  std::vector<PerkConditionTab> tabs;
  PerkFunctionParameters parameters;
};

enum class PerkEffectWrite {
  written,
  record_missing,
  unsupported_record,
  unexpected_layout,
};

struct PerkCreation {
  std::string_view editor_id;
  std::string_view full_name;
  std::string_view description;
  std::uint8_t trait{0};
  std::uint8_t level{0};
  std::uint8_t ranks{1};
  std::uint8_t playable{1};
  std::vector<PerkEffectEntry> effects;
};

FormKey create_perk(Plugin& plugin, const PerkCreation& creation);

std::vector<PerkEffectEntry> read_perk_effects(const Plugin& plugin, const Record& record);

PerkEffectWrite set_perk_effects(Plugin& plugin,
                                 const FormKey& key,
                                 std::span<const PerkEffectEntry> effects);

std::optional<float> perk_epfd_float(const PerkEffectEntry& effect);
std::optional<std::pair<float, float>> perk_epfd_float_pair(const PerkEffectEntry& effect);
std::optional<FormKey> perk_epfd_form(const Plugin& plugin, const PerkEffectEntry& effect);
std::optional<std::string> perk_epfd_text(const PerkEffectEntry& effect);
std::optional<std::pair<FormKey, float>> perk_epfd_actor_value(const Plugin& plugin,
                                                               const PerkEffectEntry& effect);

}  // namespace fo4::forms
