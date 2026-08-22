#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <cstring>

namespace fo4::forms {
namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_indx = Signature::from_string("INDX");
constexpr auto k_qsdt = Signature::from_string("QSDT");
constexpr auto k_cnam = Signature::from_string("CNAM");
constexpr auto k_nam2 = Signature::from_string("NAM2");
constexpr auto k_qobj = Signature::from_string("QOBJ");
constexpr auto k_anam = Signature::from_string("ANAM");

std::string decode_note(const Subrecord& subrecord) {
  const auto& data = subrecord.data;
  if (data.empty() || data.back() != std::byte{0}) {
    throw QuestError("QUST stage NAM2 is not a NUL-terminated string");
  }
  const auto* begin = reinterpret_cast<const char*>(data.data());
  return std::string(begin, begin + data.size() - 1);
}

}  // namespace

QuestStages decode_quest_stages(const Plugin& plugin, const FormKey& quest) {
  const auto* record = find_record(plugin, quest);
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
      // A stage member appearing before any QSDT is not a shape seen in
      // shipped data; keep it rather than dropping it silently.
      throw QuestError("QUST stage " + subrecord.signature.to_string() +
                       " appears before any QSDT log entry");
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
      continue;
    }

    if (subrecord.signature == k_nam2) {
      if (entry->note.has_value()) {
        throw QuestError("QUST log entry contains duplicate NAM2 subrecords");
      }
      entry->note = decode_note(subrecord);
      continue;
    }

    entry->subrecords.push_back(subrecord);
  }

  return result;
}

}  // namespace fo4::forms
