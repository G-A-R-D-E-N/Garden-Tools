#include "fo4/plugin/mutation.hpp"

#include "fo4/plugin/record_edit.hpp"

#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"

#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <string>

namespace fo4 {
namespace {

using detail::mark_record_modified;
using Rules = std::span<const schema::SubrecordAuthoringConstraint>;

std::optional<std::size_t> rule_index(Rules rules, Signature signature) {
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == signature) return index;
  }
  return std::nullopt;
}

struct TemplateRun {
  std::size_t begin{0};
  std::size_t end{0};
  bool terminated{false};

  bool contains(std::size_t index) const noexcept { return index >= begin && index < end; }
};

// Constraint: an object-template run is the first OBTE through its STOP; a FULL inside it names a template combination, not the record.
TemplateRun template_run(const Record& record) {
  const auto size = record.subrecords.size();
  for (std::size_t index = 0; index < size; ++index) {
    if (record.subrecords[index].signature != sig_obte) continue;
    for (std::size_t stop = index + 1; stop < size; ++stop) {
      if (record.subrecords[stop].signature == sig_stop) return {index, stop + 1, true};
    }
    return {index, size, false};
  }
  return {size, size, true};
}

// Constraint: the record name is the first FULL outside the template run: ahead of it on WEAP, ARMO and FURN, after its STOP on NPC_.
std::optional<std::size_t> header_full(const Record& record, const TemplateRun& run) {
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (!run.contains(index) && record.subrecords[index].signature == sig_full) return index;
  }
  return std::nullopt;
}

// Constraint: an absent FULL goes after the last listed subrecord that authors before it; unlisted subrecords are skipped.
std::size_t insertion_point(const Record& record, Rules rules, std::size_t full_rule) {
  std::size_t position = 0;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto rule = rule_index(rules, record.subrecords[index].signature);
    if (rule.has_value() && *rule < full_rule) position = index + 1;
  }
  return position;
}

// Constraint: LCTN keeps unlisted location-reference rows (LCSR and others) ahead of FULL, so its authoring rows cannot place an insertion.
bool has_unlisted_header_row(const Record& record, Rules rules) {
  for (const auto& subrecord : record.subrecords) {
    if (!rule_index(rules, subrecord.signature).has_value()) return true;
  }
  return false;
}

bool existing_readable(const Plugin& plugin, const Record& record, const Subrecord& subrecord) {
  if (plugin.header.tes4_flags.is_localized() && schema::is_localized_field(record.header.signature, sig_full)) {
    return subrecord.data.size() == 4;
  }
  try {
    static_cast<void>(schema::read_localized_string(plugin, record.header.signature, sig_full, subrecord));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Constraint: REFR FULL is coupled to its map-marker rows, an LCTN with unlisted rows has no known position, and a FULL placed inside a template run that has no STOP would read back as a combination name.
std::optional<std::size_t> insertion_position(const Record& record, Rules rules, std::size_t full_rule,
                                              const TemplateRun& run) {
  if (rules[full_rule].coupled) return std::nullopt;
  if (record.header.signature == sig_lctn && has_unlisted_header_row(record, rules)) return std::nullopt;
  const auto position = insertion_point(record, rules, full_rule);
  if (!run.terminated && position > run.begin) return std::nullopt;
  return position;
}

std::optional<std::size_t> full_rule_index(Signature signature, Rules& rules) {
  const auto constraint = schema::record_authoring_constraint(signature);
  if (!constraint.has_value()) return std::nullopt;
  rules = schema::authoring_subrecords(*constraint);
  return rule_index(rules, sig_full);
}

}  // namespace

RecordNameStatus set_record_full_name(Plugin& plugin,
                                      const FormKey& record_key,
                                      const schema::LocalizedString& name,
                                      CarriedStringId carried) {
  auto* record = detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return RecordNameStatus::record_missing;
  const auto signature = record->header.signature;
  Rules rules;
  const auto full_rule = full_rule_index(signature, rules);
  if (!full_rule.has_value()) return RecordNameStatus::not_named_type;

  const bool id_form = plugin.header.tes4_flags.is_localized() && schema::is_localized_field(signature, sig_full);
  if (name.id.has_value() != id_form) return RecordNameStatus::form_mismatch;
  if (name.raw_id.has_value()) return RecordNameStatus::invalid_name;
  if (!id_form && name.inline_text().find('\0') != std::string::npos) return RecordNameStatus::invalid_name;
  const auto encoded = schema::write_localized_string(name);

  const Record& view = *record;
  const auto run = template_run(view);
  if (const auto existing = header_full(view, run)) {
    const bool carried_id = carried == CarriedStringId::replace && !id_form && view.subrecords[*existing].data.size() == 4;
    if (!carried_id && !existing_readable(plugin, view, view.subrecords[*existing])) return RecordNameStatus::opaque_existing;
    if (view.subrecords[*existing].data == encoded) return RecordNameStatus::unchanged;
    record->subrecords[*existing].data = encoded;
    mark_record_modified(plugin, *record);
    return RecordNameStatus::updated;
  }
  const auto position = insertion_position(view, rules, *full_rule, run);
  if (!position.has_value()) return RecordNameStatus::not_insertable;
  record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(*position),
                            Subrecord{sig_full, encoded, false});
  mark_record_modified(plugin, *record);
  return RecordNameStatus::updated;
}

RecordFullName record_full_name(const Plugin& plugin, const Record& record) {
  RecordFullName out;
  const auto signature = record.header.signature;
  Rules rules;
  const auto full_rule = full_rule_index(signature, rules);
  if (!full_rule.has_value()) return out;
  const auto run = template_run(record);
  if (const auto existing = header_full(record, run)) {
    const auto& subrecord = record.subrecords[*existing];
    if (!existing_readable(plugin, record, subrecord)) {
      out.state = RecordNameState::opaque;
      return out;
    }
    out.state = RecordNameState::present;
    out.value = schema::read_localized_string(plugin, signature, sig_full, subrecord);
    return out;
  }
  out.state = insertion_position(record, rules, *full_rule, run).has_value() ? RecordNameState::absent
                                                                              : RecordNameState::absent_not_insertable;
  return out;
}

}  // namespace fo4
