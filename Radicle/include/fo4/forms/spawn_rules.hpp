#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"
#include "fo4/forms/npc_visual.hpp"
#include "fo4/formid.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Constraint: must match engine Services/Placement/EnableState.cs; XESP bit 0 per xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:4330-4335.
namespace fo4::forms {

// Constraint: parent is set only when XESP is present; opposite is XESP bit 0.
struct EnableLink {
  bool initially_disabled{false};
  std::optional<FormKey> parent;
  bool opposite{false};
};

// Constraint: nullopt means no live placed record (a missing parent).
using EnableLinkLookup = std::function<std::optional<EnableLink>(const FormKey&)>;
// Constraint: nullopt means no runtime override for the key.
using EnableOverrideLookup = std::function<std::optional<bool>(const FormKey&)>;

// Constraint: depth 16 mirrors the engine interim copy; game source UNVERIFIED.
constexpr std::size_t k_enable_max_depth = 16;

enum class EnableStop : std::uint8_t {
  root = 0,
  overridden = 1,
  cycle = 2,
  missing_parent = 3,
  depth_limit = 4,
};

struct EnableVerdict {
  bool enabled{true};
  FormKey deciding{};
  std::size_t chain_length{0};
  EnableStop stop{EnableStop::root};
};

// Constraint: effective state is the override if set, else parent state XOR opposite; an unfollowable parent falls back to the node's own flag.
EnableVerdict evaluate_enable_state(const FormKey& ref,
                                    const EnableLink& start,
                                    const EnableLinkLookup& lookup,
                                    const EnableOverrideLookup& overrides = {},
                                    std::size_t max_depth = k_enable_max_depth);

// Constraint: deleted, undecodable or non-placed records have no link.
std::optional<EnableLink> enable_link_of(const env::Environment& environment, const FormKey& key);

enum class SpawnBaseKind : std::uint8_t {
  none = 0,
  npc = 1,
  leveled_npc = 2,
  leveled_item = 3,
  other = 4,
};

// Constraint: a missing or deleted base is none.
SpawnBaseKind spawn_base_kind_of(const env::Environment& environment, const FormKey& base);

// Constraint: level_modifier is raw; the XLCM level rule is UNVERIFIED and not applied.
struct PlacedActorSpawn {
  std::optional<FormKey> base;
  SpawnBaseKind base_kind{SpawnBaseKind::none};
  std::optional<FormKey> resolved_base;
  bool actor_reference{false};
  std::optional<std::int32_t> count;
  std::optional<std::int32_t> level_modifier;
  std::optional<FormKey> encounter_zone;
  // Constraint: ECZN DATA byte 9 as i8 (xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:8022-8037).
  std::optional<std::int32_t> encounter_zone_min_level;
  bool persistent{false};
  bool initially_disabled{false};
  EnableVerdict enable;
  std::vector<std::string> stubs;
};

// Constraint: throws env::WorldDecodeError on a malformed placed reference; resolved_base is picked at level with seed.
PlacedActorSpawn decode_placed_actor_spawn(const env::Environment& environment,
                                           const env::RecordRef& placed,
                                           std::uint16_t level,
                                           std::uint64_t seed,
                                           const EnableOverrideLookup& overrides = {});

// Constraint: level_modifier is echoed, not applied (meaning UNVERIFIED).
struct SpawnPlan {
  SpawnBaseKind base_kind{SpawnBaseKind::none};
  std::optional<FormKey> base;
  std::vector<FormKey> entries;
  LeveledPickReason reason{LeveledPickReason::nothing_live};
  std::size_t depth_used{0};
  std::optional<std::int32_t> level_modifier;
};

// Constraint: other base kinds give no entries and report not_spawnable.
SpawnPlan plan_script_spawn(const env::Environment& environment,
                            const FormKey& base,
                            std::uint16_t level,
                            std::uint64_t seed,
                            std::optional<std::int32_t> level_modifier = std::nullopt,
                            std::vector<std::string>* stubs = nullptr);

}  // namespace fo4::forms
