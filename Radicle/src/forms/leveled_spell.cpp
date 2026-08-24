#include "fo4/forms/leveled_spell.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_lvsp = Signature::from_string("LVSP");
constexpr auto k_lvld = Signature::from_string("LVLD");
constexpr auto k_lvlm = Signature::from_string("LVLM");
constexpr auto k_lvlf = Signature::from_string("LVLF");
constexpr auto k_llct = Signature::from_string("LLCT");
constexpr auto k_lvlo = Signature::from_string("LVLO");

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

void write_u16(std::vector<std::byte>& data, std::size_t offset,
               std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, std::size_t offset,
               std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

Record* find_mutable_record(Plugin& plugin, const FormKey& key) {
  Record* found = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (found == nullptr && record.header.signature != sig_tes4 &&
        record.form_key == key) {
      found = &record;
    }
  });
  return found;
}

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

std::uint8_t read_single_byte(const Subrecord& subrecord,
                              std::string_view field_name) {
  if (subrecord.data.size() != 1) {
    throw LeveledSpellError(std::string(field_name) +
                            " must be exactly one byte");
  }
  return std::to_integer<std::uint8_t>(subrecord.data.front());
}

Subrecord byte_subrecord(Signature signature, std::uint8_t value) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data.push_back(std::byte{value});
  return subrecord;
}

Subrecord count_subrecord(std::size_t count) {
  if (count > 0xFFu) {
    throw LeveledSpellError("LVSP cannot contain more than 255 LVLO entries");
  }
  return byte_subrecord(k_llct, static_cast<std::uint8_t>(count));
}

Subrecord encode_entry(const LeveledSpellEntry& entry,
                       const PluginFormIdContext& context) {
  if (entry.spell.owner().empty()) {
    throw LeveledSpellError("LVSP entry must reference a SPEL or LVSP FormKey");
  }

  RawFormId raw{};
  try {
    raw = encode_plugin_form_id(entry.spell, context);
  } catch (const FormIdError& error) {
    throw LeveledSpellError(std::string("cannot encode LVSP entry FormKey: ") +
                            error.what());
  }
  if (raw.is_null()) {
    throw LeveledSpellError("LVSP entry cannot encode to a null FormID");
  }

  Subrecord subrecord;
  subrecord.signature = k_lvlo;
  subrecord.data.resize(12);
  write_u16(subrecord.data, 0, entry.level);
  subrecord.data[2] = entry.reserved_before_form_id[0];
  subrecord.data[3] = entry.reserved_before_form_id[1];
  write_u32(subrecord.data, 4, raw.value());
  write_u16(subrecord.data, 8, entry.count);
  subrecord.data[10] = std::byte{entry.chance_none};
  subrecord.data[11] = entry.reserved_tail;
  return subrecord;
}

}  // namespace

LeveledSpell decode_leveled_spell(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) {
    throw LeveledSpellError("LVSP record not found: " + key.to_string());
  }
  if (record->header.signature != k_lvsp) {
    throw LeveledSpellError("record is not LVSP: " + key.to_string());
  }

  LeveledSpell result;
  result.key = key;
  const auto context = plugin_form_id_context(plugin);
  std::optional<std::uint8_t> declared_count;
  bool saw_lvld = false;
  bool saw_lvlm = false;
  bool saw_lvlf = false;

  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == k_lvld) {
      if (saw_lvld) throw LeveledSpellError("LVSP contains duplicate LVLD subrecords");
      saw_lvld = true;
      result.chance_none = read_single_byte(subrecord, "LVLD");
      continue;
    }
    if (subrecord.signature == k_lvlm) {
      if (saw_lvlm) throw LeveledSpellError("LVSP contains duplicate LVLM subrecords");
      saw_lvlm = true;
      result.max_count = read_single_byte(subrecord, "LVLM");
      continue;
    }
    if (subrecord.signature == k_lvlf) {
      if (saw_lvlf) throw LeveledSpellError("LVSP contains duplicate LVLF subrecords");
      saw_lvlf = true;
      result.flags = read_single_byte(subrecord, "LVLF");
      continue;
    }
    if (subrecord.signature == k_llct) {
      if (declared_count.has_value()) {
        throw LeveledSpellError("LVSP contains duplicate LLCT subrecords");
      }
      declared_count = read_single_byte(subrecord, "LLCT");
      continue;
    }
    if (subrecord.signature != k_lvlo) continue;
    if (subrecord.data.size() != 12) {
      throw LeveledSpellError("FO4 LVSP LVLO entry must be exactly 12 bytes");
    }

    LeveledSpellEntry entry;
    entry.level = read_u16(subrecord.data, 0);
    entry.reserved_before_form_id = {subrecord.data[2], subrecord.data[3]};
    const auto raw = RawFormId{read_u32(subrecord.data, 4)};
    if (raw.is_null()) {
      throw LeveledSpellError("LVSP LVLO entry contains a null FormID");
    }
    try {
      entry.spell = resolve_plugin_form_id(raw, context);
    } catch (const FormIdError& error) {
      throw LeveledSpellError(std::string("invalid LVSP entry FormID: ") +
                              error.what());
    }
    entry.count = read_u16(subrecord.data, 8);
    entry.chance_none = std::to_integer<std::uint8_t>(subrecord.data[10]);
    entry.reserved_tail = subrecord.data[11];
    result.entries.push_back(std::move(entry));
  }

  if (declared_count.has_value() &&
      static_cast<std::size_t>(*declared_count) != result.entries.size()) {
    throw LeveledSpellError("LVSP LLCT does not match the number of LVLO entries");
  }
  if (result.entries.size() > 0xFFu) {
    throw LeveledSpellError("LVSP contains more entries than LLCT can represent");
  }
  return result;
}

