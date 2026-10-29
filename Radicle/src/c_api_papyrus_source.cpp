#include "fo4/c_api_papyrus_source.h"

#include "c_api_internal.hpp"
#include "fo4/papyrus_source/parser.hpp"
#include "fo4/papyrus_source/summary.hpp"

#include <algorithm>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;

std::string row_text(const std::string& value) {
  std::string out = value;
  for (char& character : out) {
    if (character == '\t' || character == '\r' || character == '\n' || character == '\0') {
      character = ' ';
    }
  }
  return out;
}

}  // namespace

extern "C" {

radicle_status radicle_papyrus_source_summary(const uint8_t* source,
                                              size_t size,
                                              char*** out_rows,
                                              size_t* out_count) {
  clear_error();
  if (!out_rows || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_rows and out_count are required");
  }
  *out_rows = nullptr;
  *out_count = 0;
  if (!source) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source is required");
  }
  if (size == 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source must not be empty");
  }
  if (size > RADICLE_PAPYRUS_SOURCE_MAX_BYTES) {
    char message[96];
    std::snprintf(message, sizeof(message), "source size %zu is above the %u-byte cap", size,
                  static_cast<unsigned>(RADICLE_PAPYRUS_SOURCE_MAX_BYTES));
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, message);
  }

  try {
    const std::string text(reinterpret_cast<const char*>(source), size);
    const fo4::psc::ParseResult parsed = fo4::psc::parse(text);
    std::vector<std::string> rows;
    if (!parsed.ok()) {
      const fo4::psc::ParseError& error = parsed.error;
      rows.push_back("error\t" + std::to_string(error.position.line) + "\t" +
                     std::to_string(error.position.column) + "\t" + row_text(error.message));
      return copy_strings(rows, out_rows, out_count);
    }

    const fo4::psc::ScriptSummary summary = fo4::psc::summarize(parsed.script);
    rows.push_back("script\t" + row_text(summary.name) + "\t" + row_text(summary.extends) + "\t" +
                   row_text(summary.autoState));
    for (const std::string& import_name : summary.imports) {
      rows.push_back("import\t" + row_text(import_name));
    }
    for (const std::string& property : summary.declaredProperties) {
      const bool is_auto = std::find(summary.autoProperties.begin(), summary.autoProperties.end(),
                                     property) != summary.autoProperties.end();
      rows.push_back("property\t" + row_text(property) + "\t" + (is_auto ? "1" : "0"));
    }
    for (const std::string& state : summary.states) {
      rows.push_back("state\t" + row_text(state));
    }
    for (const fo4::psc::FunctionSummary& function : summary.functions) {
      rows.push_back("function\t" + row_text(function.name) + "\t" + row_text(function.state) + "\t" +
                     (function.isNative ? "1" : "0") + "\t" + (function.isEvent ? "1" : "0"));
    }
    return copy_strings(rows, out_rows, out_count);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while summarizing Papyrus source");
  }
}

}  // extern "C"
