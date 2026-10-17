#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

// source:xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:10286 NPC_, :3290 COED, :10387 PRKR; byte offsets must match those layouts
namespace fo4::forms {

class NpcError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct NpcFaction {
  FormKey faction{};
  std::int8_t rank{0};

  friend bool operator==(const NpcFaction&, const NpcFaction&) = default;
};

struct NpcPerk {
  FormKey perk{};
  std::uint8_t rank{0};

  friend bool operator==(const NpcPerk&, const NpcPerk&) = default;
};

struct NpcItemExtraData {
  std::optional<FormKey> owner;
  std::uint32_t global_or_rank{0};
  float condition{0.0f};

  friend bool operator==(const NpcItemExtraData&, const NpcItemExtraData&) = default;
};

struct NpcInventoryItem {
  FormKey item{};
  std::int32_t count{1};
  std::optional<NpcItemExtraData> extra_data;

  friend bool operator==(const NpcInventoryItem&, const NpcInventoryItem&) = default;
};

std::vector<NpcFaction> decode_npc_factions(const Plugin& plugin, const FormKey& key);
bool add_npc_faction(Plugin& plugin, const FormKey& key, const NpcFaction& value);
bool remove_npc_faction(Plugin& plugin, const FormKey& key, std::size_t index);
bool set_npc_faction(Plugin& plugin, const FormKey& key, std::size_t index, const NpcFaction& value);

std::vector<NpcPerk> decode_npc_perks(const Plugin& plugin, const FormKey& key);
bool add_npc_perk(Plugin& plugin, const FormKey& key, const NpcPerk& value);
bool remove_npc_perk(Plugin& plugin, const FormKey& key, std::size_t index);
bool set_npc_perk(Plugin& plugin, const FormKey& key, std::size_t index, const NpcPerk& value);

std::vector<NpcInventoryItem> decode_npc_inventory(const Plugin& plugin, const FormKey& key);
bool add_npc_inventory_item(Plugin& plugin, const FormKey& key, const NpcInventoryItem& value);
bool remove_npc_inventory_item(Plugin& plugin, const FormKey& key, std::size_t index);
bool set_npc_inventory_item(Plugin& plugin, const FormKey& key, std::size_t index, const NpcInventoryItem& value);

}  // namespace fo4::forms
