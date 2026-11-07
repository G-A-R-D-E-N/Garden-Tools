#pragma once

#include "fo4/plugin/container.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fo4::schema {

enum class PreflightSeverity {
  warning,
  error,
};

struct PreflightIssue {
  FormKey record{};
  std::string path;
  std::string code;
  std::string message;
  PreflightSeverity severity{PreflightSeverity::error};
};

// Validates the machine-readable schema surfaces that Radicle currently knows
// how to prove. Missing existing_only fields are legal. Duplicate or malformed
// scalar storage is an error; readable legacy values outside current write
// constraints are warnings so inspection remains lossless and non-destructive.
std::vector<PreflightIssue> preflight_record(const Record& record, bool localized = false);
std::vector<PreflightIssue> preflight_plugin(const Plugin& plugin);

// Invariant: these four codes are the record-list coupling codes shared by the authoring gate and tests.
bool is_record_list_invariant(std::string_view code);

// Validates a newly authored record against the generated creation contract:
// supported containment, required/cardinality rules, and relative ordering of
// schema-known subrecords. This is intentionally separate from legacy-read
// preflight so old files are not rejected merely for differing authoring shape.
std::vector<PreflightIssue> preflight_authoring_record(
    const Plugin& plugin,
    const FormKey& record_key);

}  // namespace fo4::schema
