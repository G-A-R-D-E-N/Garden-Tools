#include "fo4/plugin/mutation.hpp"

#include "record_edit.hpp"

#include "fo4/environment/sound.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {
namespace {

using detail::find_mutable_record;
using detail::mark_record_modified;

void require_scalar_record(const Record& record, const schema::ScalarFieldDescriptor& field) {
  if (record.header.signature != field.record) {
    throw schema::ScalarError(std::string(field.path) + " does not apply to " +
                              record.header.signature.to_string() + " records");
  }
}

std::vector<SubrecordGroupSpan> group_spans(const Record& record, const schema::SubrecordGroupDescriptor& group);

bool response_shape_valid(const Record& record) {
  constexpr Signature trda = Signature::from_string("TRDA");
  constexpr Signature members[] = {Signature::from_string("NAM1"), Signature::from_string("NAM2"),
                                    Signature::from_string("NAM3"), Signature::from_string("NAM4")};
  std::size_t responses = 0;
  std::size_t counts[4] = {};
  const auto& subs = record.subrecords;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    for (std::size_t m = 0; m < 4; ++m) {
      if (subs[i].signature == members[m]) { ++counts[m]; }
    }
    if (subs[i].signature != trda) { continue; }
    ++responses;
    if (subs[i].data.size() != 20 || subs.size() - i <= 4) { return false; }
    for (std::size_t m = 0; m < 4; ++m) {
      if (subs[i + 1 + m].signature != members[m]) { return false; }
    }
  }
  return std::all_of(counts, counts + 4,
                     [&](std::size_t count) { return count == responses; });
}

schema::FieldSlots scalar_slots(const Record& record, const schema::ScalarFieldDescriptor& field) {
  if (field.record == Signature::from_string("INFO") && field.scalar.address.repeated &&
      !response_shape_valid(record)) {
    throw schema::ScalarError("INFO response fields require complete TRDA,NAM1..NAM4 groups");
  }
  if (const auto group = schema::subrecord_group_for_member(record.header.signature, field.scalar.subrecord)) {
    try {
      static_cast<void>(group_spans(record, *group));
    } catch (const schema::SubrecordGroupError& error) {
      throw schema::ScalarError(error.what());
    }
  }
  try {
    if (record.header.signature == Signature::from_string("SNDR") &&
        (field.scalar.subrecord == Signature::from_string("INTV") ||
         field.scalar.subrecord == Signature::from_string("FNAM"))) {
      static_cast<void>(env::decode_sound_rate_files(record));
    } else if (record.header.signature == Signature::from_string("SOUN") &&
               field.scalar.subrecord == Signature::from_string("REPT")) {
      static_cast<void>(env::decode_sound_repeat(record));
    }
  } catch (const env::WorldDecodeError& error) {
    throw schema::ScalarError(error.what());
  }
  auto resolved = schema::resolve_field_slots(record, schema::scalar_field_shape(field.scalar),
                                              field.scalar.address);
  if (resolved.duplicate) {
    throw schema::ScalarError("record contains duplicate scalar subrecords: " +
                              field.scalar.subrecord.to_string());
  }
  if (resolved.malformed) {
    throw schema::ScalarError(std::string(field.scalar.field_name) + " has unexpected binary width");
  }
  return resolved;
}

