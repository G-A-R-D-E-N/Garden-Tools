#pragma once

#include "fo4/plugin/container.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fo4 {

enum class RecordJsonEntryStatus {
  applied,
  unchanged,
  unknown_path,
  invalid_value,
  refused,
};

enum class RecordJsonImportStatus {
  committed,
  refused,
  malformed_json,
  form_key_mismatch,
  signature_mismatch,
  record_missing,
  record_undecodable,
};

struct RecordJsonEntryResult {
  std::string path;
  RecordJsonEntryStatus status{RecordJsonEntryStatus::refused};
};

struct RecordJsonImportOptions {
  bool allow_raw{false};
  bool ignore_form_key{false};
  bool skip_unknown{false};
};

struct RecordJsonImportResult {
  RecordJsonImportStatus status{RecordJsonImportStatus::refused};
  std::vector<RecordJsonEntryResult> entries;
};

std::string_view to_string(RecordJsonEntryStatus status) noexcept;
std::string_view to_string(RecordJsonImportStatus status) noexcept;

RecordJsonImportResult record_apply_json(Plugin& plugin,
                                         const FormKey& key,
                                         std::string_view json_text,
                                         const RecordJsonImportOptions& options = {});

}
