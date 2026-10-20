#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"
#include "../plugin/record_edit.hpp"
#include "quest_aliases_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
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
constexpr auto k_ctda = Signature::from_string("CTDA");
constexpr auto k_cis1 = Signature::from_string("CIS1");
constexpr auto k_cis2 = Signature::from_string("CIS2");
constexpr auto k_cnto = Signature::from_string("CNTO");
constexpr auto k_coed = Signature::from_string("COED");

struct AliasRange {
  std::size_t begin{0};
  std::size_t end{0};
  std::uint32_t id{0};
  bool collection{false};
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

using fo4::detail::find_mutable_record;

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
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

    if (record.subrecords[cursor].signature == k_alcs) {
      while (cursor < record.subrecords.size() &&
             record.subrecords[cursor].signature == k_alcs) {
        static_cast<void>(required_alias_id(record.subrecords[cursor]));
        ++cursor;
        if (cursor < record.subrecords.size() &&
            record.subrecords[cursor].signature == k_almi) {
          if (record.subrecords[cursor].data.size() != 1) {
            throw QuestError("QUST collection alias ALMI must be exactly one byte");
          }
          ++cursor;
        }
      }
      section.ranges.push_back({begin, cursor, 0, true});
      continue;
    }

    const auto id = required_alias_id(record.subrecords[cursor]);
    if (!ids.insert(id).second) {
      throw QuestError("QUST contains duplicate Alias ID " + std::to_string(id));
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
    section.ranges.push_back({begin, cursor, id, false});
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
    if (!range.collection && range.id == id) return &range;
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

void refuse_adjacent_collections(const std::vector<std::uint8_t>& flags) {
  for (std::size_t i = 1; i < flags.size(); ++i) {
    if (flags[i - 1] != 0 && flags[i] != 0) {
      throw QuestError(
          "QUST collection aliases cannot be adjacent; contiguous ALCS runs are one alias");
    }
  }
}

std::vector<std::uint8_t> collection_flags(const AliasSection& section) {
  std::vector<std::uint8_t> flags;
  flags.reserve(section.ranges.size());
  for (const auto& range : section.ranges) {
    flags.push_back(range.collection ? 1 : 0);
  }
  return flags;
}

constexpr std::string_view k_reference_member_order[] = {
    "ALST", "ALID", "FNAM", "ALFI", "ALFR", "ALUA", "ALFA", "ALRT", "ALEQ",
    "ALEA", "ALCO", "ALCA", "ALCL", "ALNA", "ALNT", "ALFE", "ALFD", "ALCC",
    "CTDA", "CIS1", "CIS2", "KSIZ", "KWDA", "COCT", "CNTO", "COED", "SPOR",
    "OCOR", "GWOR", "ECOR", "ALLA", "ALDN", "ALFV", "ALDI", "ALSP", "ALFC",
    "ALPC", "VTCK", "ALED"};

constexpr std::string_view k_location_member_order[] = {
    "ALLS", "ALID", "FNAM", "ALFI", "ALFL", "ALFA", "KNAM", "ALEQ", "ALEA",
    "ALFE", "ALFD", "CTDA", "CIS1", "CIS2", "ALCC", "ALED"};

constexpr std::string_view k_alias_frame[] = {
    "ALST", "ALLS", "ALCS", "ALMI", "ALID", "FNAM", "ALFI", "ALED"};

constexpr std::string_view k_reference_fill_members[] = {
    "ALFR", "ALUA", "ALFA", "ALRT", "ALEQ", "ALEA", "ALCO", "ALCA", "ALCL",
    "ALNA", "ALNT", "ALFE", "ALFD", "ALCC"};

constexpr std::string_view k_location_fill_members[] = {
    "ALFL", "ALFA", "KNAM", "ALEQ", "ALEA", "ALFE", "ALFD"};

constexpr std::string_view k_condition_run[] = {"CTDA", "CIS1", "CIS2"};
constexpr std::string_view k_item_run[] = {"COCT", "CNTO", "COED"};

template <std::size_t N>
std::optional<std::size_t> member_rank(const std::string_view (&names)[N],
                                       Signature signature) {
  for (std::size_t index = 0; index < N; ++index) {
    if (names[index] == signature.view()) return index;
  }
  return std::nullopt;
}

enum class AliasMemberRun { none, fill, conditions, items };

AliasMemberRun member_run(QuestAliasKind kind, Signature signature) {
  if (kind == QuestAliasKind::reference &&
      member_rank(k_reference_fill_members, signature).has_value()) {
    return AliasMemberRun::fill;
  }
  if (kind == QuestAliasKind::location &&
      member_rank(k_location_fill_members, signature).has_value()) {
    return AliasMemberRun::fill;
  }
  if (member_rank(k_condition_run, signature).has_value()) {
    return AliasMemberRun::conditions;
  }
  if (member_rank(k_item_run, signature).has_value()) return AliasMemberRun::items;
  return AliasMemberRun::none;
}

bool run_written(const std::vector<Subrecord>& members,
                 QuestAliasKind kind,
                 AliasMemberRun run) {
  for (const auto& member : members) {
    if (member_run(kind, member.signature) == run) return true;
  }
  return false;
}

std::optional<std::size_t> alias_member_rank(QuestAliasKind kind,
                                             Signature signature) {
  if (signature == k_cis1 || signature == k_cis2) signature = k_ctda;
  if (signature == k_coed) signature = k_cnto;
  if (kind == QuestAliasKind::reference) {
    return member_rank(k_reference_member_order, signature);
  }
  if (kind == QuestAliasKind::location) {
    return member_rank(k_location_member_order, signature);
  }
  return std::nullopt;
}

std::vector<Subrecord> merge_alias_members(QuestAliasKind kind,
                                           const std::vector<Subrecord>& preserved,
                                           const QuestAlias& alias) {
  if (preserved.empty()) return alias.subrecords;

  std::vector<std::size_t> ranks;
  ranks.reserve(preserved.size());
  for (const auto& member : preserved) {
    const auto rank = alias_member_rank(kind, member.signature);
    if (!rank.has_value()) {
      throw QuestError("QUST alias member " + member.signature.to_string() +
                       " cannot be placed in the " + detail::alias_label(kind) +
                       " alias member order");
    }
    if (!ranks.empty() && *rank < ranks.back()) {
      throw QuestError("QUST " + std::string(detail::alias_label(kind)) +
                       " alias preserved members are not in member order");
    }
    ranks.push_back(*rank);
  }

  std::vector<Subrecord> merged;
  merged.reserve(preserved.size() + alias.subrecords.size());
  std::size_t next = 0;
  for (const auto& member : alias.subrecords) {
    const auto rank = alias_member_rank(kind, member.signature);
    if (!rank.has_value()) {
      throw QuestError("QUST encoded alias member " + member.signature.to_string() +
                       " has no " + detail::alias_label(kind) + " alias member order");
    }
    while (next < preserved.size() && ranks[next] < *rank) {
      merged.push_back(preserved[next]);
      ++next;
    }
    merged.push_back(member);
  }
  while (next < preserved.size()) {
    merged.push_back(preserved[next]);
    ++next;
  }
  return merged;
}

}  // namespace

void write_quest_aliases(Plugin& plugin,
                         const FormKey& quest,
                         std::span<const QuestAliasCreation> creations) {
  auto next = plugin;
  assign_record_form_keys(next);
  validate_quest_record(next, quest);
  auto* record = find_mutable_record(next, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());

  std::vector<Subrecord> encoded;
  std::set<std::uint32_t> ids;
  std::uint32_t max_ref_id = 0;
  bool have_ref_id = false;
  bool previous_collection = false;
  for (const auto& creation : creations) {
    const bool is_collection = creation.kind == QuestAliasKind::collection;
    if (is_collection && previous_collection) {
      throw QuestError(
          "QUST collection aliases cannot be adjacent; contiguous ALCS runs are one alias");
    }
    previous_collection = is_collection;
    const auto alias = encode_quest_alias(next, creation);
    if (!is_collection) {
      if (!ids.insert(creation.id).second) {
        throw QuestError("QUST Alias ID already exists: " + std::to_string(creation.id));
      }
      if (!have_ref_id || creation.id > max_ref_id) max_ref_id = creation.id;
      have_ref_id = true;
    }
    encoded.insert(encoded.end(), alias.subrecords.begin(), alias.subrecords.end());
  }

  const auto previous = decode_quest_aliases(next, quest);
  sync_quest_vmad_aliases(*record, previous, creations);

  const auto section = inspect_alias_section(*record);
  auto begin = record->subrecords.begin() +
               static_cast<std::ptrdiff_t>(section.anam_index + 1);
  auto end = record->subrecords.begin() +
             static_cast<std::ptrdiff_t>(section.insertion_index);
  auto insertion = record->subrecords.erase(begin, end);
  record->subrecords.insert(insertion, encoded.begin(), encoded.end());

  std::uint32_t next_id = section.next_alias_id;
  if (have_ref_id) {
    if (max_ref_id == std::numeric_limits<std::uint32_t>::max()) {
      throw QuestError("QUST Alias ID UINT32_MAX cannot advance ANAM Next Alias ID");
    }
    const auto needed = max_ref_id + 1;
    if (needed > next_id) next_id = needed;
  }
  write_u32(record->subrecords[section.anam_index].data, next_id);
  mark_modified(next, *record);
  plugin = std::move(next);
}

void add_quest_alias(Plugin& plugin,
                     const FormKey& quest,
                     const QuestAliasCreation& creation) {
  auto next = plugin;
  assign_record_form_keys(next);
  validate_quest_record(next, quest);
  auto* record = find_mutable_record(next, quest);
  if (record == nullptr) throw QuestError("QUST record not found: " + quest.to_string());
  const auto section = inspect_alias_section(*record);
  const bool is_collection = creation.kind == QuestAliasKind::collection;
  if (!is_collection && find_range(section, creation.id) != nullptr) {
    throw QuestError("QUST Alias ID already exists: " + std::to_string(creation.id));
  }
  auto flags = collection_flags(section);
  flags.push_back(is_collection ? 1 : 0);
  refuse_adjacent_collections(flags);

  const auto alias = encode_quest_alias(next, creation);
  if (!is_collection) {
    grow_next_alias_id(*record, section, creation.id);
  }
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
  auto flags = collection_flags(section);
  flags[static_cast<std::size_t>(range - section.ranges.data())] =
      creation.kind == QuestAliasKind::collection ? 1 : 0;
  refuse_adjacent_collections(flags);

  const auto existing_kind =
      detail::alias_kind(record->subrecords[range->begin].signature);
  const auto alias = encode_quest_alias(next, creation);

  std::set<Signature> written;
  for (const auto& member : alias.subrecords) written.insert(member.signature);
  const bool fill_written =
      run_written(alias.subrecords, creation.kind, AliasMemberRun::fill);
  const bool conditions_written =
      run_written(alias.subrecords, creation.kind, AliasMemberRun::conditions);
  const bool items_written =
      run_written(alias.subrecords, creation.kind, AliasMemberRun::items);
  const auto replaced = [&](Signature signature) {
    if (written.count(signature) != 0) return true;
    const auto existing_run = member_run(existing_kind, signature);
    if (fill_written &&
        (existing_run == AliasMemberRun::fill ||
         member_run(creation.kind, signature) == AliasMemberRun::fill)) {
      return true;
    }
    switch (existing_run) {
      case AliasMemberRun::conditions:
        return conditions_written;
      case AliasMemberRun::items:
        return items_written;
      case AliasMemberRun::fill:
      case AliasMemberRun::none:
        return false;
    }
    return false;
  };

  const bool kind_changed = creation.kind != existing_kind;
  std::vector<Subrecord> preserved;
  for (std::size_t index = range->begin; index < range->end; ++index) {
    const auto& member = record->subrecords[index];
    if (replaced(member.signature)) continue;
    if (kind_changed &&
        member_rank(k_alias_frame, member.signature).has_value()) {
      continue;
    }
    if (kind_changed && !fill_written &&
        (member_run(existing_kind, member.signature) == AliasMemberRun::fill ||
         member_run(creation.kind, member.signature) == AliasMemberRun::fill)) {
      throw QuestError(
          "QUST alias member " + member.signature.to_string() +
          " cannot be preserved across a " + detail::alias_label(existing_kind) +
          " to " + detail::alias_label(creation.kind) +
          " alias kind change that writes no fill");
    }
    preserved.push_back(member);
  }
  const auto merged = merge_alias_members(creation.kind, preserved, alias);

  const auto begin = record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->begin);
  const auto end = record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->end);
  auto insertion = record->subrecords.erase(begin, end);
  record->subrecords.insert(insertion, merged.begin(), merged.end());
  static_cast<void>(decode_quest_aliases(next, quest));
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
  auto flags = collection_flags(section);
  flags.erase(flags.begin() + (range - section.ranges.data()));
  refuse_adjacent_collections(flags);

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
  auto flags = collection_flags(section);
  const auto moved_flag = flags[from];
  flags.erase(flags.begin() + static_cast<std::ptrdiff_t>(from));
  if (to > flags.size()) {
    throw QuestError("QUST alias reorder target is out of range");
  }
  flags.insert(flags.begin() + static_cast<std::ptrdiff_t>(to), moved_flag);
  refuse_adjacent_collections(flags);

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
