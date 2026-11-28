#include "fo4/forms/scene.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "../plugin/scene_writer_internal.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/scalar.hpp"
#include "placed_reference_copy_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {

namespace {

using Located = std::optional<fo4::detail::MutableLocation>;

template <typename Value>
void append_scalar(std::vector<std::byte>& out, Value value) {
  const auto at = out.size();
  out.resize(at + sizeof(Value));
  std::memcpy(out.data() + at, &value, sizeof(Value));
}

template <typename... Values>
std::vector<std::byte> packed(Values... values) {
  std::vector<std::byte> out;
  ((append_scalar(out, values)), ...);
  return out;
}

std::vector<std::byte> zstring_bytes(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

bool contains_nul(std::string_view text) {
  return text.find('\0') != std::string_view::npos;
}

std::uint32_t raw_form_id(const FormKey& key, const Plugin& plugin) {
  return encode_plugin_form_id(key, plugin_form_id_context(plugin)).value();
}

Record make_record(const Plugin& plugin,
                   const FormKey& key,
                   Signature signature,
                   std::vector<Subrecord> subrecords) {
  Record record;
  record.header.signature = signature;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(plugin));
  record.header.form_version = plugin.header.form_version;
  record.form_key = key;
  record.subrecords = std::move(subrecords);
  return record;
}

Group& ensure_adjacent_group(Group& parent,
                             std::size_t record_index,
                             const GroupHeader& wanted) {
  std::optional<std::size_t> found;
  for (std::size_t index = 0; index < parent.children.size(); ++index) {
    const auto* group = std::get_if<Group>(&parent.children[index]);
    if (group == nullptr || !detail::same_group_identity(group->header, wanted)) continue;
    if (found.has_value()) {
      throw std::runtime_error("duplicate quest child group");
    }
    found = index;
  }
  if (found.has_value()) {
    if (*found != record_index + 1) {
      throw std::runtime_error("quest child group is not adjacent to its record");
    }
    return std::get<Group>(parent.children[*found]);
  }
  Group created;
  created.header = wanted;
  const auto it = parent.children.insert(
      parent.children.begin() + static_cast<std::ptrdiff_t>(record_index + 1),
      GroupChild{std::move(created)});
  return std::get<Group>(*it);
}

std::optional<SceneAuthorStatus> refusal(const Located& location,
                                         Signature signature,
                                         SceneAuthorStatus missing) {
  if (!location.has_value() || location->record().header.signature != signature) {
    return missing;
  }
  if (location->record().undecodable) return SceneAuthorStatus::undecodable;
  return std::nullopt;
}

void commit(Plugin& plugin, Plugin& next) {
  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
}

SceneCreateResult scene_impl(Plugin& plugin, const FormKey& quest, const SceneSpec& spec) {
  if (quest.owner() != plugin.key) return {SceneAuthorStatus::not_local, {}};
  if (contains_nul(spec.editor_id)) return {SceneAuthorStatus::invalid_argument, {}};
  auto next = plugin;
  assign_record_form_keys(next);
  auto quest_location = fo4::detail::find_mutable_location(next, quest);
  if (auto refused = refusal(quest_location, sig_qust, SceneAuthorStatus::quest_missing)) {
    return {*refused, {}};
  }
  if (quest_location->path.size() != 1) {
    throw std::runtime_error("QUST is not a top-level group member");
  }

  const auto key = reserve_local_form_key(next);
  const auto raw_quest = raw_form_id(quest, next);
  auto& quest_children = ensure_adjacent_group(
      *quest_location->path.front(), quest_location->index,
      detail::group_header(GroupType::quest_children, raw_quest));

  std::vector<Subrecord> subrecords;
  if (!spec.editor_id.empty()) {
    subrecords.push_back(Subrecord{sig_edid, zstring_bytes(stored_editor_id(spec.editor_id)), false});
  }
  subrecords.push_back(Subrecord{sig_fnam, packed(spec.flags), false});
  subrecords.push_back(Subrecord{sig_pnam, packed(raw_quest), false});
  subrecords.push_back(Subrecord{sig_inam, packed(std::uint32_t{0}), false});
  subrecords.push_back(Subrecord{sig_vnam, std::vector<std::byte>(16), false});
  subrecords.push_back(Subrecord{sig_xnam, packed(spec.index), false});

  quest_children.children.emplace_back(make_record(next, key, sig_scen, std::move(subrecords)));
  commit(plugin, next);
  return {SceneAuthorStatus::ok, key};
}

}  // namespace

