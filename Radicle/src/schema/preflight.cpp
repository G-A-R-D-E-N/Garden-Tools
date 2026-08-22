#include "fo4/schema/preflight.hpp"

#include "fo4/plugin/visit.hpp"
#include "fo4/schema/scalar.hpp"

#include <iterator>
#include <string>
#include <utility>

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

}  // namespace fo4::schema
