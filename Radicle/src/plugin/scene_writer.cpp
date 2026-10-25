#include "fo4/plugin/scene_writer.hpp"

#include "../environment/scene_layout.hpp"
#include "record_edit.hpp"
#include "scene_writer_internal.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4 {
namespace {

using detail::locate_mutable_record;
using detail::mark_record_modified;
using schema::ScalarStorage;

constexpr Signature k_scen = Signature::from_string("SCEN");

enum class Scope { head, tail, phase, actor, action, action_payload };

constexpr std::uint16_t action_type_bit(std::uint16_t type) {
  return static_cast<std::uint16_t>(1u << type);
}

constexpr std::uint16_t k_any_action = 0;
constexpr std::uint16_t k_dialogue_action = action_type_bit(0);
constexpr std::uint16_t k_timer_action = action_type_bit(2);
constexpr std::uint16_t k_player_dialogue_action = action_type_bit(3);
constexpr std::uint16_t k_start_scene_action = action_type_bit(4);
constexpr std::uint16_t k_npc_response_action = action_type_bit(5);
constexpr std::uint16_t k_radio_action = action_type_bit(6);

struct ScalarEntry {
  std::string_view path;
  Scope scope;
  Signature subrecord;
  std::size_t offset;
  ScalarStorage storage;
  std::size_t payload_size;
  std::uint16_t action_types;
};

struct LinkEntry {
  std::string_view path;
  Signature subrecord;
  Signature target;
  Scope scope;
  std::uint16_t action_types;
};

constexpr Signature s(const char* text) { return Signature::from_string(text); }

const ScalarEntry k_scalars[] = {
    {"SCEN\\Flags", Scope::head, s("FNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\LastActionIndex", Scope::tail, s("INAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\FovOverride", Scope::tail, s("CRIS"), 0, ScalarStorage::f32, 4, k_any_action},
    {"SCEN\\ParentQuestStageOnBegin", Scope::tail, s("SCQS"), 0, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\ParentQuestStageOnEnd", Scope::tail, s("SCQS"), 2, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\Phase\\EditorWidth", Scope::phase, s("WNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Phase\\Flags", Scope::phase, s("FNAM"), 0, ScalarStorage::u16, 2, k_any_action},
    {"SCEN\\Phase\\QuestStageOnStart", Scope::phase, s("SCQS"), 0, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\Phase\\QuestStageOnCompletion", Scope::phase, s("SCQS"), 2, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\Actor\\Alias", Scope::actor, s("ALID"), 0, ScalarStorage::i32, 4, k_any_action},
    {"SCEN\\Actor\\Flags", Scope::actor, s("LNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Actor\\BehaviorFlags", Scope::actor, s("DNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\ActorAlias", Scope::action, s("ALID"), 0, ScalarStorage::i32, 4, k_any_action},
    {"SCEN\\Action\\Index", Scope::action, s("INAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\Flags", Scope::action, s("FNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\StartPhase", Scope::action, s("SNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\EndPhase", Scope::action, s("ENAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\LoopingMax", Scope::action_payload, s("DMAX"), 0, ScalarStorage::f32, 4,
     k_dialogue_action | k_radio_action},
    {"SCEN\\Action\\LoopingMin", Scope::action_payload, s("DMIN"), 0, ScalarStorage::f32, 4, k_dialogue_action},
    {"SCEN\\Action\\CameraFovOnPlayerCamera", Scope::action_payload, s("CRIS"), 0, ScalarStorage::f32, 8,
     k_dialogue_action},
    {"SCEN\\Action\\CameraRateOfChange", Scope::action_payload, s("CRIS"), 4, ScalarStorage::f32, 8,
     k_dialogue_action},
    {"SCEN\\Action\\EmotionType", Scope::action_payload, s("DEMO"), 0, ScalarStorage::u32, 4, k_dialogue_action},
    {"SCEN\\Action\\EmotionValue", Scope::action_payload, s("DEVA"), 0, ScalarStorage::u32, 4, k_dialogue_action},
    {"SCEN\\Action\\TimerMax", Scope::action_payload, s("SNAM"), 0, ScalarStorage::f32, 4, k_timer_action},
    {"SCEN\\Action\\TimerMin", Scope::action_payload, s("TNAM"), 0, ScalarStorage::f32, 4, k_timer_action},
    {"SCEN\\Action\\DialogueTargetActor", Scope::action_payload, s("DTGT"), 0, ScalarStorage::i32, 4,
     k_player_dialogue_action},
};

const LinkEntry k_links[] = {
    {"SCEN\\ParentQuest", s("PNAM"), s("QUST"), Scope::tail, k_any_action},
    {"SCEN\\TemplateScene", s("TNAM"), s("SCEN"), Scope::tail, k_any_action},
    {"SCEN\\Action\\Topic", s("DATA"), s("DIAL"), Scope::action_payload,
     k_dialogue_action | k_radio_action},
    {"SCEN\\Action\\DialogueSubtype", s("VENC"), s("KYWD"), Scope::action_payload,
     k_dialogue_action | k_radio_action},
    {"SCEN\\Action\\AnimArchType", s("PNAM"), s("KYWD"), Scope::action_payload, k_dialogue_action},
    {"SCEN\\Action\\AudioOutputOverride", s("ONAM"), s("SOPM"), Scope::action_payload,
     k_dialogue_action | k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerPositiveResponse", s("PTOP"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNegativeResponse", s("NTOP"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNeutralResponse", s("NETO"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerQuestionResponse", s("QTOP"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerPositiveDialogueSubtype", s("VENC"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNegativeDialogueSubtype", s("PLVD"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNeutralDialogueSubtype", s("JOUT"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerQuestionDialogueSubtype", s("DALC"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\NpcPositiveResponse", s("NPOT"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNegativeResponse", s("NNGT"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNeutralResponse", s("NNUT"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcQuestionResponse", s("NQUT"), s("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcPositiveDialogueSubtype", s("NPOS"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNegativeDialogueSubtype", s("NNGS"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNeutralDialogueSubtype", s("NNUS"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcQuestionDialogueSubtype", s("NQUS"), s("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\StartSceneTopic", s("STSC"), s("DIAL"), Scope::action_payload, k_start_scene_action},
    {"SCEN\\Action\\PlaySound", s("HTID"), s("SNDR"), Scope::action_payload, k_radio_action},
};

struct Range {
  std::size_t begin{0}, end{0};
};

const ScalarEntry& scalar_entry(std::string_view path) {
  for (const auto& entry : k_scalars) {
    if (entry.path == path) return entry;
  }
  throw schema::ScalarError("unknown scene field path: " + std::string(path));
}

const LinkEntry& link_entry(std::string_view path) {
  for (const auto& entry : k_links) {
    if (entry.path == path) return entry;
  }
  throw schema::FormLinkFieldError("unknown scene link path: " + std::string(path));
}

schema::ScalarDescriptor descriptor_for(const ScalarEntry& entry) {
  schema::ScalarDescriptor out;
  out.subrecord = entry.subrecord;
  out.field_name = entry.path;
  out.storage = entry.storage;
  out.offset = entry.offset;
  out.embedded = true;
  out.payload_size = entry.payload_size;
  switch (entry.storage) {
    case ScalarStorage::u16: out.maximum = 65535.0; break;
    case ScalarStorage::i16: out.minimum = -32768.0; out.maximum = 32767.0; break;
    case ScalarStorage::i32: out.minimum = -2147483648.0; out.maximum = 2147483647.0; break;
    case ScalarStorage::f32: out.minimum = -1e9; out.maximum = 1e9; break;
    default: out.maximum = 4294967295.0; break;
  }
  return out;
}

env::detail::SceneLayout checked_layout(const Record& record) {
  return detail::checked_scene_layout(record);
}

std::optional<Range> scope_range(const env::detail::SceneLayout& layout,
                                 std::size_t size,
                                 Scope scope,
                                 std::size_t group) {
  switch (scope) {
    case Scope::head:
      return group == 0 ? std::optional{Range{0, layout.first_group}} : std::nullopt;
    case Scope::tail:
      return group == 0 ? std::optional{Range{layout.tail, size}} : std::nullopt;
    case Scope::phase:
      if (group >= layout.phases.size()) return std::nullopt;
      return Range{layout.phases[group].open + 1, layout.phases[group].close};
    case Scope::actor:
      if (group >= layout.actors.size()) return std::nullopt;
      return Range{layout.actors[group], layout.actors[group] + 3};
    case Scope::action:
      if (group >= layout.actions.size()) return std::nullopt;
      return Range{layout.actions[group].open + 1, layout.actions[group].header_end};
    case Scope::action_payload:
      if (group >= layout.actions.size()) return std::nullopt;
      return Range{layout.actions[group].header_end, layout.actions[group].close};
  }
  return std::nullopt;
}

bool action_type_matches(const Record& record,
                         const env::detail::SceneLayout& layout,
                         std::size_t group,
                         std::uint16_t types) {
  std::uint16_t type = 0;
  const auto& data = record.subrecords[layout.actions[group].open].data;
  std::memcpy(&type, data.data(), sizeof(type));
  return type < 16 && (types & action_type_bit(type)) != 0;
}

std::optional<std::size_t> find_in(const Record& record, Range range, Signature tag, std::string_view path) {
  std::optional<std::size_t> found;
  for (std::size_t at = range.begin; at < range.end; ++at) {
    if (record.subrecords[at].signature != tag) continue;
    if (found) throw schema::ScalarError(std::string(path) + " subrecord is duplicated in its scene scope");
    found = at;
  }
  return found;
}

std::vector<std::byte> zstring_payload(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> u32_payload(std::uint32_t value) {
  return {std::byte{static_cast<std::uint8_t>(value & 0xFFu)},
          std::byte{static_cast<std::uint8_t>((value >> 8) & 0xFFu)},
          std::byte{static_cast<std::uint8_t>((value >> 16) & 0xFFu)},
          std::byte{static_cast<std::uint8_t>((value >> 24) & 0xFFu)}};
}

std::size_t tail_anchor(const Record& record) { return detail::tail_anchor(record); }

std::size_t phase_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index) {
  return detail::phase_anchor(layout, record, index);
}

std::vector<Subrecord> new_phase_subrecords(std::string_view name) {
  return {
      Subrecord{s("HNAM"), {}, false},
      Subrecord{s("NAM0"), zstring_payload(name), false},
      Subrecord{s("NEXT"), {}, false},
      Subrecord{s("NEXT"), {}, false},
      Subrecord{s("WNAM"), u32_payload(350), false},
      Subrecord{s("HNAM"), {}, false},
  };
}

Range phase_body(const env::detail::SceneLayout& layout, std::size_t index) {
  return Range{layout.phases[index].open + 1, layout.phases[index].close};
}

std::size_t groups_end(const env::detail::SceneLayout& layout, const Record& record) {
  return detail::groups_end(layout, record);
}

std::size_t actor_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index) {
  return detail::actor_anchor(layout, record, index);
}

std::size_t action_anchor(const env::detail::SceneLayout& layout,
                          const Record& record,
                          std::size_t index) {
  return detail::action_anchor(layout, record, index);
}

std::vector<std::byte> i32_payload(std::int32_t value) {
  const auto bits = static_cast<std::uint32_t>(value);
  return u32_payload(bits);
}

std::vector<std::byte> u16_payload(std::uint16_t value) {
  return {std::byte{static_cast<std::uint8_t>(value & 0xFFu)},
          std::byte{static_cast<std::uint8_t>((value >> 8) & 0xFFu)}};
}

std::vector<Subrecord> new_actor_subrecords() {
  return {
      Subrecord{s("ALID"), i32_payload(-1), false},
      Subrecord{s("LNAM"), u32_payload(0), false},
      Subrecord{s("DNAM"), u32_payload(26), false},
  };
}

std::vector<Subrecord> new_action_subrecords(std::uint16_t type, std::uint32_t index) {
  return {
      Subrecord{s("ANAM"), u16_payload(type), false},
      Subrecord{s("ALID"), i32_payload(-1), false},
      Subrecord{s("INAM"), u32_payload(index), false},
      Subrecord{s("ANAM"), {}, false},
  };
}

SubrecordGroupMutationStatus insert_at(Plugin& plugin,
                                       Record& record,
                                       std::size_t anchor,
                                       std::vector<Subrecord> added) {
  record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(anchor),
                           std::make_move_iterator(added.begin()),
                           std::make_move_iterator(added.end()));
  mark_record_modified(plugin, record);
  return SubrecordGroupMutationStatus::updated;
}

void check_link_target(Plugin& plugin,
                       const LinkEntry& entry,
                       std::string_view path,
                       const FormKey& target) {
  if (target.owner().empty()) {
    throw schema::FormLinkFieldError(std::string(path) +
                                     (target.local().value() == 0 ? " does not allow NULL" : " target has no owner"));
  }
  if (target.owner() != plugin.key) return;
  const auto* local_target = find_record(plugin, target);
  if (local_target == nullptr || local_target->header.signature != entry.target) {
    throw schema::FormLinkFieldError(std::string(path) + " target type is not allowed");
  }
}

FormLinkFieldMutationStatus write_link(Plugin& plugin,
                                       Record& record,
                                       std::size_t at,
                                       std::string_view path,
                                       const FormKey& target) {
  auto& data = record.subrecords[at].data;
  if (data.size() != 4) throw schema::FormLinkFieldError(std::string(path) + " has unexpected binary width");
  const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(plugin)).value();
  std::uint32_t current = 0;
  std::memcpy(&current, data.data(), sizeof(current));
  if (current == encoded) return FormLinkFieldMutationStatus::unchanged;
  std::memcpy(data.data(), &encoded, sizeof(encoded));
  mark_record_modified(plugin, record);
  return FormLinkFieldMutationStatus::updated;
}

}

ScalarMutationStatus set_scene_scalar(Plugin& plugin,
                                      const FormKey& scene,
                                      std::string_view path,
                                      std::size_t group_index,
                                      std::string_view value) {
  const auto& entry = scalar_entry(path);
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return ScalarMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  const auto range = scope_range(layout, record->subrecords.size(), entry.scope, group_index);
  if (!range) return ScalarMutationStatus::subrecord_missing;
  if (entry.action_types != k_any_action &&
      !action_type_matches(*record, layout, group_index, entry.action_types)) {
    return ScalarMutationStatus::subrecord_missing;
  }
  const auto index = find_in(*record, *range, entry.subrecord, path);
  if (!index) return ScalarMutationStatus::subrecord_missing;
  auto& data = record->subrecords[*index].data;
  if (data.size() != entry.payload_size) {
    throw schema::ScalarError(std::string(path) + " has unexpected binary width");
  }
  const auto encoded = schema::encode_scalar(value, descriptor_for(entry));
  const auto at = data.begin() + static_cast<std::ptrdiff_t>(entry.offset);
  if (std::equal(encoded.begin(), encoded.end(), at)) return ScalarMutationStatus::unchanged;
  std::copy(encoded.begin(), encoded.end(), at);
  mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

FormLinkFieldMutationStatus set_scene_formlink(Plugin& plugin,
                                               const FormKey& scene,
                                               std::string_view path,
                                               const FormKey& target) {
  const auto& entry = link_entry(path);
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != k_scen) return FormLinkFieldMutationStatus::record_mismatch;
  if (entry.scope != Scope::tail) {
    throw schema::FormLinkFieldError(std::string(path) + " requires an action index");
  }
  check_link_target(plugin, entry, path, target);
  std::optional<std::size_t> index;
  try {
    const auto layout = checked_layout(*record);
    index = find_in(*record, Range{layout.tail, record->subrecords.size()}, entry.subrecord, path);
  } catch (const schema::ScalarError& error) {
    throw schema::FormLinkFieldError(error.what());
  }
  if (!index) return FormLinkFieldMutationStatus::subrecord_missing;
  return write_link(plugin, *record, *index, path, target);
}

FormLinkFieldMutationStatus set_scene_action_formlink(Plugin& plugin,
                                                      const FormKey& scene,
                                                      std::size_t index,
                                                      std::string_view path,
                                                      const FormKey& target) {
  const auto& entry = link_entry(path);
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != k_scen) return FormLinkFieldMutationStatus::record_mismatch;
  if (entry.scope != Scope::action_payload) {
    throw schema::FormLinkFieldError(std::string(path) + " is not an action payload link");
  }
  check_link_target(plugin, entry, path, target);
  try {
    const auto layout = checked_layout(*record);
    if (index >= layout.actions.size()) return FormLinkFieldMutationStatus::subrecord_missing;
    if (entry.action_types != k_any_action &&
        !action_type_matches(*record, layout, index, entry.action_types)) {
      return FormLinkFieldMutationStatus::subrecord_missing;
    }
    const auto range = scope_range(layout, record->subrecords.size(), Scope::action_payload, index);
    if (!range) return FormLinkFieldMutationStatus::subrecord_missing;
    const auto at = find_in(*record, *range, entry.subrecord, path);
    if (!at) return FormLinkFieldMutationStatus::subrecord_missing;
    return write_link(plugin, *record, *at, path, target);
  } catch (const schema::ScalarError& error) {
    throw schema::FormLinkFieldError(error.what());
  }
}

std::optional<std::size_t> scene_group_count(const Plugin& plugin, const FormKey& scene, ScenePart part) {
  const auto* record = find_record(plugin, scene);
  if (record == nullptr) return std::nullopt;
  const auto layout = checked_layout(*record);
  switch (part) {
    case ScenePart::phase: return layout.phases.size();
    case ScenePart::actor: return layout.actors.size();
    case ScenePart::action: return layout.actions.size();
  }
  return std::nullopt;
}

SubrecordGroupMutationStatus insert_scene_phase(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index,
                                                std::string_view name) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index > layout.phases.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto count = layout.phases.size();
  const auto references = detail::action_phase_references(*record, layout, count);
  std::vector<std::uint32_t> mapping(count);
  for (std::size_t old = 0; old < count; ++old) {
    mapping[old] = static_cast<std::uint32_t>(old < index ? old : old + 1);
  }
  detail::check_phase_mapping(references, mapping);
  detail::remap_scene_phase_vmad(plugin, *record, mapping);
  detail::remap_phase_references(*record, references, mapping);
  return insert_at(plugin, *record, phase_anchor(layout, *record, index), new_phase_subrecords(name));
}

SubrecordGroupMutationStatus remove_scene_phase(Plugin& plugin, const FormKey& scene, std::size_t index) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index >= layout.phases.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto count = layout.phases.size();
  const auto references = detail::action_phase_references(*record, layout, count);
  std::vector<std::uint32_t> mapping(count);
  for (std::size_t old = 0; old < count; ++old) {
    if (old < index) {
      mapping[old] = static_cast<std::uint32_t>(old);
    } else if (old == index) {
      mapping[old] = k_scene_phase_removed;
    } else {
      mapping[old] = static_cast<std::uint32_t>(old - 1);
    }
  }
  detail::check_phase_mapping(references, mapping);
  detail::remap_scene_phase_vmad(plugin, *record, mapping);
  detail::remap_phase_references(*record, references, mapping);
  const auto begin = record->subrecords.begin() + static_cast<std::ptrdiff_t>(layout.phases[index].open);
  const auto end = record->subrecords.begin() + static_cast<std::ptrdiff_t>(layout.phases[index].close) + 1;
  record->subrecords.erase(begin, end);
  mark_record_modified(plugin, *record);
  return SubrecordGroupMutationStatus::updated;
}

ScalarMutationStatus set_scene_phase_name(Plugin& plugin,
                                          const FormKey& scene,
                                          std::size_t index,
                                          std::string_view name) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return ScalarMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index >= layout.phases.size()) return ScalarMutationStatus::subrecord_missing;
  const auto found = find_in(*record, phase_body(layout, index), s("NAM0"), "SCEN\\Phase\\Name");
  if (!found) return ScalarMutationStatus::subrecord_missing;
  auto payload = zstring_payload(name);
  auto& data = record->subrecords[*found].data;
  if (std::equal(payload.begin(), payload.end(), data.begin(), data.end())) {
    return ScalarMutationStatus::unchanged;
  }
  data = std::move(payload);
  mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

SubrecordGroupMutationStatus insert_scene_actor(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index > layout.actors.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  return insert_at(plugin, *record, actor_anchor(layout, *record, index), new_actor_subrecords());
}

SubrecordGroupMutationStatus remove_scene_actor(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index >= layout.actors.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto begin = record->subrecords.begin() + static_cast<std::ptrdiff_t>(layout.actors[index]);
  record->subrecords.erase(begin, begin + 3);
  mark_record_modified(plugin, *record);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus insert_scene_action(Plugin& plugin,
                                                 const FormKey& scene,
                                                 std::size_t index,
                                                 std::uint16_t type) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  if (type >= 7) throw schema::ScalarError("scene action type is not in the stored enum");
  const auto layout = checked_layout(*record);
  if (index > layout.actions.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto base = detail::action_index_base(*record, layout);
  const auto status = insert_at(plugin, *record, action_anchor(layout, *record, index),
                                new_action_subrecords(type, base + static_cast<std::uint32_t>(index)));
  const auto moved = checked_layout(*record);
  detail::renumber_actions(*record, moved, base);
  mark_record_modified(plugin, *record);
  return status;
}

SubrecordGroupMutationStatus remove_scene_action(Plugin& plugin,
                                                 const FormKey& scene,
                                                 std::size_t index) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index >= layout.actions.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto base = detail::action_index_base(*record, layout);
  const auto begin = record->subrecords.begin() + static_cast<std::ptrdiff_t>(layout.actions[index].open);
  const auto end = record->subrecords.begin() + static_cast<std::ptrdiff_t>(layout.actions[index].close) + 1;
  record->subrecords.erase(begin, end);
  const auto moved = checked_layout(*record);
  detail::renumber_actions(*record, moved, base);
  mark_record_modified(plugin, *record);
  return SubrecordGroupMutationStatus::updated;
}

ScalarMutationStatus set_scene_action_name(Plugin& plugin,
                                           const FormKey& scene,
                                           std::size_t index,
                                           std::string_view name) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return ScalarMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index >= layout.actions.size()) return ScalarMutationStatus::subrecord_missing;
  const auto span = layout.actions[index];
  auto payload = zstring_payload(name);
  const auto found = find_in(*record, Range{span.open + 1, span.header_end}, s("NAM0"), "SCEN\\Action\\Name");
  if (!found) {
    record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(span.open + 1),
                              Subrecord{s("NAM0"), std::move(payload), false});
    mark_record_modified(plugin, *record);
    return ScalarMutationStatus::updated;
  }
  auto& data = record->subrecords[*found].data;
  if (std::equal(payload.begin(), payload.end(), data.begin(), data.end())) {
    return ScalarMutationStatus::unchanged;
  }
  data = std::move(payload);
  mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

}