SceneCreateResult create_scene(Plugin& plugin, const FormKey& quest, const SceneSpec& spec) {
  try {
    return scene_impl(plugin, quest, spec);
  } catch (const std::exception&) {
    return {SceneAuthorStatus::structure_invalid, {}};
  }
}

namespace {

constexpr auto k_lcep = Signature::from_string("LCEP");
constexpr auto k_intt = Signature::from_string("INTT");
constexpr auto k_sspn = Signature::from_string("SSPN");
constexpr auto k_citc = Signature::from_string("CITC");
constexpr auto k_htid = Signature::from_string("HTID");
constexpr std::uint16_t k_start_scene_action = 4;

struct EntryRange {
  std::size_t begin{0};
  std::size_t end{0};
};

struct StartSceneTarget {
  Record* record{nullptr};
  env::detail::SceneActionSpan span{};
  std::vector<EntryRange> ranges;
};

// Constraint: the stored action type is the two-byte ANAM payload that opens the action span.
std::uint16_t stored_action_type(const Record& record, const env::detail::SceneActionSpan& span) {
  const auto& data = record.subrecords[span.open].data;
  if (data.size() != 2) throw schema::ScalarError("SCEN action opener is not a two-byte type");
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[0]) |
                                    std::to_integer<std::uint8_t>(data[1]) << 8);
}

// Requires: at most one such subrecord in the scanned scope, so a duplicate is refused instead of guessed at.
std::optional<std::size_t> scope_marker(const Record& record, std::size_t begin, std::size_t end, Signature tag) {
  std::optional<std::size_t> found;
  for (std::size_t at = begin; at < end; ++at) {
    if (record.subrecords[at].signature != tag) continue;
    if (found.has_value()) {
      throw schema::ScalarError("SCEN action payload repeats the " + tag.to_string() + " subrecord");
    }
    found = at;
  }
  return found;
}

// Constraint: an entry runs from its LCEP to the next LCEP, and a trailing HTID marker belongs to the action, not to the entry.
std::vector<EntryRange> entry_ranges(const Record& record, const env::detail::SceneActionSpan& span) {
  const auto htid = scope_marker(record, span.header_end, span.close, k_htid);
  const auto limit = htid.value_or(span.close);
  std::vector<std::size_t> opens;
  for (std::size_t at = span.header_end; at < limit; ++at) {
    if (record.subrecords[at].signature == k_lcep) opens.push_back(at);
  }
  std::vector<EntryRange> ranges;
  ranges.reserve(opens.size());
  for (std::size_t at = 0; at < opens.size(); ++at) {
    ranges.push_back({opens[at], at + 1 < opens.size() ? opens[at + 1] : limit});
  }
  return ranges;
}

std::string payload_text(const std::vector<std::byte>& data) {
  std::string out;
  out.reserve(data.size());
  for (const auto byte : data) {
    const auto ch = static_cast<char>(std::to_integer<std::uint8_t>(byte));
    if (ch == '\0') break;
    out.push_back(ch);
  }
  return out;
}

void check_scene_target(const Plugin& plugin, const FormKey& target) {
  if (target.owner().empty() || target.local().value() == 0) {
    throw schema::FormLinkFieldError("SCEN\\Action\\StartScene\\Scene must name a scene");
  }
  if (target.owner() != plugin.key) return;
  const auto* record = find_record(plugin, target);
  if (record == nullptr || record->header.signature != sig_scen) {
    throw schema::FormLinkFieldError("SCEN\\Action\\StartScene\\Scene target is not a local SCEN record");
  }
}

std::vector<Subrecord> entry_subrecords(const Plugin& plugin, const SceneStartSceneEntry& entry) {
  check_scene_target(plugin, entry.scene);
  if (entry.phase_name && contains_nul(*entry.phase_name)) {
    throw schema::ScalarError("SCEN\\Action\\StartScene\\PhaseName must not hold a NUL byte");
  }
  const auto raw = encode_plugin_form_id(entry.scene, plugin_form_id_context(plugin));
  if (raw.is_null()) throw schema::FormLinkFieldError("SCEN\\Action\\StartScene\\Scene target does not encode");
  std::vector<Subrecord> out;
  out.push_back(Subrecord{k_lcep, packed(raw.value()), false});
  out.push_back(Subrecord{k_intt, packed(entry.phase_index), false});
  if (entry.phase_name) out.push_back(Subrecord{k_sspn, zstring_bytes(*entry.phase_name), false});
  out.push_back(Subrecord{k_citc, packed(static_cast<std::uint32_t>(entry.conditions.size())), false});
  auto run = serialize_ctda_run(entry.conditions);
  out.insert(out.end(), std::make_move_iterator(run.begin()), std::make_move_iterator(run.end()));
  return out;
}

