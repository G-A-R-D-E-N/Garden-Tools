#include "fo4/plugin/destructible_stage.hpp"

#include "fo4/plugin/record_edit.hpp"

#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr std::size_t k_max_stages = 0xFF;

constexpr Signature k_stage_order[] = {sig_dstd, sig_dsta, sig_dmdl,
                                       sig_dmdt, sig_dmdc, sig_dmds, sig_dstf};

std::optional<std::size_t> member_rank(Signature signature) {
  for (std::size_t i = 0; i < std::size(k_stage_order); ++i) {
    if (k_stage_order[i] == signature) return i;
  }
  return std::nullopt;
}

bool stage_member(Signature signature) { return member_rank(signature).has_value(); }

bool optional_member(Signature signature) {
  return stage_member(signature) && signature != sig_dstd && signature != sig_dstf;
}

std::optional<std::vector<std::byte>> member_default(Signature signature) {
  if (signature == sig_dsta || signature == sig_dmdl) return std::vector<std::byte>(1, std::byte{0});
  if (signature == sig_dmdc || signature == sig_dmds) return std::vector<std::byte>(4, std::byte{0});
  return std::nullopt;
}

bool destructible_carrier(Signature record) {
  const std::string path = record.to_string() + "\\DestructionStageHealthPercent";
  return schema::scalar_field_descriptor(record, path).has_value();
}

std::optional<std::size_t> header_index(const Record& record) {
  std::optional<std::size_t> found;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto& subrecord = record.subrecords[i];
    if (subrecord.signature != sig_dest) continue;
    if (subrecord.data.size() != 8 || found.has_value()) return std::nullopt;
    found = i;
  }
  return found;
}

std::size_t stage_start(const Record& record) {
  std::size_t found = record.subrecords.size();
  for (std::size_t i = record.subrecords.size(); i > 0; --i) {
    const auto signature = record.subrecords[i - 1].signature;
    if (signature != sig_dest && signature != sig_damc) continue;
    found = i;
    break;
  }
  return found;
}

struct StageSpan {
  std::size_t first{0};
  std::size_t end{0};
};

std::vector<StageSpan> stage_spans(const Record& record) {
  std::vector<std::size_t> starts;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig_dstd) starts.push_back(i);
  }
  std::vector<StageSpan> spans;
  for (const auto start : starts) {
    std::size_t end = start + 1;
    // Invariant: a run ends right after its DSTF, so a subrecord that follows the marker stays
// Constraint: where it is and belongs to no run. A run with no DSTF keeps its consecutive members and stops at the next DSTD or at the first subrecord that is not a stage member.
    while (end < record.subrecords.size()) {
      const auto signature = record.subrecords[end].signature;
      if (signature == sig_dstf) {
        ++end;
        break;
      }
      if (signature == sig_dstd || !stage_member(signature)) break;
      ++end;
    }
    spans.push_back(StageSpan{start, end});
  }
  return spans;
}

// Invariant: a run is well formed only when the DEST stage count matches the DSTD runs and no
// Constraint: stage member follows the DSTF that closes its own run.
enum class StageLayout { ok, interleaved, malformed };

StageLayout stage_layout(const Record& record) {
  const auto header = header_index(record);
  if (!header.has_value()) return StageLayout::ok;
  const auto spans = stage_spans(record);
  if (record.subrecords[*header].data.size() < 5) return StageLayout::malformed;
  if (record.subrecords[*header].data[4] != static_cast<std::byte>(spans.size())) {
    return StageLayout::malformed;
  }
  // Constraint: a stage member that no run covers (before the first DSTD, or between a DSTF and
// Constraint: the next DSTD) belongs to no stage, so the run cannot be re-cut without guessing where the reader meant it to sit.
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (!stage_member(record.subrecords[index].signature)) continue;
    bool covered = false;
    for (const auto& span : spans) {
      if (index >= span.first && index < span.end) {
        covered = true;
        break;
      }
    }
    if (!covered) return StageLayout::interleaved;
  }
  return StageLayout::ok;
}

const Record* find_const_record(const Plugin& plugin, const FormKey& record_key) {
  const Record* found = nullptr;
  visit_records(plugin, [&](const Record& record) {
    if (found != nullptr) return;
    if (record.form_key == record_key && record.header.signature != sig_tes4) found = &record;
  });
  return found;
}

