#include "fo4/forms/copy_as_wrapper.hpp"

#include "fo4/forms/copy_as_new.hpp"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/text/encoding.hpp"

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

std::string editor_id_of(const Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig_edid || subrecord.data.empty()) continue;
    const std::string_view bytes(reinterpret_cast<const char*>(subrecord.data.data()),
                                 subrecord.data.size());
    return text::windows1252_to_utf8(bytes.substr(0, bytes.find('\0')));
  }
  return {};
}

}  // namespace

WrapperCopyResult copy_record_as_wrapper(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record,
                                         std::optional<std::string_view> wrapper_editor_id) {
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
  if (wrapper_editor_id.has_value()) options.editor_id = std::string{*wrapper_editor_id};
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
  if (overridden == RecordCopyStatus::source_missing) {
    result.status = WrapperCopyStatus::source_missing;
    return result;
  }
  if (overridden == RecordCopyStatus::source_containment_unsupported) {
    result.status = WrapperCopyStatus::nested_source;
    return result;
  }
  if (overridden == RecordCopyStatus::replaced) {
    result.status = WrapperCopyStatus::override_failed;
    return result;
  }

  if (!reset_leveled_list(next, record)) {
    result.status = WrapperCopyStatus::leveled_list_failed;
    return result;
  }
  const auto source_editor_id = editor_id_of(*source_record);
  if (!source_editor_id.empty() && !set_record_editor_id(next, record, source_editor_id)) {
    result.status = WrapperCopyStatus::leveled_list_failed;
    return result;
  }

  LeveledListEntry entry;
  entry.item = copied.new_record;
  if (!insert_leveled_list_entry(next, record, 0, entry)) {
    result.status = WrapperCopyStatus::leveled_list_failed;
    return result;
  }

  destination = std::move(next);
  result.copy = copied.new_record;
  result.status = WrapperCopyStatus::wrapped;
  return result;
}

}  // namespace fo4::forms
