#include "fo4/plugin/scene_writer.hpp"

#include "fo4/plugin/scene_layout.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "scene_writer_internal.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <array>
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

const ScalarEntry k_scalars[] = {
    {"SCEN\\Flags", Scope::head, sig("FNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\LastActionIndex", Scope::tail, sig("INAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\FovOverride", Scope::tail, sig("CRIS"), 0, ScalarStorage::f32, 4, k_any_action},
    {"SCEN\\Index", Scope::tail, sig("XNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\ParentQuestStageOnBegin", Scope::tail, sig("SCQS"), 0, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\ParentQuestStageOnEnd", Scope::tail, sig("SCQS"), 2, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\Phase\\EditorWidth", Scope::phase, sig("WNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Phase\\Flags", Scope::phase, sig("FNAM"), 0, ScalarStorage::u16, 2, k_any_action},
    {"SCEN\\Phase\\QuestStageOnStart", Scope::phase, sig("SCQS"), 0, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\Phase\\QuestStageOnCompletion", Scope::phase, sig("SCQS"), 2, ScalarStorage::i16, 4, k_any_action},
    {"SCEN\\Actor\\Alias", Scope::actor, sig("ALID"), 0, ScalarStorage::i32, 4, k_any_action},
    {"SCEN\\Actor\\Flags", Scope::actor, sig("LNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Actor\\BehaviorFlags", Scope::actor, sig("DNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\ActorAlias", Scope::action, sig("ALID"), 0, ScalarStorage::i32, 4, k_any_action},
    {"SCEN\\Action\\Index", Scope::action, sig("INAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\Flags", Scope::action, sig("FNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\StartPhase", Scope::action, sig("SNAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\EndPhase", Scope::action, sig("ENAM"), 0, ScalarStorage::u32, 4, k_any_action},
    {"SCEN\\Action\\LoopingMax", Scope::action_payload, sig("DMAX"), 0, ScalarStorage::f32, 4,
     k_dialogue_action | k_radio_action},
    {"SCEN\\Action\\LoopingMin", Scope::action_payload, sig("DMIN"), 0, ScalarStorage::f32, 4, k_dialogue_action},
    {"SCEN\\Action\\CameraFovOnPlayerCamera", Scope::action_payload, sig("CRIS"), 0, ScalarStorage::f32, 8,
     k_dialogue_action},
    {"SCEN\\Action\\CameraRateOfChange", Scope::action_payload, sig("CRIS"), 4, ScalarStorage::f32, 8,
     k_dialogue_action},
    {"SCEN\\Action\\EmotionType", Scope::action_payload, sig("DEMO"), 0, ScalarStorage::u32, 4, k_dialogue_action},
    {"SCEN\\Action\\EmotionValue", Scope::action_payload, sig("DEVA"), 0, ScalarStorage::u32, 4, k_dialogue_action},
    {"SCEN\\Action\\TimerMax", Scope::action_payload, sig("SNAM"), 0, ScalarStorage::f32, 4, k_timer_action},
    {"SCEN\\Action\\TimerMin", Scope::action_payload, sig("TNAM"), 0, ScalarStorage::f32, 4, k_timer_action},
    {"SCEN\\Action\\DialogueTargetActor", Scope::action_payload, sig("DTGT"), 0, ScalarStorage::i32, 4,
     k_player_dialogue_action},
};

const LinkEntry k_links[] = {
    {"SCEN\\ParentQuest", sig("PNAM"), sig("QUST"), Scope::tail, k_any_action},
    {"SCEN\\TemplateScene", sig("TNAM"), sig("SCEN"), Scope::tail, k_any_action},
    {"SCEN\\Action\\Topic", sig("DATA"), sig("DIAL"), Scope::action_payload,
     k_dialogue_action | k_radio_action},
    {"SCEN\\Action\\DialogueSubtype", sig("VENC"), sig("KYWD"), Scope::action_payload,
     k_dialogue_action | k_radio_action},
    {"SCEN\\Action\\AnimArchType", sig("PNAM"), sig("KYWD"), Scope::action_payload, k_dialogue_action},
    {"SCEN\\Action\\AudioOutputOverride", sig("ONAM"), sig("SOPM"), Scope::action_payload,
     k_dialogue_action | k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerPositiveResponse", sig("PTOP"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNegativeResponse", sig("NTOP"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNeutralResponse", sig("NETO"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerQuestionResponse", sig("QTOP"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerPositiveDialogueSubtype", sig("VENC"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNegativeDialogueSubtype", sig("PLVD"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerNeutralDialogueSubtype", sig("JOUT"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\PlayerQuestionDialogueSubtype", sig("DALC"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action | k_npc_response_action},
    {"SCEN\\Action\\NpcPositiveResponse", sig("NPOT"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNegativeResponse", sig("NNGT"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNeutralResponse", sig("NNUT"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcQuestionResponse", sig("NQUT"), sig("DIAL"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcPositiveDialogueSubtype", sig("NPOS"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNegativeDialogueSubtype", sig("NNGS"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcNeutralDialogueSubtype", sig("NNUS"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\NpcQuestionDialogueSubtype", sig("NQUS"), sig("KYWD"), Scope::action_payload,
     k_player_dialogue_action},
    {"SCEN\\Action\\StartSceneTopic", sig("STSC"), sig("DIAL"), Scope::action_payload, k_start_scene_action},
    {"SCEN\\Action\\PlaySound", sig("HTID"), sig("SNDR"), Scope::action_payload, k_radio_action},
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

std::optional<std::size_t> timer_payload_anchor(const Record& record, Range range, Signature wanted) {
  const auto snam = sig("SNAM"), tnam = sig("TNAM");
  const int wanted_order = wanted == snam ? 0 : 1;
  int previous = -1;
  std::size_t anchor = range.end;
  for (std::size_t i = range.begin; i < range.end; ++i) {
    const auto& sub = record.subrecords[i];
    const int current = sub.signature == snam ? 0 : sub.signature == tnam ? 1 : -1;
    if (current < 0 || current <= previous || sub.data.size() != 4) return std::nullopt;
    if (current > wanted_order && anchor == range.end) anchor = i;
    previous = current;
  }
  return anchor;
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

std::size_t phase_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index) {
  return detail::phase_anchor(layout, record, index);
}

std::vector<Subrecord> new_phase_subrecords(std::string_view name) {
  return {
      Subrecord{sig("HNAM"), {}, false},
      Subrecord{sig("NAM0"), zstring_payload(name), false},
      Subrecord{sig("NEXT"), {}, false},
      Subrecord{sig("NEXT"), {}, false},
      Subrecord{sig("WNAM"), u32_payload(350), false},
      Subrecord{sig("HNAM"), {}, false},
  };
}

Range phase_body(const env::detail::SceneLayout& layout, std::size_t index) {
  return Range{layout.phases[index].open + 1, layout.phases[index].close};
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
      Subrecord{sig("ALID"), i32_payload(-1), false},
      Subrecord{sig("LNAM"), u32_payload(0), false},
      Subrecord{sig("DNAM"), u32_payload(26), false},
  };
}

std::vector<Subrecord> new_action_subrecords(std::uint16_t type, std::uint32_t index) {
  return {
      Subrecord{sig("ANAM"), u16_payload(type), false},
      Subrecord{sig("ALID"), i32_payload(-1), false},
      Subrecord{sig("INAM"), u32_payload(index), false},
      Subrecord{sig("ANAM"), {}, false},
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

const std::array k_head_order{sig("EDID"), sig("VMAD"), sig("FNAM")};
const std::array k_phase_order{sig("NAM0"), sig("WNAM"), sig("FNAM"), sig("SCQS")};
const std::array k_action_header_order{sig("NAM0"), sig("ALID"), sig("INAM"), sig("FNAM"), sig("SNAM"), sig("ENAM")};
const std::array k_timer_order{sig("SNAM"), sig("SCQS"), sig("TNAM")};
const std::array k_tail_order{sig("PNAM"), sig("INAM"), sig("VNAM"), sig("CNAM"), sig("ACTV"), sig("CRIS"), sig("KSIZ"),
                              sig("KWDA"), sig("CTDA"), sig("SCQS"), sig("NNAM"), sig("TNAM"), sig("XNAM")};

std::span<const Signature> canonical_order(Scope scope) {
  switch (scope) {
    case Scope::head: return k_head_order;
    case Scope::phase: return k_phase_order;
    case Scope::action: return k_action_header_order;
    case Scope::action_payload: return k_timer_order;
    case Scope::tail: return k_tail_order;
    case Scope::actor: break;
  }
  return {};
}

std::optional<std::size_t> rank_of(std::span<const Signature> order, Signature tag) {
  const auto found = std::find(order.begin(), order.end(), tag);
  if (found == order.end()) return std::nullopt;
  return static_cast<std::size_t>(found - order.begin());
}

std::size_t canonical_anchor(const Record& record, Range range, Scope scope, Signature tag) {
  const auto order = canonical_order(scope);
  const auto self = rank_of(order, tag);
  if (self) {
    for (std::size_t at = range.begin; at < range.end; ++at) {
      const auto rank = rank_of(order, record.subrecords[at].signature);
      if (rank && *rank > *self) return at;
    }
  }
  return range.end;
}

struct MemberShape {
  Scope scope;
  Signature subrecord;
  std::size_t size;
  std::uint16_t action_types;
};

MemberShape member_shape(std::string_view path) {
  for (const auto& entry : k_scalars) {
    if (entry.path == path) return {entry.scope, entry.subrecord, entry.payload_size, entry.action_types};
  }
  for (const auto& entry : k_links) {
    if (entry.path == path) return {entry.scope, entry.subrecord, sizeof(std::uint32_t), entry.action_types};
  }
  throw schema::ScalarError("unknown scene field path: " + std::string(path));
}

std::vector<std::byte> new_member_payload(const Record& record,
                                          const env::detail::SceneLayout& layout,
                                          std::size_t group,
                                          const MemberShape& shape) {
  if (shape.subrecord == sig("ALID") || shape.subrecord == sig("DTGT")) return i32_payload(-1);
  if (shape.subrecord == sig("SCQS")) return std::vector<std::byte>(shape.size, std::byte{0xFF});
  if (shape.scope == Scope::action && shape.subrecord == sig("INAM")) {
    return u32_payload(detail::action_index_base(record, layout) + static_cast<std::uint32_t>(group));
  }
  return std::vector<std::byte>(shape.size);
}

void add_member(Record& record, std::size_t group, const MemberShape& shape, std::string_view path) {
  const auto layout = checked_layout(record);
  const auto range = scope_range(layout, record.subrecords.size(), shape.scope, group);
  if (!range) throw schema::ScalarError(std::string(path) + " has no scene scope to hold it");
  if (shape.scope == Scope::action && (shape.subrecord == sig("SNAM") || shape.subrecord == sig("ENAM")) &&
      layout.phases.empty()) {
    throw schema::ScalarError(std::string(path) + " needs a scene phase to reference first");
  }
  auto payload = new_member_payload(record, layout, group, shape);
  const auto anchor = canonical_anchor(record, *range, shape.scope, shape.subrecord);
  record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(anchor),
                           Subrecord{shape.subrecord, std::move(payload), false});
  const auto placed = scope_range(checked_layout(record), record.subrecords.size(), shape.scope, group);
  if (!placed || !find_in(record, *placed, shape.subrecord, path)) {
    throw schema::ScalarError(std::string(path) + " does not read back in its scene scope");
  }
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
  if (!index) {
    if (path == "SCEN\\Action\\StartPhase" &&
        action_type_matches(*record, layout, group_index, k_timer_action)) {
      const auto payload = *scope_range(layout, record->subrecords.size(), Scope::action_payload, group_index);
      if (!timer_payload_anchor(*record, payload, sig("SNAM"))) return ScalarMutationStatus::subrecord_missing;
      auto anchor = range->end;
      for (auto i = range->begin; i < range->end; ++i) {
        if (record->subrecords[i].signature == sig("ENAM")) { anchor = i; break; }
      }
      auto encoded = schema::encode_scalar(value, descriptor_for(entry));
      record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(anchor),
                                Subrecord{entry.subrecord, std::move(encoded), false});
      mark_record_modified(plugin, *record);
      return ScalarMutationStatus::updated;
    }
    if (path == "SCEN\\Action\\TimerMax" || path == "SCEN\\Action\\TimerMin") {
      const auto header = *scope_range(layout, record->subrecords.size(), Scope::action, group_index);
      if (entry.subrecord == sig("SNAM") && !find_in(*record, header, sig("SNAM"), path)) {
        return ScalarMutationStatus::subrecord_missing;
      }
      const auto anchor = timer_payload_anchor(*record, *range, entry.subrecord);
      if (!anchor) return ScalarMutationStatus::subrecord_missing;
      auto encoded = schema::encode_scalar(value, descriptor_for(entry));
      record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(*anchor),
                                Subrecord{entry.subrecord, std::move(encoded), false});
      mark_record_modified(plugin, *record);
      return ScalarMutationStatus::updated;
    }
    return ScalarMutationStatus::subrecord_missing;
  }
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
  if (record->header.signature != sig_scen) return FormLinkFieldMutationStatus::record_mismatch;
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
  if (record->header.signature != sig_scen) return FormLinkFieldMutationStatus::record_mismatch;
  if (entry.scope != Scope::action_payload) {
    throw schema::FormLinkFieldError(std::string(path) + " is not an action payload link");
  }
  const bool null_topic = path == "SCEN\\Action\\Topic" && target.owner().empty() && target.local().value() == 0;
  if (!null_topic) check_link_target(plugin, entry, path, target);
  try {
    const auto layout = checked_layout(*record);
    if (null_topic && (index >= layout.actions.size() ||
                       !action_type_matches(*record, layout, index, k_dialogue_action))) {
      check_link_target(plugin, entry, path, target);
    }
    if (index >= layout.actions.size()) return FormLinkFieldMutationStatus::subrecord_missing;
    if (entry.action_types != k_any_action &&
        !action_type_matches(*record, layout, index, entry.action_types)) {
      return FormLinkFieldMutationStatus::subrecord_missing;
    }
    const auto range = scope_range(layout, record->subrecords.size(), Scope::action_payload, index);
    if (!range) return FormLinkFieldMutationStatus::subrecord_missing;
    const auto at = find_in(*record, *range, entry.subrecord, path);
    if (!at) {
      if (path != "SCEN\\Action\\Topic" ||
          !action_type_matches(*record, layout, index, k_dialogue_action) || range->begin != range->end) {
        return FormLinkFieldMutationStatus::subrecord_missing;
      }
      const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(plugin)).value();
      record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(range->begin),
                                Subrecord{entry.subrecord, u32_payload(encoded), false});
      mark_record_modified(plugin, *record);
      return FormLinkFieldMutationStatus::updated;
    }
    return write_link(plugin, *record, *at, path, target);
  } catch (const schema::ScalarError& error) {
    throw schema::FormLinkFieldError(error.what());
  }
}

// Requires: one row per SCEN action payload member the writer accepts, so a caller's key table can be checked against it.
std::vector<ScenePayloadPath> scene_action_payload_paths() {
  std::vector<ScenePayloadPath> rows;
  for (const auto& entry : k_links) {
    if (entry.scope == Scope::action_payload) rows.push_back({entry.path, entry.subrecord, true});
  }
  for (const auto& entry : k_scalars) {
    if (entry.scope == Scope::action_payload) rows.push_back({entry.path, entry.subrecord, false});
  }
  return rows;
}
FormLinkFieldMutationStatus set_scene_action_packages(Plugin& plugin,
                                                       const FormKey& scene,
                                                       std::size_t action,
                                                       const std::vector<FormKey>& packages) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != sig_scen) return FormLinkFieldMutationStatus::record_mismatch;
  const auto layout = checked_layout(*record);
  if (action >= layout.actions.size() || !action_type_matches(*record, layout, action, action_type_bit(1))) {
    return FormLinkFieldMutationStatus::subrecord_missing;
  }
  const auto range = *scope_range(layout, record->subrecords.size(), Scope::action_payload, action);
  static_cast<void>(read_scene_action_packages(plugin, scene, action));
  std::size_t begin = range.end, end = range.end;
  for (std::size_t i = range.begin; i < range.end; ++i) {
    if (record->subrecords[i].signature != sig("PNAM")) continue;
    if (record->subrecords[i].data.size() != 4 || (begin != range.end && i != end)) {
      throw schema::FormLinkFieldError("SCEN action package list is malformed");
    }
    if (begin == range.end) begin = i;
    end = i + 1;
  }
  const LinkEntry package_entry{"SCEN action package", sig("PNAM"), sig("PACK"), Scope::action_payload, action_type_bit(1)};
  std::vector<Subrecord> replacements;
  replacements.reserve(packages.size());
  for (const auto& package : packages) {
    check_link_target(plugin, package_entry, package_entry.path, package);
    const auto encoded = encode_plugin_form_id(package, plugin_form_id_context(plugin)).value();
    if (encoded == 0) throw schema::FormLinkFieldError("SCEN action package link cannot encode as null");
    replacements.push_back(Subrecord{sig("PNAM"), u32_payload(encoded), false});
  }
  if (begin == range.end) begin = end = range.begin;
  if (end - begin == replacements.size() &&
      std::equal(replacements.begin(), replacements.end(), record->subrecords.begin() + static_cast<std::ptrdiff_t>(begin),
                 [](const Subrecord& a, const Subrecord& b) { return a.data == b.data; })) {
    return FormLinkFieldMutationStatus::unchanged;
  }
  auto updated = record->subrecords;
  auto pos = updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(begin),
                           updated.begin() + static_cast<std::ptrdiff_t>(end));
  updated.insert(pos, std::make_move_iterator(replacements.begin()), std::make_move_iterator(replacements.end()));
  record->subrecords = std::move(updated);
  mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
}

FormLinkFieldMutationStatus set_scene_action_headtracking(Plugin& plugin, const FormKey& scene,
                                                           std::size_t action,
                                                           const std::vector<std::int32_t>& aliases) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != sig_scen) return FormLinkFieldMutationStatus::record_mismatch;
  const auto layout = checked_layout(*record);
  if (action >= layout.actions.size()) return FormLinkFieldMutationStatus::subrecord_missing;
  const auto type0 = action_type_matches(*record, layout, action, k_dialogue_action);
  if (!type0 && !action_type_matches(*record, layout, action, k_player_dialogue_action | k_npc_response_action)) {
    return FormLinkFieldMutationStatus::subrecord_missing;
  }
  const auto tag = type0 ? sig("HTID") : sig("DTID");
  const auto range = *scope_range(layout, record->subrecords.size(), Scope::action_payload, action);
  const auto at = find_in(*record, range, tag, "SCEN action headtracking");
  if (at && record->subrecords[*at].data.size() % 4 != 0) {
    throw schema::FormLinkFieldError("SCEN action headtracking list is malformed");
  }
  if (aliases.size() > UINT32_MAX / 4) throw schema::FormLinkFieldError("SCEN action headtracking list is too large");
  std::vector<std::byte> bytes;
  for (const auto alias : aliases) {
    const auto part = u32_payload(static_cast<std::uint32_t>(alias));
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  if ((!at && bytes.empty()) || (at && record->subrecords[*at].data == bytes)) {
    return FormLinkFieldMutationStatus::unchanged;
  }
  auto updated = record->subrecords;
  if (at && bytes.empty()) updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(*at));
  else if (at) updated[*at].data = std::move(bytes);
  else {
    // Invariant: a dialogue action stores HTID before VENC, ONAM and PNAM, so a new list goes ahead of them.
    auto position = range.end;
    if (type0) {
      for (auto i = range.begin; i < range.end; ++i) {
        const auto signature = record->subrecords[i].signature;
        if (signature == sig("VENC") || signature == sig("ONAM") || signature == sig("PNAM")) {
          position = i;
          break;
        }
      }
    }
    updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(position), Subrecord{tag, std::move(bytes), false});
  }
  record->subrecords = std::move(updated);
  mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
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
  const auto found = find_in(*record, phase_body(layout, index), sig("NAM0"), "SCEN\\Phase\\Name");
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

namespace {

std::optional<std::int32_t> alias_of(const Subrecord& subrecord) {
  if (subrecord.data.size() != sizeof(std::int32_t)) return std::nullopt;
  std::int32_t value = 0;
  std::memcpy(&value, subrecord.data.data(), sizeof(value));
  return value;
}

}  // namespace

SubrecordGroupMutationStatus remove_scene_actor(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index) {
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = checked_layout(*record);
  if (index >= layout.actors.size()) return SubrecordGroupMutationStatus::index_out_of_range;
  const auto alias = alias_of(record->subrecords[layout.actors[index]]);
  const bool shared = std::any_of(layout.actors.begin(), layout.actors.end(), [&](std::size_t other) {
    return other != layout.actors[index] && alias_of(record->subrecords[other]) == alias;
  });
  if (alias && *alias >= 0 && !shared) {
    for (std::size_t action = 0; action < layout.actions.size(); ++action) {
      const auto& span = layout.actions[action];
      for (std::size_t at = span.open + 1; at < span.header_end; ++at) {
        if (record->subrecords[at].signature == sig("ALID") && alias_of(record->subrecords[at]) == alias) {
          throw schema::ScalarError("scene action " + std::to_string(action) +
                                    " references the actor alias this edit removes");
        }
      }
    }
  }
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
  const auto found = find_in(*record, Range{span.open + 1, span.header_end}, sig("NAM0"), "SCEN\\Action\\Name");
  if (!found) {
    record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(span.open + 1),
                              Subrecord{sig("NAM0"), std::move(payload), false});
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

ScalarMutationStatus ensure_scene_member(Plugin& plugin,
                                         const FormKey& scene,
                                         std::string_view path,
                                         std::size_t group_index) {
  const auto shape = member_shape(path);
  auto* record = locate_mutable_record(plugin, scene);
  if (record == nullptr) return ScalarMutationStatus::record_missing;
  if (record->header.signature != sig_scen) throw schema::ScalarError(std::string(path) + " target is not a SCEN record");
  const auto layout = checked_layout(*record);
  const auto range = scope_range(layout, record->subrecords.size(), shape.scope, group_index);
  if (!range) return ScalarMutationStatus::subrecord_missing;
  if (shape.action_types != k_any_action &&
      !action_type_matches(*record, layout, group_index, shape.action_types)) {
    return ScalarMutationStatus::subrecord_missing;
  }
  if (find_in(*record, *range, shape.subrecord, path)) return ScalarMutationStatus::unchanged;
  constexpr std::string_view start_phase_path = "SCEN\\Action\\StartPhase";
  Record candidate = *record;
  if (shape.scope == Scope::action_payload && shape.subrecord == sig("SNAM")) {
    const auto candidate_layout = checked_layout(candidate);
    const auto header = scope_range(candidate_layout, candidate.subrecords.size(), Scope::action, group_index);
    if (!find_in(candidate, *header, sig("SNAM"), start_phase_path)) {
      add_member(candidate, group_index, MemberShape{Scope::action, sig("SNAM"), sizeof(std::uint32_t), k_any_action},
                 start_phase_path);
    }
  }
  add_member(candidate, group_index, shape, path);
  record->subrecords = std::move(candidate.subrecords);
  mark_record_modified(plugin, *record);
  return ScalarMutationStatus::updated;
}

}
