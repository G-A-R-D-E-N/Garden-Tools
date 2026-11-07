// Layouts and sources: Radicle/docs/LEVELED_LISTS.md (TODO-verify until LeveledListReal runs on CI).
#include "fo4/forms/leveled_list.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/preflight.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr std::uint16_t k_entry_chance_form_version = 69;

constexpr auto k_lvli = Signature::from_string("LVLI");
constexpr auto k_lvln = Signature::from_string("LVLN");
constexpr auto k_lvsp = Signature::from_string("LVSP");
constexpr auto k_lvld = Signature::from_string("LVLD");
constexpr auto k_lvlm = Signature::from_string("LVLM");
constexpr auto k_lvlf = Signature::from_string("LVLF");
constexpr auto k_lvlg = Signature::from_string("LVLG");
constexpr auto k_lvsg = Signature::from_string("LVSG");
constexpr auto k_llct = Signature::from_string("LLCT");
constexpr auto k_lvlo = Signature::from_string("LVLO");
constexpr auto k_coed = Signature::from_string("COED");
constexpr auto k_onam = Signature::from_string("ONAM");
constexpr auto k_obnd = Signature::from_string("OBND");

using fo4::detail::find_mutable_record;
using fo4::detail::mark_record_modified;

bool is_leveled_list(Signature signature) {
  return signature == k_lvli || signature == k_lvln || signature == k_lvsp;
}

bool is_header(Signature signature) {
  return signature == k_lvld || signature == k_lvlm || signature == k_lvlf || signature == k_lvlg;
}

std::uint8_t read_single_byte(const Subrecord& subrecord, std::string_view field_name) {
  if (subrecord.data.size() != 1) {
    throw LeveledListError(std::string(field_name) + " must be exactly one byte");
  }
  return std::to_integer<std::uint8_t>(subrecord.data.front());
}

