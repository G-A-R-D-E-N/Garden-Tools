#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_anam = Signature::from_string("ANAM");
constexpr auto k_alst = Signature::from_string("ALST");
constexpr auto k_alls = Signature::from_string("ALLS");
constexpr auto k_alcs = Signature::from_string("ALCS");
constexpr auto k_almi = Signature::from_string("ALMI");
constexpr auto k_aled = Signature::from_string("ALED");

struct AliasRange {
  std::size_t begin{0};
  std::size_t end{0};
  std::uint32_t id{0};
};

struct AliasSection {
  std::size_t anam_index{0};
  std::size_t insertion_index{0};
  std::uint32_t next_alias_id{0};
  std::vector<AliasRange> ranges;
};

bool is_alias_start(Signature signature) {
  return signature == k_alst || signature == k_alls || signature == k_alcs;
}

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

void write_u32(std::vector<std::byte>& data, std::uint32_t value) {
  if (data.size() != sizeof(value)) data.resize(sizeof(value));
  std::memcpy(data.data(), &value, sizeof(value));
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
  record.original_bytes.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_bytes.clear();
  visit_groups(plugin, [](Group& group) { group.original_bytes.clear(); });
}

std::uint32_t required_alias_id(const Subrecord& start) {
  if (start.data.size() != 4) {
    throw QuestError(
        "QUST " + start.signature.to_string() +
        " alias ID must be exactly four bytes");
  }
  return read_u32(start.data);
}

AliasSection inspect_alias_section(const Record& record) {
  AliasSection section;
  bool found_anam = false;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto& subrecord = record.subrecords[index];
    if (subrecord.signature != k_anam) continue;
    if (found_anam) {
      throw QuestError("QUST contains duplicate ANAM Next Alias ID subrecords");
    }
    if (subrecord.data.size() != 4) {
      throw QuestError("QUST ANAM Next Alias ID must be exactly four bytes");
    }
    found_anam = true;
    section.anam_index = index;
    section.next_alias_id = read_u32(subrecord.data);
  }
  if (!found_anam) {
    throw QuestError("QUST is missing required ANAM Next Alias ID");
  }

  std::set<std::uint32_t> ids;
  std::size_t cursor = section.anam_index + 1;
  while (cursor < record.subrecords.size() &&
         is_alias_start(record.subrecords[cursor].signature)) {
    const auto begin = cursor;
    const auto id = required_alias_id(record.subrecords[cursor]);
    if (!ids.insert(id).second) {
      throw QuestError("QUST contains duplicate Alias ID " + std::to_string(id));
    }

    if (record.subrecords[cursor].signature == k_alcs) {
      ++cursor;
      if (cursor < record.subrecords.size() &&
          record.subrecords[cursor].signature == k_almi) {
        if (record.subrecords[cursor].data.size() != 1) {
          throw QuestError("QUST collection alias ALMI must be exactly one byte");
        }
        ++cursor;
      }
      section.ranges.push_back({begin, cursor, id});
      continue;
    }

    ++cursor;
    bool ended = false;
    while (cursor < record.subrecords.size()) {
      const auto& member = record.subrecords[cursor];
      if (is_alias_start(member.signature)) {
        throw QuestError("QUST alias begins another alias before required ALED");
      }
      ++cursor;
      if (member.signature != k_aled) continue;
      if (!member.data.empty()) {
        throw QuestError("QUST ALED Alias End marker must be empty");
      }
      ended = true;
      break;
    }
    if (!ended) {
      throw QuestError("QUST reference/location alias is missing required ALED");
    }
    section.ranges.push_back({begin, cursor, id});
  }

  section.insertion_index = cursor;

  // xEdit places NNAM/GNAM/SNAM after the Alias array. Once that tail starts,
  // a later alias marker is structurally invalid rather than a second array.
  for (; cursor < record.subrecords.size(); ++cursor) {
    const auto signature = record.subrecords[cursor].signature;
    if (is_alias_start(signature) || signature == k_aled || signature == k_almi) {
      throw QuestError("QUST alias member appears after the Alias array tail boundary");
    }
  }
  return section;
}

const AliasRange* find_range(const AliasSection& section, std::uint32_t id) {
  for (const auto& range : section.ranges) {
    if (range.id == id) return &range;
  }
  return nullptr;
}

