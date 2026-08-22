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
  record.original_bytes.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_bytes.clear();
  visit_groups(plugin, [](Group& group) { group.original_bytes.clear(); });
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
  const auto targets = formlink_list_targets(descriptor);
  return std::find(targets.begin(), targets.end(), signature) != targets.end();
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
  if (!descriptor.can_insert) {
    return FormLinkListMutationStatus::insertion_boundary_unavailable;
  }

  auto next = plugin;
  Record* record = nullptr;
  const auto validation =
      validate_record_and_target(next, record_key, descriptor, &target, record);
  if (validation != FormLinkListMutationStatus::unchanged) return validation;

  const auto positions = matching_positions(*record, descriptor);
  if (index > positions.size()) {
    return FormLinkListMutationStatus::index_out_of_range;
  }
  if (positions.empty()) {
    return FormLinkListMutationStatus::insertion_boundary_unavailable;
  }

  const auto context = plugin_form_id_context(next);
  const auto encoded = encode_plugin_form_id(target, context);
  const auto insertion =
      index < positions.size() ? positions[index] : positions.back() + 1;
  record->subrecords.insert(
      record->subrecords.begin() + static_cast<std::ptrdiff_t>(insertion),
      encoded_element(descriptor, encoded));
  mark_modified(next, *record);
  plugin = std::move(next);
  return FormLinkListMutationStatus::updated;
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

}  // namespace fo4::schema
