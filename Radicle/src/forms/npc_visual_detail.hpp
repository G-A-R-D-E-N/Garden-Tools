#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/forms/npc_visual.hpp"
#include "fo4/formid.hpp"
#include "fo4/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms::detail {

// Constraint: leveled lists nest at most eight deep and share a 1024-node budget per template resolution and per leveled outfit item.
constexpr std::size_t k_max_leveled_depth = 8;
constexpr std::size_t k_max_leveled_nodes = 1024;
// Constraint: a calculate-for-each call stops at 64 times the per-roll budget in total, whatever the count.
constexpr std::size_t k_max_leveled_total_nodes = k_max_leveled_nodes * 64;
constexpr std::size_t k_max_leveled_total_entries = 65536;
constexpr std::uint64_t k_salt_chance_none = 0x9E3779B97F4A7C15ull;
constexpr std::uint64_t k_salt_entry = 0xC2B2AE3D27D4EB4Full;
constexpr std::uint64_t k_salt_pick = 0x165667B19E3779F9ull;
constexpr std::uint64_t k_salt_roll = 0xD6E8FEB86659FD93ull;
constexpr std::uint8_t k_leveled_calc_each = 0x02;

inline void add_stub(std::vector<std::string>* stubs, std::string text) {
  if (stubs == nullptr) return;
  if (std::find(stubs->begin(), stubs->end(), text) == stubs->end()) {
    stubs->push_back(std::move(text));
  }
}

inline std::uint64_t fnv_mix(std::uint64_t hash, std::uint64_t value) {
  hash ^= value;
  hash *= k_fnv64_prime;
  return hash;
}

// Constraint: % count reads only the low bits, so every roll requires a full-avalanche finalizer.
inline std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30;
  value *= 0xBF58476D1CE4E5B9ull;
  value ^= value >> 27;
  value *= 0x94D049BB133111EBull;
  return value ^ (value >> 31);
}

inline std::string ascii_lower(std::string text) {
  for (auto& ch : text) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return text;
}

inline std::uint64_t seed_of(std::uint64_t seed, std::string_view text) {
  std::uint64_t hash = seed;
  for (const char ch : text) {
    hash = fnv_mix(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
  }
  return hash;
}

// Invariant: master names are case insensitive, so the seed reads the lowercased plugin file name.
inline std::uint64_t seed_of(std::uint64_t seed, const FormKey& key) {
  return fnv_mix(seed_of(seed, ascii_lower(std::string(key.owner().filename()))),
                 key.local().value());
}

inline const env::RecordRef* winner_of(const env::Environment& env, const FormKey& key) {
  const auto* ref = env.winner(key);
  if (ref == nullptr || ref->record->header.flags.is_deleted() || ref->record->undecodable) {
    return nullptr;
  }
  return ref;
}

// Constraint: limit reasons outrank the rest.
inline bool is_limit_reason(LeveledPickReason reason) {
  return reason == LeveledPickReason::budget_exceeded || reason == LeveledPickReason::depth_exceeded;
}

struct LeveledTrace {
  std::optional<LeveledPickReason> reason;
  std::size_t deepest{0};
  std::optional<std::uint8_t> top_flags;
  std::vector<std::uint16_t> counts;
};

// Invariant: roll 0 keeps the caller's seed, so a count of one equals the plain pick.
// Constraint: the seed is mixed before the roll index is added, so seed ^ roll collisions cannot map two (seed, roll) pairs to one stream.
inline std::uint64_t roll_seed(std::uint64_t seed, std::uint64_t roll) {
  return roll == 0 ? seed : mix64(mix64(seed ^ k_salt_roll) + roll * k_salt_pick);
}

// Constraint: limit reasons outrank the rest, nothing_live ranks lowest, and ties keep the first note.
inline int reason_rank(LeveledPickReason reason) {
  if (is_limit_reason(reason)) return 2;
  return reason == LeveledPickReason::nothing_live ? 0 : 1;
}

inline void note_reason(LeveledTrace* trace, LeveledPickReason reason) {
  if (trace == nullptr) return;
  if (!trace->reason || reason_rank(reason) > reason_rank(*trace->reason)) trace->reason = reason;
}

// Constraint: seed_dependent is only ever set to true.
std::vector<FormKey> pick_leveled_impls(const env::Environment& env,
                                        const FormKey& list_key,
                                        std::uint16_t level,
                                        std::uint64_t seed,
                                        std::size_t depth,
                                        std::vector<std::string>* stubs,
                                        std::size_t& budget,
                                        bool* seed_dependent = nullptr,
                                        LeveledTrace* trace = nullptr,
                                        std::uint8_t forced_flags = 0);

}  // namespace fo4::forms::detail
