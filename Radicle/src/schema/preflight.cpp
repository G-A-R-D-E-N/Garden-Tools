#include "fo4/schema/preflight.hpp"

#include "fo4/plugin/visit.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"

#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::schema {
namespace {

PreflightIssue issue_for(const Record& record,
                         const ScalarFieldDescriptor& field,
                         std::string code,
                         std::string message,
                         PreflightSeverity severity) {
  return PreflightIssue{
      .record = record.form_key,
      .path = std::string(field.path),
      .code = std::move(code),
      .message = std::move(message),
      .severity = severity,
  };
}

PreflightIssue authoring_issue(const FormKey& key,
                               std::string path,
                               std::string code,
                               std::string message) {
  return PreflightIssue{
      .record = key,
      .path = std::move(path),
      .code = std::move(code),
      .message = std::move(message),
      .severity = PreflightSeverity::error,
  };
}

struct RecordLocation {
  const Record* record{nullptr};
  bool direct_top_level{false};
  bool top_label_matches{false};
};

void locate_record_in_group(const Group& group,
                            const FormKey& key,
                            bool top_level_group,
                            Signature top_label,
                            std::size_t depth,
                            RecordLocation& result) {
  if (result.record != nullptr) return;
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (record->form_key != key) continue;
      result.record = record;
      result.direct_top_level = top_level_group && depth == 0;
      result.top_label_matches = top_label == record->header.signature;
      return;
    }
    locate_record_in_group(
        std::get<Group>(child), key, top_level_group, top_label, depth + 1, result);
    if (result.record != nullptr) return;
  }
}

RecordLocation locate_record(const Plugin& plugin, const FormKey& key) {
  RecordLocation result;
  for (const auto& group : plugin.groups) {
    const bool top_level_group =
        group.header.group_type == static_cast<std::int32_t>(GroupType::top);
    locate_record_in_group(
        group, key, top_level_group, group.header.top_label(), 0, result);
    if (result.record != nullptr) break;
  }
  return result;
}

}  // namespace

std::vector<PreflightIssue> preflight_record(const Record& record) {
  std::vector<PreflightIssue> issues;

  for (const auto& field : scalar_fields(record.header.signature)) {
    const Subrecord* found = nullptr;
    bool duplicate = false;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature != field.scalar.subrecord) continue;
      if (found != nullptr) {
        duplicate = true;
        break;
      }
      found = &subrecord;
    }

    if (duplicate) {
      issues.push_back(issue_for(
          record,
          field,
          "duplicate_scalar_subrecord",
          record.header.signature.to_string() + " contains duplicate " +
              field.scalar.subrecord.to_string() + " subrecords for " +
              std::string(field.path),
          PreflightSeverity::error));
      continue;
    }

    // The current schema only permits existing_only insertion policy. Absence
    // is therefore legal and must remain read-only rather than being treated
    // as a required-field failure.
    if (found == nullptr) continue;

    std::string decoded;
    try {
      decoded = decode_scalar(found->data, field.scalar);
    } catch (const ScalarError& error) {
      issues.push_back(issue_for(
          record,
          field,
          "invalid_scalar_storage",
          error.what(),
          PreflightSeverity::error));
      continue;
    }

    // Reading is deliberately more permissive than writing so odd legacy
    // values are preserved. Re-running the decoded value through the encoder
    // is a non-mutating way to report current write-constraint violations.
    try {
      static_cast<void>(encode_scalar(decoded, field.scalar));
    } catch (const ScalarError& error) {
      issues.push_back(issue_for(
          record,
          field,
          "scalar_value_out_of_range",
          error.what(),
          PreflightSeverity::warning));
    }
  }

  return issues;
}

std::vector<PreflightIssue> preflight_plugin(const Plugin& plugin) {
  std::vector<PreflightIssue> issues;
  visit_records(plugin, [&](const Record& record) {
    auto record_issues = preflight_record(record);
    issues.insert(issues.end(),
                  std::make_move_iterator(record_issues.begin()),
                  std::make_move_iterator(record_issues.end()));
  });
  return issues;
}

std::vector<PreflightIssue> preflight_authoring_record(
    const Plugin& plugin,
    const FormKey& record_key) {
  std::vector<PreflightIssue> issues;
  const auto location = locate_record(plugin, record_key);
  if (location.record == nullptr) {
    issues.push_back(authoring_issue(
        record_key,
        {},
        "record_missing",
        "authored record is not present in the plugin"));
    return issues;
  }

  const auto constraint =
      record_authoring_constraint(location.record->header.signature);
  if (!constraint.has_value()) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string(),
        "authoring_schema_missing",
        "record type has no generated authoring schema"));
    return issues;
  }

  if (constraint->placement == RecordPlacement::top_level &&
      (!location.direct_top_level || !location.top_label_matches)) {
    issues.push_back(authoring_issue(
        record_key,
        location.record->header.signature.to_string(),
        "record_placement_invalid",
        "record must be a direct child of its matching top-level GRUP"));
  }

  const auto rules = authoring_subrecords(*constraint);
  std::vector<std::size_t> counts(rules.size(), 0);
  std::optional<std::size_t> last_rule;
  bool order_reported = false;

  for (const auto& subrecord : location.record->subrecords) {
    std::optional<std::size_t> rule_index;
    for (std::size_t index = 0; index < rules.size(); ++index) {
      if (rules[index].signature == subrecord.signature) {
        rule_index = index;
        break;
      }
    }
    if (!rule_index.has_value()) continue;

    ++counts[*rule_index];
    if (!order_reported && last_rule.has_value() && *rule_index < *last_rule) {
      issues.push_back(authoring_issue(
          record_key,
          location.record->header.signature.to_string() + "\\" +
              subrecord.signature.to_string(),
          "subrecord_order_invalid",
          "schema-known subrecords are not in the generated authoring order"));
      order_reported = true;
    }
    last_rule = rule_index;
  }

  for (std::size_t index = 0; index < rules.size(); ++index) {
    const auto count = counts[index];
    const auto& rule = rules[index];
    const auto path = location.record->header.signature.to_string() + "\\" +
                      rule.signature.to_string();
    if (count < rule.minimum) {
      issues.push_back(authoring_issue(
          record_key,
          path,
          "required_subrecord_missing",
          "authored record is missing required " + rule.signature.to_string() +
              " subrecord"));
    } else if (count > rule.maximum) {
      issues.push_back(authoring_issue(
          record_key,
          path,
          "subrecord_cardinality_invalid",
          "authored record contains too many " + rule.signature.to_string() +
              " subrecords"));
    }
  }

  auto scalar_issues = preflight_record(*location.record);
  issues.insert(issues.end(),
                std::make_move_iterator(scalar_issues.begin()),
                std::make_move_iterator(scalar_issues.end()));
  return issues;
}

}  // namespace fo4::schema
