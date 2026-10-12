#include "fo4/forms/quest.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"

#include "fo4/plugin/visit.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace fo4::forms {
namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_qobj = Signature::from_string("QOBJ");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_nnam = Signature::from_string("NNAM");
constexpr auto k_qsta = Signature::from_string("QSTA");
constexpr auto k_anam = Signature::from_string("ANAM");
constexpr std::uint16_t k_qsta_keyword_form_version = 82;

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

QuestObjectives decode_quest_objectives_record(const Plugin& plugin,
                                               const FormKey& quest,
                                               const Record* record) {
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
      const bool keyword_layout = record->header.form_version >= k_qsta_keyword_form_version;
      if (subrecord.data.size() != (keyword_layout ? 12u : 8u)) {
        throw QuestError(keyword_layout ? "QUST QSTA must be exactly twelve bytes"
                                        : "QUST QSTA must be exactly eight bytes");
      }
      QuestObjectiveTarget next;
      std::memcpy(&next.alias, subrecord.data.data(), 4);
      next.flags = read_u32(subrecord.data, 4);
      if (keyword_layout) {
        if (const auto raw = RawFormId{read_u32(subrecord.data, 8)}; !raw.is_null()) {
          try {
            next.keyword = resolve_plugin_form_id(raw, context);
          } catch (const FormIdError&) {
            next.keyword_unresolved = true;
          }
        }
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

}  // namespace

QuestObjectives decode_quest_objectives(const Plugin& plugin, const FormKey& quest) {
  return decode_quest_objectives_record(plugin, quest, find_record(plugin, quest));
}

QuestObjectives decode_quest_objectives(const RecordIndex& index, const FormKey& quest) {
  return decode_quest_objectives_record(index.plugin(), quest, index.find(quest));
}

namespace {

// The objective array runs from the first QOBJ to ANAM (or end of record).
std::pair<std::size_t, std::size_t> objective_array_bounds(const Record& record) {
  std::size_t begin = record.subrecords.size();
  std::size_t end = record.subrecords.size();
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == k_anam) {
      end = i;
      if (begin > i) begin = i;
      return {begin, end};
    }
    if (signature == k_qobj && begin == record.subrecords.size()) begin = i;
  }
  return {begin, end};
}

Subrecord marker(Signature signature, std::vector<std::byte> data) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data = std::move(data);
  return subrecord;
}

std::vector<std::byte> bytes_of(const void* source, std::size_t size) {
  std::vector<std::byte> out(size);
  std::memcpy(out.data(), source, size);
  return out;
}

std::vector<Subrecord> encode_objectives(const Plugin& plugin,
                                         const QuestObjectives& objectives,
                                         std::uint16_t form_version) {
  const auto context = plugin_form_id_context(plugin);
  const bool keyword_layout = form_version >= k_qsta_keyword_form_version;
  std::vector<Subrecord> out;
  for (const auto& objective : objectives.objectives) {
    out.push_back(marker(k_qobj, bytes_of(&objective.index, 2)));
    out.push_back(marker(k_fnam, bytes_of(&objective.flags, 4)));
    if (objective.display_text.has_value()) {
      out.push_back(marker(k_nnam, schema::write_localized_string(*objective.display_text)));
    }
    for (const auto& target : objective.targets) {
      if (target.keyword_unresolved) {
        throw QuestError("QUST objective target keyword does not resolve and cannot be rewritten");
      }
      if (!keyword_layout && target.keyword.has_value()) {
        throw QuestError("QUST objective target keyword requires form version 82 or later");
      }
      std::vector<std::byte> qsta(keyword_layout ? 12 : 8);
      std::memcpy(qsta.data(), &target.alias, 4);
      std::memcpy(qsta.data() + 4, &target.flags, 4);
      if (keyword_layout) {
        const std::uint32_t keyword =
            target.keyword.has_value() ? encode_plugin_form_id(*target.keyword, context).value() : 0u;
        std::memcpy(qsta.data() + 8, &keyword, 4);
      }
      out.push_back(marker(k_qsta, std::move(qsta)));
      out.insert(out.end(), target.subrecords.begin(), target.subrecords.end());
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

void write_quest_objectives(Plugin& plugin,
                            const FormKey& quest,
                            const QuestObjectives& objectives) {
  auto next = plugin;
  auto* record = mutable_quest(next, quest);
  const auto [begin, end] = objective_array_bounds(*record);
  const auto encoded = encode_objectives(next, objectives, record->header.form_version);

  std::vector<Subrecord> rebuilt;
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

void add_quest_objective(Plugin& plugin,
                         const FormKey& quest,
                         const QuestObjective& objective) {
  auto objectives = decode_quest_objectives(plugin, quest);
  for (const auto& existing : objectives.objectives) {
    if (existing.index == objective.index) {
      throw QuestError("QUST already has an objective with index " +
                       std::to_string(objective.index));
    }
  }
  objectives.objectives.push_back(objective);
  write_quest_objectives(plugin, quest, objectives);
}

bool remove_quest_objective(Plugin& plugin, const FormKey& quest, std::uint16_t index) {
  auto objectives = decode_quest_objectives(plugin, quest);
  const auto before = objectives.objectives.size();
  std::erase_if(objectives.objectives,
                [&](const QuestObjective& o) { return o.index == index; });
  if (objectives.objectives.size() == before) return false;
  write_quest_objectives(plugin, quest, objectives);
  return true;
}

bool reorder_quest_objective(Plugin& plugin,
                             const FormKey& quest,
                             std::uint16_t index,
                             std::size_t to) {
  auto objectives = decode_quest_objectives(plugin, quest);
  auto& list = objectives.objectives;
  const auto found = std::find_if(list.begin(), list.end(),
                                  [&](const QuestObjective& o) { return o.index == index; });
  if (found == list.end()) return false;
  if (to >= list.size()) {
    throw QuestError("QUST objective reorder target is out of range");
  }
  auto moved = *found;
  list.erase(found);
  list.insert(list.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
  write_quest_objectives(plugin, quest, objectives);
  return true;
}

}  // namespace fo4::forms
