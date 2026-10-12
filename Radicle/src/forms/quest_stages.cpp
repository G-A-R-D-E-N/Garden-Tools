#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/localized_fields.hpp"

#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <span>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_indx = Signature::from_string("INDX");
constexpr auto k_qsdt = Signature::from_string("QSDT");
constexpr auto k_cnam = Signature::from_string("CNAM");
constexpr auto k_nam2 = Signature::from_string("NAM2");
constexpr auto k_ctda = Signature::from_string("CTDA");
constexpr auto k_cis1 = Signature::from_string("CIS1");
constexpr auto k_cis2 = Signature::from_string("CIS2");
constexpr auto k_qobj = Signature::from_string("QOBJ");
constexpr auto k_anam = Signature::from_string("ANAM");

bool exact_zstring(std::span<const std::byte> data, std::string& text) {
  if (data.empty() || data.back() != std::byte{0}) return false;
  text.clear();
  text.reserve(data.size() - 1);
  for (std::size_t i = 0; i + 1 < data.size(); ++i) {
    const auto ch = static_cast<char>(std::to_integer<std::uint8_t>(data[i]));
    if (ch == '\0') return false;
    text.push_back(ch);
  }
  return true;
}

std::string decode_note(const Subrecord& subrecord) {
  const auto& data = subrecord.data;
  if (data.empty() || data.back() != std::byte{0}) {
    throw QuestError("QUST stage NAM2 is not a NUL-terminated string");
  }
  const auto* begin = reinterpret_cast<const char*>(data.data());
  return std::string(begin, begin + data.size() - 1);
}

QuestStages decode_quest_stages_record(const Plugin& plugin,
                                       const FormKey& quest,
                                       const Record* record) {
  if (record == nullptr) {
    throw QuestError("QUST record " + quest.to_string() + " was not found");
  }
  if (record->header.signature != k_qust) {
    throw QuestError(quest.to_string() + " is not a QUST record");
  }

  QuestStages result;
  QuestStage* stage = nullptr;
  QuestLogEntry* entry = nullptr;

  for (const auto& subrecord : record->subrecords) {
    // The stage array ends where objectives or the alias array begin.
    if (subrecord.signature == k_qobj || subrecord.signature == k_anam) break;

    if (subrecord.signature == k_indx) {
      if (subrecord.data.size() != 4) {
        throw QuestError("QUST stage INDX must be exactly four bytes");
      }
      QuestStage next;
      std::memcpy(&next.index, subrecord.data.data(), 2);
      next.flags = std::to_integer<std::uint8_t>(subrecord.data[2]);
      next.unknown = std::to_integer<std::uint8_t>(subrecord.data[3]);
      result.stages.push_back(next);
      stage = &result.stages.back();
      entry = nullptr;
      continue;
    }

    if (stage == nullptr) continue;  // still ahead of the stage array

    if (subrecord.signature == k_qsdt) {
      if (subrecord.data.size() != 1) {
        throw QuestError("QUST stage QSDT must be exactly one byte");
      }
      QuestLogEntry next;
      next.flags = std::to_integer<std::uint8_t>(subrecord.data[0]);
      stage->log_entries.push_back(std::move(next));
      entry = &stage->log_entries.back();
      continue;
    }

    if (entry == nullptr) {
      // Not a shape Bethesda emits, but third-party plugins do. Keep it on the
      // stage rather than rejecting the whole quest.
      stage->leading_members.push_back(subrecord);
      continue;
    }

    if (subrecord.signature == k_cnam) {
      if (entry->log_text.has_value()) {
        throw QuestError("QUST log entry contains duplicate CNAM subrecords");
      }
      try {
        entry->log_text = schema::read_localized_string(plugin, k_qust, k_cnam, subrecord);
      } catch (const std::exception& error) {
        throw QuestError(error.what());
      }
      if (entry->log_text->raw_id.has_value()) {
        throw QuestError("QUST CNAM is localized and must be exactly four bytes");
      }
    } else if (subrecord.signature == k_nam2) {
      if (entry->note.has_value()) {
        throw QuestError("QUST log entry contains duplicate NAM2 subrecords");
      }
      entry->note = decode_note(subrecord);
    }

    // Every member is kept in file order, decoded ones included, so encoding
    // reproduces the original sequence exactly.
    entry->members.push_back(subrecord);
  }

  return result;
}

}  // namespace

QuestStages decode_quest_stages(const Plugin& plugin, const FormKey& quest) {
  return decode_quest_stages_record(plugin, quest, find_record(plugin, quest));
}

QuestStages decode_quest_stages(const RecordIndex& index, const FormKey& quest) {
  return decode_quest_stages_record(index.plugin(), quest, index.find(quest));
}

