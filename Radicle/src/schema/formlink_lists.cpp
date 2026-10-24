#include "fo4/schema/formlink_lists.hpp"

#include "fo4/plugin/reader.hpp"
#include "../plugin/record_edit.hpp"
#include "formlink_segments.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::schema {
namespace {

using detail::erase_list_element;
using detail::move_list_element;
using detail::packed_segment;
using detail::reconcile_counter;

bool iequals_ascii(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::uint32_t read_u32(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data(), sizeof(value));
  return value;
}

void write_u32(std::vector<std::byte>& data, std::uint32_t value) {
  std::memcpy(data.data(), &value, sizeof(value));
}

FormLinkListMutationStatus validate_record_and_target(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    const FormKey* target,
    Record*& record) {
  record = fo4::detail::find_mutable_record(plugin, record_key);
  if (record == nullptr || record->header.signature == sig_tes4) return FormLinkListMutationStatus::record_missing;
  if (record->header.signature != descriptor.record) {
    return FormLinkListMutationStatus::record_mismatch;
  }
  if (target != nullptr && target->owner().empty() && target->local().value() == 0 && !descriptor.nullable) {
    throw FormLinkListError(std::string(descriptor.path) + " does not allow null FormLink elements");
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

std::optional<std::size_t> empty_list_insertion_point(
    const Record& record,
    const FormLinkListDescriptor& descriptor) {
  const auto predecessors = anchor_predecessors(descriptor);
  std::optional<std::size_t> after;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto signature = record.subrecords[index].signature;
    if (std::find(predecessors.begin(), predecessors.end(), signature) != predecessors.end()) after = index + 1;
  }
  if (after.has_value()) return after;
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
  reconcile_counter(*record, descriptor, static_cast<std::uint32_t>(positions.size() + 1));
  fo4::detail::mark_record_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

Subrecord* find_subrecord(Record& record, Signature signature) {
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

std::optional<std::size_t> subrecord_index(const Record& record, Signature signature) {
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature == signature) return index;
  }
  return std::nullopt;
}

std::size_t packed_element_count(const Record& record,
                                 const FormLinkListDescriptor& descriptor) {
  return packed_segment(record, descriptor).count;
}

std::uint32_t packed_read(const std::vector<std::byte>& data, std::size_t element) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + element * 4, sizeof(value));
  return value;
}

void packed_write(std::vector<std::byte>& data, std::size_t element,
                  std::uint32_t value) {
  std::memcpy(data.data() + element * 4, &value, sizeof(value));
}

