#include "fo4/plugin/subrecord_copy.hpp"

#include "record_edit.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace fo4 {
namespace {

bool is_master_or_self(const Plugin& plugin, const PluginKey& owner) {
  if (owner == plugin.key) return true;
  return std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                     [&](const MasterReference& master) { return master.key == owner; });
}

SubrecordCopyStatus copy_status(SubrecordAddStatus status) {
  switch (status) {
    case SubrecordAddStatus::added: return SubrecordCopyStatus::copied;
    case SubrecordAddStatus::record_missing: return SubrecordCopyStatus::record_missing;
    case SubrecordAddStatus::not_authorable: return SubrecordCopyStatus::not_authorable;
    case SubrecordAddStatus::unknown_order: return SubrecordCopyStatus::unknown_order;
    case SubrecordAddStatus::no_default: return SubrecordCopyStatus::no_default;
    case SubrecordAddStatus::already_present: return SubrecordCopyStatus::occurrence_unreachable;
  }
  return SubrecordCopyStatus::occurrence_unreachable;
}

std::size_t occurrences_of(const Record& record, Signature subrecord) {
  return static_cast<std::size_t>(std::count_if(
      record.subrecords.begin(), record.subrecords.end(),
      [&](const Subrecord& sub) { return sub.signature == subrecord; }));
}

std::optional<std::size_t> nth_occurrence(const Record& record, Signature subrecord, std::size_t occurrence) {
  std::size_t seen = 0;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature != subrecord) continue;
    if (seen++ == occurrence) return index;
  }
  return std::nullopt;
}

}  // namespace

SubrecordCopyStatus copy_record_subrecord(Plugin& destination,
                                          const FormKey& destination_key,
                                          const Plugin& source,
                                          const FormKey& source_key,
                                          Signature subrecord,
                                          std::size_t occurrence) {
  const auto* source_record = find_record(source, source_key);
  if (source_record == nullptr) return SubrecordCopyStatus::source_missing;
  const auto source_index = nth_occurrence(*source_record, subrecord, occurrence);
  if (!source_index) return SubrecordCopyStatus::source_occurrence_missing;
  const auto& element = source_record->subrecords[*source_index];
  if (schema::is_localized_field(source_record->header.signature, subrecord) &&
      (source.header.tes4_flags.is_localized() != destination.header.tes4_flags.is_localized() ||
       (source.header.tes4_flags.is_localized() && source.key != destination.key))) {
    return SubrecordCopyStatus::localized_mismatch;
  }

  for (const auto& hit : collect_form_links(source)) {
    if (hit.record != source_key || hit.subrecord != subrecord || hit.subrecord_ordinal != occurrence) {
      continue;
    }
    if (hit.key.owner().empty()) continue;
    if (!is_master_or_self(destination, hit.key.owner())) return SubrecordCopyStatus::owner_not_master;
  }

  Record payload;
  payload.header.signature = source_record->header.signature;
  payload.form_key = source_key;
  payload.subrecords.push_back(element);
  remap_record_form_links(payload, plugin_form_id_context(source), plugin_form_id_context(destination),
                          plugin_type_lookup(destination));

  auto* record = detail::locate_mutable_record(destination, destination_key);
  if (record == nullptr) return SubrecordCopyStatus::record_missing;
  const auto present = occurrences_of(*record, subrecord);
  if (present < occurrence) return SubrecordCopyStatus::occurrence_unreachable;
  if (present == occurrence) {
    const auto status = copy_status(add_record_subrecord(destination, destination_key, subrecord));
    if (status != SubrecordCopyStatus::copied) return status;
    record = detail::locate_mutable_record(destination, destination_key);
    if (record == nullptr) return SubrecordCopyStatus::record_missing;
  }

  const auto target_index = nth_occurrence(*record, subrecord, occurrence);
  if (!target_index) return SubrecordCopyStatus::occurrence_unreachable;
  auto& target = record->subrecords[*target_index];
  target.data = payload.subrecords.front().data;
  target.extended_size = payload.subrecords.front().extended_size;
  detail::mark_record_modified(destination, *record);
  return SubrecordCopyStatus::copied;
}

}  // namespace fo4
