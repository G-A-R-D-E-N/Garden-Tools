#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4::actor_state {

// Invariant: the format version is 1; a blob of any other version is refused, since no earlier format exists.
inline constexpr std::uint32_t kFormatVersion = 1;
// Invariant: a blob starts with the four bytes "RASV", then u32 version and u32 payload size, all little-endian.
inline constexpr std::size_t kHeaderBytes = 12;
inline constexpr std::size_t kMaxStringBytes = 512;
inline constexpr std::size_t kMaxListEntries = 4096;

inline constexpr std::uint32_t kFlagEnabled = 1u << 0;
inline constexpr std::uint32_t kFlagDeleted = 1u << 1;
inline constexpr std::uint32_t kFlagDead = 1u << 2;

// Invariant: a plugin form is named by plugin file name and local id; an empty plugin is "none" and needs local id 0.
struct FormKey {
  std::string plugin;
  std::uint32_t local_id{0};

  bool empty() const noexcept { return plugin.empty(); }
  bool operator==(const FormKey&) const = default;
};

enum class RefKind : std::uint8_t {
  PluginReference = 0,
  CreatedReference = 1,
};

// Invariant: a PluginReference names its form; a CreatedReference names an engine-allocated id, its base and its spawn seed.
struct RefIdentity {
  RefKind kind{RefKind::PluginReference};
  FormKey form;
  std::uint32_t created_id{0};
  FormKey base;
  std::uint64_t spawn_seed{0};

  bool operator==(const RefIdentity&) const = default;
};

struct Transform {
  std::array<float, 3> position{};
  std::array<float, 3> rotation{};
  float scale{1.0f};

  bool operator==(const Transform&) const = default;
};

struct InventoryDelta {
  FormKey item;
  std::int32_t count{0};

  bool operator==(const InventoryDelta&) const = default;
};

struct ActorValueModifier {
  std::string actor_value;
  float value{0.0f};

  bool operator==(const ActorValueModifier&) const = default;
};

struct FactionRank {
  FormKey faction;
  std::int32_t rank{0};

  bool operator==(const FactionRank&) const = default;
};

struct ActorStateRecord {
  RefIdentity ref;
  std::uint32_t flags{kFlagEnabled};
  FormKey cell;
  Transform transform;
  std::vector<InventoryDelta> inventory;
  std::vector<ActorValueModifier> actor_values;
  std::vector<FactionRank> factions;
  FormKey forced_package;
  FormKey current_package;
  std::optional<RefIdentity> combat_target;
  std::int32_t bounty{0};
  std::uint16_t spawn_level{1};
  // Invariant: bytes after the known fields in the payload, kept verbatim so a re-encode writes them back.
  std::vector<std::uint8_t> unknown_tail;

  bool operator==(const ActorStateRecord&) const = default;
};

// Invariant: throws std::invalid_argument when a string, a list, a form key or a reference kind is outside what decode accepts; flag bits are kept as written.
std::vector<std::uint8_t> encode(const ActorStateRecord& record);

// Invariant: throws fo4::parse_error for any malformed, truncated or oversized input; it never reads past the input.
ActorStateRecord decode(std::span<const std::uint8_t> bytes);

}  // namespace fo4::actor_state