std::uint16_t read_u16(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

float read_f32(const std::vector<std::byte>& data, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_f32(std::vector<std::byte>& data, std::size_t offset, float value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

Subrecord make_subrecord(Signature signature, std::size_t size) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.resize(size);
  return subrecord;
}

Subrecord byte_subrecord(Signature signature, std::uint8_t value) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.push_back(std::byte{value});
  return subrecord;
}

Subrecord count_subrecord(std::size_t count) {
  if (count > 0xFFu) {
    throw LeveledListError("leveled list cannot contain more than 255 LVLO entries");
  }
  return byte_subrecord(k_llct, static_cast<std::uint8_t>(count));
}

void require_once(bool& seen, Signature signature) {
  if (seen) {
    throw LeveledListError("leveled list contains duplicate " + signature.to_string() + " subrecords");
  }
  seen = true;
}

FormKey resolve_form_key(RawFormId raw, const PluginFormIdContext& context, std::string_view what) {
  try {
    return resolve_plugin_form_id(raw, context);
  } catch (const FormIdError& error) {
    throw LeveledListError("invalid " + std::string(what) + " FormID: " + error.what());
  }
}

RawFormId encode_form_key(const FormKey& key, const PluginFormIdContext& context, std::string_view what) {
  try {
    return encode_plugin_form_id(key, context);
  } catch (const FormIdError& error) {
    throw LeveledListError("cannot encode " + std::string(what) + " FormKey: " + error.what());
  }
}

std::optional<FormKey> decode_optional_form_key(const Subrecord& subrecord,
                                                const PluginFormIdContext& context,
                                                std::string_view what) {
  if (subrecord.data.size() != 4) {
    throw LeveledListError(std::string(what) + " must be exactly four bytes");
  }
  const auto raw = RawFormId{read_u32(subrecord.data, 0)};
  if (raw.is_null()) return std::nullopt;
  return resolve_form_key(raw, context, what);
}

LeveledListEntry decode_lvlo(const Subrecord& subrecord, const PluginFormIdContext& context) {
  if (subrecord.data.size() != 12) {
    throw LeveledListError("leveled list LVLO entry must be exactly 12 bytes");
  }
  LeveledListEntry entry;
  entry.level = read_u16(subrecord.data, 0);
  entry.reserved_before_form_id = {subrecord.data[2], subrecord.data[3]};
  const auto raw = RawFormId{read_u32(subrecord.data, 4)};
  if (raw.is_null()) {
    throw LeveledListError("leveled list LVLO entry contains a null FormID");
  }
  entry.item = resolve_form_key(raw, context, "leveled list entry");
  entry.count = read_u16(subrecord.data, 8);
  entry.chance_none = std::to_integer<std::uint8_t>(subrecord.data[10]);
  entry.reserved_tail = subrecord.data[11];
  return entry;
}

Subrecord encode_lvlo(const LeveledListEntry& entry, const PluginFormIdContext& context) {
  if (entry.item.owner().empty()) {
    throw LeveledListError("leveled list entry must reference a FormKey");
  }
  const auto raw = encode_form_key(entry.item, context, "leveled list entry");
  if (raw.is_null()) {
    throw LeveledListError("leveled list entry cannot encode to a null FormID");
  }
  auto subrecord = make_subrecord(k_lvlo, 12);
  write_u16(subrecord.data, 0, entry.level);
  subrecord.data[2] = entry.reserved_before_form_id[0];
  subrecord.data[3] = entry.reserved_before_form_id[1];
  write_u32(subrecord.data, 4, raw.value());
  write_u16(subrecord.data, 8, entry.count);
  subrecord.data[10] = std::byte{entry.chance_none};
  subrecord.data[11] = entry.reserved_tail;
  return subrecord;
}

LeveledListExtraData decode_coed(const Subrecord& subrecord, const PluginFormIdContext& context) {
  if (subrecord.data.size() != 12) {
    throw LeveledListError("leveled list COED must be exactly 12 bytes");
  }
  LeveledListExtraData extra;
  const auto owner = RawFormId{read_u32(subrecord.data, 0)};
  if (!owner.is_null()) {
    extra.owner = resolve_form_key(owner, context, "leveled list COED owner");
  }
  extra.global_or_rank = read_u32(subrecord.data, 4);
  extra.item_condition = read_f32(subrecord.data, 8);
  return extra;
}

Subrecord encode_coed(const LeveledListExtraData& extra, const PluginFormIdContext& context) {
  auto subrecord = make_subrecord(k_coed, 12);
  if (!extra.owner.owner().empty()) {
    write_u32(subrecord.data, 0,
              encode_form_key(extra.owner, context, "leveled list COED owner").value());
  }
  write_u32(subrecord.data, 4, extra.global_or_rank);
  write_f32(subrecord.data, 8, extra.item_condition);
  return subrecord;
}

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw LeveledListError("leveled list editor ID cannot contain an embedded NUL");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  data.push_back(std::byte{0});
  return data;
}

constexpr std::string_view k_lvli_targets[] = {
    "ACTI", "ADDN", "ALCH", "AMMO", "ARMO", "ARTO", "ASPC", "BNDS", "BOOK", "CMPO", "COBJ",
    "CONT", "DEBR", "DOOR", "EXPL", "FLST", "FLOR", "FURN", "HAZD", "IDLM", "INGR", "KEYM",
    "LIGH", "LVLI", "LVLN", "LVSP", "MISC", "MSTT", "NOTE", "NPC_", "OMOD", "PROJ", "SCOL",
    "SCRL", "SOUN", "SPEL", "STAT", "TACT", "TERM", "TREE", "TXST", "WATR", "WEAP", "ENCH",
};
constexpr std::string_view k_lvln_targets[] = {"LVLN", "NPC_"};
constexpr std::string_view k_lvsp_targets[] = {"LVSP", "SPEL"};

std::span<const std::string_view> allowed_entry_targets(Signature list) {
  if (list == k_lvln) return k_lvln_targets;
  if (list == k_lvsp) return k_lvsp_targets;
  return k_lvli_targets;
}

void validate_entry_target(const Plugin& plugin, Signature list, const LeveledListEntry& entry) {
  if (entry.item.owner() != plugin.key) return;
  const auto* target = find_record(plugin, entry.item);
  if (target == nullptr) {
    throw LeveledListError("leveled list entry does not resolve to a record in this plugin: " +
                           entry.item.to_string());
  }
  const auto target_type = target->header.signature.to_string();
  const auto allowed = allowed_entry_targets(list);
  if (std::none_of(allowed.begin(), allowed.end(),
                   [&](std::string_view name) { return name == target_type; })) {
    throw LeveledListError(entry.item.to_string() + " is " + target_type + ", which " +
                           list.to_string() + " entries cannot reference");
  }
}

