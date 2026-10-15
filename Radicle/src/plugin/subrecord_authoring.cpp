#include "fo4/plugin/subrecord_authoring.hpp"

#include "record_edit.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/preflight.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using Rules = std::span<const schema::SubrecordAuthoringConstraint>;

std::optional<std::size_t> rule_index(Rules rules, Signature signature) {
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == signature) return index;
  }
  return std::nullopt;
}

Rules generated_rules(Signature record) {
  const auto constraint = schema::record_authoring_constraint(record);
  return constraint.has_value() ? schema::authoring_subrecords(*constraint) : Rules{};
}

std::vector<std::byte> default_payload(const schema::SubrecordAuthoringConstraint& rule) {
  const auto bytes = std::as_bytes(rule.default_bytes());
  return {bytes.begin(), bytes.end()};
}

bool default_usable(const Plugin& plugin,
                    Signature record,
                    const schema::SubrecordAuthoringConstraint& rule) {
  if (!rule.has_default()) return false;
  return !(plugin.header.tes4_flags.is_localized() &&
           schema::is_localized_field(record, rule.signature));
}

bool local_layout_valid(Rules rules, const Record& record) {
  std::vector<std::size_t> counts(rules.size(), 0);
  std::optional<std::size_t> last;
  for (const auto& subrecord : record.subrecords) {
    const auto index = rule_index(rules, subrecord.signature);
    if (!index.has_value()) continue;
    if (last.has_value() && *index < *last) return false;
    last = index;
    ++counts[*index];
  }
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (counts[index] < rules[index].minimum || counts[index] > rules[index].maximum) {
      return false;
    }
  }
  return true;
}

bool layout_valid(Rules rules, bool generated, const Record& record, bool localized) {
  if (!local_layout_valid(rules, record)) return false;
  if (!generated) return true;
  for (const auto& issue : schema::preflight_record(record, localized)) {
    if (issue.severity == schema::PreflightSeverity::error) return false;
  }
  return true;
}

bool editable(const schema::SubrecordAuthoringConstraint& rule) {
  return !rule.coupled && (rule.maximum == 1 || (rule.maximum > 1 && rule.uncounted));
}

SubrecordAddStatus add_with_rules(Plugin& plugin,
                                  const FormKey& key,
                                  Signature subrecord,
                                  Rules explicit_rules,
                                  bool explicit_given) {
  auto* record = detail::locate_mutable_record(plugin, key);
  if (record == nullptr) return SubrecordAddStatus::record_missing;

  const auto rules = explicit_given ? explicit_rules : generated_rules(record->header.signature);
  const auto target = rule_index(rules, subrecord);
  if (!target.has_value() || !editable(rules[*target])) {
    return SubrecordAddStatus::not_authorable;
  }

  const auto present = static_cast<std::size_t>(std::count_if(
      record->subrecords.begin(), record->subrecords.end(),
      [&](const Subrecord& existing) { return existing.signature == subrecord; }));
  if (present >= rules[*target].maximum) return SubrecordAddStatus::already_present;
  if (!default_usable(plugin, record->header.signature, rules[*target])) {
    return SubrecordAddStatus::no_default;
  }

  for (const auto& existing : record->subrecords) {
    if (!rule_index(rules, existing.signature).has_value()) {
      return SubrecordAddStatus::unknown_order;
    }
  }

  std::size_t position = 0;
  for (std::size_t index = record->subrecords.size(); index > 0; --index) {
    const auto existing = rule_index(rules, record->subrecords[index - 1].signature);
    if (*existing <= *target) {
      position = index;
      break;
    }
  }

  Record candidate = *record;
  candidate.subrecords.insert(candidate.subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                              Subrecord{subrecord, default_payload(rules[*target]), false});
  if (!layout_valid(rules, !explicit_given, candidate, plugin.header.tes4_flags.is_localized())) {
    return SubrecordAddStatus::unknown_order;
  }

  *record = std::move(candidate);
  detail::mark_record_modified(plugin, *record);
  return SubrecordAddStatus::added;
}