schema::FieldSlots formlink_slots(const Record& record, const schema::FormLinkFieldDescriptor& field) {
  if (record.header.signature == Signature::from_string("FSTS") &&
      field.subrecord == Signature::from_string("DATA")) {
    try {
      static_cast<void>(env::decode_footstep_counts(record));
    } catch (const env::WorldDecodeError& error) {
      throw schema::FormLinkFieldError(error.what());
    }
  }
  if (field.record == Signature::from_string("INFO") && field.address.repeated &&
      !response_shape_valid(record)) {
    throw schema::FormLinkFieldError("INFO response fields require complete TRDA,NAM1..NAM4 groups");
  }
  if (const auto group = schema::subrecord_group_for_member(record.header.signature, field.subrecord)) {
    try {
      static_cast<void>(group_spans(record, *group));
    } catch (const schema::SubrecordGroupError& error) {
      throw schema::FormLinkFieldError(error.what());
    }
  }
  schema::FieldShape shape;
  shape.subrecord = field.subrecord;
  shape.offset = field.offset;
  shape.width = sizeof(std::uint32_t);
  shape.max_form_version = field.max_form_version;
  auto resolved = schema::resolve_field_slots(record, shape, field.address);
  if (resolved.duplicate) {
    throw schema::FormLinkFieldError("record contains duplicate FormLink subrecords: " +
                                     field.subrecord.to_string());
  }
  if (field.payload_sizes[0] != 0 &&
      (field.address.when.count == 0 || std::any_of(resolved.slots.begin(), resolved.slots.end(),
                                                   [](const auto& slot) { return slot.valid || slot.misfit; }))) {
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == field.subrecord &&
          subrecord.data.size() != field.payload_sizes[0] &&
          (field.payload_sizes[1] == 0 || subrecord.data.size() != field.payload_sizes[1])) {
        throw schema::FormLinkFieldError(std::string(field.path) + " has unexpected payload size");
      }
    }
  }
  return resolved;
}

ScalarMutationStatus write_scalar(Plugin& plugin,
                                  const FormKey& record_key,
                                  const schema::ScalarFieldDescriptor& field,
                                  std::size_t index,
                                  std::string_view value,
                                  bool string_setter) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return ScalarMutationStatus::record_missing;
  require_scalar_record(*target, field);
  if (schema::is_counted_array_count_subrecord(field.record, field.scalar.subrecord)) {
    throw schema::ScalarError(std::string(field.path) + " is maintained by counted array insert and remove");
  }
  if (schema::is_subrecord_group_count_subrecord(field.record, field.scalar.subrecord)) {
    throw schema::ScalarError(std::string(field.path) + " is maintained by subrecord group insert and remove");
  }
  if (field.record == Signature::from_string("FSTS") &&
      field.scalar.subrecord == Signature::from_string("XCNT")) {
    throw schema::ScalarError(std::string(field.path) +
                              " is derived from the five footstep category lists in DATA");
  }

  const bool is_string = field.scalar.storage == schema::ScalarStorage::zstring;
  if (is_string != string_setter) {
    throw schema::ScalarError(std::string(field.path) +
                              (is_string ? " is a string field; use the string setter"
                                         : " is not a string field"));
  }

  const auto resolved = scalar_slots(*target, field);
  if (index >= resolved.slots.size() || !resolved.slots[index].valid) {
    return ScalarMutationStatus::subrecord_missing;
  }
  const auto slot = resolved.slots[index];
  auto& data = target->subrecords[slot.subrecord].data;

  if (is_string && plugin.header.tes4_flags.is_localized() &&
      (field.scalar.localized || schema::is_localized_field(field.record, field.scalar.subrecord))) {
    throw schema::ScalarError(std::string(field.path) +
                              " is a localized string in a localized plugin");
  }

  const auto existing = schema::scalar_slot_bytes(data, slot, field.scalar);
  static_cast<void>(schema::decode_scalar(existing, field.scalar));

  const auto encoded = schema::encode_scalar(value, field.scalar);
  if (std::equal(existing.begin(), existing.end(), encoded.begin(), encoded.end())) {
    return ScalarMutationStatus::unchanged;
  }

  if (is_string) {
    data = encoded;
  } else {
    std::copy(encoded.begin(), encoded.end(),
              data.begin() + static_cast<std::ptrdiff_t>(slot.offset));
  }
  mark_record_modified(plugin, *target);
  return ScalarMutationStatus::updated;
}

void require_counted_array_record(const Record& record, const schema::CountedArrayDescriptor& array) {
  if (record.header.signature != array.record) {
    throw schema::CountedArrayError(std::string(array.path) + " does not apply to " +
                                    record.header.signature.to_string() + " records");
  }
}

std::uint32_t read_counter(const std::vector<std::byte>& data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[i])) << (8 * i);
  }
  return value;
}

