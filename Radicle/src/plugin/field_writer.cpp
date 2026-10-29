#include "fo4/plugin/mutation.hpp"

#include "field_writer_internal.hpp"
#include "record_edit.hpp"

#include "fo4/environment/sound.hpp"
#include "fo4/forms/quest.hpp"
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
using detail::group_spans;
using detail::mark_record_modified;

void require_scalar_record(const Record& record, const schema::ScalarFieldDescriptor& field) {
  if (record.header.signature != field.record) {
    throw schema::ScalarError(std::string(field.path) + " does not apply to " +
                              record.header.signature.to_string() + " records");
  }
}

const schema::SubrecordGroupMemberDescriptor* group_member(const schema::SubrecordGroupDescriptor& group,
                                                           Signature subrecord) {
  for (const auto& member : group.members) {
    if (member.subrecord == subrecord) return &member;
  }
  return nullptr;
}

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
  shape.min_form_version = field.min_form_version;
  shape.max_form_version = field.max_form_version;
  auto resolved = schema::resolve_field_slots(record, shape, field.address);
  if (resolved.duplicate) {
    throw schema::FormLinkFieldError("record contains duplicate FormLink subrecords: " +
                                     field.subrecord.to_string());
  }
  const bool version_admits =
      record.header.form_version >= field.min_form_version &&
      (field.max_form_version == 0 || record.header.form_version < field.max_form_version);
  if (field.payload_sizes[0] != 0 && version_admits &&
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

std::optional<std::uint32_t> highest_alias_id(const Plugin& plugin, const FormKey& record_key) {
  std::optional<std::uint32_t> highest;
  for (const auto& alias : forms::decode_quest_aliases(plugin, record_key).aliases) {
    if (alias.kind == forms::QuestAliasKind::collection) continue;
    highest = highest ? std::max(*highest, alias.id) : alias.id;
  }
  return highest;
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
  if (schema::is_derived_scalar_row(field.record, field.scalar.subrecord)) {
    if (field.record == Signature::from_string("INNR")) {
      throw schema::ScalarError(std::string(field.path) +
                                " is maintained by the INNR naming-rule and keyword lists");
    }
    throw schema::ScalarError(std::string(field.path) +
                              " is derived from the five footstep category lists in DATA");
  }

  const bool is_string = field.scalar.storage == schema::ScalarStorage::zstring ||
                         field.scalar.storage == schema::ScalarStorage::fixed_string;
  if (is_string != string_setter) {
    throw schema::ScalarError(std::string(field.path) +
                              (is_string ? " is a string field; use the string setter"
                                         : " is not a string field"));
  }
  const auto member_group = schema::subrecord_group_for_member(field.record, field.scalar.subrecord);
  const bool member_string = is_string && field.scalar.embedded && member_group.has_value();
  if (is_string && field.scalar.embedded && !member_string) {
    throw schema::ScalarError(std::string(field.path) +
                              " sits in a fixed-size slot and cannot be written generically");
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

  const auto encoded = schema::encode_scalar(value, field.scalar);
  if (field.path == "QUST\\NextAliasID") {
    // Invariant: QUST Next Alias ID must stay above every existing alias ID.
    std::uint32_t next_alias_id = 0;
    std::memcpy(&next_alias_id, encoded.data(), sizeof(next_alias_id));
    const auto highest = highest_alias_id(plugin, record_key);
    if (highest && next_alias_id <= *highest) return ScalarMutationStatus::next_alias_id_too_low;
  }
  std::size_t slot_size = 0;
  {
    const auto existing = schema::scalar_slot_bytes(data, slot, field.scalar);
    static_cast<void>(schema::decode_scalar(existing, field.scalar));
    if (std::equal(existing.begin(), existing.end(), encoded.begin(), encoded.end())) {
      return ScalarMutationStatus::unchanged;
    }
    slot_size = existing.size();
  }

  if (is_string && member_string) {
    // Constraint: a variable-length member string row that owns the whole payload must resize it to the encoded value; every other member string row requires the encoded value to fit the slot the subrecord already carries.
    const auto* member = group_member(*member_group, field.scalar.subrecord);
    if (member != nullptr && member->payload_size == 0 && slot.offset == 0 &&
        slot_size == data.size()) {
      data = encoded;
    } else {
      if (encoded.size() > slot_size) {
        throw schema::ScalarError(std::string(field.path) + " does not fit in its " +
                                  std::to_string(slot_size) + "-byte slot");
      }
      std::copy(encoded.begin(), encoded.end(),
                data.begin() + static_cast<std::ptrdiff_t>(slot.offset));
      if (encoded.size() < slot_size) {
        std::fill(data.begin() + static_cast<std::ptrdiff_t>(slot.offset + encoded.size()),
                  data.begin() + static_cast<std::ptrdiff_t>(slot.offset + slot_size), std::byte{0});
      }
    }
  } else if (is_string) {
    data = encoded;
  } else {
    std::copy(encoded.begin(), encoded.end(),
              data.begin() + static_cast<std::ptrdiff_t>(slot.offset));
  }
  mark_record_modified(plugin, *target);
  return ScalarMutationStatus::updated;
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
  if (const auto list = schema::formlink_list_descriptor(
          Signature::from_string(path.substr(0, 4)), path)) {
    return schema::record_formlink_list_size(plugin, record_key, *list);
  }
  throw schema::ScalarError("unknown field path: " + std::string(path));
}

}