void validate_quest_record(const Plugin& plugin, const FormKey& quest) {
  const auto* record = find_record(plugin, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());
  if (record->header.signature != k_qust) {
    throw QuestError("record is not QUST: " + quest.to_string());
  }
  static_cast<void>(decode_quest_aliases(plugin, quest));
}

void grow_next_alias_id(Record& record,
                        const AliasSection& section,
                        std::uint32_t inserted_id) {
  if (inserted_id < section.next_alias_id) return;
  if (inserted_id == std::numeric_limits<std::uint32_t>::max()) {
    throw QuestError("QUST Alias ID UINT32_MAX cannot advance ANAM Next Alias ID");
  }
  write_u32(record.subrecords[section.anam_index].data, inserted_id + 1);
}

}  // namespace

void add_quest_alias(Plugin& plugin,
                     const FormKey& quest,
                     const QuestAliasCreation& creation) {
  auto next = plugin;
  assign_record_form_keys(next);
  validate_quest_record(next, quest);
  auto* record = find_mutable_record(next, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());
  const auto section = inspect_alias_section(*record);
  if (find_range(section, creation.id) != nullptr) {
    throw QuestError("QUST Alias ID already exists: " + std::to_string(creation.id));
  }

  const auto alias = encode_quest_alias(next, creation);
  grow_next_alias_id(*record, section, creation.id);
  record->subrecords.insert(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(section.insertion_index),
      alias.subrecords.begin(), alias.subrecords.end());
  mark_modified(next, *record);
  plugin = std::move(next);
}

void replace_quest_alias(Plugin& plugin,
                         const FormKey& quest,
                         std::uint32_t alias_id,
                         const QuestAliasCreation& creation) {
  if (creation.id != alias_id) {
    throw QuestError("replacement QUST alias must preserve its Alias ID");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  validate_quest_record(next, quest);
  auto* record = find_mutable_record(next, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());
  const auto section = inspect_alias_section(*record);
  const auto* range = find_range(section, alias_id);
  if (range == nullptr) {
    throw QuestError("QUST Alias ID not found: " + std::to_string(alias_id));
  }

  const auto alias = encode_quest_alias(next, creation);
  const auto begin = record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->begin);
  const auto end = record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->end);
  auto insertion = record->subrecords.erase(begin, end);
  record->subrecords.insert(insertion, alias.subrecords.begin(), alias.subrecords.end());
  mark_modified(next, *record);
  plugin = std::move(next);
}

bool remove_quest_alias(Plugin& plugin,
                        const FormKey& quest,
                        std::uint32_t alias_id) {
  auto next = plugin;
  assign_record_form_keys(next);
  validate_quest_record(next, quest);
  auto* record = find_mutable_record(next, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());
  const auto section = inspect_alias_section(*record);
  const auto* range = find_range(section, alias_id);
  if (range == nullptr) return false;

  record->subrecords.erase(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->begin),
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->end));
  mark_modified(next, *record);
  plugin = std::move(next);
  return true;
}

bool reorder_quest_alias(Plugin& plugin,
                         const FormKey& quest,
                         std::uint32_t alias_id,
                         std::size_t to) {
  auto next = plugin;
  assign_record_form_keys(next);
  validate_quest_record(next, quest);
  auto* record = find_mutable_record(next, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());

  const auto section = inspect_alias_section(*record);
  const auto* range = find_range(section, alias_id);
  if (range == nullptr) return false;
  if (to >= section.ranges.size()) {
    throw QuestError("QUST alias reorder target is out of range");
  }

  // Lift the alias's contiguous subrecord run out, then reinsert it at the
  // start of whichever alias currently sits at `to`. Positions are recomputed
  // against the shortened list so a forward move lands where the caller means.
  const std::vector<Subrecord> moved(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->begin),
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->end));
  const auto from = static_cast<std::size_t>(range - section.ranges.data());
  if (from == to) return true;

  record->subrecords.erase(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->begin),
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->end));

  const auto after = inspect_alias_section(*record);
  const auto insert_at =
      to < after.ranges.size() ? after.ranges[to].begin : after.insertion_index;
  record->subrecords.insert(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(insert_at),
      moved.begin(), moved.end());

  mark_modified(next, *record);
  plugin = std::move(next);
  return true;
}

}  // namespace fo4::forms
