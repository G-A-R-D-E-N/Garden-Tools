#include "fo4/plugin/subrecord_authoring.hpp"

#include "fo4/plugin/record_edit.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
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

// Constraint: a guard-matched localized string row holds inline text outside a localized plugin.
bool inline_string_default(const Plugin& plugin, const Record& record, Signature subrecord) {
  if (plugin.header.tes4_flags.is_localized()) return false;
  for (const auto& field : schema::scalar_fields(record.header.signature)) {
    if (field.scalar.subrecord != subrecord || !field.scalar.localized) continue;
    if (schema::discriminator_guarded(field.scalar.address.when) &&
        schema::record_discriminator_matches(record, field.scalar.address.when)) {
      return true;
    }
  }
  return false;
}

std::vector<std::byte> default_payload(const Plugin& plugin,
                                       const Record& record,
                                       const schema::SubrecordAuthoringConstraint& rule) {
  if (inline_string_default(plugin, record, rule.signature)) return {std::byte{0}};
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

std::optional<std::uint32_t> supplied_id(const Plugin& plugin,
                                         Signature record,
                                         std::span<const LocalizedRowId> ids,
                                         Signature subrecord) {
  if (!plugin.header.tes4_flags.is_localized() || !schema::is_localized_field(record, subrecord)) {
    return std::nullopt;
  }
  for (const auto& id : ids) {
    if (id.subrecord == subrecord) return id.id;
  }
  return std::nullopt;
}

// Constraint: a required localized row can only be built when the caller supplies its string table, so a create
// without tables must still refuse instead of writing an inline default into a localized plugin.
bool localized_row_available(const Plugin& plugin,
                             Signature record,
                             const schema::SubrecordAuthoringConstraint& rule,
                             std::span<const LocalizedRowId> ids,
                             const localization::LocalizationTables* tables) {
  if (tables == nullptr || !rule.has_default()) return false;
  if (!plugin.header.tes4_flags.is_localized()) return false;
  return schema::is_localized_field(record, rule.signature) &&
         !supplied_id(plugin, record, ids, rule.signature).has_value();
}

localization::LocalizedStringTable& row_table(localization::LocalizationTables& tables,
                                             Signature record,
                                             Signature subrecord) {
  return localization::table_for(tables, localization::string_table_kind(record, subrecord));
}

void release_rows(localization::LocalizationTables& tables,
                  Signature record,
                  std::span<const LocalizedRowId> rows) {
  for (const auto& row : rows) {
    localization::remove_string(row_table(tables, record, row.subrecord), row.id);
  }
}

std::vector<std::byte> row_payload(const Plugin& plugin,
                                   const Record& record,
                                   const schema::SubrecordAuthoringConstraint& rule,
                                   std::span<const LocalizedRowId> ids) {
  if (const auto id = supplied_id(plugin, record.header.signature, ids, rule.signature)) {
    std::vector<std::byte> bytes(4);
    std::memcpy(bytes.data(), &*id, bytes.size());
    return bytes;
  }
  return default_payload(plugin, record, rule);
}

std::vector<std::size_t> subrecord_counts(Rules rules, const Record& record) {
  std::vector<std::size_t> counts(rules.size(), 0);
  for (const auto& subrecord : record.subrecords) {
    const auto index = rule_index(rules, subrecord.signature);
    if (index.has_value()) ++counts[*index];
  }
  return counts;
}

bool local_layout_valid(Rules rules, const Record& before, const Record& after) {
  // constraint: a second DSTD restarts the DSTD..DSTF destructible run.
  auto dest_run_restart = [&](std::size_t index, std::size_t last_index) {
    if (rules[index].signature != Signature::from_string("DSTD")) return false;
    if (rules[index].maximum <= 1) return false;
    for (std::size_t i = index; i <= last_index && i < rules.size(); ++i) {
      const auto sig = rules[i].signature.to_string();
      if (sig != "DSTD" && sig != "DSTA" && sig != "DMDL" && sig != "DMDT" &&
          sig != "DMDC" && sig != "DMDS" && sig != "DSTF") {
        return false;
      }
    }
    return true;
  };
  std::optional<std::size_t> last;
  for (const auto& subrecord : after.subrecords) {
    const auto index = rule_index(rules, subrecord.signature);
    if (!index.has_value()) continue;
    if (last.has_value() && *index < *last && !dest_run_restart(*index, *last)) return false;
    last = index;
  }
  const auto counts = subrecord_counts(rules, after);
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (counts[index] < rules[index].minimum || counts[index] > rules[index].maximum) {
      return false;
    }
  }
  const auto before_counts = subrecord_counts(rules, before);
  return !schema::worsened_subrecord_pair(after.header.signature, rules, before_counts, counts)
              .has_value();
}