std::uint32_t read_le_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(data[offset + index]) << (8 * index);
  }
  return value;
}

bool model_follower(Signature signature) {
  return signature == sig_dmdt || signature == sig_dmdc || signature == sig_dmds;
}

bool stage_has_member(const Record& record, const StageSpan& span, Signature signature) {
  for (std::size_t i = span.first; i < span.end; ++i) {
    if (record.subrecords[i].signature == signature) return true;
  }
  return false;
}

bool stage_has_model_follower(const Record& record, const StageSpan& span) {
  for (std::size_t i = span.first; i < span.end; ++i) {
    if (model_follower(record.subrecords[i].signature)) return true;
  }
  return false;
}

struct StageTarget {
  Record* record{nullptr};
  StageSpan span{};
  DestructibleStageMemberStatus status{DestructibleStageMemberStatus::updated};
};

using Rules = std::span<const schema::SubrecordAuthoringConstraint>;

std::optional<std::size_t> rule_index(Rules rules, Signature signature) {
  for (std::size_t i = 0; i < rules.size(); ++i) {
    if (rules[i].signature == signature) return i;
  }
  return std::nullopt;
}

std::optional<std::size_t> group_position(const Record& record, Signature anchor) {
  const auto constraint = schema::record_authoring_constraint(record.header.signature);
  if (!constraint.has_value()) return std::nullopt;
  const auto rules = schema::authoring_subrecords(*constraint);
  const auto target = rule_index(rules, anchor);
  if (!target.has_value()) return std::nullopt;
  std::size_t position = 0;
  std::optional<std::size_t> previous;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto existing = rule_index(rules, record.subrecords[index].signature);
    if (!existing.has_value()) return std::nullopt;
    if (previous.has_value() && *existing < *previous) return std::nullopt;
    previous = existing;
    if (*existing <= *target) position = index + 1;
  }
  return position;
}

bool carries_header(const Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_dest || subrecord.signature == sig_damc) return true;
  }
  return false;
}

std::optional<std::vector<std::byte>> header_payload(const Record& record) {
  std::size_t stages = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig_dstd) ++stages;
  }
  if (stages > k_max_stages) return std::nullopt;
  std::vector<std::byte> payload(8, std::byte{0});
  payload[4] = static_cast<std::byte>(stages);
  return payload;
}

bool group_member(Signature signature) {
  return signature == sig_dest || signature == sig_damc || stage_member(signature);
}

bool has_group_member(const Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (group_member(subrecord.signature)) return true;
  }
  return false;
}

StageTarget locate_stage(Plugin& plugin, const FormKey& record_key, std::size_t stage_index) {
  StageTarget found;
  found.record = detail::locate_mutable_record(plugin, record_key);
  if (found.record == nullptr) {
    found.status = DestructibleStageMemberStatus::record_missing;
    return found;
  }
  if (!destructible_carrier(found.record->header.signature)) {
    found.status = DestructibleStageMemberStatus::record_mismatch;
    return found;
  }
  if (!header_index(*found.record).has_value()) {
    found.status = DestructibleStageMemberStatus::header_missing;
    return found;
  }
  const auto layout = stage_layout(*found.record);
  if (layout == StageLayout::malformed) {
    found.status = DestructibleStageMemberStatus::malformed_stages;
    return found;
  }
  if (layout == StageLayout::interleaved) {
    found.status = DestructibleStageMemberStatus::interleaved_stages;
    return found;
  }
  const auto spans = stage_spans(*found.record);
  if (stage_index >= spans.size()) {
    found.status = DestructibleStageMemberStatus::stage_index_out_of_range;
    return found;
  }
  found.span = spans[stage_index];
  return found;
}

}  // namespace

