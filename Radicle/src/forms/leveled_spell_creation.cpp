#include "fo4/forms/leveled_spell.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/preflight.hpp"
#include "../plugin/record_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_lvsp = Signature::from_string("LVSP");
constexpr auto k_obnd = Signature::from_string("OBND");
constexpr auto k_spel = Signature::from_string("SPEL");
constexpr auto k_lvld = Signature::from_string("LVLD");
constexpr auto k_lvlm = Signature::from_string("LVLM");
constexpr auto k_lvlf = Signature::from_string("LVLF");
constexpr auto k_llct = Signature::from_string("LLCT");

Subrecord byte_subrecord(Signature signature, std::uint8_t value) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.push_back(std::byte{value});
  return subrecord;
}

using fo4::detail::find_mutable_record;

void validate_local_entry_targets(const Plugin& plugin,
                                  std::span<const LeveledSpellEntry> entries) {
  for (const auto& entry : entries) {
    if (entry.spell.owner().empty()) {
      throw LeveledSpellError("LVSP entry must reference a SPEL or LVSP FormKey");
    }
    if (entry.spell.owner() != plugin.key) continue;

    const auto* target = find_record(plugin, entry.spell);
    if (target == nullptr ||
        (target->header.signature != k_spel && target->header.signature != k_lvsp)) {
      throw LeveledSpellError(
          "local LVSP entry FormKey must resolve to an existing SPEL or LVSP record: " +
          entry.spell.to_string());
    }
  }
}

}  // namespace

FormKey create_leveled_spell(Plugin& plugin,
                             std::uint8_t chance_none,
                             std::uint8_t max_count,
                             std::uint8_t flags,
                             std::span<const LeveledSpellEntry> entries) {
  if ((flags & ~0x07u) != 0) {
    throw LeveledSpellError(
        "LVSP flags may use only defined bits 0x01, 0x02, and 0x04");
  }
  if (entries.size() > 0xFFu) {
    throw LeveledSpellError("LVSP cannot contain more than 255 LVLO entries");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  validate_local_entry_targets(next, entries);

  const auto key = create_local_record(next, k_lvsp);
  auto* record = find_mutable_record(next, key);
  if (record == nullptr) {
    throw LeveledSpellError("newly allocated LVSP record could not be resolved");
  }

  // Proven FO4 LVSP order for the required list settings. Seed LLCT at zero;
  // replace_leveled_spell_entries owns the established 12-byte LVLO encoder
  // and will update this count before any non-empty entry list is committed.
  record->subrecords.push_back(Subrecord{k_obnd, std::vector<std::byte>(12), false});
  record->subrecords.push_back(byte_subrecord(k_lvld, chance_none));
  record->subrecords.push_back(byte_subrecord(k_lvlm, max_count));
  record->subrecords.push_back(byte_subrecord(k_lvlf, flags));
  record->subrecords.push_back(byte_subrecord(k_llct, 0));

  if (!entries.empty()) {
    if (!replace_leveled_spell_entries(next, key, entries)) {
      throw LeveledSpellError("newly allocated LVSP record could not accept entries");
    }
  }

  for (const auto& issue : schema::preflight_authoring_record(next, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw LeveledSpellError("LVSP authoring preflight failed: " + issue.message);
    }
  }

  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