bool zeroes_chance_none(const Record& record) {
  return record.header.form_version < k_entry_chance_form_version &&
         (record.header.signature == k_lvli || record.header.signature == k_lvln);
}

using EntrySortKey = std::tuple<std::uint16_t, std::uint32_t, std::uint16_t>;

EntrySortKey entry_sort_key(const LeveledListEntry& entry, const PluginFormIdContext& context) {
  return {entry.level, encode_form_key(entry.item, context, "leveled list entry").value(), entry.count};
}

std::vector<LeveledListEntry> sorted_entries(std::span<const LeveledListEntry> entries,
                                             const PluginFormIdContext& context) {
  std::vector<LeveledListEntry> ordered(entries.begin(), entries.end());
  std::stable_sort(ordered.begin(), ordered.end(),
                   [&](const LeveledListEntry& left, const LeveledListEntry& right) {
                     return entry_sort_key(left, context) < entry_sort_key(right, context);
                   });
  return ordered;
}

void require_editable(const Plugin& plugin, const FormKey& key) {
  if (const auto* record = find_record(plugin, key)) require_decodable(*record);
}

}  // namespace

LeveledList decode_leveled_list(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    throw LeveledListError("leveled list record not found: " + key.to_string());
  }
  if (!is_leveled_list(record->header.signature)) {
    throw LeveledListError("record is not LVLI, LVLN, or LVSP: " + key.to_string());
  }

  LeveledList result;
  result.key = key;
  const auto context = plugin_form_id_context(plugin);
  const bool chance_is_zeroed = zeroes_chance_none(*record);
  bool saw_lvld = false;
  bool saw_lvlm = false;
  bool saw_lvlf = false;
  bool saw_lvlg = false;
  bool saw_lvsg = false;
  bool saw_onam = false;
  bool saw_llct = false;
  std::uint8_t declared_count = 0;
  bool after_entry = false;

  for (const auto& subrecord : record->subrecords) {
    const bool follows_entry = after_entry;
    after_entry = false;
    const auto signature = subrecord.signature;
    if (signature == k_lvlo) {
      result.entries.push_back(decode_lvlo(subrecord, context));
      if (chance_is_zeroed) result.entries.back().chance_none = 0;
      after_entry = true;
    } else if (signature == k_coed) {
      if (!follows_entry || result.entries.back().extra.has_value()) {
        throw LeveledListError("leveled list COED must follow an LVLO entry");
      }
      result.entries.back().extra = decode_coed(subrecord, context);
    } else if (signature == k_lvld) {
      require_once(saw_lvld, signature);
      result.chance_none = read_single_byte(subrecord, "LVLD");
    } else if (signature == k_lvlm) {
      require_once(saw_lvlm, signature);
      result.max_count = read_single_byte(subrecord, "LVLM");
    } else if (signature == k_lvlf) {
      require_once(saw_lvlf, signature);
      result.flags = read_single_byte(subrecord, "LVLF");
    } else if (signature == k_lvlg) {
      require_once(saw_lvlg, signature);
      result.use_global = decode_optional_form_key(subrecord, context, "LVLG");
    } else if (signature == k_lvsg) {
      require_once(saw_lvsg, signature);
      result.epic_loot_chance = decode_optional_form_key(subrecord, context, "LVSG");
    } else if (signature == k_onam) {
      require_once(saw_onam, signature);
      result.override_name =
          schema::read_localized_string(plugin, record->header.signature, k_onam, subrecord);
    } else if (signature == k_llct) {
      require_once(saw_llct, signature);
      declared_count = read_single_byte(subrecord, "LLCT");
    }
  }

  if (!saw_llct && !result.entries.empty()) {
    throw LeveledListError("leveled list has LVLO entries but no LLCT subrecord");
  }
  if (saw_llct && static_cast<std::size_t>(declared_count) != result.entries.size()) {
    throw LeveledListError("leveled list LLCT does not match the number of LVLO entries");
  }
  return result;
}