std::optional<std::vector<SceneStartSceneEntry>> read_entries(const Plugin& plugin,
                                                             const Record& record,
                                                             std::size_t action_index) {
  const auto layout = fo4::detail::checked_scene_layout(record);
  if (action_index >= layout.actions.size()) return std::nullopt;
  const auto span = layout.actions[action_index];
  if (stored_action_type(record, span) != k_start_scene_action) return std::nullopt;
  const auto context = plugin_form_id_context(plugin);
  std::vector<SceneStartSceneEntry> entries;
  for (const auto& range : entry_ranges(record, span)) {
    SceneStartSceneEntry entry;
    const auto scene = scope_marker(record, range.begin, range.end, k_lcep);
    if (!scene.has_value()) throw schema::ScalarError("SCEN\\Action\\StartScene entry has no LCEP scene link");
    const auto& scene_data = record.subrecords[*scene].data;
    if (scene_data.size() != 4) {
      throw schema::ScalarError("SCEN\\Action\\StartScene LCEP is not a four-byte form id");
    }
    std::uint32_t raw = 0;
    std::memcpy(&raw, scene_data.data(), sizeof(raw));
    entry.scene = resolve_plugin_form_id(RawFormId{raw}, context);
    if (const auto intt = scope_marker(record, range.begin, range.end, k_intt)) {
      const auto& data = record.subrecords[*intt].data;
      if (data.size() != 2) throw schema::ScalarError("SCEN\\Action\\StartScene INTT is not two bytes");
      entry.phase_index = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[0]) |
                                                    std::to_integer<std::uint8_t>(data[1]) << 8);
    }
    if (const auto sspn = scope_marker(record, range.begin, range.end, k_sspn)) {
      entry.phase_name = payload_text(record.subrecords[*sspn].data);
    }
    entry.conditions = read_ctda_conditions_in(record, range.begin, range.end);
    entries.push_back(std::move(entry));
  }
  return entries;
}

bool same_entry(const SceneStartSceneEntry& left, const SceneStartSceneEntry& right) {
  if (!(left.scene == right.scene) || left.phase_index != right.phase_index || left.phase_name != right.phase_name ||
      left.conditions.size() != right.conditions.size()) {
    return false;
  }
  for (std::size_t at = 0; at < left.conditions.size(); ++at) {
    if (serialize_ctda_condition(left.conditions[at]) != serialize_ctda_condition(right.conditions[at])) return false;
  }
  return true;
}

// Constraint: a wrong record type is a caller error, so it throws the same way the action group writers do.
std::optional<SceneStartSceneStatus> locate_start_scene(Plugin& plugin,
                                                       const FormKey& scene,
                                                       std::size_t action_index,
                                                       StartSceneTarget& out) {
  auto* record = fo4::detail::locate_mutable_record(plugin, scene);
  if (record == nullptr) return SceneStartSceneStatus::record_missing;
  if (record->header.signature != sig_scen) {
    throw schema::ScalarError("SCEN\\Action\\StartScene target is not a SCEN record");
  }
  const auto layout = fo4::detail::checked_scene_layout(*record);
  if (action_index >= layout.actions.size()) return SceneStartSceneStatus::action_missing;
  out.record = record;
  out.span = layout.actions[action_index];
  if (stored_action_type(*record, out.span) != k_start_scene_action) {
    return SceneStartSceneStatus::action_not_start_scene;
  }
  out.ranges = entry_ranges(*record, out.span);
  return std::nullopt;
}

void commit_start_scene(Plugin& plugin, Record& record, std::vector<Subrecord> subrecords) {
  record.subrecords = std::move(subrecords);
  fo4::detail::mark_record_modified(plugin, record);
}

}  // namespace