void write_counter(std::vector<std::byte>& data, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    data[i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
}

struct CountedArraySubrecords {
  std::size_t payload;
  std::optional<std::size_t> counter;
  std::size_t count;
};

std::optional<CountedArraySubrecords> counted_array_subrecords(const Record& record,
                                                               const schema::CountedArrayDescriptor& array) {
  std::optional<std::size_t> payload;
  std::optional<std::size_t> counter;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    const auto signature = record.subrecords[i].signature;
    if (signature == array.subrecord) {
      if (payload) {
        throw schema::CountedArrayError(std::string(array.path) + " has duplicate " +
                                        array.subrecord.to_string() + " subrecords");
      }
      payload = i;
    } else if (signature == array.count_subrecord) {
      if (counter) {
        throw schema::CountedArrayError(std::string(array.path) + " has duplicate " +
                                        array.count_subrecord.to_string() + " subrecords");
      }
      counter = i;
    }
  }
  if (!payload) return std::nullopt;

  const auto& data = record.subrecords[*payload].data;
  if (array.count_subrecord == Signature{}) {
    if (data.size() % array.stride != 0) {
      throw schema::CountedArrayError(std::string(array.path) + " " + array.subrecord.to_string() +
                                      " payload is not a whole number of elements");
    }
    return CountedArraySubrecords{*payload, std::nullopt, data.size() / array.stride};
  }
  if (!counter) return std::nullopt;

  const auto& count_data = record.subrecords[*counter].data;
  if (count_data.size() != 4) {
    throw schema::CountedArrayError(std::string(array.path) + " count subrecord must be four bytes");
  }
  const auto count = static_cast<std::size_t>(read_counter(count_data));
  if (data.size() % array.stride != 0 || data.size() / array.stride != count) {
    throw schema::CountedArrayError(std::string(array.path) + " count does not match its " +
                                    array.subrecord.to_string() + " payload");
  }
  return CountedArraySubrecords{*payload, *counter, count};
}

void require_subrecord_group_record(const Record& record, const schema::SubrecordGroupDescriptor& group) {
  if (record.header.signature != group.record) {
    throw schema::SubrecordGroupError(std::string(group.path) + " does not apply to " +
                                      record.header.signature.to_string() + " records");
  }
}

std::vector<SubrecordGroupSpan> group_spans(const Record& record, const schema::SubrecordGroupDescriptor& group) {
  const auto member_index = [&](Signature signature) -> std::optional<std::size_t> {
    for (std::size_t m = 0; m < group.members.size(); ++m) {
      if (group.members[m].subrecord == signature) return m;
    }
    return std::nullopt;
  };
  const auto refuse = [&](const std::string& reason) {
    return schema::SubrecordGroupError(std::string(group.path) + " " + reason);
  };

  std::vector<SubrecordGroupSpan> spans;
  const auto& subs = record.subrecords;
  std::size_t first = 0;
  while (first < subs.size()) {
    const auto leader = member_index(subs[first].signature);
    if (!leader) {
      ++first;
      continue;
    }
    if (*leader != 0) {
      throw refuse("has " + subs[first].signature.to_string() + " outside a leader run");
    }
    std::vector<std::size_t> counts(group.members.size());
    counts[0] = 1;
    std::size_t last = 0;
    std::size_t end = first + 1;
    for (; end < subs.size(); ++end) {
      const auto member = member_index(subs[end].signature);
      if (!member || *member == 0) break;
      if (*member < last || (*member == last && !group.members[*member].repeatable)) {
        throw refuse("has " + subs[end].signature.to_string() + " out of order or repeated");
      }
      last = *member;
      ++counts[*member];
    }
    for (std::size_t m = 0; m < group.members.size(); ++m) {
      if (group.members[m].required && counts[m] == 0) {
        throw refuse("has a run without required " + group.members[m].subrecord.to_string());
      }
    }
    for (std::size_t k = first; k < end; ++k) {
      const auto member = *member_index(subs[k].signature);
      if (subs[k].data.size() != group.members[member].payload_size) {
        throw refuse(subs[k].signature.to_string() + " size differs from its member size");
      }
    }
    spans.push_back(SubrecordGroupSpan{first, end});
    first = end;
  }
  return spans;
}

std::size_t first_successor_index(const Record& record, std::span<const Signature> successors) {
  const auto& subs = record.subrecords;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    if (std::find(successors.begin(), successors.end(), subs[i].signature) != successors.end()) return i;
  }
  return subs.size();
}

