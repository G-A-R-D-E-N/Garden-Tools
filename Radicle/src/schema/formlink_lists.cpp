#include "fo4/schema/formlink_lists.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::schema {
namespace {

Record* find_mutable_record(Plugin& plugin, const FormKey& key) {
  Record* found = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (found == nullptr && record.header.signature != sig_tes4 &&
        record.form_key == key) {
      found = &record;
    }
  });
  return found;
}

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

void write_u32(std::vector<std::byte>& data, std::uint32_t value) {
  std::memcpy(data.data(), &value, sizeof(value));
}

void mark_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

FormLinkListMutationStatus validate_record_and_target(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    const FormKey* target,
    Record*& record) {
  record = find_mutable_record(plugin, record_key);
  if (record == nullptr) return FormLinkListMutationStatus::record_missing;
  if (record->header.signature != descriptor.record) {
    return FormLinkListMutationStatus::record_mismatch;
  }
  if (target != nullptr && target->owner() == plugin.key) {
    const auto* local_target = find_record(plugin, *target);
    if (local_target == nullptr ||
        !formlink_list_target_allowed(descriptor, local_target->header.signature)) {
      return FormLinkListMutationStatus::target_type_mismatch;
    }
  }
  return FormLinkListMutationStatus::unchanged;
}

std::vector<std::size_t> matching_positions(
    const Record& record,
    const FormLinkListDescriptor& descriptor) {
  std::vector<std::size_t> positions;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto& subrecord = record.subrecords[index];
    if (subrecord.signature != descriptor.subrecord) continue;
    if (subrecord.data.size() != 4) {
      throw FormLinkListError(
          descriptor.record.to_string() + " " + std::string(descriptor.path) +
          " element storage must be exactly four bytes");
    }
    positions.push_back(index);
  }
  return positions;
}

Subrecord encoded_element(const FormLinkListDescriptor& descriptor,
                          RawFormId encoded) {
  Subrecord subrecord;
  subrecord.signature = descriptor.subrecord;
  subrecord.data.resize(4);
  write_u32(subrecord.data, encoded.value());
  return subrecord;
}

std::span<const Signature> anchor_predecessors(
    const FormLinkListDescriptor& descriptor) {
  return {k_generated_formlink_list_anchors + descriptor.first_anchor,
          descriptor.predecessor_count};
}

std::span<const Signature> anchor_successors(
    const FormLinkListDescriptor& descriptor) {
  return {k_generated_formlink_list_anchors + descriptor.first_anchor +
              descriptor.predecessor_count,
          descriptor.successor_count};
}

// Positions the first element of an otherwise-empty flat FormLink list. With no
// existing element of its own to anchor against, fall back to the schema's
// declared sibling order: insert immediately after the nearest preceding sibling
// list that is present, or immediately before the nearest following one. When no
// sibling list is present the record offers no anchor and insertion fails closed.
std::optional<std::size_t> empty_list_insertion_point(
    const Record& record,
    const FormLinkListDescriptor& descriptor) {
  for (const auto predecessor : anchor_predecessors(descriptor)) {
    std::optional<std::size_t> last;
    for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
      if (record.subrecords[index].signature == predecessor) last = index;
    }
    if (last.has_value()) return *last + 1;
  }
  for (const auto successor : anchor_successors(descriptor)) {
    for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
      if (record.subrecords[index].signature == successor) return index;
    }
  }
  // A list with no sibling to anchor against (FLST\Items holds only EDID before
  // its LNAM entries) appends its first element at the end of the record.
  if (descriptor.append_when_empty) return record.subrecords.size();
  return std::nullopt;
}