bool same_check(const schema::PreflightIssue& a, const schema::PreflightIssue& b) {
  return a.path == b.path && a.code == b.code;
}

bool layout_valid(Rules rules,
                  bool generated,
                  const Record& before,
                  const Record& after,
                  bool localized) {
  if (!local_layout_valid(rules, before, after)) return false;
  if (!generated) return true;
  // Requires: only errors absent from preflight_record(before) refuse the edit, unless pre-existing and a record-list invariant.
  const auto before_issues = schema::preflight_record(before, localized);
  for (const auto& issue : schema::preflight_record(after, localized)) {
    if (issue.severity != schema::PreflightSeverity::error) continue;
    const bool pre_existing = std::any_of(
        before_issues.begin(), before_issues.end(),
        [&](const schema::PreflightIssue& old) { return same_check(old, issue); });
    if (!pre_existing || !schema::is_record_list_invariant(issue.code)) return false;
  }
  return true;
}

bool editable(const schema::SubrecordAuthoringConstraint& rule) {
  return !rule.coupled && (rule.maximum == 1 || (rule.maximum > 1 && rule.uncounted));
}

bool addable(const schema::SubrecordAuthoringConstraint& rule) {
  return editable(rule) || (rule.add_only && rule.maximum == 1);
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
  if (!target.has_value() || !addable(rules[*target])) {
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
                              Subrecord{subrecord,
                                        default_payload(plugin, *record, rules[*target]),
                                        false});
  if (!layout_valid(rules, !explicit_given, *record, candidate,
                    plugin.header.tes4_flags.is_localized())) {
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

  std::vector<std::size_t> positions;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (record->subrecords[index].signature == subrecord) positions.push_back(index);
  }
  if (occurrence >= positions.size()) return SubrecordRemoveStatus::absent;
  if (positions.size() - 1 < rules[*target].minimum) return SubrecordRemoveStatus::below_minimum;

  if (rules[*target].maximum > 1) {
    for (const auto& existing : record->subrecords) {
      if (!rule_index(rules, existing.signature).has_value()) {
        return SubrecordRemoveStatus::unknown_order;
      }
    }
  }

  const auto before_counts = subrecord_counts(rules, *record);
  auto after_counts = before_counts;
  after_counts[*target] -= 1;
  if (schema::worsened_subrecord_pair(record->header.signature, rules, before_counts, after_counts)
          .has_value()) {
    return SubrecordRemoveStatus::unknown_order;
  }

  record->subrecords.erase(record->subrecords.begin() +
                           static_cast<std::ptrdiff_t>(positions[occurrence]));
  detail::mark_record_modified(plugin, *record);
  return SubrecordRemoveStatus::removed;
}

RecordCreateResult create_with_rules(Plugin& plugin,
                                     Signature signature,
                                     std::string_view editor_id,
                                     Rules explicit_rules,
                                     bool explicit_given,
                                     std::span<const LocalizedRowId> ids,
                                     localization::LocalizationTables* tables) {
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
  const auto usable = [&](const schema::SubrecordAuthoringConstraint& rule) {
    return default_usable(plugin, signature, rule) ||
           localized_row_available(plugin, signature, rule, ids, tables);
  };
  for (const auto& rule : rules) {
    if (rule.signature == sig_edid || rule.minimum == 0) continue;
    const bool creatable =
        rule.coupled ? (!explicit_given && rule.maximum == 1 && usable(rule)) : editable(rule);
    if (!creatable) return {RecordCreateStatus::not_authorable, {}};
    if (!usable(rule) && !supplied_id(plugin, signature, ids, rule.signature)) {
      return {RecordCreateStatus::no_default, {}};
    }
  }

  // Constraint: a reserved row must be released on every later refusal, or the table keeps a row no record names.
  std::vector<LocalizedRowId> resolved(ids.begin(), ids.end());
  std::vector<LocalizedRowId> reserved;
  if (tables != nullptr) {
    for (const auto& rule : rules) {
      if (rule.signature == sig_edid || rule.minimum == 0) continue;
      if (!localized_row_available(plugin, signature, rule, ids, tables)) continue;
      auto& table = row_table(*tables, signature, rule.signature);
      const auto id = localization::next_string_id(table);
      if (id == 0) {
        release_rows(*tables, signature, reserved);
        return {RecordCreateStatus::no_default, {}};
      }
      localization::insert_string(table, id, "");
      reserved.push_back({rule.signature, id});
      resolved.push_back({rule.signature, id});
    }
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
      built.subrecords.push_back(
          Subrecord{rule.signature, row_payload(plugin, built, rule, resolved), false});
    }
  }

  Record empty;
  if (!layout_valid(rules, !explicit_given, empty, built,
                    plugin.header.tes4_flags.is_localized())) {
    if (tables != nullptr) release_rows(*tables, signature, reserved);
    return {RecordCreateStatus::preflight_failed, {}};
  }
  FormKey key;
  try {
    key = create_local_record(plugin, signature,
                              std::move(static_cast<std::vector<Subrecord>&>(built.subrecords)));
  } catch (...) {
    if (tables != nullptr) release_rows(*tables, signature, reserved);
    throw;
  }
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