std::optional<std::size_t> group_counter(const Record& record,
                                         const schema::SubrecordGroupDescriptor& group,
                                         std::size_t runs) {
  std::optional<std::size_t> counter;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != group.count_subrecord) continue;
    if (counter) {
      throw schema::SubrecordGroupError(std::string(group.path) + " has duplicate " +
                                        group.count_subrecord.to_string() + " subrecords");
    }
    counter = i;
  }
  if (!counter) return std::nullopt;

  const auto& data = record.subrecords[*counter].data;
  if (data.size() != sizeof(std::uint32_t)) {
    throw schema::SubrecordGroupError(std::string(group.path) + " count subrecord has the wrong size");
  }
  if (static_cast<std::size_t>(read_counter(data)) != runs) {
    throw schema::SubrecordGroupError(std::string(group.path) + " count does not match its runs");
  }
  return counter;
}

}

ScalarMutationStatus set_record_scalar(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value) {
  return write_scalar(plugin, record_key, field, index, value, false);
}

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value) {
  return write_scalar(plugin, record_key, field, index, value, true);
}

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index,
                                       std::string_view value) {
  const auto field = schema::scalar_field_descriptor(path);
  if (!field) throw schema::ScalarError("unknown string field path: " + std::string(path));
  return write_scalar(plugin, record_key, *field, index, value, true);
}

FormLinkFieldMutationStatus set_record_formlink_field(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::FormLinkFieldDescriptor& field,
    std::size_t index,
    const FormKey& target) {
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, record_key);
  if (record == nullptr) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != field.record) {
    return FormLinkFieldMutationStatus::record_mismatch;
  }

  if (target.owner().empty() && target.local().value() != 0) {
    throw schema::FormLinkFieldError("FormLink target has no owner");
  }
  if (target.owner().empty() && target.local().value() == 0 && !field.nullable) {
    throw schema::FormLinkFieldError(std::string(field.path) + " does not allow NULL");
  }
  if (!target.owner().empty() && target.owner() == plugin.key) {
    const auto* local_target = find_record(plugin, target);
    if (local_target == nullptr ||
        !schema::formlink_field_target_allowed(field, local_target->header.signature)) {
      throw schema::FormLinkFieldError(std::string(field.path) + " target type is not allowed");
    }
  }

  const auto resolved = formlink_slots(*record, field);
  if (index >= resolved.slots.size()) return FormLinkFieldMutationStatus::subrecord_missing;
  const auto slot = resolved.slots[index];
  if (slot.misfit) {
    throw schema::FormLinkFieldError(std::string(field.path) +
                                     " does not fit in the subrecord payload");
  }
  if (!slot.valid) return FormLinkFieldMutationStatus::subrecord_missing;
  auto& data = record->subrecords[slot.subrecord].data;

  std::uint32_t current_raw = 0;
  std::memcpy(&current_raw, data.data() + slot.offset, sizeof(current_raw));
  const auto encoded_value = encode_plugin_form_id(target, plugin_form_id_context(plugin)).value();
  if (current_raw == encoded_value) return FormLinkFieldMutationStatus::unchanged;

  std::memcpy(data.data() + slot.offset, &encoded_value, sizeof(encoded_value));
  mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
}

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::ScalarFieldDescriptor& field) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  require_scalar_record(*record, field);
  return scalar_slots(*record, field).slots.size();
}

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::FormLinkFieldDescriptor& field) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  if (record->header.signature != field.record) {
    throw schema::FormLinkFieldError(std::string(field.path) + " does not apply to " +
                                     record->header.signature.to_string() + " records");
  }
  return formlink_slots(*record, field).slots.size();
}

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              std::string_view path) {
  if (const auto scalar = schema::scalar_field_descriptor(path)) {
    return record_field_count(plugin, record_key, *scalar);
  }
  if (const auto link = schema::formlink_field_descriptor(path)) {
    return record_field_count(plugin, record_key, *link);
  }
  if (const auto list = schema::packed_formlink_list_path(path)) {
    return schema::record_formlink_list_size(plugin, record_key, *list);
  }
  throw schema::ScalarError("unknown field path: " + std::string(path));
}