SubrecordRemoveStatus remove_with_rules(Plugin& plugin,
                                        const FormKey& key,
                                        Signature subrecord,
                                        std::size_t occurrence,
                                        Rules explicit_rules,
                                        bool explicit_given) {
  auto* record = detail::locate_mutable_record(plugin, key);
  if (record == nullptr) return SubrecordRemoveStatus::record_missing;

  const auto rules = explicit_given ? explicit_rules : generated_rules(record->header.signature);
  const auto target = rule_index(rules, subrecord);
  if (!target.has_value() || !editable(rules[*target])) {
    return SubrecordRemoveStatus::not_authorable;
  }

  for (const auto& existing : record->subrecords) {
    if (!rule_index(rules, existing.signature).has_value()) {
      return SubrecordRemoveStatus::unknown_order;
    }
  }

  std::vector<std::size_t> positions;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (record->subrecords[index].signature == subrecord) positions.push_back(index);
  }
  if (occurrence >= positions.size()) return SubrecordRemoveStatus::absent;
  if (positions.size() - 1 < rules[*target].minimum) return SubrecordRemoveStatus::below_minimum;

  record->subrecords.erase(record->subrecords.begin() +
                           static_cast<std::ptrdiff_t>(positions[occurrence]));
  detail::mark_record_modified(plugin, *record);
  return SubrecordRemoveStatus::removed;
}

RecordCreateResult create_with_rules(Plugin& plugin,
                                     Signature signature,
                                     std::string_view editor_id,
                                     Rules explicit_rules,
                                     bool explicit_given) {
  const auto rules = explicit_given ? explicit_rules : generated_rules(signature);
  if (rules.empty()) return {RecordCreateStatus::not_authorable, {}};
  const auto constraint = schema::record_authoring_constraint(signature);
  if (constraint && constraint->placement != schema::RecordPlacement::top_level) {
    return {RecordCreateStatus::not_authorable, {}};
  }
  if (editor_id.find('\0') != std::string_view::npos) {
    return {RecordCreateStatus::invalid_editor_id, {}};
  }
  if (!rule_index(rules, sig_edid).has_value() && !editor_id.empty()) {
    return {RecordCreateStatus::invalid_editor_id, {}};
  }
  for (const auto& rule : rules) {
    if (rule.signature == sig_edid || rule.minimum == 0) continue;
    if (!editable(rule)) return {RecordCreateStatus::not_authorable, {}};
    if (!default_usable(plugin, signature, rule)) return {RecordCreateStatus::no_default, {}};
  }

  Record built;
  built.header.signature = signature;
  for (const auto& rule : rules) {
    if (rule.signature == sig_edid) {
      if (rule.minimum == 0 && editor_id.empty()) continue;
      const auto text = std::as_bytes(std::span{editor_id});
      std::vector<std::byte> data{text.begin(), text.end()};
      data.push_back(std::byte{0});
      built.subrecords.push_back(Subrecord{sig_edid, std::move(data), false});
      continue;
    }
    for (std::uint16_t count = 0; count < rule.minimum; ++count) {
      built.subrecords.push_back(Subrecord{rule.signature, default_payload(rule), false});
    }
  }

  if (!layout_valid(rules, !explicit_given, built, plugin.header.tes4_flags.is_localized())) {
    return {RecordCreateStatus::preflight_failed, {}};
  }
  const auto key = create_local_record(plugin, signature, std::move(built.subrecords));
  return {RecordCreateStatus::created, key};
}

}  // namespace

SubrecordAddStatus add_record_subrecord(Plugin& plugin,
                                        const FormKey& record_key,
                                        Signature subrecord) {
  return add_with_rules(plugin, record_key, subrecord, {}, false);
}

SubrecordAddStatus add_record_subrecord(Plugin& plugin,
                                        const FormKey& record_key,
                                        Signature subrecord,
                                        Rules rules) {
  return add_with_rules(plugin, record_key, subrecord, rules, true);
}

SubrecordRemoveStatus remove_record_subrecord(Plugin& plugin,
                                              const FormKey& record_key,
                                              Signature subrecord,
                                              std::size_t occurrence) {
  return remove_with_rules(plugin, record_key, subrecord, occurrence, {}, false);
}

SubrecordRemoveStatus remove_record_subrecord(Plugin& plugin,
                                              const FormKey& record_key,
                                              Signature subrecord,
                                              std::size_t occurrence,
                                              Rules rules) {
  return remove_with_rules(plugin, record_key, subrecord, occurrence, rules, true);
}

RecordCreateResult create_record(Plugin& plugin, Signature record, std::string_view editor_id) {
  return create_with_rules(plugin, record, editor_id, {}, false);
}

RecordCreateResult create_record(Plugin& plugin,
                                 Signature record,
                                 std::string_view editor_id,
                                 Rules rules) {
  return create_with_rules(plugin, record, editor_id, rules, true);
}

}  // namespace fo4
