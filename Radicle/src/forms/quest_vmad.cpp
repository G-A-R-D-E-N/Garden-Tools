#include "fo4/forms/quest.hpp"

#include "fo4/schema/vmad.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_vmad = Signature::from_string("VMAD");
constexpr RecordFlags::value_type k_quest_partial_form = 1u << 14;

void write_i16(std::vector<std::byte>& data, std::size_t offset, std::int16_t value) {
  if (offset + 2 > data.size()) {
    throw QuestError("QUST VMAD alias ID offset is outside the payload");
  }
  std::memcpy(data.data() + offset, &value, 2);
}

void write_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
  if (offset + 2 > data.size()) {
    throw QuestError("QUST VMAD Stage offset is outside the payload");
  }
  std::memcpy(data.data() + offset, &value, 2);
}

Subrecord* unique_vmad(Record& record) {
  Subrecord* found = nullptr;
  std::size_t count = 0;
  for (auto& sub : record.subrecords) {
    if (sub.signature != k_vmad) continue;
    ++count;
    if (found == nullptr) found = &sub;
  }
  if (count > 1) {
    throw QuestError("QUST VMAD cannot be rewritten: more than one VMAD subrecord");
  }
  return found;
}

struct AliasSlot {
  std::uint32_t id{0};
  std::string name;
};

std::vector<AliasSlot> slots_from_aliases(const QuestAliases& aliases) {
  std::vector<AliasSlot> slots;
  for (const auto& alias : aliases.aliases) {
    if (alias.kind == QuestAliasKind::collection) {
      for (const auto& entry : alias.collection_entries) {
        slots.push_back(AliasSlot{entry.id, {}});
      }
      continue;
    }
    slots.push_back(AliasSlot{alias.id, alias.name.value_or(std::string{})});
  }
  return slots;
}

std::vector<AliasSlot> slots_from_creations(std::span<const QuestAliasCreation> creations) {
  std::vector<AliasSlot> slots;
  for (const auto& creation : creations) {
    if (creation.kind == QuestAliasKind::collection) {
      for (const auto& entry : creation.collection_entries) {
        slots.push_back(AliasSlot{entry.id, {}});
      }
      continue;
    }
    slots.push_back(AliasSlot{
        creation.id,
        creation.name.has_value() ? std::string{*creation.name} : std::string{}});
  }
  return slots;
}

std::map<std::string, std::uint32_t> unique_names(const std::vector<AliasSlot>& slots,
                                                  bool& duplicate) {
  std::map<std::string, std::uint32_t> names;
  duplicate = false;
  std::set<std::string> seen;
  for (const auto& slot : slots) {
    if (slot.name.empty()) continue;
    if (!seen.insert(slot.name).second) {
      duplicate = true;
      names.erase(slot.name);
      continue;
    }
    names.emplace(slot.name, slot.id);
  }
  return names;
}

std::int16_t alias_id_i16(std::uint32_t id) {
  if (id > 0x7FFFu) {
    throw QuestError("QUST VMAD alias IDs must fit in a signed 16-bit field");
  }
  return static_cast<std::int16_t>(id);
}

bool subrecord_lists_equal(const std::vector<Subrecord>& lhs,
                           const std::vector<Subrecord>& rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].signature != rhs[i].signature || lhs[i].data != rhs[i].data) {
      return false;
    }
  }
  return true;
}

bool stage_body_equal(const QuestStage& lhs, const QuestStage& rhs) {
  if (lhs.flags != rhs.flags || lhs.unknown != rhs.unknown ||
      !subrecord_lists_equal(lhs.leading_members, rhs.leading_members) ||
      lhs.log_entries.size() != rhs.log_entries.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.log_entries.size(); ++i) {
    const auto& left = lhs.log_entries[i];
    const auto& right = rhs.log_entries[i];
    if (left.flags != right.flags ||
        !subrecord_lists_equal(left.members, right.members)) {
      return false;
    }
  }
  return true;
}

}  // namespace

