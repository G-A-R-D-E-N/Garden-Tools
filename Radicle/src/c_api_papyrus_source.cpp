#include "fo4/c_api_papyrus_source.h"

#include "c_api_internal.hpp"
#include "fo4/papyrus/source_summary.hpp"

#include <cstdio>
#include <memory>
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

struct CatalogEvent {
  std::string name;
  std::string state;
  std::string declaring_script;
  radicle_event_owner_kind owner_kind{RADICLE_EVENT_OWNER_OTHER};
  bool is_custom{false};
  std::vector<fo4::papyrus::SummaryParameter> params;
};

radicle_event_owner_kind owner_kind_of(const std::string& script) {
  std::string folded = script;
  for (char& character : folded) {
    if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + 32);
  }
  if (folded == "actor") return RADICLE_EVENT_OWNER_ACTOR;
  if (folded == "objectreference") return RADICLE_EVENT_OWNER_OBJECT_REFERENCE;
  if (folded == "quest") return RADICLE_EVENT_OWNER_QUEST;
  if (folded == "alias" || folded == "referencealias") return RADICLE_EVENT_OWNER_ALIAS;
  return RADICLE_EVENT_OWNER_OTHER;
}

}  // namespace

struct radicle_event_catalog {
  std::vector<CatalogEvent> events;
};

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

  return guard_exceptions("unknown exception while summarizing Papyrus source", [&] {
    const std::string text(reinterpret_cast<const char*>(source), size);
    const fo4::papyrus::SourceSummary summary = fo4::papyrus::summarizeSource(text);
    std::vector<std::string> rows;
    if (!summary.ok) {
      rows.push_back("error\t" + std::to_string(summary.errorLine) + "\t" + std::to_string(summary.errorColumn) + "\t" +
                     row_text(summary.errorMessage));
      return copy_strings(rows, out_rows, out_count);
    }

    rows.push_back("script\t" + row_text(summary.name) + "\t" + row_text(summary.extends) + "\t" +
                   row_text(summary.autoState));
    for (const std::string& import_name : summary.imports) {
      rows.push_back("import\t" + row_text(import_name));
    }
    for (const fo4::papyrus::SummaryProperty& property : summary.properties) {
      rows.push_back("property\t" + row_text(property.name) + "\t" + (property.isAuto ? "1" : "0"));
    }
    for (const std::string& state : summary.states) {
      rows.push_back("state\t" + row_text(state));
    }
    for (const fo4::papyrus::SummaryFunction& function : summary.functions) {
      rows.push_back("function\t" + row_text(function.name) + "\t" + row_text(function.state) + "\t" +
                     (function.isNative ? "1" : "0") + "\t" + (function.isEvent ? "1" : "0"));
    }
    return copy_strings(rows, out_rows, out_count);
  });
}

radicle_status radicle_papyrus_event_catalog_parse(const uint8_t* source,
                                                   size_t size,
                                                   radicle_event_catalog** out_catalog) {
  clear_error();
  if (!out_catalog) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_catalog is required");
  }
  *out_catalog = nullptr;
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

  return guard_exceptions("unknown exception while cataloging Papyrus events", [&] {
    const std::string text(reinterpret_cast<const char*>(source), size);
    const fo4::papyrus::SourceSummary summary = fo4::papyrus::summarizeSource(text);
    if (!summary.ok) {
      const std::string message = std::to_string(summary.errorLine) + ":" + std::to_string(summary.errorColumn) + ": " +
                                  summary.errorMessage;
      return fail(RADICLE_STATUS_PARSE_ERROR, message.c_str());
    }

    auto catalog = std::make_unique<radicle_event_catalog>();
    const radicle_event_owner_kind kind = owner_kind_of(summary.name);
    for (const fo4::papyrus::SummaryEvent& event : summary.events) {
      catalog->events.push_back({event.name, event.state, summary.name, kind, event.isCustom, event.parameters});
    }
    *out_catalog = catalog.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_event_catalog_destroy(radicle_event_catalog* catalog) { delete catalog; }

size_t radicle_papyrus_event_catalog_count(const radicle_event_catalog* catalog) {
  return catalog ? catalog->events.size() : 0;
}

radicle_status radicle_papyrus_event_catalog_get(const radicle_event_catalog* catalog,
                                                 size_t index,
                                                 radicle_event_info* out_info) {
  clear_error();
  if (out_info) *out_info = radicle_event_info{};
  if (!catalog) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "catalog is required");
  }
  if (!out_info) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_info is required");
  }
  if (index >= catalog->events.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "event index is at or past the catalog count");
  }
  const CatalogEvent& event = catalog->events[index];
  out_info->name = event.name.c_str();
  out_info->state = event.state.c_str();
  out_info->declaring_script = event.declaring_script.c_str();
  out_info->owner_kind = event.owner_kind;
  out_info->is_custom = event.is_custom ? 1 : 0;
  out_info->param_count = event.params.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_event_catalog_param(const radicle_event_catalog* catalog,
                                                   size_t event_index,
                                                   size_t param_index,
                                                   radicle_event_param* out_param) {
  clear_error();
  if (out_param) *out_param = radicle_event_param{};
  if (!catalog) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "catalog is required");
  }
  if (!out_param) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_param is required");
  }
  if (event_index >= catalog->events.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "event index is at or past the catalog count");
  }
  const CatalogEvent& event = catalog->events[event_index];
  if (param_index >= event.params.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "param index is at or past the event parameter count");
  }
  const fo4::papyrus::SummaryParameter& param = event.params[param_index];
  out_param->name = param.name.c_str();
  out_param->type = param.type.c_str();
  out_param->is_array = param.isArray ? 1 : 0;
  return RADICLE_STATUS_OK;
}

}  // extern "C"
