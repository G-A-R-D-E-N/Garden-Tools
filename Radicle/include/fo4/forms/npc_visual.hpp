#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/formid.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// source:xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas NPC_ :10286; field sources and rules in docs/NPC_VISUAL.md
namespace fo4::forms {

class NpcVisualError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class NpcVisualSource : std::uint8_t {
  facegen = 0,
  skin = 1,
  outfit = 2,
  object_template = 3,
};

struct NpcVisualPart {
  std::string mesh_entry;
  std::uint32_t biped_slots{0};
  NpcVisualSource source{NpcVisualSource::facegen};

  friend bool operator==(const NpcVisualPart&, const NpcVisualPart&) = default;
};

// Leveled lists require a spawn level; the seed must keep one placed reference's pick stable.
struct NpcVisualOptions {
  std::optional<std::uint16_t> level;
  std::uint64_t seed{0};
};

struct NpcVisual {
  FormKey npc{};
  std::optional<FormKey> race;
  std::optional<std::string> race_editor_id;
  bool female{false};
  std::optional<std::array<float, 4>> skin_tint;
  std::optional<std::string> skeleton_hkx_entry;
  std::vector<NpcVisualPart> parts;
  std::optional<std::string> idle_clip_entry;
  std::vector<std::string> stubs;
  // Constraint: only valid for the level in NpcVisualOptions; the rules are in the CodeNotes entry NpcVisual::seed_dependent.
  bool seed_dependent{false};
};

std::string npc_visual_mesh_entry(std::string_view model_path);
std::string npc_visual_hkx_entry(std::string_view model_path);

// Constraint: budget_exceeded and depth_exceeded outrank the other reasons.
enum class LeveledPickReason : std::uint8_t {
  picked = 0,
  chance_none = 1,
  no_entry_at_level = 2,
  entries_rolled_out = 3,
  nothing_live = 4,
  budget_exceeded = 5,
  depth_exceeded = 6,
  list_missing = 7,
  not_leveled_list = 8,
  undecodable = 9,
  not_spawnable = 10,
};

struct LeveledPick {
  std::vector<FormKey> entries;
  LeveledPickReason reason{LeveledPickReason::nothing_live};
  std::size_t depth_used{0};
};

// Constraint: flags_override ORs into the top list's LVLF only; the C API passes 0x04 (use all) and 0x02 (calculate for each; only pick_leveled_counted reads it).
LeveledPick pick_leveled(const env::Environment& environment,
                         const FormKey& list,
                         std::uint16_t level,
                         std::uint64_t seed,
                         std::vector<std::string>* stubs = nullptr,
                         std::uint8_t flags_override = 0);

// Constraint: counts[i] must be the LVLO count of the row that produced entries[i].
struct LeveledCountedPick {
  std::vector<FormKey> entries;
  std::vector<std::uint16_t> counts;
  LeveledPickReason reason{LeveledPickReason::nothing_live};
  std::size_t depth_used{0};
  std::uint32_t rolls{0};
  std::uint32_t multiplier{1};
};

// Constraint: LVLF 0x02 on the top list (or in flags_override) makes count independent rolls with multiplier 1; clear is one roll with multiplier count. Roll 0 uses the caller's seed.
LeveledCountedPick pick_leveled_counted(const env::Environment& environment,
                                        const FormKey& list,
                                        std::uint16_t level,
                                        std::uint64_t seed,
                                        std::uint32_t count,
                                        std::vector<std::string>* stubs = nullptr,
                                        std::uint8_t flags_override = 0);

// An LVLI with Use all returns every surviving entry, one of each; every other list returns at most one.
std::vector<FormKey> pick_leveled_entries(const env::Environment& environment,
                                          const FormKey& list,
                                          std::uint16_t level,
                                          std::uint64_t seed,
                                          std::vector<std::string>* stubs = nullptr);

// Must be deterministic: the same seed always picks the same entry; nests cap at depth 8.
std::optional<FormKey> pick_leveled_entry(const env::Environment& environment,
                                          const FormKey& list,
                                          std::uint16_t level,
                                          std::uint64_t seed,
                                          std::vector<std::string>* stubs = nullptr);

NpcVisual decode_npc_visual(const env::Environment& environment,
                            const env::RecordRef& npc,
                            NpcVisualOptions options = {});

// Constraint: parts are tagged outfit; matched_slots includes addons without a model; slot conflicts between two ARMO are the caller's.
struct ArmorAddons {
  std::vector<NpcVisualPart> parts;
  std::uint32_t matched_slots{0};
  std::vector<std::string> stubs;
};

ArmorAddons decode_armor_addons(const env::Environment& environment,
                                const env::RecordRef& armor,
                                const FormKey& race,
                                bool female,
                                bool first_person);

}  // namespace fo4::forms
