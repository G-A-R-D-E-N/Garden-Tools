#include "fo4/forms/copy_as_new.hpp"

#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"
#include "fo4/text/encoding.hpp"
#include "placed_reference_copy_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {

using namespace detail;

namespace {

const Record* top_level_record_of(const Plugin& plugin, const FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top)) continue;
    for (const auto& child : group.children) {
      const auto* record = std::get_if<Record>(&child);
      if (record != nullptr && record->form_key == key) return record;
    }
  }
  return nullptr;
}

Record* mutable_record_of(Plugin& plugin, const FormKey& key) {
  Record* found = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (found == nullptr && record.header.signature != sig_tes4 && record.form_key == key) {
      found = &record;
    }
  });
  return found;
}

bool refused_top_level(Signature signature) {
  return signature == Signature::from_string("PLYR") || signature == Signature::from_string("NOCM");
}

std::string editor_id_text_of(const Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig_edid || subrecord.data.empty()) continue;
    const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
    const auto* end = begin + subrecord.data.size();
    return fo4::text::windows1252_to_utf8(std::string(begin, std::find(begin, end, '\0')));
  }
  return {};
}

bool equal_ignore_case(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) ==
                  std::tolower(static_cast<unsigned char>(b));
         });
}

std::string strip_editor_id(std::string text, const RecordCopyNewOptions& options) {
  const std::string_view view(text);
  const auto& prefix = options.editor_id_remove_prefix;
  if (view.size() >= prefix.size() && equal_ignore_case(view.substr(0, prefix.size()), prefix)) {
    text.erase(0, prefix.size());
  }
  const auto& suffix = options.editor_id_remove_suffix;
  const std::string_view current(text);
  if (current.size() >= suffix.size() &&
      equal_ignore_case(current.substr(current.size() - suffix.size()), suffix)) {
    text.erase(current.size() - suffix.size());
  }
  return text;
}

std::optional<std::string> resolved_editor_id(const Record& source_record,
                                              const RecordCopyNewOptions& options) {
  if (options.editor_id.has_value()) return options.editor_id;
  if (options.editor_id_remove_prefix.empty() && options.editor_id_remove_suffix.empty() &&
      options.editor_id_prefix.empty() && options.editor_id_suffix.empty()) {
    return std::nullopt;
  }
  if (source_record.header.flags.is_deleted()) return std::nullopt;
  const auto original = editor_id_text_of(source_record);
  if (original.empty()) return std::nullopt;
  return options.editor_id_prefix + strip_editor_id(original, options) + options.editor_id_suffix;
}

void apply_editor_id(Plugin& destination,
                     const FormKey& key,
                     const Record& source_record,
                     const RecordCopyNewOptions& options) {
  const auto edid = resolved_editor_id(source_record, options);
  if (!edid.has_value()) return;
  if (!set_record_editor_id(destination, key, *edid)) {
    throw std::logic_error("copied record is missing from the destination plugin");
  }
}

FormLinkTypeLookup composed_type_lookup(const Plugin& source, const Plugin& destination) {
  const FormLinkTypeLookup source_types = plugin_type_lookup(source);
  const FormLinkTypeLookup destination_types = plugin_type_lookup(destination);
  return [source_types, destination_types](const FormKey& key) -> std::optional<Signature> {
    if (const auto own = source_types(key)) return own;
    return destination_types(key);
  };
}