DestructibleHeaderStatus insert_destructible_header(Plugin& plugin, const FormKey& record_key) {
  auto* target = detail::locate_mutable_record(plugin, record_key);
  if (target == nullptr) return DestructibleHeaderStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleHeaderStatus::record_mismatch;
  }
  if (carries_header(*target)) return DestructibleHeaderStatus::header_present;
  const auto position = group_position(*target, sig_dest);
  if (!position.has_value()) return DestructibleHeaderStatus::unknown_order;

  const auto payload = header_payload(*target);
  if (!payload.has_value()) return DestructibleHeaderStatus::too_many_stages;
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(*position),
                            Subrecord{sig_dest, *payload, false});
  detail::mark_record_modified(plugin, *target);
  return DestructibleHeaderStatus::updated;
}

DestructibleHeaderStatus remove_destructible_header(Plugin& plugin, const FormKey& record_key) {
  auto* target = detail::locate_mutable_record(plugin, record_key);
  if (target == nullptr) return DestructibleHeaderStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleHeaderStatus::record_mismatch;
  }
  if (!has_group_member(*target)) return DestructibleHeaderStatus::header_missing;

  auto& subrecords = target->subrecords;
  subrecords.erase(std::remove_if(subrecords.begin(), subrecords.end(),
                                  [](const Subrecord& subrecord) {
                                    return group_member(subrecord.signature);
                                  }),
                   subrecords.end());
  detail::mark_record_modified(plugin, *target);
  return DestructibleHeaderStatus::updated;
}

DestructibleStageMutationStatus insert_destructible_stage(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         std::size_t index) {
  auto* target = detail::locate_mutable_record(plugin, record_key);
  if (target == nullptr) return DestructibleStageMutationStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleStageMutationStatus::record_mismatch;
  }
  const auto header = header_index(*target);
  if (!header.has_value()) return DestructibleStageMutationStatus::header_missing;
  const auto layout = stage_layout(*target);
  if (layout == StageLayout::malformed) return DestructibleStageMutationStatus::malformed_stages;
  if (layout == StageLayout::interleaved) return DestructibleStageMutationStatus::interleaved_stages;
  const auto spans = stage_spans(*target);
  if (index > spans.size()) return DestructibleStageMutationStatus::index_out_of_range;
  if (spans.size() >= k_max_stages) return DestructibleStageMutationStatus::too_many_stages;

  const std::size_t position =
      index < spans.size() ? spans[index].first
      : spans.empty()     ? stage_start(*target)
                          : spans.back().end;

  std::vector<Subrecord> created;
  Subrecord stage_data;
  stage_data.signature = sig_dstd;
  stage_data.data.assign(20, std::byte{0});
  created.push_back(std::move(stage_data));
  Subrecord end_marker;
  end_marker.signature = sig_dstf;
  created.push_back(std::move(end_marker));

  target->subrecords[*header].data[4] = static_cast<std::byte>(spans.size() + 1);
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            std::make_move_iterator(created.begin()),
                            std::make_move_iterator(created.end()));
  detail::mark_record_modified(plugin, *target);
  return DestructibleStageMutationStatus::updated;
}

DestructibleStageMutationStatus remove_destructible_stage(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         std::size_t index) {
  auto* target = detail::locate_mutable_record(plugin, record_key);
  if (target == nullptr) return DestructibleStageMutationStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleStageMutationStatus::record_mismatch;
  }
  const auto header = header_index(*target);
  if (!header.has_value()) return DestructibleStageMutationStatus::header_missing;
  const auto layout = stage_layout(*target);
  if (layout == StageLayout::malformed) return DestructibleStageMutationStatus::malformed_stages;
  if (layout == StageLayout::interleaved) return DestructibleStageMutationStatus::interleaved_stages;
  const auto spans = stage_spans(*target);
  if (index >= spans.size()) return DestructibleStageMutationStatus::index_out_of_range;

  const auto span = spans[index];
  target->subrecords[*header].data[4] = static_cast<std::byte>(spans.size() - 1);
  const auto first = target->subrecords.begin() + static_cast<std::ptrdiff_t>(span.first);
  target->subrecords.erase(first, first + static_cast<std::ptrdiff_t>(span.end - span.first));
  detail::mark_record_modified(plugin, *target);
  return DestructibleStageMutationStatus::updated;
}

