// Layouts and sources: Radicle/docs/LEVELED_LISTS.md (TODO-verify until LeveledListReal runs on CI).
#include "fo4/forms/leveled_spell.hpp"

#include "fo4/plugin/reader.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_lvsp = Signature::from_string("LVSP");

LeveledListEntry to_list_entry(const LeveledSpellEntry& entry) {
  LeveledListEntry result;
  result.level = entry.level;
  result.reserved_before_form_id = entry.reserved_before_form_id;
  result.item = entry.spell;
  result.count = entry.count;
  result.chance_none = entry.chance_none;
  result.reserved_tail = entry.reserved_tail;
  return result;
}

LeveledSpellEntry to_spell_entry(const LeveledListEntry& entry) {
  LeveledSpellEntry result;
  result.level = entry.level;
  result.reserved_before_form_id = entry.reserved_before_form_id;
  result.spell = entry.item;
  result.count = entry.count;
  result.chance_none = entry.chance_none;
  result.reserved_tail = entry.reserved_tail;
  return result;
}

void require_lvsp(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    throw LeveledSpellError("LVSP record not found: " + key.to_string());
  }
  if (record->header.signature != k_lvsp) {
    throw LeveledSpellError("record is not LVSP: " + key.to_string());
  }
}

}  // namespace

LeveledSpell decode_leveled_spell(const Plugin& plugin, const FormKey& key) {
  require_lvsp(plugin, key);
  const auto list = decode_leveled_list(plugin, key);
  LeveledSpell result;
  result.key = key;
  result.chance_none = list.chance_none;
  result.max_count = list.max_count;
  result.flags = list.flags;
  for (const auto& entry : list.entries) {
    result.entries.push_back(to_spell_entry(entry));
  }
  return result;
}

bool replace_leveled_spell_entries(Plugin& plugin,
                                   const FormKey& key,
                                   std::span<const LeveledSpellEntry> entries) {
  assign_record_form_keys(plugin);
  require_lvsp(plugin, key);
  std::vector<LeveledListEntry> converted;
  converted.reserve(entries.size());
  for (const auto& entry : entries) {
    converted.push_back(to_list_entry(entry));
  }
  return replace_leveled_list_entries(plugin, key, converted);
}

bool set_leveled_spell_settings(Plugin& plugin,
                                const FormKey& key,
                                std::uint8_t chance_none,
                                std::uint8_t max_count,
                                std::uint8_t flags) {
  assign_record_form_keys(plugin);
  require_lvsp(plugin, key);
  return set_leveled_list_settings(plugin, key, chance_none, max_count, flags);
}

}  // namespace fo4::forms
