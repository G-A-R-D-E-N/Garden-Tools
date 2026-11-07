#include "fo4/forms/npc.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

using fo4::detail::find_mutable_record;
using fo4::detail::mark_record_modified;

constexpr auto k_npc = Signature::from_string("NPC_");
constexpr auto k_snam = Signature::from_string("SNAM");
constexpr auto k_prkz = Signature::from_string("PRKZ");
constexpr auto k_prkr = Signature::from_string("PRKR");
constexpr auto k_coct = Signature::from_string("COCT");
constexpr auto k_cnto = Signature::from_string("CNTO");
constexpr auto k_coed = Signature::from_string("COED");
constexpr auto k_fact = Signature::from_string("FACT");
constexpr auto k_perk = Signature::from_string("PERK");

constexpr std::array k_faction_run{k_snam};
constexpr std::array k_perk_run{k_prkz, k_prkr};
constexpr std::array k_inventory_run{k_coct, k_cnto, k_coed};

constexpr std::array k_order{
    Signature::from_string("ACBS"), k_snam,
    Signature::from_string("INAM"), Signature::from_string("VTCK"),
    Signature::from_string("TPLT"), Signature::from_string("LTPT"),
    Signature::from_string("LTPC"), Signature::from_string("TPTA"),
    Signature::from_string("RNAM"), Signature::from_string("SPCT"),
    Signature::from_string("SPLO"), Signature::from_string("DEST"),
    Signature::from_string("DAMC"), Signature::from_string("DSTD"),
    Signature::from_string("DSTA"), Signature::from_string("DMDL"),
    Signature::from_string("DMDT"), Signature::from_string("DMDC"),
    Signature::from_string("DMDS"), Signature::from_string("DSTF"),
    Signature::from_string("WNAM"), Signature::from_string("ANAM"),
    Signature::from_string("ATKR"), Signature::from_string("ATKD"),
    Signature::from_string("ATKE"), Signature::from_string("ATKW"),
    Signature::from_string("ATKS"), Signature::from_string("ATKT"),
    Signature::from_string("SPOR"),
    Signature::from_string("OCOR"), Signature::from_string("GWOR"),
    Signature::from_string("ECOR"), Signature::from_string("FCPL"),
    Signature::from_string("RCLR"), k_prkz,
    k_prkr, Signature::from_string("PRPS"),
    Signature::from_string("FTYP"), Signature::from_string("NTRM"),
    k_coct, k_cnto,
    k_coed, Signature::from_string("AIDT"),
    Signature::from_string("PKID"),
};

template <typename T>
T read_at(const std::vector<std::byte>& data, std::size_t offset) {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

template <typename T>
void write_at(std::vector<std::byte>& data, std::size_t offset, T value) {
  std::memcpy(data.data() + offset, &value, sizeof(T));
}

Subrecord make_subrecord(Signature signature, std::size_t size) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.assign(size, std::byte{0});
  return subrecord;
}

void require_size(const Subrecord& subrecord, std::size_t size) {
  if (subrecord.data.size() != size) {
    throw NpcError(subrecord.signature.to_string() + " must be exactly " +
                   std::to_string(size) + " bytes");
  }
}

FormKey resolve_non_null(std::uint32_t raw_value,
                         const PluginFormIdContext& context,
                         std::string_view label) {
  const RawFormId raw{raw_value};
  if (raw.is_null()) {
    throw NpcError(std::string(label) + " must not be null");
  }
  try {
    return resolve_plugin_form_id(raw, context);
  } catch (const FormIdError& error) {
    throw NpcError(std::string(label) + ": " + error.what());
  }
}

std::uint32_t encode_non_null(const FormKey& key,
                              const PluginFormIdContext& context,
                              std::string_view label) {
  if (key.owner().empty()) {
    throw NpcError(std::string(label) + " must reference a record");
  }
  try {
    const auto raw = encode_plugin_form_id(key, context);
    if (raw.is_null()) {
      throw NpcError(std::string(label) + " cannot encode to a null form ID");
    }
    return raw.value();
  } catch (const FormIdError& error) {
    throw NpcError(std::string(label) + ": " + error.what());
  }
}

const Record& npc_record(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    throw NpcError("NPC_ record not found: " + key.to_string());
  }
  if (record->header.signature != k_npc) {
    throw NpcError("record is not NPC_: " + key.to_string());
  }
  return *record;
}

void require_editable(const Plugin& plugin, const FormKey& key) {
  require_decodable(npc_record(plugin, key));
}

void require_local_target(const Plugin& plugin,
                          const FormKey& target,
                          Signature expected,
                          std::string_view label) {
  if (target.owner() != plugin.key) return;
  const auto* record = find_record(plugin, target);
  if (record == nullptr) {
    throw NpcError(std::string(label) + " must resolve to a record in this plugin: " +
                   target.to_string());
  }
  if (record->header.signature != expected) {
    throw NpcError(std::string(label) + " target " + target.to_string() + " is " +
                   record->header.signature.to_string() + ", not " + expected.to_string());
  }
}

