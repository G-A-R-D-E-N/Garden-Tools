#include "fo4/forms/copy_as_wrapper.hpp"

#include "fo4/forms/copy_as_new.hpp"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"

#include <array>
#include <string>
#include <utility>

namespace fo4::forms {
namespace {

constexpr Signature k_wrapper_signatures[] = {
    Signature::from_string("LVLI"),
    Signature::from_string("LVLN"),
    Signature::from_string("LVSP"),
};

bool signature_supports_wrapper(Signature signature) {
  for (const auto candidate : k_wrapper_signatures) {
    if (candidate == signature) return true;
  }
  return false;
}

}  // namespace

WrapperCopyResult copy_record_as_wrapper(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record,
                                         std::optional<std::string_view> copy_editor_id) {
  WrapperCopyResult result;
  result.wrapper = record;

  const auto* source_record = find_record(source, record);
  if (source_record == nullptr) return result;
  if (!signature_supports_wrapper(source_record->header.signature)) {
    result.status = WrapperCopyStatus::unsupported_signature;
    return result;
  }

  auto next = destination;
  RecordCopyNewOptions options;
  if (copy_editor_id.has_value()) options.editor_id = std::string{*copy_editor_id};
  const auto copied = copy_record_as_new(next, source, record, options);
  if (copied.status != RecordCopyNewStatus::created) {
    if (copied.status == RecordCopyNewStatus::source_missing) {
      result.status = WrapperCopyStatus::source_missing;
    } else if (copied.status == RecordCopyNewStatus::parent_cell_missing ||
               copied.status == RecordCopyNewStatus::parent_dial_missing) {
      result.status = WrapperCopyStatus::nested_source;
    } else {
      result.status = WrapperCopyStatus::unsupported_signature;
    }
    return result;
  }

  const auto overridden = copy_record_as_override(next, source, record, false);
  if (overridden != RecordCopyStatus::inserted) {
    result.status = overridden == RecordCopyStatus::source_containment_unsupported
                        ? WrapperCopyStatus::nested_source
                        : WrapperCopyStatus::override_failed;
    return result;
  }

  LeveledListEntry entry;
  entry.item = copied.new_record;
  const std::array<LeveledListEntry, 1> entries{entry};
  if (!replace_leveled_list_entries(next, record, entries)) {
    result.status = WrapperCopyStatus::leveled_list_failed;
    return result;
  }

  destination = std::move(next);
  result.copy = copied.new_record;
  result.status = WrapperCopyStatus::wrapped;
  return result;
}

}  // namespace fo4::forms