RecordCopyNewResult copy_top_level_as_new(Plugin& destination,
                                          const Plugin& source,
                                          const Record& source_record,
                                          const RecordCopyNewOptions& options,
                                          const FormLinkTypeLookup& type_lookup) {
  require_decodable(source_record);
  const auto types = type_lookup ? type_lookup : composed_type_lookup(source, destination);

  std::set<PluginKey> required_owners;
  for (const auto& link : collect_form_links(source)) {
    if (link.record != source_record.form_key || link.key.owner().empty()) continue;
    required_owners.insert(link.key.owner());
  }

  std::vector<MasterDescriptor> masters;
  masters.reserve(destination.header.masters.size() + required_owners.size());
  for (const auto& master : destination.header.masters) {
    masters.push_back({master.key, master.kind});
  }
  bool added_master = false;
  for (const auto& owner : required_owners) {
    if (owner == destination.key || has_master(destination, owner)) continue;
    masters.push_back({owner, owner_kind(source, owner)});
    added_master = true;
  }
  if (added_master) {
    destination = remap_plugin_masters(destination, std::move(masters), types);
  }

  const auto new_key = create_local_record(destination, source_record.header.signature);
  const auto source_context = plugin_form_id_context(source);
  const auto destination_context = plugin_form_id_context(destination);
  auto copied = source_record;
  copied.form_key = new_key;
  copied.header.form_id = encode_plugin_form_id(new_key, destination_context);
  remap_record_form_links(copied, source_context, destination_context, types);
  copied.original_range.clear();
  copied.decompressed_payload.clear();
  copied.header.data_size = 0;
  copied.uncompressed_size = 0;

  auto* allocated = mutable_record_of(destination, new_key);
  if (allocated == nullptr) {
    throw std::logic_error("allocated record disappeared before duplication");
  }
  *allocated = std::move(copied);
  apply_editor_id(destination, new_key, source_record, options);
  return {RecordCopyNewStatus::created, new_key};
}

RecordCopyNewResult copy_contained_as_new(Plugin& destination,
                                          const Plugin& source,
                                          const FormKey& record,
                                          const Record& source_record,
                                          const RecordCopyNewOptions& options) {
  if (source_record.header.signature == Signature::from_string("INFO")) {
    const auto copied = copy_info_as_new(destination, source, record);
    switch (copied.status) {
      case InfoCopyNewStatus::created:
        break;
      case InfoCopyNewStatus::source_missing:
        return {RecordCopyNewStatus::source_missing, {}};
      case InfoCopyNewStatus::unsupported_signature:
        return {RecordCopyNewStatus::unsupported_signature, {}};
      case InfoCopyNewStatus::parent_dial_missing:
        return {RecordCopyNewStatus::parent_dial_missing, {}};
    }
    apply_editor_id(destination, copied.new_info, source_record, options);
    return {RecordCopyNewStatus::created, copied.new_info};
  }

  const auto copied = copy_placed_reference_as_new(destination, source, record);
  switch (copied.status) {
    case PlacedReferenceCopyNewStatus::created:
      break;
    case PlacedReferenceCopyNewStatus::source_missing:
      return {RecordCopyNewStatus::source_missing, {}};
    case PlacedReferenceCopyNewStatus::unsupported_signature:
      return {RecordCopyNewStatus::unsupported_signature, {}};
    case PlacedReferenceCopyNewStatus::parent_cell_missing:
      return {RecordCopyNewStatus::parent_cell_missing, {}};
  }
  apply_editor_id(destination, copied.new_reference, source_record, options);
  return {RecordCopyNewStatus::created, copied.new_reference};
}

}  // namespace

RecordCopyNewResult copy_record_as_new(Plugin& destination,
                                       const Plugin& source,
                                       const FormKey& record,
                                       const RecordCopyNewOptions& options,
                                       FormLinkTypeLookup type_lookup) {
  if (const auto* top = top_level_record_of(source, record); top != nullptr) {
    if (refused_top_level(top->header.signature)) {
      return {RecordCopyNewStatus::unsupported_signature, {}};
    }
    return copy_top_level_as_new(destination, source, *top, options, type_lookup);
  }
  const auto* contained = find_record(source, record);
  if (contained == nullptr) return {RecordCopyNewStatus::source_missing, {}};
  return copy_contained_as_new(destination, source, record, *contained, options);
}

}  // namespace fo4::forms