void require_local_base_object(const Plugin& plugin, const FormKey& item) {
  if (item.owner() != plugin.key) return;
  const auto* record = find_record(plugin, item);
  if (record == nullptr) {
    throw NpcError("CNTO item must resolve to a record in this plugin: " + item.to_string());
  }
  if (!schema::is_sig_base_object(record->header.signature)) {
    throw NpcError("CNTO item target " + item.to_string() + " is " +
                   record->header.signature.to_string() + ", which is not a base object");
  }
}

void require_local_coed_owner(const Plugin& plugin, const FormKey& owner) {
  if (owner.owner() != plugin.key) return;
  const auto* record = find_record(plugin, owner);
  if (record == nullptr) {
    throw NpcError("COED owner must resolve to a record in this plugin: " + owner.to_string());
  }
  const auto signature = record->header.signature;
  if (signature != k_npc && signature != k_fact) {
    throw NpcError("COED owner target " + owner.to_string() + " is " + signature.to_string() +
                   ", not NPC_ or FACT");
  }
}

void check_declared_count(const std::optional<std::uint32_t>& declared,
                          std::size_t entries,
                          std::string_view count_label,
                          std::string_view entry_label) {
  if (declared.has_value() && static_cast<std::size_t>(*declared) != entries) {
    throw NpcError(std::string(count_label) + " does not match the number of " +
                   std::string(entry_label) + " entries");
  }
}

std::optional<std::size_t> order_rank(Signature signature) {
  const auto found = std::find(k_order.begin(), k_order.end(), signature);
  if (found == k_order.end()) return std::nullopt;
  return static_cast<std::size_t>(found - k_order.begin());
}

std::size_t insertion_index(const std::vector<Subrecord>& subrecords, Signature anchor) {
  const auto anchor_rank = *order_rank(anchor);
  std::optional<std::size_t> after;
  for (std::size_t index = 0; index < subrecords.size(); ++index) {
    const auto rank = order_rank(subrecords[index].signature);
    if (rank.has_value() && *rank < anchor_rank) after = index + 1;
  }
  if (!after.has_value()) {
    throw NpcError("NPC_ has no subrecord before " + anchor.to_string() +
                   " to insert after");
  }
  return *after;
}

bool same_subrecord(const Subrecord& left, const Subrecord& right) {
  return left.signature == right.signature && left.data == right.data;
}

template <typename Signatures>
bool replace_run(Plugin& plugin,
                 const FormKey& key,
                 const Signatures& owned,
                 Signature anchor,
                 std::vector<Subrecord> encoded) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) {
    throw NpcError("NPC_ record not found: " + key.to_string());
  }
  if (record->header.signature != k_npc) {
    throw NpcError("record is not NPC_: " + key.to_string());
  }
  require_decodable(*record);

  auto& subrecords = record->subrecords;
  std::optional<std::size_t> first;
  std::optional<std::size_t> last;
  std::size_t owned_count = 0;
  for (std::size_t index = 0; index < subrecords.size(); ++index) {
    const auto signature = subrecords[index].signature;
    if (std::find(owned.begin(), owned.end(), signature) == owned.end()) continue;
    if (!first.has_value()) first = index;
    last = index;
    ++owned_count;
  }
  if (first.has_value() && owned_count != *last - *first + 1) {
    throw NpcError("NPC_ " + anchor.to_string() + " entries are not contiguous");
  }

  const std::size_t start = first.has_value() ? *first : insertion_index(subrecords, anchor);
  const std::size_t stop = first.has_value() ? *last + 1 : start;
  const bool unchanged =
      encoded.size() == stop - start &&
      std::equal(encoded.begin(), encoded.end(),
                 subrecords.begin() + static_cast<std::ptrdiff_t>(start), same_subrecord);
  if (unchanged) return false;

  subrecords.erase(subrecords.begin() + static_cast<std::ptrdiff_t>(start),
                   subrecords.begin() + static_cast<std::ptrdiff_t>(stop));
  subrecords.insert(subrecords.begin() + static_cast<std::ptrdiff_t>(start),
                    encoded.begin(), encoded.end());
  mark_record_modified(plugin, *record);
  return true;
}

template <typename Entry>
void require_index(const std::vector<Entry>& entries, std::size_t index, std::string_view label) {
  if (index >= entries.size()) {
    throw NpcError(std::string(label) + " index " + std::to_string(index) +
                   " is out of range");
  }
}