bool replace_leveled_spell_entries(Plugin& plugin,
                                   const FormKey& key,
                                   std::span<const LeveledSpellEntry> entries) {
  if (entries.size() > 0xFFu) {
    throw LeveledSpellError("LVSP cannot contain more than 255 LVLO entries");
  }

  assign_record_form_keys(plugin);
  const auto current = decode_leveled_spell(plugin, key);
  if (current.entries.size() == entries.size() &&
      std::equal(current.entries.begin(), current.entries.end(), entries.begin())) {
    return true;
  }

  auto* record = find_mutable_record(plugin, key);
  if (record == nullptr) return false;
  const auto context = plugin_form_id_context(plugin);

  std::vector<Subrecord> encoded_entries;
  encoded_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    encoded_entries.push_back(encode_entry(entry, context));
  }

  std::optional<std::size_t> llct_index;
  std::optional<std::size_t> first_lvlo_index;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    const auto signature = record->subrecords[index].signature;
    if (signature == k_llct) {
      if (llct_index.has_value()) {
        throw LeveledSpellError("LVSP contains duplicate LLCT subrecords");
      }
      llct_index = index;
    } else if (signature == k_lvlo && !first_lvlo_index.has_value()) {
      first_lvlo_index = index;
    }
  }

  std::vector<Subrecord> next;
  next.reserve(record->subrecords.size() + encoded_entries.size() + 1);
  bool inserted_entries = false;
  bool inserted_missing_list = false;
  bool inserted_after_flags = false;

  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    const auto& subrecord = record->subrecords[index];
    if (subrecord.signature == k_llct) {
      next.push_back(count_subrecord(entries.size()));
      if (!first_lvlo_index.has_value()) {
        next.insert(next.end(), encoded_entries.begin(), encoded_entries.end());
        inserted_entries = true;
      }
      continue;
    }
    if (subrecord.signature == k_lvlo) {
      if (!inserted_entries) {
        if (!llct_index.has_value()) {
          next.push_back(count_subrecord(entries.size()));
          inserted_missing_list = true;
        }
        next.insert(next.end(), encoded_entries.begin(), encoded_entries.end());
        inserted_entries = true;
      }
      continue;
    }

    next.push_back(subrecord);
    if (!llct_index.has_value() && !first_lvlo_index.has_value() &&
        !inserted_missing_list && subrecord.signature == k_lvlf) {
      next.push_back(count_subrecord(entries.size()));
      next.insert(next.end(), encoded_entries.begin(), encoded_entries.end());
      inserted_missing_list = true;
      inserted_entries = true;
      inserted_after_flags = true;
    }
  }

  if (!llct_index.has_value() && !first_lvlo_index.has_value() &&
      !inserted_missing_list) {
    next.push_back(count_subrecord(entries.size()));
    next.insert(next.end(), encoded_entries.begin(), encoded_entries.end());
    inserted_missing_list = true;
    inserted_entries = true;
  }
  (void)inserted_missing_list;
  (void)inserted_after_flags;
  (void)inserted_entries;

  record->subrecords = std::move(next);
  mark_modified(plugin, *record);
  return true;
}

bool set_leveled_spell_settings(Plugin& plugin,
                                const FormKey& key,
                                std::uint8_t chance_none,
                                std::uint8_t max_count,
                                std::uint8_t flags) {
  assign_record_form_keys(plugin);
  const auto current = decode_leveled_spell(plugin, key);
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
      if (saw_lvld) throw LeveledSpellError("LVSP contains duplicate LVLD subrecords");
      saw_lvld = true;
    } else if (subrecord.signature == k_lvlm) {
      if (saw_lvlm) throw LeveledSpellError("LVSP contains duplicate LVLM subrecords");
      saw_lvlm = true;
    } else if (subrecord.signature == k_lvlf) {
      if (saw_lvlf) throw LeveledSpellError("LVSP contains duplicate LVLF subrecords");
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
  mark_modified(plugin, *record);
  return true;
}

}  // namespace fo4::forms