void sync_quest_vmad_aliases(Record& record,
                             const QuestAliases& previous,
                             std::span<const QuestAliasCreation> next) {
  require_decodable(record);
  auto* vmad = unique_vmad(record);
  if (vmad == nullptr) return;
  const auto layout = inspect_quest_vmad(vmad->data);
  if (layout.unparseable || layout.unsupported) {
    throw QuestError("QUST VMAD cannot be rewritten: extras cannot be inspected");
  }
  if (layout.aliases.empty()) return;
  if (layout.object_format != 2) {
    throw QuestError(
        "QUST VMAD alias extras use object format 1 which cannot be rewritten");
  }

  const auto old_slots = slots_from_aliases(previous);
  const auto new_slots = slots_from_creations(next);
  bool old_dup = false;
  bool new_dup = false;
  const auto old_names = unique_names(old_slots, old_dup);
  const auto new_names = unique_names(new_slots, new_dup);
  std::set<std::uint32_t> new_ids;
  for (const auto& slot : new_slots) new_ids.insert(slot.id);

  std::map<std::uint32_t, std::uint32_t> id_map;
  if (!old_dup && !new_dup) {
    for (const auto& [name, old_id] : old_names) {
      const auto found = new_names.find(name);
      if (found != new_names.end()) id_map[old_id] = found->second;
    }
  }
  for (const auto& slot : old_slots) {
    if (id_map.contains(slot.id)) continue;
    if (new_ids.contains(slot.id)) id_map[slot.id] = slot.id;
  }

  for (const auto& binding : layout.aliases) {
    if (binding.alias_id < 0) {
      throw QuestError("QUST VMAD binds alias IDs that are not in the replacement aliases");
    }
    const auto old_id = static_cast<std::uint32_t>(binding.alias_id);
    const auto mapped = id_map.find(old_id);
    if (mapped == id_map.end()) {
      throw QuestError("QUST VMAD binds alias IDs that are not in the replacement aliases");
    }
    write_i16(vmad->data, binding.alias_id_offset, alias_id_i16(mapped->second));
  }
}

void sync_quest_vmad_stages(Record& record,
                            const QuestStages& previous,
                            const QuestStages& next) {
  require_decodable(record);
  auto* vmad = unique_vmad(record);
  if (vmad == nullptr) return;
  const auto layout = inspect_quest_vmad(vmad->data);
  if (layout.unparseable || layout.unsupported) {
    throw QuestError("QUST VMAD cannot be rewritten: extras cannot be inspected");
  }
  if (layout.fragments.empty()) return;
  if (record.header.flags.test(k_quest_partial_form) && previous.stages.empty()) {
    if (next.stages.empty()) return;
    throw QuestError(
        "QUST partial form VMAD binds master stages that this override does not carry");
  }

  std::set<std::uint16_t> new_indexes;
  for (const auto& stage : next.stages) {
    new_indexes.insert(stage.index);
  }

  for (const auto& fragment : layout.fragments) {
    if (new_indexes.contains(fragment.stage)) {
      write_u16(vmad->data, fragment.stage_offset, fragment.stage);
      continue;
    }
    const auto old_stage = std::find_if(
        previous.stages.begin(), previous.stages.end(),
        [&](const QuestStage& stage) { return stage.index == fragment.stage; });
    if (old_stage == previous.stages.end()) {
      throw QuestError(
          "QUST VMAD binds stage identity that is not in the replacement stages");
    }
    std::size_t old_hits = 0;
    std::size_t new_hits = 0;
    std::uint16_t new_index = 0;
    for (const auto& stage : previous.stages) {
      if (stage_body_equal(*old_stage, stage)) ++old_hits;
    }
    for (const auto& stage : next.stages) {
      if (!stage_body_equal(*old_stage, stage)) continue;
      ++new_hits;
      new_index = stage.index;
    }
    if (old_hits != 1 || new_hits != 1) {
      throw QuestError(
          "QUST VMAD binds stage identity that is not in the replacement stages");
    }
    write_u16(vmad->data, fragment.stage_offset, new_index);
  }
}

}  // namespace fo4::forms
