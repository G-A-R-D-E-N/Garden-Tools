#include "fo4/schema/formlink_lists.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "../plugin/record_edit.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::schema {
namespace {

bool parse_index(std::string_view text, std::size_t& value) noexcept {
  if (text.empty()) return false;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value, 10);
  return error == std::errc{} && ptr == end;
}

std::optional<ScopedNestedFormLinkListPath> parse_scoped_path(
    std::string_view path) noexcept {
  for (const auto& descriptor : k_generated_nested_formlink_lists) {
    if (!path.starts_with(descriptor.container_path)) continue;
    auto cursor = descriptor.container_path.size();
    if (cursor >= path.size() || path[cursor] != '[') continue;
    const auto close = path.find(']', cursor + 1);
    if (close == std::string_view::npos) continue;

    std::size_t container_index = 0;
    if (!parse_index(path.substr(cursor + 1, close - cursor - 1), container_index)) {
      continue;
    }
    if (close + 2 > path.size() || path[close + 1] != '\\') continue;
    cursor = close + 2;
    if (path.substr(cursor) != descriptor.member) continue;
    return ScopedNestedFormLinkListPath{descriptor, container_index};
  }
  return std::nullopt;
}

using fo4::detail::find_mutable_record;

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

struct GroupRange {
  std::size_t begin{0};
  std::size_t terminator{0};
};

std::optional<GroupRange> terminated_group_range(
    const Record& record,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index) {
  std::size_t begin = 0;
  std::size_t ordinal = 0;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    const auto& subrecord = record.subrecords[index];
    if (subrecord.signature != descriptor.terminator) continue;
    if (subrecord.data.size() != descriptor.terminator_size) {
      throw FormLinkListError(
          descriptor.record.to_string() + " " +
          std::string(descriptor.container_path) +
          " terminator " + descriptor.terminator.to_string() +
          " has unexpected storage size");
    }
    if (ordinal == container_index) {
      return GroupRange{begin, index};
    }
    ++ordinal;
    begin = index + 1;
  }
  return std::nullopt;
}

std::vector<std::size_t> matching_positions(
    const Record& record,
    const NestedFormLinkListDescriptor& descriptor,
    const GroupRange& range) {
  std::vector<std::size_t> positions;
  for (std::size_t index = range.begin; index < range.terminator; ++index) {
    const auto& subrecord = record.subrecords[index];
    if (subrecord.signature != descriptor.subrecord) continue;
    if (subrecord.data.size() != 4) {
      throw FormLinkListError(
          descriptor.record.to_string() + " " +
          std::string(descriptor.container_path) + "\\" +
          std::string(descriptor.member) +
          " element storage must be exactly four bytes");
    }
    positions.push_back(index);
  }
  return positions;
}

FormLinkListMutationStatus validate_record_and_target(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
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
        !nested_formlink_list_target_allowed(
            descriptor, local_target->header.signature)) {
      return FormLinkListMutationStatus::target_type_mismatch;
    }
  }
  return FormLinkListMutationStatus::unchanged;
}

Subrecord encoded_element(const NestedFormLinkListDescriptor& descriptor,
                          RawFormId encoded) {
  Subrecord subrecord;
  subrecord.signature = descriptor.subrecord;
  subrecord.data.resize(4);
  write_u32(subrecord.data, encoded.value());
  return subrecord;
}

}  // namespace

std::optional<ScopedNestedFormLinkListPath> scoped_nested_formlink_list_path(
    std::string_view path) noexcept {
  return parse_scoped_path(path);
}

std::optional<IndexedNestedFormLinkListPath> indexed_nested_formlink_list_path(
    std::string_view path) noexcept {
  if (path.empty() || path.back() != ']') return std::nullopt;
  const auto element_open = path.rfind('[');
  if (element_open == std::string_view::npos || element_open == 0) {
    return std::nullopt;
  }
  std::size_t element_index = 0;
  if (!parse_index(
          path.substr(element_open + 1, path.size() - element_open - 2),
          element_index)) {
    return std::nullopt;
  }
  const auto scoped = parse_scoped_path(path.substr(0, element_open));
  if (!scoped.has_value()) return std::nullopt;
  return IndexedNestedFormLinkListPath{
      scoped->descriptor, scoped->container_index, element_index};
}

std::span<const Signature> nested_formlink_list_targets(
    const NestedFormLinkListDescriptor& descriptor) noexcept {
  return {k_generated_formlink_list_targets + descriptor.first_target,
          descriptor.target_count};
}

bool nested_formlink_list_target_allowed(
    const NestedFormLinkListDescriptor& descriptor,
    Signature signature) noexcept {
  const auto targets = nested_formlink_list_targets(descriptor);
  return std::find(targets.begin(), targets.end(), signature) != targets.end();
}

std::optional<std::size_t> record_nested_formlink_list_size(
    const Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  if (record->header.signature != descriptor.record) {
    throw FormLinkListError(
        std::string(descriptor.container_path) + " does not apply to " +
        record->header.signature.to_string() + " records");
  }
  const auto range = terminated_group_range(*record, descriptor, container_index);
  if (!range.has_value()) return std::nullopt;
  return matching_positions(*record, descriptor, *range).size();
}

FormLinkListMutationStatus set_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index,
    const FormKey& target) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, &target, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto range = terminated_group_range(*record, descriptor, container_index);
  if (!range.has_value()) {
    return FormLinkListMutationStatus::container_index_out_of_range;
  }
  const auto positions = matching_positions(*record, descriptor, *range);
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

FormLinkListMutationStatus insert_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index,
    const FormKey& target) {
  if (!descriptor.can_insert) {
    return FormLinkListMutationStatus::insertion_boundary_unavailable;
  }

  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, &target, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto range = terminated_group_range(*record, descriptor, container_index);
  if (!range.has_value()) {
    return FormLinkListMutationStatus::container_index_out_of_range;
  }
  const auto positions = matching_positions(*record, descriptor, *range);
  if (index > positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  const auto context = plugin_form_id_context(next);
  const auto encoded = encode_plugin_form_id(target, context);
  std::size_t insertion = range->terminator;
  if (index < positions.size()) {
    insertion = positions[index];
  } else if (!positions.empty()) {
    insertion = positions.back() + 1;
  }
  record->subrecords.insert(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(insertion),
      encoded_element(descriptor, encoded));
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus remove_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t index) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto range = terminated_group_range(*record, descriptor, container_index);
  if (!range.has_value()) {
    return FormLinkListMutationStatus::container_index_out_of_range;
  }
  const auto positions = matching_positions(*record, descriptor, *range);
  if (index >= positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }

  record->subrecords.erase(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(positions[index]));
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
}

FormLinkListMutationStatus move_record_nested_formlink_list_element(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index,
    std::size_t from,
    std::size_t to) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto range = terminated_group_range(*record, descriptor, container_index);
  if (!range.has_value()) {
    return FormLinkListMutationStatus::container_index_out_of_range;
  }
  const auto positions = matching_positions(*record, descriptor, *range);
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

FormLinkListMutationStatus clear_record_nested_formlink_list(
    Plugin& plugin,
    const FormKey& record_key,
    const NestedFormLinkListDescriptor& descriptor,
    std::size_t container_index) {
  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, nullptr, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto range = terminated_group_range(*record, descriptor, container_index);
  if (!range.has_value()) {
    return FormLinkListMutationStatus::container_index_out_of_range;
  }
  const auto positions = matching_positions(*record, descriptor, *range);
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
