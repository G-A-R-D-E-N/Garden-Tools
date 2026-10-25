#include "scene_writer_internal.hpp"

#include "fo4/plugin/scene_writer.hpp"
#include "fo4/plugin/vmad_writer.hpp"
#include "record_edit.hpp"

#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace fo4::detail {
namespace {

using detail::locate_mutable_record;
using detail::mark_record_modified;

constexpr Signature s(const char* text) { return Signature::from_string(text); }

std::optional<std::uint32_t> read_u32(const Record& record, std::size_t at) {
  const auto& data = record.subrecords[at].data;
  if (data.size() != 4) return std::nullopt;
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

void write_u32(Record& record, std::size_t at, std::uint32_t value) {
  auto& data = record.subrecords[at].data;
  if (data.size() != 4) {
    throw schema::ScalarError("scene index field is not four bytes");
  }
  for (std::size_t byte = 0; byte < 4; ++byte) {
    data[byte] = std::byte{static_cast<std::uint8_t>((value >> (8 * byte)) & 0xFFu)};
  }
}

std::optional<std::size_t> header_member(const Record& record,
                                         const env::detail::SceneLayout& layout,
                                         std::size_t action,
                                         Signature tag) {
  std::optional<std::size_t> found;
  const auto& span = layout.actions[action];
  for (std::size_t at = span.open + 1; at < span.header_end; ++at) {
    if (record.subrecords[at].signature != tag) continue;
    if (found) {
      throw schema::ScalarError("scene action has a duplicate " + tag.to_string() + " header member");
    }
    found = at;
  }
  return found;
}

std::optional<std::size_t> tail_last_action_index(const Record& record,
                                                  const env::detail::SceneLayout& layout) {
  std::optional<std::size_t> found;
  for (std::size_t at = layout.tail; at < record.subrecords.size(); ++at) {
    if (record.subrecords[at].signature != s("INAM")) continue;
    if (found) throw schema::ScalarError("scene has a duplicate Last Action Index subrecord");
    found = at;
  }
  return found;
}

std::uint32_t removed_phase() { return k_scene_phase_removed; }

}  // namespace

env::detail::SceneLayout checked_scene_layout(const Record& record) {
  if (record.header.signature != Signature::from_string("SCEN")) {
    throw schema::ScalarError("scene fields do not apply to " +
                              record.header.signature.to_string() + " records");
  }
  try {
    return env::detail::scene_layout(record.subrecords);
  } catch (const env::detail::SceneLayoutError& error) {
    throw schema::ScalarError("malformed scene " + error.tag.to_string() + ": " + error.what());
  }
}

std::size_t tail_anchor(const Record& record) {
  for (std::size_t at = 0; at < record.subrecords.size(); ++at) {
    const auto tag = record.subrecords[at].signature;
    if (tag == s("PNAM") || tag == s("INAM") || tag == s("CRIS") || tag == s("SCQS") ||
        tag == s("TNAM")) {
      return at;
    }
  }
  return record.subrecords.size();
}

std::size_t groups_end(const env::detail::SceneLayout& layout, const Record& record) {
  if (!layout.actions.empty()) return layout.actions.back().close + 1;
  if (!layout.actors.empty()) return layout.actors.back() + 3;
  if (!layout.phases.empty()) return layout.phases.back().close + 1;
  return tail_anchor(record);
}

std::size_t phase_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index) {
  if (index < layout.phases.size()) return layout.phases[index].open;
  if (!layout.phases.empty()) return layout.phases.back().close + 1;
  if (!layout.actors.empty()) return layout.actors.front();
  if (!layout.actions.empty()) return layout.actions.front().open;
  return tail_anchor(record);
}

std::size_t actor_anchor(const env::detail::SceneLayout& layout,
                         const Record& record,
                         std::size_t index) {
  if (index < layout.actors.size()) return layout.actors[index];
  if (!layout.actors.empty()) return layout.actors.back() + 3;
  if (!layout.actions.empty()) return layout.actions.front().open;
  return groups_end(layout, record);
}