bool lossless_quest_log_entry_conditions(std::span<const Subrecord> members,
                                         std::vector<CtdaCondition>& conditions,
                                         std::size_t& consumed) {
  conditions.clear();
  consumed = 0;
  std::vector<Subrecord> prefix;
  std::size_t i = 0;
  while (i < members.size()) {
    const auto& member = members[i];
    if (member.signature == k_nam2 || member.signature == k_cnam) break;
    if (member.signature != k_ctda) return false;
    CtdaCondition condition;
    try {
      condition = parse_ctda_condition(member.data);
    } catch (const std::exception&) {
      return false;
    }
    if (serialize_ctda_condition(condition) != member.data) return false;
    prefix.push_back(member);
    ++i;
    bool saw_cis1 = false;
    bool saw_cis2 = false;
    while (i < members.size()) {
      const auto& extra = members[i];
      if (extra.signature != k_cis1 && extra.signature != k_cis2) break;
      std::string text;
      if (!exact_zstring(extra.data, text)) return false;
      if (extra.signature == k_cis1) {
        if (saw_cis1) return false;
        saw_cis1 = true;
        condition.parameter1_string = std::move(text);
      } else {
        if (saw_cis2) return false;
        saw_cis2 = true;
        condition.parameter2_string = std::move(text);
      }
      prefix.push_back(extra);
      ++i;
    }
    conditions.push_back(std::move(condition));
  }
  consumed = i;
  const auto rebuilt = serialize_ctda_run(conditions);
  if (rebuilt.size() != prefix.size()) return false;
  for (std::size_t n = 0; n < prefix.size(); ++n) {
    if (rebuilt[n].signature != prefix[n].signature ||
        rebuilt[n].data != prefix[n].data) {
      return false;
    }
  }
  return true;
}

namespace {

// The stage array runs from the first INDX to the first QOBJ/ANAM. Returns
// [begin, end) into the record's subrecords, both equal to the insertion point
// when the quest has no stages yet.
std::pair<std::size_t, std::size_t> stage_array_bounds(const Record& record) {
  std::size_t begin = record.subrecords.size();
  std::size_t end = record.subrecords.size();
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == k_qobj || signature == k_anam) {
      end = i;
      if (begin > i) begin = i;
      return {begin, end};
    }
    if (signature == k_indx && begin == record.subrecords.size()) begin = i;
  }
  return {begin, end};
}

Subrecord marker(Signature signature, std::vector<std::byte> data) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data = std::move(data);
  return subrecord;
}

std::vector<Subrecord> encode_stages(const QuestStages& stages) {
  std::vector<Subrecord> out;
  for (const auto& stage : stages.stages) {
    std::vector<std::byte> indx(4);
    std::memcpy(indx.data(), &stage.index, 2);
    indx[2] = std::byte{stage.flags};
    indx[3] = std::byte{stage.unknown};
    out.push_back(marker(k_indx, std::move(indx)));
    out.insert(out.end(), stage.leading_members.begin(), stage.leading_members.end());
    for (const auto& entry : stage.log_entries) {
      out.push_back(marker(k_qsdt, std::vector<std::byte>{std::byte{entry.flags}}));
      out.insert(out.end(), entry.members.begin(), entry.members.end());
    }
  }
  return out;
}

Record* mutable_quest(Plugin& plugin, const FormKey& quest) {
  Record* found = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (record.form_key == quest && record.header.signature == k_qust) found = &record;
  });
  if (found == nullptr) {
    throw QuestError("QUST record " + quest.to_string() + " was not found");
  }
  require_decodable(*found);
  return found;
}

}  // namespace

void write_quest_stages(Plugin& plugin, const FormKey& quest, const QuestStages& stages) {
  auto next = plugin;
  auto* record = mutable_quest(next, quest);
  const auto previous = decode_quest_stages(next, quest);
  sync_quest_vmad_stages(*record, previous, stages);
  const auto [begin, end] = stage_array_bounds(*record);
  const auto encoded = encode_stages(stages);

  std::vector<Subrecord> rebuilt;
  rebuilt.reserve(record->subrecords.size() - (end - begin) + encoded.size());
  rebuilt.insert(rebuilt.end(), record->subrecords.begin(),
                 record->subrecords.begin() + static_cast<std::ptrdiff_t>(begin));
  rebuilt.insert(rebuilt.end(), encoded.begin(), encoded.end());
  rebuilt.insert(rebuilt.end(),
                 record->subrecords.begin() + static_cast<std::ptrdiff_t>(end),
                 record->subrecords.end());

  record->subrecords = std::move(rebuilt);
  record->original_range.clear();
  record->decompressed_payload.clear();
  record->header.data_size = 0;
  record->uncompressed_size = 0;
  next.original_range.clear();
  plugin = std::move(next);
}

void add_quest_stage(Plugin& plugin, const FormKey& quest, const QuestStage& stage) {
  auto stages = decode_quest_stages(plugin, quest);
  for (const auto& existing : stages.stages) {
    if (existing.index == stage.index) {
      throw QuestError("QUST already has a stage with index " + std::to_string(stage.index));
    }
  }
  stages.stages.push_back(stage);
  write_quest_stages(plugin, quest, stages);
}

bool remove_quest_stage(Plugin& plugin, const FormKey& quest, std::uint16_t index) {
  auto stages = decode_quest_stages(plugin, quest);
  const auto before = stages.stages.size();
  std::erase_if(stages.stages,
                [&](const QuestStage& stage) { return stage.index == index; });
  if (stages.stages.size() == before) return false;
  write_quest_stages(plugin, quest, stages);
  return true;
}

bool reorder_quest_stage(Plugin& plugin,
                         const FormKey& quest,
                         std::uint16_t index,
                         std::size_t to) {
  auto stages = decode_quest_stages(plugin, quest);
  const auto found = std::find_if(stages.stages.begin(), stages.stages.end(),
                                  [&](const QuestStage& s) { return s.index == index; });
  if (found == stages.stages.end()) return false;
  if (to >= stages.stages.size()) {
    throw QuestError("QUST stage reorder target is out of range");
  }
  auto moved = *found;
  stages.stages.erase(found);
  stages.stages.insert(stages.stages.begin() + static_cast<std::ptrdiff_t>(to),
                       std::move(moved));
  write_quest_stages(plugin, quest, stages);
  return true;
}

}  // namespace fo4::forms
