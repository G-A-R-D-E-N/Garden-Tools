#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <cstring>

namespace fo4::forms {
namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_qobj = Signature::from_string("QOBJ");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_qsta = Signature::from_string("QSTA");
constexpr auto k_anam = Signature::from_string("ANAM");

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

}  // namespace

QuestObjectives decode_quest_objectives(const Plugin& plugin, const FormKey& quest) {
  const auto* record = find_record(plugin, quest);
  if (record == nullptr) {
    throw QuestError("QUST record " + quest.to_string() + " was not found");
  }
  if (record->header.signature != k_qust) {
    throw QuestError(quest.to_string() + " is not a QUST record");
  }

  const auto context = plugin_form_id_context(plugin);
  QuestObjectives result;
  QuestObjective* objective = nullptr;
  QuestObjectiveTarget* target = nullptr;

  for (const auto& subrecord : record->subrecords) {
    // The alias array terminates the objective array.
    if (subrecord.signature == k_anam) break;

    if (subrecord.signature == k_qobj) {
      if (subrecord.data.size() != 2) {
        throw QuestError("QUST QOBJ must be exactly two bytes");
      }
      QuestObjective next;
      std::memcpy(&next.index, subrecord.data.data(), 2);
      result.objectives.push_back(next);
      objective = &result.objectives.back();
      target = nullptr;
      continue;
    }

    if (objective == nullptr) continue;  // still ahead of the objective array

    if (subrecord.signature == k_qsta) {
      if (subrecord.data.size() != 12) {
        throw QuestError("QUST QSTA must be exactly twelve bytes");
      }
      QuestObjectiveTarget next;
      std::memcpy(&next.alias, subrecord.data.data(), 4);
      next.flags = read_u32(subrecord.data, 4);
      if (const auto raw = RawFormId{read_u32(subrecord.data, 8)}; !raw.is_null()) {
        next.keyword = resolve_plugin_form_id(raw, context);
      }
      objective->targets.push_back(std::move(next));
      target = &objective->targets.back();
      continue;
    }

    // FNAM and NNAM belong to the objective and precede its targets.
    if (subrecord.signature == k_fnam && target == nullptr) {
      if (subrecord.data.size() != 4) {
        throw QuestError("QUST objective FNAM must be exactly four bytes");
      }
      objective->flags = read_u32(subrecord.data, 0);
      continue;
    }

    if (subrecord.signature == k_nnam && target == nullptr) {
      if (objective->display_text.has_value()) {
        throw QuestError("QUST objective contains duplicate NNAM subrecords");
      }
      try {
        objective->display_text =
            schema::read_localized_string(plugin, k_qust, k_nnam, subrecord);
      } catch (const std::exception& error) {
        throw QuestError(error.what());
      }
      continue;
    }

    if (target == nullptr) {
      throw QuestError("QUST objective " + subrecord.signature.to_string() +
                       " appears before any QSTA target");
    }
    target->subrecords.push_back(subrecord);
  }

  return result;
}

}  // namespace fo4::forms
