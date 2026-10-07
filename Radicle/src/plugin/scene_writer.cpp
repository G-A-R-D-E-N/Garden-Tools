#include "fo4/plugin/scene_writer.hpp"

#include "../environment/scene_layout.hpp"
#include "record_edit.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace fo4 {
namespace {

using detail::locate_mutable_record;
using detail::mark_record_modified;
using schema::ScalarStorage;

constexpr Signature k_scen = Signature::from_string("SCEN");

enum class Scope { head, tail, phase, actor, action };

struct ScalarEntry {
  std::string_view path;
  Scope scope;
  Signature subrecord;
  std::size_t offset;
  ScalarStorage storage;
  std::size_t payload_size;
};

struct LinkEntry {
  std::string_view path;
  Signature subrecord;
  Signature target;
};

constexpr Signature s(const char* text) { return Signature::from_string(text); }

const ScalarEntry k_scalars[] = {
    {"SCEN\\Flags", Scope::head, s("FNAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\LastActionIndex", Scope::tail, s("INAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\FovOverride", Scope::tail, s("CRIS"), 0, ScalarStorage::f32, 4},
    {"SCEN\\ParentQuestStageOnBegin", Scope::tail, s("SCQS"), 0, ScalarStorage::i16, 4},
    {"SCEN\\ParentQuestStageOnEnd", Scope::tail, s("SCQS"), 2, ScalarStorage::i16, 4},
    {"SCEN\\Phase\\EditorWidth", Scope::phase, s("WNAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\Phase\\Flags", Scope::phase, s("FNAM"), 0, ScalarStorage::u16, 2},
    {"SCEN\\Phase\\QuestStageOnStart", Scope::phase, s("SCQS"), 0, ScalarStorage::i16, 4},
    {"SCEN\\Phase\\QuestStageOnCompletion", Scope::phase, s("SCQS"), 2, ScalarStorage::i16, 4},
    {"SCEN\\Actor\\Alias", Scope::actor, s("ALID"), 0, ScalarStorage::i32, 4},
    {"SCEN\\Actor\\Flags", Scope::actor, s("LNAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\Actor\\BehaviorFlags", Scope::actor, s("DNAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\Action\\ActorAlias", Scope::action, s("ALID"), 0, ScalarStorage::i32, 4},
    {"SCEN\\Action\\Index", Scope::action, s("INAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\Action\\Flags", Scope::action, s("FNAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\Action\\StartPhase", Scope::action, s("SNAM"), 0, ScalarStorage::u32, 4},
    {"SCEN\\Action\\EndPhase", Scope::action, s("ENAM"), 0, ScalarStorage::u32, 4},
};

const LinkEntry k_links[] = {
    {"SCEN\\ParentQuest", s("PNAM"), s("QUST")},
    {"SCEN\\TemplateScene", s("TNAM"), s("SCEN")},
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
  if (record.header.signature != k_scen) {
    throw schema::ScalarError("scene fields do not apply to " + record.header.signature.to_string() + " records");
  }
  try {
    return env::detail::scene_layout(record.subrecords);
  } catch (const env::detail::SceneLayoutError& error) {
    throw schema::ScalarError("malformed scene " + error.tag.to_string() + ": " + error.what());
  }
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
  }
  return std::nullopt;
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

}  // namespace

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
  if (target.owner().empty()) {
    throw schema::FormLinkFieldError(std::string(path) +
                                     (target.local().value() == 0 ? " does not allow NULL" : " target has no owner"));
  }
  if (target.owner() == plugin.key) {
    const auto* local_target = find_record(plugin, target);
    if (local_target == nullptr || local_target->header.signature != entry.target) {
      throw schema::FormLinkFieldError(std::string(path) + " target type is not allowed");
    }
  }
  std::optional<std::size_t> index;
  try {
    const auto layout = checked_layout(*record);
    index = find_in(*record, Range{layout.tail, record->subrecords.size()}, entry.subrecord, path);
  } catch (const schema::ScalarError& error) {
    throw schema::FormLinkFieldError(error.what());
  }
  if (!index) return FormLinkFieldMutationStatus::subrecord_missing;
  auto& data = record->subrecords[*index].data;
  if (data.size() != 4) throw schema::FormLinkFieldError(std::string(path) + " has unexpected binary width");

  const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(plugin)).value();
  std::uint32_t current = 0;
  std::memcpy(&current, data.data(), sizeof(current));
  if (current == encoded) return FormLinkFieldMutationStatus::unchanged;
  std::memcpy(data.data(), &encoded, sizeof(encoded));
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

}  // namespace fo4