FormLinkListMutationStatus set_packed_element(
    Plugin& plugin, const FormKey& record_key,
    const FormLinkListDescriptor& descriptor, std::size_t index,
    RawFormId encoded) {
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;
  Subrecord* array = find_subrecord(*record, descriptor.subrecord);
  const auto segment = packed_segment(*record, descriptor);
  if (array == nullptr || index >= segment.count) {
    return FormLinkListMutationStatus::index_out_of_range;
  }
  if (packed_read(array->data, segment.base + index) == encoded.value()) {
    return FormLinkListMutationStatus::unchanged;
  }
  packed_write(array->data, segment.base + index, encoded.value());
  fo4::detail::mark_record_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus insert_packed_element(
    Plugin& plugin, const FormKey& record_key,
    const FormLinkListDescriptor& descriptor, std::size_t index,
    RawFormId encoded) {
  if (!descriptor.can_insert) {
    return FormLinkListMutationStatus::insertion_boundary_unavailable;
  }
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto array_index = subrecord_index(*record, descriptor.subrecord);
  const auto segment = packed_segment(*record, descriptor);
  if (index > segment.count) return FormLinkListMutationStatus::index_out_of_range;

  if (!array_index.has_value()) {
    std::size_t insertion = 0;
    const auto counter_index = descriptor.counter == Signature{}
                                   ? std::optional<std::size_t>{}
                                   : subrecord_index(*record, descriptor.counter);
    if (counter_index.has_value()) {
      insertion = *counter_index + 1;
    } else {
      const auto anchored = empty_list_insertion_point(*record, descriptor);
      if (!anchored.has_value() && descriptor.counter == Signature{}) {
        return FormLinkListMutationStatus::insertion_boundary_unavailable;
      }
      // Constraint: a declared counter is created before the array, so an empty list without an anchor appends at the end of the record.
      insertion = anchored.value_or(record->subrecords.size());
    }
    Subrecord created;
    created.signature = descriptor.subrecord;
    created.data.resize(4);
    write_u32(created.data, encoded.value());
    record->subrecords.insert(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(insertion),
        std::move(created));
  } else {
    auto& data = record->subrecords[*array_index].data;
    std::byte bytes[4];
    const std::uint32_t raw = encoded.value();
    std::memcpy(bytes, &raw, sizeof(raw));
    data.insert(data.begin() + static_cast<std::ptrdiff_t>((segment.base + index) * 4),
                bytes, bytes + 4);
  }
  reconcile_counter(*record, descriptor,
                    static_cast<std::uint32_t>(segment.count + 1));
  fo4::detail::mark_record_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus remove_packed_element(
    Plugin& plugin, const FormKey& record_key,
    const FormLinkListDescriptor& descriptor, std::size_t index) {
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;
  const auto array_index = subrecord_index(*record, descriptor.subrecord);
  const auto segment = packed_segment(*record, descriptor);
  if (!array_index.has_value() || index >= segment.count) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  auto& data = record->subrecords[*array_index].data;
  const std::size_t offset = (segment.base + index) * 4;
  data.erase(data.begin() + static_cast<std::ptrdiff_t>(offset),
             data.begin() + static_cast<std::ptrdiff_t>(offset + 4));
  if (data.empty()) {
    record->subrecords.erase(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(*array_index));
    reconcile_counter(*record, descriptor, 0);
  } else {
    reconcile_counter(*record, descriptor,
                      static_cast<std::uint32_t>(segment.count - 1));
  }
  fo4::detail::mark_record_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

std::size_t remove_packed_elements_matching(
    Plugin& plugin, const FormKey& record_key,
    const FormLinkListDescriptor& descriptor, std::uint32_t encoded) {
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return 0;
  const auto array_index = subrecord_index(*record, descriptor.subrecord);
  if (!array_index.has_value()) return 0;
  const auto segment = packed_segment(*record, descriptor);

  std::vector<std::byte> kept;
  std::size_t removed = 0;
  auto& data = record->subrecords[*array_index].data;
  const std::size_t elements = data.size() / 4;
  for (std::size_t element = 0; element < elements; ++element) {
    const bool in_segment = element >= segment.base &&
                            element - segment.base < segment.count;
    if (in_segment && packed_read(data, element) == encoded) {
      ++removed;
      continue;
    }
    kept.insert(kept.end(), data.begin() + static_cast<std::ptrdiff_t>(element * 4),
                data.begin() + static_cast<std::ptrdiff_t>(element * 4 + 4));
  }
  if (removed == 0) return 0;
  if (kept.empty()) {
    record->subrecords.erase(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(*array_index));
    reconcile_counter(*record, descriptor, 0);
  } else {
    data = std::move(kept);
    reconcile_counter(*record, descriptor,
                      static_cast<std::uint32_t>(segment.count - removed));
  }
  fo4::detail::mark_record_modified(plugin, *record);
  return removed;
}

FormLinkListMutationStatus move_packed_element(
    Plugin& plugin, const FormKey& record_key,
    const FormLinkListDescriptor& descriptor, std::size_t from, std::size_t to) {
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;
  const auto array_index = subrecord_index(*record, descriptor.subrecord);
  const auto segment = packed_segment(*record, descriptor);
  if (from >= segment.count || to >= segment.count) {
    return FormLinkListMutationStatus::index_out_of_range;
  }
  if (from == to) return FormLinkListMutationStatus::unchanged;
  auto& data = record->subrecords[*array_index].data;
  const std::size_t base = segment.base;
  const std::uint32_t moving = packed_read(data, base + from);
  if (from < to) {
    for (std::size_t element = from; element < to; ++element) {
      packed_write(data, base + element, packed_read(data, base + element + 1));
    }
  } else {
    for (std::size_t element = from; element > to; --element) {
      packed_write(data, base + element, packed_read(data, base + element - 1));
    }
  }
  packed_write(data, base + to, moving);
  fo4::detail::mark_record_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus clear_packed(
    Plugin& plugin, const FormKey& record_key,
    const FormLinkListDescriptor& descriptor) {
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(plugin, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;
  const auto array_index = subrecord_index(*record, descriptor.subrecord);
  const auto counter_index = subrecord_index(*record, descriptor.counter);
  if (!array_index.has_value() && !counter_index.has_value()) {
    return FormLinkListMutationStatus::unchanged;
  }
  if (descriptor.segment_count != 0) {
    const auto segment = packed_segment(*record, descriptor);
    if (segment.count == 0) return FormLinkListMutationStatus::unchanged;
    auto& data = record->subrecords[*array_index].data;
    data.erase(data.begin() + static_cast<std::ptrdiff_t>(segment.base * 4),
               data.begin() +
                   static_cast<std::ptrdiff_t>((segment.base + segment.count) * 4));
    if (data.empty()) {
      record->subrecords.erase(
          record->subrecords.begin() + static_cast<std::ptrdiff_t>(*array_index));
    }
    reconcile_counter(*record, descriptor, 0);
    fo4::detail::mark_record_modified(plugin, *record);
    return FormLinkListMutationStatus::updated;
  }
  if (array_index.has_value()) {
    record->subrecords.erase(
        record->subrecords.begin() + static_cast<std::ptrdiff_t>(*array_index));
  }
  reconcile_counter(*record, descriptor, 0);
  fo4::detail::mark_record_modified(plugin, *record);
  return FormLinkListMutationStatus::updated;
}

}  // namespace

bool is_sig_base_object(Signature signature) noexcept {
  const auto begin = std::begin(k_generated_sig_base_objects);
  const auto end = std::end(k_generated_sig_base_objects);
  return std::find(begin, end, signature) != end;
}

std::optional<FormLinkFieldDescriptor> formlink_field_descriptor(
    std::string_view path) noexcept {
  for (const auto& descriptor : k_generated_formlink_fields) {
    if (descriptor.path == path) return descriptor;
  }
  return std::nullopt;
}

std::optional<FormLinkFieldDescriptor> formlink_field_descriptor(
    Signature record, std::string_view path) noexcept {
  for (const auto& descriptor : k_generated_formlink_fields) {
    if (descriptor.record == record && descriptor.path == path) return descriptor;
  }
  return std::nullopt;
}

std::span<const FormLinkFieldDescriptor> formlink_fields(
    Signature record) noexcept {
  const auto rows = std::ranges::equal_range(k_generated_formlink_fields, record,
                                             std::ranges::less{},
                                             &FormLinkFieldDescriptor::record);
  return std::span<const FormLinkFieldDescriptor>(rows.begin(), rows.end());
}

std::span<const Signature> formlink_field_targets(
    const FormLinkFieldDescriptor& descriptor) noexcept {
  return {k_generated_formlink_field_targets + descriptor.first_target,
          descriptor.target_count};
}

bool formlink_field_target_allowed(
    const FormLinkFieldDescriptor& descriptor, Signature signature) noexcept {
  const auto targets = formlink_field_targets(descriptor);
  return std::find(targets.begin(), targets.end(), signature) != targets.end();
}

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
    if (descriptor.record == record &&
        iequals_ascii(descriptor.field_alias, field_alias)) {
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

std::optional<FormLinkListDescriptor> packed_formlink_list_path(
    std::string_view path) noexcept {
  const auto list = formlink_list_descriptor(Signature::from_string(path.substr(0, 4)), path);
  return list && list->storage == FormLinkListStorage::packed_array ? list : std::nullopt;
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
  if (descriptor.storage == FormLinkListStorage::packed_array) {
    return packed_element_count(*record, descriptor);
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

  if (descriptor.storage == FormLinkListStorage::packed_array) {
    const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(next));
    const auto status = set_packed_element(next, record_key, descriptor, index, encoded);
    if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
    return status;
  }

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
  fo4::detail::mark_record_modified(next, *record);
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
  const auto status = descriptor.storage == FormLinkListStorage::packed_array
      ? insert_packed_element(next, record_key, descriptor, index, encoded)
      : insert_encoded_element(next, record_key, descriptor, index, encoded);
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
  const auto status = descriptor.storage == FormLinkListStorage::packed_array
      ? insert_packed_element(next, record_key, descriptor, index, RawFormId{0})
      : insert_encoded_element(next, record_key, descriptor, index, RawFormId{0});
  if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
  return status;
}

FormLinkListMutationStatus remove_record_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    std::size_t index) {
  auto next = plugin;
  if (descriptor.storage == FormLinkListStorage::packed_array) {
    const auto status = remove_packed_element(next, record_key, descriptor, index);
    if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
    return status;
  }
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (index >= positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  erase_list_element(*record, descriptor, positions[index]);
  reconcile_counter(*record, descriptor,
                    static_cast<std::uint32_t>(positions.size() - 1));
  fo4::detail::mark_record_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

std::size_t remove_record_formlink_list_elements_matching(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor,
    const FormKey& target) {
  auto next = plugin;
  if (descriptor.storage == FormLinkListStorage::packed_array) {
    const auto encoded = encode_plugin_form_id(target, plugin_form_id_context(next));
    const auto removed =
        remove_packed_elements_matching(next, record_key, descriptor, encoded.value());
    if (removed > 0) plugin = std::move(next);
    return removed;
  }
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
    erase_list_element(*record, descriptor, *it);
  }
  reconcile_counter(*record, descriptor,
                    static_cast<std::uint32_t>(positions.size() - victims.size()));
  fo4::detail::mark_record_modified(next, *record);
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
  if (descriptor.storage == FormLinkListStorage::packed_array) {
    const auto status = move_packed_element(next, record_key, descriptor, from, to);
    if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
    return status;
  }
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (from >= positions.size() || to >= positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }
  if (from == to) return FormLinkListMutationStatus::unchanged;

  move_list_element(*record, descriptor, positions, from, to);
  fo4::detail::mark_record_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus clear_record_formlink_list(
    Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor) {
  auto next = plugin;
  if (descriptor.storage == FormLinkListStorage::packed_array) {
    const auto status = clear_packed(next, record_key, descriptor);
    if (status == FormLinkListMutationStatus::updated) plugin = std::move(next);
    return status;
  }
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (positions.empty()) return FormLinkListMutationStatus::unchanged;
  for (auto it = positions.rbegin(); it != positions.rend(); ++it) {
    erase_list_element(*record, descriptor, *it);
  }
  reconcile_counter(*record, descriptor, 0);
  fo4::detail::mark_record_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

}  // namespace fo4::schema