std::vector<Subrecord> encode_factions(const std::vector<NpcFaction>& entries,
                                       const PluginFormIdContext& context) {
  std::vector<Subrecord> encoded;
  for (const auto& entry : entries) {
    auto subrecord = make_subrecord(k_snam, 5);
    write_at(subrecord.data, 0, encode_non_null(entry.faction, context, "SNAM faction"));
    write_at(subrecord.data, 4, entry.rank);
    encoded.push_back(std::move(subrecord));
  }
  return encoded;
}

std::vector<Subrecord> encode_perks(const std::vector<NpcPerk>& entries,
                                    const PluginFormIdContext& context) {
  std::vector<Subrecord> encoded;
  auto count = make_subrecord(k_prkz, 4);
  write_at(count.data, 0, static_cast<std::uint32_t>(entries.size()));
  encoded.push_back(std::move(count));
  for (const auto& entry : entries) {
    auto subrecord = make_subrecord(k_prkr, 5);
    write_at(subrecord.data, 0, encode_non_null(entry.perk, context, "PRKR perk"));
    write_at(subrecord.data, 4, entry.rank);
    encoded.push_back(std::move(subrecord));
  }
  return encoded;
}

std::vector<Subrecord> encode_inventory(const std::vector<NpcInventoryItem>& entries,
                                        const PluginFormIdContext& context) {
  std::vector<Subrecord> encoded;
  auto count = make_subrecord(k_coct, 4);
  write_at(count.data, 0, static_cast<std::uint32_t>(entries.size()));
  encoded.push_back(std::move(count));
  for (const auto& entry : entries) {
    auto item = make_subrecord(k_cnto, 8);
    write_at(item.data, 0, encode_non_null(entry.item, context, "CNTO item"));
    write_at(item.data, 4, entry.count);
    encoded.push_back(std::move(item));
    if (!entry.extra_data.has_value()) continue;
    const auto& extra = *entry.extra_data;
    auto coed = make_subrecord(k_coed, 12);
    write_at(coed.data, 0,
             extra.owner.has_value()
                 ? encode_non_null(*extra.owner, context, "COED owner")
                 : std::uint32_t{0});
    write_at(coed.data, 4, extra.global_or_rank);
    write_at(coed.data, 8, extra.condition);
    encoded.push_back(std::move(coed));
  }
  return encoded;
}

bool write_factions(Plugin& plugin, const FormKey& key, const std::vector<NpcFaction>& entries) {
  const auto context = plugin_form_id_context(plugin);
  return replace_run(plugin, key, k_faction_run, k_snam, encode_factions(entries, context));
}

bool write_perks(Plugin& plugin, const FormKey& key, const std::vector<NpcPerk>& entries) {
  const auto context = plugin_form_id_context(plugin);
  return replace_run(plugin, key, k_perk_run, k_prkz, encode_perks(entries, context));
}

bool write_inventory(Plugin& plugin, const FormKey& key, const std::vector<NpcInventoryItem>& entries) {
  const auto context = plugin_form_id_context(plugin);
  return replace_run(plugin, key, k_inventory_run, k_coct, encode_inventory(entries, context));
}

}  // namespace

std::vector<NpcFaction> decode_npc_factions(const Plugin& plugin, const FormKey& key) {
  const auto& record = npc_record(plugin, key);
  const auto context = plugin_form_id_context(plugin);
  std::vector<NpcFaction> result;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != k_snam) continue;
    require_size(subrecord, 5);
    NpcFaction entry;
    entry.faction = resolve_non_null(read_at<std::uint32_t>(subrecord.data, 0), context, "SNAM faction");
    entry.rank = read_at<std::int8_t>(subrecord.data, 4);
    result.push_back(std::move(entry));
  }
  return result;
}

bool add_npc_faction(Plugin& plugin, const FormKey& key, const NpcFaction& value) {
  require_editable(plugin, key);
  require_local_target(plugin, value.faction, k_fact, "SNAM faction");
  auto entries = decode_npc_factions(plugin, key);
  entries.push_back(value);
  return write_factions(plugin, key, entries);
}

bool remove_npc_faction(Plugin& plugin, const FormKey& key, std::size_t index) {
  require_editable(plugin, key);
  auto entries = decode_npc_factions(plugin, key);
  require_index(entries, index, "SNAM");
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
  return write_factions(plugin, key, entries);
}

bool set_npc_faction(Plugin& plugin, const FormKey& key, std::size_t index, const NpcFaction& value) {
  require_editable(plugin, key);
  require_local_target(plugin, value.faction, k_fact, "SNAM faction");
  auto entries = decode_npc_factions(plugin, key);
  require_index(entries, index, "SNAM");
  entries[index] = value;
  return write_factions(plugin, key, entries);
}