std::optional<std::vector<SceneStartSceneEntry>> read_scene_start_scenes(const Plugin& plugin,
                                                                        const FormKey& scene,
                                                                        std::size_t action_index) {
  const auto* record = find_record(plugin, scene);
  if (record == nullptr || record->header.signature != sig_scen) return std::nullopt;
  return read_entries(plugin, *record, action_index);
}

SceneStartSceneStatus insert_scene_start_scene(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t action_index,
                                               std::size_t index,
                                               const SceneStartSceneEntry& entry) {
  StartSceneTarget target;
  if (const auto status = locate_start_scene(plugin, scene, action_index, target)) return *status;
  if (index > target.ranges.size()) return SceneStartSceneStatus::index_out_of_range;
  auto added = entry_subrecords(plugin, entry);
  std::size_t anchor = target.span.close;
  if (const auto htid = scope_marker(*target.record, target.span.header_end, target.span.close, k_htid)) {
    anchor = *htid;
  }
  if (index < target.ranges.size()) anchor = target.ranges[index].begin;
  Record candidate = *target.record;
  candidate.subrecords.insert(candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(anchor),
                              std::make_move_iterator(added.begin()),
                              std::make_move_iterator(added.end()));
  const auto reread = read_entries(plugin, candidate, action_index);
  if (!reread.has_value() || reread->size() != target.ranges.size() + 1 ||
      !same_entry((*reread)[index], entry)) {
    return SceneStartSceneStatus::structure_invalid;
  }
  commit_start_scene(plugin, *target.record, std::move(candidate.subrecords));
  return SceneStartSceneStatus::ok;
}

SceneStartSceneStatus update_scene_start_scene(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t action_index,
                                               std::size_t index,
                                               const SceneStartSceneEntry& entry) {
  StartSceneTarget target;
  if (const auto status = locate_start_scene(plugin, scene, action_index, target)) return *status;
  if (index >= target.ranges.size()) return SceneStartSceneStatus::index_out_of_range;
  auto replacement = entry_subrecords(plugin, entry);
  const auto range = target.ranges[index];
  const bool identical = static_cast<std::size_t>(range.end - range.begin) == replacement.size() &&
      std::equal(replacement.begin(), replacement.end(),
                 std::as_const(*target.record).subrecords.begin() + static_cast<std::ptrdiff_t>(range.begin),
                 [](const Subrecord& left, const Subrecord& right) {
                   return left.signature == right.signature && left.data == right.data;
                 });
  if (identical) return SceneStartSceneStatus::unchanged;
  Record candidate = *target.record;
  const auto open = candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(range.begin);
  candidate.subrecords.erase(open, candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(range.end));
  candidate.subrecords.insert(candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(range.begin),
                              std::make_move_iterator(replacement.begin()),
                              std::make_move_iterator(replacement.end()));
  const auto reread = read_entries(plugin, candidate, action_index);
  if (!reread.has_value() || reread->size() != target.ranges.size() ||
      !same_entry((*reread)[index], entry)) {
    return SceneStartSceneStatus::structure_invalid;
  }
  commit_start_scene(plugin, *target.record, std::move(candidate.subrecords));
  return SceneStartSceneStatus::ok;
}

SceneStartSceneStatus remove_scene_start_scene(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t action_index,
                                               std::size_t index) {
  StartSceneTarget target;
  if (const auto status = locate_start_scene(plugin, scene, action_index, target)) return *status;
  if (index >= target.ranges.size()) return SceneStartSceneStatus::index_out_of_range;
  const auto before = read_entries(plugin, *target.record, action_index);
  if (!before.has_value() || before->size() != target.ranges.size()) {
    return SceneStartSceneStatus::structure_invalid;
  }
  const auto range = target.ranges[index];
  Record candidate = *target.record;
  candidate.subrecords.erase(candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(range.begin),
                             candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(range.end));
  const auto reread = read_entries(plugin, candidate, action_index);
  if (!reread.has_value() || reread->size() + 1 != before->size()) {
    return SceneStartSceneStatus::structure_invalid;
  }
  for (std::size_t at = 0; at < reread->size(); ++at) {
    const auto expected = at < index ? at : at + 1;
    if (!same_entry((*reread)[at], (*before)[expected])) return SceneStartSceneStatus::structure_invalid;
  }
  commit_start_scene(plugin, *target.record, std::move(candidate.subrecords));
  return SceneStartSceneStatus::ok;
}

}  // namespace fo4::forms
