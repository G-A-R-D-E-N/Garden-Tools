#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class FactionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class FactionCombatReaction : std::uint32_t {
  neutral = 0,
  enemy = 1,
  ally = 2,
  friendly = 3,
};

// TODO-verify: XNAM layout and reaction values from xEdit 93cc0bc5 wbDefinitionsCommon.pas:8543-8553; FactionReal checks them on official masters.
struct FactionRelation {
  FormKey faction{};
  std::int32_t modifier{0};
  FactionCombatReaction reaction{FactionCombatReaction::neutral};
};

// TODO-verify: RNAM/MNAM/FNAM/INAM from xEdit 93cc0bc5 wbDefinitionsFO4.pas:6771-6776; FactionReal checks them on official masters.
struct FactionRank {
  std::uint32_t number{0};
  std::optional<schema::LocalizedString> male_title;
  std::optional<schema::LocalizedString> female_title;
  std::optional<std::vector<std::byte>> insignia;
};

std::vector<FactionRelation> decode_faction_relations(const Plugin& plugin, const FormKey& key);
std::vector<FactionRank> decode_faction_ranks(const Plugin& plugin, const FormKey& key);

void add_faction_relation(Plugin& plugin, const FormKey& key, const FactionRelation& relation);
void set_faction_relation(Plugin& plugin,
                          const FormKey& key,
                          std::size_t index,
                          const FactionRelation& relation);
void remove_faction_relation(Plugin& plugin, const FormKey& key, std::size_t index);

void add_faction_rank(Plugin& plugin, const FormKey& key, const FactionRank& rank);
void set_faction_rank(Plugin& plugin,
                      const FormKey& key,
                      std::size_t index,
                      const FactionRank& rank);
void remove_faction_rank(Plugin& plugin, const FormKey& key, std::size_t index);

}  // namespace fo4::forms
