#pragma once

#include "fo4/forms/leveled_list.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

// Layouts and sources: Radicle/docs/LEVELED_LISTS.md (TODO-verify until LeveledListReal runs on CI).
namespace fo4::forms {

using LeveledSpellError = LeveledListError;

struct LeveledSpellEntry {
  std::uint16_t level{1};
  std::array<std::byte, 2> reserved_before_form_id{};
  FormKey spell{};
  std::uint16_t count{1};
  std::uint8_t chance_none{0};
  std::byte reserved_tail{};

  friend bool operator==(const LeveledSpellEntry&, const LeveledSpellEntry&) = default;
};

struct LeveledSpell {
  FormKey key{};
  std::uint8_t chance_none{0};
  std::uint8_t max_count{0};
  std::uint8_t flags{0};
  std::vector<LeveledSpellEntry> entries;
};

LeveledSpell decode_leveled_spell(const Plugin& plugin, const FormKey& key);

FormKey create_leveled_spell(Plugin& plugin,
                             std::uint8_t chance_none,
                             std::uint8_t max_count,
                             std::uint8_t flags,
                             std::span<const LeveledSpellEntry> entries);

bool replace_leveled_spell_entries(Plugin& plugin,
                                   const FormKey& key,
                                   std::span<const LeveledSpellEntry> entries);

bool set_leveled_spell_settings(Plugin& plugin,
                                const FormKey& key,
                                std::uint8_t chance_none,
                                std::uint8_t max_count,
                                std::uint8_t flags);

}  // namespace fo4::forms
