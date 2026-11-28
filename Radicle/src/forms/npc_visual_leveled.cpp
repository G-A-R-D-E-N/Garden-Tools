#include "npc_visual_detail.hpp"

#include "fo4/forms/leveled_list.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace detail {

using env::Environment;
using env::RecordRef;

constexpr std::uint8_t k_leveled_all_levels = 0x01;
constexpr std::uint8_t k_leveled_use_all = 0x04;

std::vector<FormKey> pick_leveled_impls(const Environment& env,
                                       const FormKey& list_key,
                                       std::uint16_t level,
                                       std::uint64_t seed,
                                       std::size_t depth,
                                       std::vector<std::string>* stubs,
                                       std::size_t& budget,
                                       bool* seed_dependent,
                                       LeveledTrace* trace,
                                       std::uint8_t forced_flags) {
  if (trace != nullptr) trace->deepest = std::max(trace->deepest, depth);
  if (depth >= k_max_leveled_depth) {
    add_stub(stubs, "leveled list " + list_key.to_string() + " nests deeper than 8; stopped");
    note_reason(trace, LeveledPickReason::depth_exceeded);
    return {};
  }
  if (budget == 0) {
    add_stub(stubs, "leveled list " + list_key.to_string() +
                        " exceeded the 1024-node budget; stopped");
    note_reason(trace, LeveledPickReason::budget_exceeded);
    return {};
  }
  --budget;
  const auto* raw_ref = env.winner(list_key);
  if (raw_ref == nullptr || raw_ref->record->header.flags.is_deleted()) {
    add_stub(stubs, "leveled list " + list_key.to_string() + " not found");
    note_reason(trace, LeveledPickReason::list_missing);
    return {};
  }
  if (raw_ref->record->undecodable) {
    add_stub(stubs, "leveled list " + list_key.to_string() + " is undecodable");
    note_reason(trace, LeveledPickReason::undecodable);
    return {};
  }
  const auto list_signature = raw_ref->record->header.signature;
  if (list_signature != sig_lvln && list_signature != sig_lvli) {
    add_stub(stubs, "leveled list " + list_key.to_string() + " is a " +
                        list_signature.to_string() + " record");
    note_reason(trace, LeveledPickReason::not_leveled_list);
    return {};
  }

  LeveledList list;
  try {
    list = decode_leveled_list(env.plugin(raw_ref->plugin), list_key);
  } catch (const LeveledListError& error) {
    add_stub(stubs, "leveled list " + list_key.to_string() + " did not decode: " + error.what());
    note_reason(trace, LeveledPickReason::undecodable);
    return {};
  }

  const auto base = seed_of(seed, list_key);
  // Constraint: forced flags come from the caller's override mask and apply to this list only.
  const auto flags = static_cast<std::uint8_t>(list.flags | forced_flags);
  if (trace != nullptr && depth == 0) trace->top_flags = flags;
  if (list.use_global) {
    add_stub(stubs, "leveled list " + list_key.to_string() +
                        " reads its chance none from global " + list.use_global->to_string() +
                        "; the stored value is used");
    // Constraint: the global's chance none is not read, so the stored value cannot prove a fixed roll.
    if (seed_dependent != nullptr) *seed_dependent = true;
  }
  // Invariant: any chance none above 0 on a consulted list counts, even on rows above the spawn level.
  if (seed_dependent != nullptr) {
    const bool any_chance_none = list.chance_none > 0 ||
        std::any_of(list.entries.begin(), list.entries.end(),
                    [](const LeveledListEntry& entry) { return entry.chance_none > 0; });
    if (any_chance_none) *seed_dependent = true;
  }
  if (list.chance_none > 0 && mix64(base ^ k_salt_chance_none) % 100u < list.chance_none) {
    add_stub(stubs, "note: leveled list " + list_key.to_string() + " rolled its chance none");
    note_reason(trace, LeveledPickReason::chance_none);
    return {};
  }

  std::vector<const LeveledListEntry*> candidates;
  for (const auto& entry : list.entries) {
    if (entry.item.owner().empty()) continue;
    if (entry.level > level) continue;
    candidates.push_back(&entry);
  }
  if (candidates.empty()) {
    std::uint16_t lowest = 0xFFFF;
    for (const auto& entry : list.entries) {
      if (entry.item.owner().empty()) continue;
      lowest = std::min(lowest, entry.level);
    }
    auto text = "leveled list " + list_key.to_string() + " has no entry at level " +
                std::to_string(level);
    if (lowest != 0xFFFF) {
      text = "note: " + text + "; the lowest entry is at level " + std::to_string(lowest);
    }
    add_stub(stubs, std::move(text));
    note_reason(trace, LeveledPickReason::no_entry_at_level);
    return {};
  }

  // Constraint: only an LVLI with use-all keeps every tier.
  if ((flags & k_leveled_all_levels) == 0 && !(list_signature == sig_lvli && (flags & k_leveled_use_all) != 0)) {
    std::uint16_t highest = 0;
    for (const auto* candidate : candidates) {
      highest = std::max(highest, candidate->level);
    }
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [highest](const LeveledListEntry* candidate) {
                                      return candidate->level != highest;
                                    }),
                     candidates.end());
  }

  // Constraint: more than one candidate after the tier filter means the seed picks between them.
  if (seed_dependent != nullptr && candidates.size() > 1) *seed_dependent = true;

  // Constraint: the tier filter runs first, so a rolled-out top tier draws nothing instead of a lower tier.
  std::vector<const LeveledListEntry*> survivors;
  for (const auto* candidate : candidates) {
    // Invariant: the roll reads the entry's position, so two rows naming one item roll apart.
    const auto index = static_cast<std::uint64_t>(candidate - list.entries.data());
    const auto entry_roll = mix64(fnv_mix(base ^ k_salt_entry, index));
    if (candidate->chance_none > 0 && entry_roll % 100u < candidate->chance_none) continue;
    survivors.push_back(candidate);
  }
  if (survivors.empty()) {
    add_stub(stubs, "note: leveled list " + list_key.to_string() + " rolled out every entry at level " +
                        std::to_string(level));
    note_reason(trace, LeveledPickReason::entries_rolled_out);
    return {};
  }
  candidates = std::move(survivors);

  // Constraint: xEdit marks LVLN use-all as still picking one, so only LVLI returns every entry.
  const auto use_all = list_signature == sig_lvli && (flags & k_leveled_use_all) != 0;
  if (!use_all) {
    const auto count = static_cast<std::uint64_t>(candidates.size());
    // Offset: one mixed pick per list and depth, so nested picks differ.
    const auto* chosen = candidates[mix64(base ^ k_salt_pick ^ static_cast<std::uint64_t>(depth)) % count];
    candidates.assign(1, chosen);
  }

  std::vector<FormKey> picked;
  for (const auto* candidate : candidates) {
    const auto* item_ref = winner_of(env, candidate->item);
    if (item_ref == nullptr) {
      add_stub(stubs, "leveled list " + list_key.to_string() + " picks " +
                          candidate->item.to_string() + " which is not a live record");
      note_reason(trace, LeveledPickReason::nothing_live);
      continue;
    }
    const auto item_signature = item_ref->record->header.signature;
    if (item_signature == sig_lvln || item_signature == sig_lvli) {
      for (auto& nested : pick_leveled_impls(env, candidate->item, level,
                                             seed_of(seed, candidate->item), depth + 1, stubs,
                                             budget, seed_dependent, trace)) {
        picked.push_back(std::move(nested));
      }
      continue;
    }
    picked.push_back(candidate->item);
    if (trace != nullptr) trace->counts.push_back(candidate->count);
  }
  return picked;
}

}  // namespace detail
}  // namespace fo4::forms
