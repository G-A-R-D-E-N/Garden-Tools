#pragma once

#include "fo4/plugin/container.hpp"

#include <string>
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
std::vector<PreflightIssue> preflight_record(const Record& record);
std::vector<PreflightIssue> preflight_plugin(const Plugin& plugin);

}  // namespace fo4::schema