DestructibleStageMemberStatus insert_destructible_stage_member(Plugin& plugin,
                                                              const FormKey& record_key,
                                                              std::size_t stage_index,
                                                              Signature member) {
  const auto found = locate_stage(plugin, record_key, stage_index);
  if (found.status != DestructibleStageMemberStatus::updated) return found.status;
  if (!optional_member(member)) return DestructibleStageMemberStatus::not_optional_member;
  const auto payload = member_default(member);
  if (!payload.has_value()) return DestructibleStageMemberStatus::no_default;

  Record& record = *found.record;
  if (model_follower(member) && !stage_has_member(record, found.span, sig_dmdl)) {
    return DestructibleStageMemberStatus::model_required;
  }
  if (stage_has_member(record, found.span, member)) {
    return DestructibleStageMemberStatus::member_present;
  }
  const auto rank = *member_rank(member);
  std::size_t position = found.span.end;
  for (std::size_t i = found.span.first; i < found.span.end; ++i) {
    const auto existing = member_rank(record.subrecords[i].signature);
    if (existing.has_value() && *existing > rank) {
      position = i;
      break;
    }
  }
  record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                           Subrecord{member, *payload, false});
  detail::mark_record_modified(plugin, record);
  return DestructibleStageMemberStatus::updated;
}

DestructibleStageMemberStatus remove_destructible_stage_member(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               std::size_t stage_index,
                                                               Signature member) {
  const auto found = locate_stage(plugin, record_key, stage_index);
  if (found.status != DestructibleStageMemberStatus::updated) return found.status;
  if (!optional_member(member)) return DestructibleStageMemberStatus::not_optional_member;

  Record& record = *found.record;
  for (std::size_t i = found.span.first; i < found.span.end; ++i) {
    if (record.subrecords[i].signature != member) continue;
    if (member == sig_dmdl && stage_has_model_follower(record, found.span)) {
      return DestructibleStageMemberStatus::model_required;
    }
    record.subrecords.erase(record.subrecords.begin() + static_cast<std::ptrdiff_t>(i));
    detail::mark_record_modified(plugin, record);
    return DestructibleStageMemberStatus::updated;
  }
  return DestructibleStageMemberStatus::member_absent;
}

DestructibleGroupReadStatus read_destructible_group(const Plugin& plugin,
                                                    const FormKey& record_key,
                                                    DestructibleGroupView& out_group) {
  const Record* target = find_const_record(plugin, record_key);
  if (target == nullptr) return DestructibleGroupReadStatus::record_missing;
  if (!destructible_carrier(target->header.signature)) {
    return DestructibleGroupReadStatus::record_mismatch;
  }
  const auto header = header_index(*target);
  if (!header.has_value()) return DestructibleGroupReadStatus::header_missing;
  const auto layout = stage_layout(*target);
  if (layout == StageLayout::malformed) return DestructibleGroupReadStatus::malformed_stages;
  if (layout == StageLayout::interleaved) return DestructibleGroupReadStatus::interleaved_stages;

  DestructibleGroupView view;
  const auto& dest = target->subrecords[*header].data;
  view.health = static_cast<std::int32_t>(read_le_u32(dest, 0));
  view.stage_count = static_cast<std::uint8_t>(dest[4]);
  view.flags = static_cast<std::uint8_t>(dest[5]);

  const auto context = plugin_form_id_context(plugin);
  for (const auto& subrecord : target->subrecords) {
    if (subrecord.signature != sig_damc) continue;
    if (subrecord.data.size() % 8 != 0) return DestructibleGroupReadStatus::malformed_stages;
    for (std::size_t offset = 0; offset < subrecord.data.size(); offset += 8) {
      DestructibleGroupResistance entry;
      entry.damage_type =
          resolve_plugin_form_id(RawFormId{read_le_u32(subrecord.data, offset)}, context);
      entry.value = read_le_u32(subrecord.data, offset + 4);
      view.resistances.push_back(std::move(entry));
    }
  }

  for (const auto& span : stage_spans(*target)) {
    DestructibleGroupStage stage;
    for (std::size_t index = span.first; index < span.end; ++index) {
      stage.members.push_back(DestructibleGroupMember{
          target->subrecords[index].signature,
          std::span<const std::byte>(target->subrecords[index].data)});
    }
    view.stages.push_back(std::move(stage));
  }

  out_group = std::move(view);
  return DestructibleGroupReadStatus::present;
}

}  // namespace fo4
