#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/forms/gameplay_rules.hpp"
#include "fo4/forms/npc.hpp"
#include "fo4/formid.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::forms {

struct NpcAiData {
  std::uint8_t aggression{0};
  std::uint8_t confidence{0};
  std::uint8_t energy_level{0};
  std::uint8_t morality{0};
  std::uint8_t mood{0};
  std::uint8_t assistance{0};

  friend bool operator==(const NpcAiData&, const NpcAiData&) = default;
};

struct NpcCalculatedStats {
  std::uint16_t health{0};
  std::uint16_t action_points{0};

  friend bool operator==(const NpcCalculatedStats&, const NpcCalculatedStats&) = default;
};

struct NpcGameplay {
  FormKey npc{};
  std::uint32_t configuration_flags{0};
  std::uint32_t level_raw{0};
  std::uint32_t calc_min_level{0};
  std::uint32_t calc_max_level{0};
  std::uint32_t template_flags{0};
  bool level_is_multiplier{false};
  std::optional<FormKey> race;
  std::optional<FormKey> class_form;
  std::optional<FormKey> combat_style;
  std::optional<FormKey> crime_faction;
  std::optional<FormKey> template_base;
  std::vector<FormKey> template_chain;
  std::vector<FormKey> spells;
  std::vector<NpcFaction> factions;
  std::vector<NpcPerk> perks;
  std::optional<NpcAiData> ai;
  std::optional<NpcCalculatedStats> calculated;
  std::vector<std::string> stubs;
};

// Constraint: slot numbers are the ACBS template flag bit and the TPTA link index; the offset in TPTA is 4 * slot.
enum class NpcTemplateSlot : std::uint8_t {
  traits = 0,
  stats = 1,
  factions = 2,
  spell_list = 3,
  ai_data = 4,
  ai_packages = 5,
  model_animation = 6,
  base_data = 7,
  inventory = 8,
  script = 9,
  def_pack_list = 10,
  attack_data = 11,
  keywords = 12,
};

enum class NpcTemplateReason : std::uint8_t {
  own = 0,
  template_npc = 1,
  no_link = 2,
  not_found = 3,
  undecodable = 4,
  not_npc = 5,
  leveled_list = 6,
  too_deep = 7,
};

struct NpcTemplateSource {
  FormKey source{};
  NpcTemplateReason reason{NpcTemplateReason::own};
  std::uint32_t hops{0};
  std::optional<FormKey> blocker;
};

// Constraint: source is the last NPC_ reached; a reason other than own and template_npc means the chain stopped there.
NpcTemplateSource resolve_npc_template_source(const env::Environment& environment,
                                              const env::RecordRef& npc,
                                              NpcTemplateSlot slot);

struct NpcPackageList {
  std::vector<FormKey> keys;
  NpcTemplateSource source;
};

// Constraint: keys are meaningful only when source.reason is own or template_npc; otherwise source is the first slot (AI packages, then Def Pack List) whose chain stopped.
NpcPackageList npc_package_list(const env::Environment& environment, const env::RecordRef& npc);

struct NpcVoiceType {
  std::optional<FormKey> voice;
  NpcTemplateSource origin;
};

// Constraint: own VTCK first, else TPLT is followed regardless of ACBS template flags and TPTA (engine parity); origin.reason is own or template_npc when a voice was found.
NpcVoiceType resolve_npc_voice_type(const env::Environment& environment, const env::RecordRef& npc);

enum class GlobalValueReason : std::uint8_t {
  ok = 0,
  not_glob = 1,
  not_constant = 2,
  no_value = 3,
};

struct GlobalValue {
  GlobalValueReason reason{GlobalValueReason::ok};
  double value{0.0};
};

// Constraint: the Constant record flag (0x40) is checked only when constant_only is set; deleted and FNAM type are not consulted.
GlobalValue read_global_value(const env::RecordRef& glob, bool constant_only);

NpcGameplay decode_npc_gameplay(const env::Environment& environment, const env::RecordRef& npc);

}  // namespace fo4::forms