std::size_t action_anchor(const env::detail::SceneLayout& layout,
                          const Record& record,
                          std::size_t index) {
  if (index < layout.actions.size()) return layout.actions[index].open;
  if (!layout.actions.empty()) return layout.actions.back().close + 1;
  return groups_end(layout, record);
}

std::vector<PhaseReference> action_phase_references(const Record& record,
                                                    const env::detail::SceneLayout& layout,
                                                    std::size_t phase_count) {
  std::vector<PhaseReference> out;
  for (std::size_t action = 0; action < layout.actions.size(); ++action) {
    for (const auto tag : {s("SNAM"), s("ENAM")}) {
      const auto at = header_member(record, layout, action, tag);
      if (!at) continue;
      const auto value = read_u32(record, *at);
      if (!value) {
        throw schema::ScalarError("scene action " + std::to_string(action) + " has a " +
                                  tag.to_string() + " header member that is not four bytes");
      }
      if (*value >= phase_count) {
        throw schema::ScalarError("scene action " + std::to_string(action) + " references phase " +
                                  std::to_string(*value) + " but the scene has " +
                                  std::to_string(phase_count) + " phases");
      }
      out.push_back(PhaseReference{*at, *value, action});
    }
  }
  return out;
}

void check_phase_mapping(const std::vector<PhaseReference>& references,
                         const std::vector<std::uint32_t>& old_to_new) {
  for (const auto& reference : references) {
    if (reference.value >= old_to_new.size()) {
      throw schema::ScalarError("scene action " + std::to_string(reference.action) +
                                " references a phase the scene does not have");
    }
    if (old_to_new[reference.value] == removed_phase()) {
      throw schema::ScalarError("scene action " + std::to_string(reference.action) +
                                " references the phase this edit removes");
    }
  }
}

void remap_phase_references(Record& record,
                            const std::vector<PhaseReference>& references,
                            const std::vector<std::uint32_t>& old_to_new) {
  check_phase_mapping(references, old_to_new);
  for (const auto& reference : references) {
    const auto mapped = old_to_new[reference.value];
    if (mapped != reference.value) write_u32(record, reference.subrecord, mapped);
  }
}

void remap_scene_phase_vmad(Plugin& plugin, const Record& record,
                            const std::vector<std::uint32_t>& old_to_new) {
  static_cast<void>(remap_scene_phase_indices(plugin, record.form_key, old_to_new));
}

std::uint32_t action_index_base(const Record& record, const env::detail::SceneLayout& layout) {
  bool found = false;
  std::uint32_t base = 0;
  for (std::size_t action = 0; action < layout.actions.size(); ++action) {
    const auto at = header_member(record, layout, action, s("INAM"));
    if (!at) continue;
    const auto value = read_u32(record, *at);
    if (!value) {
      throw schema::ScalarError("scene action " + std::to_string(action) +
                                " has an Index header member that is not four bytes");
    }
    const auto position = static_cast<std::uint32_t>(action);
    if (!found) {
      if (*value == position) {
        base = 0;
      } else if (*value == position + 1) {
        base = 1;
      } else {
        throw schema::ScalarError("scene action indices do not follow the stored action order, "
                                  "so a renumber would change their meaning");
      }
      found = true;
      continue;
    }
    if (*value != position + base) {
      throw schema::ScalarError("scene action indices do not follow the stored action order, "
                                "so a renumber would change their meaning");
    }
  }
  const auto last = tail_last_action_index(record, layout);
  if (last) {
    const auto value = read_u32(record, *last);
    if (!value) throw schema::ScalarError("scene Last Action Index is not four bytes");
    const auto expected = layout.actions.empty()
                              ? base
                              : base + static_cast<std::uint32_t>(layout.actions.size()) - 1;
    if (*value != expected) {
      throw schema::ScalarError("scene Last Action Index does not follow the stored action order, "
                                "so a renumber would change its meaning");
    }
  }
  return base;
}

