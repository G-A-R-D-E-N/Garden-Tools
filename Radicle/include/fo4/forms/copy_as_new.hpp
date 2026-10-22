#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/form_links.hpp"

#include <optional>
#include <string>

namespace fo4::forms {

struct RecordCopyNewOptions {
  std::optional<std::string> editor_id;
  std::string editor_id_remove_prefix;
  std::string editor_id_remove_suffix;
  std::string editor_id_prefix;
  std::string editor_id_suffix;
};

enum class RecordCopyNewStatus {
  created,
  source_missing,
  unsupported_signature,
  parent_cell_missing,
  parent_dial_missing,
};

struct RecordCopyNewResult {
  RecordCopyNewStatus status{RecordCopyNewStatus::source_missing};
  FormKey new_record{};
};

RecordCopyNewResult copy_record_as_new(Plugin& destination,
                                       const Plugin& source,
                                       const FormKey& record,
                                       const RecordCopyNewOptions& options = {},
                                       FormLinkTypeLookup type_lookup = {});

}  // namespace fo4::forms