FormKey create_leveled_list(Plugin& plugin,
                            Signature signature,
                            std::optional<std::string_view> editor_id,
                            std::uint8_t chance_none,
                            std::uint8_t max_count,
                            std::uint8_t flags) {
  if (!is_leveled_list(signature)) {
    throw LeveledListError("leveled list creation requires LVLI, LVLN, or LVSP");
  }
  if ((flags & ~0x07u) != 0) {
    throw LeveledListError("leveled list flags may use only defined bits 0x01, 0x02, and 0x04");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  std::vector<Subrecord> subrecords;
  if (editor_id.has_value()) {
    subrecords.push_back(Subrecord{sig_edid, zstring(stored_editor_id(*editor_id)), false});
  }
  subrecords.push_back(make_subrecord(k_obnd, 12));
  subrecords.push_back(byte_subrecord(k_lvld, chance_none));
  subrecords.push_back(byte_subrecord(k_lvlm, max_count));
  subrecords.push_back(byte_subrecord(k_lvlf, flags));
  subrecords.push_back(count_subrecord(0));

  const auto key = create_local_record(next, signature, std::move(subrecords));
  for (const auto& issue : schema::preflight_authoring_record(next, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw LeveledListError(signature.to_string() + " authoring preflight failed: " + issue.message);
    }
  }
  plugin = std::move(next);
  return key;
}

bool reset_leveled_list(Plugin& plugin, const FormKey& key) {
  assign_record_form_keys(plugin);
  require_editable(plugin, key);
  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) return false;
  if (!is_leveled_list(record->header.signature)) {
    throw LeveledListError("leveled list reset requires LVLI, LVLN, or LVSP");
  }

  std::vector<Subrecord> next;
  next.push_back(make_subrecord(k_obnd, 12));
  next.push_back(byte_subrecord(k_lvld, 0));
  next.push_back(byte_subrecord(k_lvlf, 0));
  next.push_back(count_subrecord(0));

  record->subrecords = std::move(next);
  mark_record_modified(plugin, *record);
  return true;
}

bool replace_leveled_list_entries(Plugin& plugin,
                                  const FormKey& key,
                                  std::span<const LeveledListEntry> entries) {
  if (entries.size() > 0xFFu) {
    throw LeveledListError("leveled list cannot contain more than 255 LVLO entries");
  }

  assign_record_form_keys(plugin);
  require_editable(plugin, key);
  const auto current = decode_leveled_list(plugin, key);
  if (current.entries.size() == entries.size() &&
      std::equal(current.entries.begin(), current.entries.end(), entries.begin())) {
    return true;
  }

  const auto list = find_record(plugin, key)->header.signature;
  for (const auto& entry : entries) {
    if (list == k_lvsp && entry.extra.has_value()) {
      throw LeveledListError("LVSP entries cannot carry COED extra data");
    }
    if (std::none_of(current.entries.begin(), current.entries.end(),
                     [&](const LeveledListEntry& known) { return known.item == entry.item; })) {
      validate_entry_target(plugin, list, entry);
    }
  }

  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) return false;
  const auto context = plugin_form_id_context(plugin);
  auto ordered = sorted_entries(entries, context);
  if (zeroes_chance_none(*record)) {
    for (auto& entry : ordered) entry.chance_none = 0;
  }

  std::vector<Subrecord> encoded;
  for (const auto& entry : ordered) {
    encoded.push_back(encode_lvlo(entry, context));
    if (entry.extra.has_value()) {
      encoded.push_back(encode_coed(*entry.extra, context));
    }
  }

  bool has_llct = false;
  bool has_lvlo = false;
  std::optional<std::size_t> last_header;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    const auto signature = record->subrecords[index].signature;
    has_llct = has_llct || signature == k_llct;
    has_lvlo = has_lvlo || signature == k_lvlo;
    if (is_header(signature)) last_header = index;
  }

  std::vector<Subrecord> next;
  next.reserve(record->subrecords.size() + encoded.size() + 1);
  bool placed = false;
  auto place = [&] {
    if (!has_llct) next.push_back(count_subrecord(entries.size()));
    next.insert(next.end(), encoded.begin(), encoded.end());
    placed = true;
  };

  bool previous_lvlo = false;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    const auto& subrecord = record->subrecords[index];
    const bool entry_extra = previous_lvlo && subrecord.signature == k_coed;
    previous_lvlo = subrecord.signature == k_lvlo;
    if (entry_extra) continue;
    if (subrecord.signature == k_lvlo) {
      if (!placed) place();
      continue;
    }
    if (subrecord.signature == k_llct) {
      next.push_back(count_subrecord(entries.size()));
      if (!has_lvlo && !placed) place();
      continue;
    }
    next.push_back(subrecord);
    if (!placed && !has_llct && !has_lvlo && last_header == index) place();
  }
  if (!placed) place();

  record->subrecords = std::move(next);
  mark_record_modified(plugin, *record);
  return true;
}

