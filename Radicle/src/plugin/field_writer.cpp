#include "fo4/plugin/mutation.hpp"

#include "record_edit.hpp"

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
#include <optional>
#include <string>
#include <string_view>

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

schema::FieldSlots scalar_slots(const Record& record, const schema::ScalarFieldDescriptor& field) {
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
  schema::FieldShape shape;
  shape.subrecord = field.subrecord;
  shape.offset = field.offset;
  shape.width = sizeof(std::uint32_t);
  auto resolved = schema::resolve_field_slots(record, shape, field.address);
  if (resolved.duplicate) {
    throw schema::FormLinkFieldError("record contains duplicate FormLink subrecords: " +
                                     field.subrecord.to_string());
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
      schema::is_localized_field(field.record, field.scalar.subrecord)) {
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
  const auto context = plugin_form_id_context(plugin);
  const auto current = resolve_plugin_form_id(RawFormId{current_raw}, context);
  const auto encoded = encode_plugin_form_id(target, context);
  if (current == target) return FormLinkFieldMutationStatus::unchanged;

  const auto encoded_value = encoded.value();
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
  throw schema::ScalarError("unknown field path: " + std::string(path));
}

}