void renumber_actions(Record& record, const env::detail::SceneLayout& layout, std::uint32_t base) {
  for (std::size_t action = 0; action < layout.actions.size(); ++action) {
    const auto at = header_member(record, layout, action, s("INAM"));
    if (!at) continue;
    const auto wanted = base + static_cast<std::uint32_t>(action);
    if (read_u32(record, *at) != wanted) write_u32(record, *at, wanted);
  }
  const auto last = tail_last_action_index(record, layout);
  if (!last) return;
  const auto wanted = layout.actions.empty()
                          ? base
                          : base + static_cast<std::uint32_t>(layout.actions.size()) - 1;
  if (read_u32(record, *last) != wanted) write_u32(record, *last, wanted);
}

void move_span(Record& record, std::size_t open, std::size_t close, std::size_t anchor) {
  if (open > close || close >= record.subrecords.size()) {
    throw schema::ScalarError("scene group span is outside the record");
  }
  std::vector<Subrecord> moved(record.subrecords.begin() + static_cast<std::ptrdiff_t>(open),
                               record.subrecords.begin() + static_cast<std::ptrdiff_t>(close) + 1);
  record.subrecords.erase(record.subrecords.begin() + static_cast<std::ptrdiff_t>(open),
                          record.subrecords.begin() + static_cast<std::ptrdiff_t>(close) + 1);
  const auto span = close - open + 1;
  if (anchor > open) anchor -= span;
  if (anchor > record.subrecords.size()) anchor = record.subrecords.size();
  record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(anchor),
                           std::make_move_iterator(moved.begin()),
                           std::make_move_iterator(moved.end()));
}

}  // namespace fo4::detail

namespace fo4 {

SubrecordGroupMutationStatus move_scene_phase(Plugin& plugin, const FormKey& scene,
                                              std::size_t from, std::size_t to) {
  auto* record = detail::locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = detail::checked_scene_layout(*record);
  if (from >= layout.phases.size() || to >= layout.phases.size()) {
    return SubrecordGroupMutationStatus::index_out_of_range;
  }
  if (from == to) return SubrecordGroupMutationStatus::unchanged;
  const auto count = layout.phases.size();
  std::vector<std::uint32_t> mapping(count);
  for (std::size_t index = 0; index < count; ++index) mapping[index] = static_cast<std::uint32_t>(index);
  const auto removed = mapping[from];
  mapping.erase(mapping.begin() + static_cast<std::ptrdiff_t>(from));
  mapping.insert(mapping.begin() + static_cast<std::ptrdiff_t>(to), removed);
  // Constraint: the mapping is phase order after the move, so a reference keeps pointing at the same phase.
  std::vector<std::uint32_t> old_to_new(count);
  for (std::size_t index = 0; index < count; ++index) {
    old_to_new[mapping[index]] = static_cast<std::uint32_t>(index);
  }
  const auto references = detail::action_phase_references(*record, layout, count);
  const auto span = layout.phases[from];
  auto anchor_index = to;
  if (to >= from && to + 1 < count) anchor_index = to + 1;
  const auto anchor = detail::phase_anchor(layout, *record, anchor_index);
  detail::remap_scene_phase_vmad(plugin, *record, old_to_new);
  detail::remap_phase_references(*record, references, old_to_new);
  detail::move_span(*record, span.open, span.close, anchor);
  detail::mark_record_modified(plugin, *record);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus move_scene_action(Plugin& plugin, const FormKey& scene,
                                               std::size_t from, std::size_t to) {
  auto* record = detail::locate_mutable_record(plugin, scene);
  if (record == nullptr) return SubrecordGroupMutationStatus::record_missing;
  const auto layout = detail::checked_scene_layout(*record);
  if (from >= layout.actions.size() || to >= layout.actions.size()) {
    return SubrecordGroupMutationStatus::index_out_of_range;
  }
  if (from == to) return SubrecordGroupMutationStatus::unchanged;
  const auto base = detail::action_index_base(*record, layout);
  const auto span = layout.actions[from];
  auto anchor_index = to;
  if (to >= from && to + 1 < layout.actions.size()) anchor_index = to + 1;
  const auto anchor = detail::action_anchor(layout, *record, anchor_index);
  detail::move_span(*record, span.open, span.close, anchor);
  const auto moved = detail::checked_scene_layout(*record);
  detail::renumber_actions(*record, moved, base);
  detail::mark_record_modified(plugin, *record);
  return SubrecordGroupMutationStatus::updated;
}

}  // namespace fo4