bool insert_leveled_list_entry(Plugin& plugin,
                               const FormKey& key,
                               std::size_t index,
                               const LeveledListEntry& entry) {
  assign_record_form_keys(plugin);
  require_editable(plugin, key);
  auto entries = decode_leveled_list(plugin, key).entries;
  if (index > entries.size()) {
    throw LeveledListError("leveled list entry index is out of range");
  }
  entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(index), entry);
  return replace_leveled_list_entries(plugin, key, entries);
}

bool remove_leveled_list_entry(Plugin& plugin,
                               const FormKey& key,
                               std::size_t index) {
  assign_record_form_keys(plugin);
  require_editable(plugin, key);
  auto entries = decode_leveled_list(plugin, key).entries;
  if (index >= entries.size()) {
    throw LeveledListError("leveled list entry index is out of range");
  }
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
  return replace_leveled_list_entries(plugin, key, entries);
}

bool set_leveled_list_entry(Plugin& plugin,
                            const FormKey& key,
                            std::size_t index,
                            const LeveledListEntry& entry) {
  assign_record_form_keys(plugin);
  require_editable(plugin, key);
  auto entries = decode_leveled_list(plugin, key).entries;
  if (index >= entries.size()) {
    throw LeveledListError("leveled list entry index is out of range");
  }
  entries[index] = entry;
  return replace_leveled_list_entries(plugin, key, entries);
}

bool set_leveled_list_settings(Plugin& plugin,
                               const FormKey& key,
                               std::uint8_t chance_none,
                               std::uint8_t max_count,
                               std::uint8_t flags) {
  if ((flags & ~0x07u) != 0) {
    throw LeveledListError("leveled list flags may use only defined bits 0x01, 0x02, and 0x04");
  }
  assign_record_form_keys(plugin);
  require_editable(plugin, key);
  const auto current = decode_leveled_list(plugin, key);
  if (current.chance_none == chance_none && current.max_count == max_count &&
      current.flags == flags) {
    return true;
  }

  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) return false;

  bool saw_lvld = false;
  bool saw_lvlm = false;
  bool saw_lvlf = false;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == k_lvld) {
      saw_lvld = true;
    } else if (subrecord.signature == k_lvlm) {
      saw_lvlm = true;
    } else if (subrecord.signature == k_lvlf) {
      saw_lvlf = true;
    }
  }

  std::vector<Subrecord> next;
  next.reserve(record->subrecords.size() + 3);
  bool inserted_missing = false;
  auto insert_missing = [&] {
    if (inserted_missing) return;
    if (!saw_lvld) next.push_back(byte_subrecord(k_lvld, chance_none));
    if (!saw_lvlm) next.push_back(byte_subrecord(k_lvlm, max_count));
    if (!saw_lvlf) next.push_back(byte_subrecord(k_lvlf, flags));
    inserted_missing = true;
  };

  for (const auto& subrecord : record->subrecords) {
    if (!inserted_missing &&
        (subrecord.signature == k_llct || subrecord.signature == k_lvlo)) {
      insert_missing();
    }
    if (subrecord.signature == k_lvld) {
      next.push_back(byte_subrecord(k_lvld, chance_none));
    } else if (subrecord.signature == k_lvlm) {
      next.push_back(byte_subrecord(k_lvlm, max_count));
    } else if (subrecord.signature == k_lvlf) {
      next.push_back(byte_subrecord(k_lvlf, flags));
    } else {
      next.push_back(subrecord);
    }
  }
  insert_missing();

  record->subrecords = std::move(next);
  mark_record_modified(plugin, *record);
  return true;
}

}  // namespace fo4::forms