FormLinkListMutationStatus insert_encoded_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index,
    RawFormId encoded) {
  if (!descriptor.can_insert) {
    return FormLinkListMutationStatus::insertion_boundary_unavailable;
  }
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (index > positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  std::size_t insertion = 0;
  if (positions.empty()) {
    const auto anchored = empty_list_insertion_point(*record, descriptor);
    if (!anchored.has_value()) {
      return FormLinkListMutationStatus::insertion_boundary_unavailable;
    }
    insertion = *anchored;
  } else {
    insertion = index < positions.size() ? positions[index] : positions.back() + 1;
  }

  record->subrecords.insert(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(insertion),
      encoded_element(descriptor, encoded));
  mark_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

}  // namespace

std::optional<FormLinkListDescriptor> formlink_list_descriptor(
    Signature record,
    std::string_view base_path) noexcept {
  for (const auto& descriptor : k_generated_formlink_lists) {
    if (descriptor.record == record && descriptor.path == base_path) {
      return descriptor;
    }
  }
  return std::nullopt;
}

std::optional<FormLinkListDescriptor> formlink_list_descriptor_by_field(
    Signature record,
    std::string_view field_alias) noexcept {
  if (field_alias.empty()) return std::nullopt;
  for (const auto& descriptor : k_generated_formlink_lists) {
    if (descriptor.record == record && descriptor.field_alias == field_alias) {
      return descriptor;
    }
  }
  return std::nullopt;
}

std::optional<IndexedFormLinkListPath> indexed_formlink_list_path(
    std::string_view path) noexcept {
  if (path.empty() || path.back() != ']') return std::nullopt;
  const auto open = path.rfind('[');
  if (open == std::string_view::npos || open == 0 || open + 2 > path.size()) {
    return std::nullopt;
  }

  std::size_t index = 0;
  const auto index_text = path.substr(open + 1, path.size() - open - 2);
  if (index_text.empty()) return std::nullopt;
  const auto* begin = index_text.data();
  const auto* end = begin + index_text.size();
  const auto [ptr, error] = std::from_chars(begin, end, index, 10);
  if (error != std::errc{} || ptr != end) return std::nullopt;

  const auto base_path = path.substr(0, open);
  for (const auto& descriptor : k_generated_formlink_lists) {
    if (descriptor.path == base_path) {
      return IndexedFormLinkListPath{descriptor, index};
    }
  }
  return std::nullopt;
}

std::span<const Signature> formlink_list_targets(
    const FormLinkListDescriptor& descriptor) noexcept {
  return {k_generated_formlink_list_targets + descriptor.first_target,
          descriptor.target_count};
}

bool formlink_list_target_allowed(const FormLinkListDescriptor& descriptor,
                                  Signature signature) noexcept {
  if (descriptor.wildcard_target) return true;
  const auto targets = formlink_list_targets(descriptor);
  return std::find(targets.begin(), targets.end(), signature) != targets.end();
}

std::optional<std::size_t> record_formlink_list_size(
    const Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  if (record->header.signature != descriptor.record) {
    throw FormLinkListError(
        std::string(descriptor.path) + " does not apply to " +
        record->header.signature.to_string() + " records");
  }
  return matching_positions(*record, descriptor).size();
}

FormLinkListMutationStatus set_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index,
    const FormKey& target) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, &target, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (index >= positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  const auto context = plugin_form_id_context(next);
  const auto encoded = encode_plugin_form_id(target, context);
  auto& subrecord = record->subrecords[positions[index]];
  if (read_u32(subrecord.data) == encoded.value()) {
    return FormLinkListMutationStatus::unchanged;
  }
  write_u32(subrecord.data, encoded.value());
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus insert_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index,
    const FormKey& target) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, &target, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;
  const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(next));
  const auto status = insert_encoded_element(next, record_key, descriptor, index, encoded);
  if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
  return status;
}

FormLinkListMutationStatus insert_record_formlink_list_null_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index) {
  if (!descriptor.nullable) {
    throw FormLinkListError(
        std::string(descriptor.path) + " does not allow null FormLink elements");
  }
  auto next = plugin;
  const auto status = insert_encoded_element(
      next, record_key, descriptor, index, RawFormId{0});
  if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
  return status;
}

FormLinkListMutationStatus remove_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (index >= positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  record->subrecords.erase(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(positions[index]));
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

std::size_t remove_record_formlink_list_elements_matching(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    const FormKey& target) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return 0;

  const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(next));
  const auto positions = matching_positions(*record, descriptor);
  std::vector<std::size_t> victims;
  for (const auto position : positions) {
    if (read_u32(record->subrecords[position].data) == encoded.value()) {
      victims.push_back(position);
    }
  }
  if (victims.empty()) return 0;

  for (auto it = victims.rbegin(); it != victims.rend(); ++it) {
    record->subrecords.erase(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(*it));
  }
  mark_modified(next, *record);
  plugin = std::move(next);
  return victims.size();
}

FormLinkListMutationStatus move_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t from,
    std::size_t to) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (from >= positions.size() || to >= positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }
  if (from == to) return FormLinkListMutationStatus::unchanged;

  auto moving = record->subrecords[positions[from]].data;
  if (from < to) {
    for (std::size_t index = from; index < to; ++index) {
      record->subrecords[positions[index]].data =
          record->subrecords[positions[index + 1]].data;
    }
  } else {
    for (std::size_t index = from; index > to; --index) {
      record->subrecords[positions[index]].data =
          record->subrecords[positions[index - 1]].data;
    }
  }
  record->subrecords[positions[to]].data = std::move(moving);
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus clear_record_formlink_list(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (positions.empty()) return FormLinkListMutationStatus::unchanged;
  for (auto it = positions.rbegin(); it != positions.rend(); ++it) {
    record->subrecords.erase(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(*it));
  }
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

}  // namespace fo4::schema