std::vector<NpcPerk> decode_npc_perks(const Plugin& plugin, const FormKey& key) {
  const auto& record = npc_record(plugin, key);
  const auto context = plugin_form_id_context(plugin);
  std::optional<std::uint32_t> declared;
  std::vector<NpcPerk> result;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_prkz) {
      require_size(subrecord, 4);
      if (declared.has_value()) throw NpcError("NPC_ contains duplicate PRKZ subrecords");
      declared = read_at<std::uint32_t>(subrecord.data, 0);
    } else if (subrecord.signature == k_prkr) {
      require_size(subrecord, 5);
      NpcPerk entry;
      entry.perk = resolve_non_null(read_at<std::uint32_t>(subrecord.data, 0), context, "PRKR perk");
      entry.rank = read_at<std::uint8_t>(subrecord.data, 4);
      result.push_back(std::move(entry));
    }
  }
  check_declared_count(declared, result.size(), "PRKZ", "PRKR");
  return result;
}

bool add_npc_perk(Plugin& plugin, const FormKey& key, const NpcPerk& value) {
  require_editable(plugin, key);
  require_local_target(plugin, value.perk, k_perk, "PRKR perk");
  auto entries = decode_npc_perks(plugin, key);
  entries.push_back(value);
  return write_perks(plugin, key, entries);
}

bool remove_npc_perk(Plugin& plugin, const FormKey& key, std::size_t index) {
  require_editable(plugin, key);
  auto entries = decode_npc_perks(plugin, key);
  require_index(entries, index, "PRKR");
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
  return write_perks(plugin, key, entries);
}

bool set_npc_perk(Plugin& plugin, const FormKey& key, std::size_t index, const NpcPerk& value) {
  require_editable(plugin, key);
  require_local_target(plugin, value.perk, k_perk, "PRKR perk");
  auto entries = decode_npc_perks(plugin, key);
  require_index(entries, index, "PRKR");
  entries[index] = value;
  return write_perks(plugin, key, entries);
}

std::vector<NpcInventoryItem> decode_npc_inventory(const Plugin& plugin, const FormKey& key) {
  const auto& record = npc_record(plugin, key);
  const auto context = plugin_form_id_context(plugin);
  std::optional<std::uint32_t> declared;
  std::vector<NpcInventoryItem> result;
  bool previous_was_cnto = false;
  for (const auto& subrecord : record.subrecords) {
    const bool follows_cnto = previous_was_cnto;
    previous_was_cnto = false;
    if (subrecord.signature == k_coct) {
      require_size(subrecord, 4);
      if (declared.has_value()) throw NpcError("NPC_ contains duplicate COCT subrecords");
      declared = read_at<std::uint32_t>(subrecord.data, 0);
    } else if (subrecord.signature == k_cnto) {
      require_size(subrecord, 8);
      NpcInventoryItem entry;
      entry.item = resolve_non_null(read_at<std::uint32_t>(subrecord.data, 0), context, "CNTO item");
      entry.count = read_at<std::int32_t>(subrecord.data, 4);
      result.push_back(std::move(entry));
      previous_was_cnto = true;
    } else if (subrecord.signature == k_coed) {
      require_size(subrecord, 12);
      if (!follows_cnto) throw NpcError("COED must directly follow CNTO");
      NpcItemExtraData extra;
      const auto owner = read_at<std::uint32_t>(subrecord.data, 0);
      if (owner != 0) extra.owner = resolve_non_null(owner, context, "COED owner");
      extra.global_or_rank = read_at<std::uint32_t>(subrecord.data, 4);
      extra.condition = read_at<float>(subrecord.data, 8);
      result.back().extra_data = extra;
    }
  }
  return result;
}

void require_inventory_target(const Plugin& plugin, const NpcInventoryItem& value) {
  require_local_base_object(plugin, value.item);
  if (value.extra_data.has_value() && value.extra_data->owner.has_value()) {
    require_local_coed_owner(plugin, *value.extra_data->owner);
  }
}

bool add_npc_inventory_item(Plugin& plugin, const FormKey& key, const NpcInventoryItem& value) {
  require_editable(plugin, key);
  require_inventory_target(plugin, value);
  auto entries = decode_npc_inventory(plugin, key);
  entries.push_back(value);
  return write_inventory(plugin, key, entries);
}

bool remove_npc_inventory_item(Plugin& plugin, const FormKey& key, std::size_t index) {
  require_editable(plugin, key);
  auto entries = decode_npc_inventory(plugin, key);
  require_index(entries, index, "CNTO");
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
  return write_inventory(plugin, key, entries);
}

bool set_npc_inventory_item(Plugin& plugin, const FormKey& key, std::size_t index, const NpcInventoryItem& value) {
  require_editable(plugin, key);
  require_inventory_target(plugin, value);
  auto entries = decode_npc_inventory(plugin, key);
  require_index(entries, index, "CNTO");
  entries[index] = value;
  return write_inventory(plugin, key, entries);
}

}  // namespace fo4::forms