SubrecordBytesStatus set_record_subrecord_bytes(Plugin& plugin,
                                                const FormKey& record_key,
                                                Signature subrecord,
                                                std::size_t occurrence,
                                                std::span<const std::byte> bytes) {
  auto* record = detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return SubrecordBytesStatus::record_missing;
  std::vector<std::size_t> positions;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (record->subrecords[index].signature == subrecord) positions.push_back(index);
  }
  if (occurrence > positions.size()) return SubrecordBytesStatus::occurrence_out_of_range;
  const std::vector<std::byte> payload(bytes.begin(), bytes.end());
  const bool append = occurrence == positions.size();
  if (append) {
    record->subrecords.push_back(Subrecord{subrecord, payload, false});
  } else {
    record->subrecords[positions[occurrence]].data = payload;
  }
  detail::mark_record_modified(plugin, *record);
  return append ? SubrecordBytesStatus::appended : SubrecordBytesStatus::replaced;
}

bool set_record_raw_compressed_payload(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::uint32_t declared_size,
                                       std::span<const std::byte> bytes) {
  auto* record = detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return false;
  detail::mark_record_modified(plugin, *record);
  record->compressed = true;
  record->header.flags = RecordFlags{record->header.flags.value() | RecordFlags::compressed};
  record->raw_compressed = RawCompressedPayload{declared_size, std::vector<std::byte>(bytes.begin(), bytes.end())};
  return true;
}

RawRecordResult create_raw_record(Plugin& plugin, Signature record, std::uint16_t form_version) {
  if (record.packed() == 0 || record == sig_tes4 || record == sig_grup) {
    return {RawRecordStatus::not_creatable, {}};
  }
  const auto constraint = schema::record_authoring_constraint(record);
  if (record == sig_land || (constraint && constraint->placement != schema::RecordPlacement::top_level)) {
    return {RawRecordStatus::nested_placement, {}};
  }
  const auto key = create_local_record(plugin, record);
  if (form_version != 0) {
    if (auto* created = detail::locate_mutable_record(plugin, key)) {
      created->header.form_version = form_version;
      detail::mark_record_modified(plugin, *created);
    }
  }
  return {RawRecordStatus::created, key};
}

RecordCreateResult create_record(Plugin& plugin, Signature record, std::string_view editor_id) {
  return create_with_rules(plugin, record, editor_id, {}, false, {}, nullptr);
}

RecordCreateResult create_record(Plugin& plugin,
                                 Signature record,
                                 std::string_view editor_id,
                                 Rules rules) {
  return create_with_rules(plugin, record, editor_id, rules, true, {}, nullptr);
}

RecordCreateResult create_record_with_localized_rows(Plugin& plugin,
                                                     Signature record,
                                                     std::string_view editor_id,
                                                     std::span<const LocalizedRowId> localized_ids) {
  return create_with_rules(plugin, record, editor_id, {}, false, localized_ids, nullptr);
}

RecordCreateResult create_record_with_localized_rows(Plugin& plugin,
                                                     Signature record,
                                                     std::string_view editor_id,
                                                     std::span<const LocalizedRowId> localized_ids,
                                                     Rules rules) {
  return create_with_rules(plugin, record, editor_id, rules, true, localized_ids, nullptr);
}

RecordCreateResult create_record_with_string_rows(Plugin& plugin,
                                                 Signature record,
                                                 std::string_view editor_id,
                                                 localization::LocalizationTables& tables) {
  return create_with_rules(plugin, record, editor_id, {}, false, {}, &tables);
}

}  // namespace fo4