CountedArrayMutationStatus insert_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return CountedArrayMutationStatus::record_missing;
  require_counted_array_record(*target, array);
  const auto found = counted_array_subrecords(*target, array);
  if (!found) return CountedArrayMutationStatus::subrecord_missing;
  if (index > found->count) return CountedArrayMutationStatus::index_out_of_range;

  auto& data = target->subrecords[found->payload].data;
  data.insert(data.begin() + static_cast<std::ptrdiff_t>(index * array.stride), array.stride, std::byte{0});
  if (found->counter) {
    write_counter(target->subrecords[*found->counter].data, static_cast<std::uint32_t>(found->count + 1));
  }
  mark_record_modified(plugin, *target);
  return CountedArrayMutationStatus::updated;
}

CountedArrayMutationStatus remove_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return CountedArrayMutationStatus::record_missing;
  require_counted_array_record(*target, array);
  const auto found = counted_array_subrecords(*target, array);
  if (!found) return CountedArrayMutationStatus::subrecord_missing;
  if (index >= found->count) return CountedArrayMutationStatus::index_out_of_range;
  if (!found->counter && found->count == 1) return CountedArrayMutationStatus::would_empty;

  auto& data = target->subrecords[found->payload].data;
  const auto first = data.begin() + static_cast<std::ptrdiff_t>(index * array.stride);
  data.erase(first, first + static_cast<std::ptrdiff_t>(array.stride));
  if (found->counter) {
    write_counter(target->subrecords[*found->counter].data, static_cast<std::uint32_t>(found->count - 1));
  }
  mark_record_modified(plugin, *target);
  return CountedArrayMutationStatus::updated;
}

std::optional<std::vector<SubrecordGroupSpan>> subrecord_group_spans(
    const Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group) {
  const auto* record = find_record(plugin, record_key);
  if (record == nullptr) return std::nullopt;
  require_subrecord_group_record(*record, group);
  return group_spans(*record, group);
}

SubrecordGroupMutationStatus insert_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  const auto spans = group_spans(*target, group);
  const auto counter = group_counter(*target, group, spans.size());
  if (!counter && !spans.empty()) return SubrecordGroupMutationStatus::subrecord_missing;
  if (index > spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;

  const std::size_t position = index < spans.size()  ? spans[index].first
                               : !counter            ? first_successor_index(*target, group.successors)
                               : spans.empty()       ? *counter + 1
                                                     : spans.back().end;
  std::vector<Subrecord> created;
  if (!counter) {
    Subrecord count;
    count.signature = group.count_subrecord;
    count.data.assign(sizeof(std::uint32_t), std::byte{0});
    created.push_back(std::move(count));
  }
  for (const auto& member : group.members) {
    if (!member.required) continue;
    Subrecord subrecord;
    subrecord.signature = member.subrecord;
    subrecord.data.assign(member.payload_size, std::byte{0});
    for (std::size_t k = 0; k < member.default_payload.size(); ++k) {
      subrecord.data[k] = std::byte{member.default_payload[k]};
    }
    created.push_back(std::move(subrecord));
  }
  target->subrecords.insert(target->subrecords.begin() + static_cast<std::ptrdiff_t>(position),
                            std::make_move_iterator(created.begin()), std::make_move_iterator(created.end()));
  const std::size_t count_index = !counter                ? position
                                  : *counter < position  ? *counter
                                                         : *counter + created.size();
  write_counter(target->subrecords[count_index].data, static_cast<std::uint32_t>(spans.size() + 1));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

SubrecordGroupMutationStatus remove_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return SubrecordGroupMutationStatus::record_missing;
  require_subrecord_group_record(*target, group);
  const auto spans = group_spans(*target, group);
  const auto counter = group_counter(*target, group, spans.size());
  if (!counter) return SubrecordGroupMutationStatus::subrecord_missing;
  if (index >= spans.size()) return SubrecordGroupMutationStatus::index_out_of_range;

  const auto span = spans[index];
  write_counter(target->subrecords[*counter].data, static_cast<std::uint32_t>(spans.size() - 1));
  const auto first = target->subrecords.begin() + static_cast<std::ptrdiff_t>(span.first);
  target->subrecords.erase(first, first + static_cast<std::ptrdiff_t>(span.end - span.first));
  mark_record_modified(plugin, *target);
  return SubrecordGroupMutationStatus::updated;
}

}
