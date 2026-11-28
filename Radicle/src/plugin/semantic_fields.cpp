#include "fo4/plugin/semantic_fields.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <exception>
#include <system_error>

namespace fo4 {
namespace {

std::string indexed_path(std::string_view path, const schema::FieldAddress& address, std::size_t index) {
  if (!address.repeated && address.stride == 0) return std::string(path);
  return std::string(path) + "[" + std::to_string(index) + "]";
}

}  // namespace

std::optional<SemanticFieldAddress> semantic_field_address(std::string_view path) {
  if (schema::scalar_field_descriptor(path) || schema::formlink_field_descriptor(path)) {
    return SemanticFieldAddress{path, 0};
  }
  if (path.empty() || path.back() != ']') return std::nullopt;
  const auto open = path.rfind('[');
  if (open == std::string_view::npos) return std::nullopt;
  const auto base = path.substr(0, open);
  const auto scalar = schema::scalar_field_descriptor(base);
  const auto link = schema::formlink_field_descriptor(base);
  if (!scalar && !link) return std::nullopt;
  const auto& address = scalar ? scalar->scalar.address : link->address;
  if (!address.repeated && address.stride == 0) return std::nullopt;
  const auto digits = path.substr(open + 1, path.size() - open - 2);
  std::size_t index = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
  if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
  return SemanticFieldAddress{base, index};
}

std::expected<std::vector<SemanticFieldCell>, SemanticFieldError> semantic_field_cells(
    const Plugin& plugin, const Record& record) {
  std::vector<SemanticFieldCell> cells;
  if (record.header.signature != Signature::from_string("EYES")) {
    const auto name = record_full_name(plugin, record);
    const auto path = record.header.signature.to_string() + "\\Full";
    if (name.state == RecordNameState::absent) {
      cells.push_back(SemanticFieldCell{path, {}, true, false});
    } else if (name.state == RecordNameState::opaque) {
      cells.push_back(SemanticFieldCell{path, "<opaque>", false, false});
    } else if (name.state == RecordNameState::present) {
      if (name.value.text.has_value()) {
        cells.push_back(SemanticFieldCell{path, text::windows1252_to_utf8(*name.value.text), true, false});
      } else {
        cells.push_back(SemanticFieldCell{path, "<opaque/localized>", false, false});
      }
    }
  }
  for (const auto& field : schema::scalar_fields(record.header.signature)) {
    try {
      static_cast<void>(record_field_count(plugin, record.form_key, field));
    } catch (const schema::ScalarError& error) {
      return std::unexpected(SemanticFieldError{"ScalarReadFailed", error.what()});
    }
    const auto& scalar = field.scalar;
    if (scalar.payload_size != 0 &&
        std::any_of(record.subrecords.begin(), record.subrecords.end(), [&](const auto& subrecord) {
          return subrecord.signature == scalar.subrecord && subrecord.data.size() != scalar.payload_size;
        })) {
      cells.push_back(SemanticFieldCell{std::string(field.path), "<opaque>", false, false});
      continue;
    }
    const auto resolved =
        schema::resolve_field_slots(record, schema::scalar_field_shape(scalar), scalar.address);
    if (resolved.misaligned) {
      return std::unexpected(SemanticFieldError{
          "ScalarReadFailed",
          record.form_key.to_string() + ": invalid " + std::string(field.path) + " storage"});
    }
    const auto count = scalar.address.repeated || scalar.address.stride != 0
                           ? resolved.slots.size()
                           : std::max(std::size_t{1}, resolved.slots.size());
    if (scalar.address.repeated || scalar.address.stride != 0) {
      cells.push_back(SemanticFieldCell{std::string(field.path) + "\\Count", std::to_string(count), true, false, false});
    }
    for (std::size_t index = 0; index < count; ++index) {
      SemanticFieldCell cell{indexed_path(field.path, scalar.address, index), {}, false, false};
      const bool localized =
          scalar.storage == schema::ScalarStorage::zstring && plugin.header.tes4_flags.is_localized() &&
          (schema::is_localized_field(field.record, scalar.subrecord) ||
           (scalar.localized && index < resolved.slots.size() && resolved.slots[index].valid));
      if (localized) {
        cell.value = "<opaque/localized>";
      } else if (index < resolved.slots.size() && resolved.slots[index].valid) {
        const auto& slot = resolved.slots[index];
        try {
          cell.value = schema::decode_scalar(
              schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, scalar), scalar);
          if (scalar.storage == schema::ScalarStorage::zstring ||
              scalar.storage == schema::ScalarStorage::fixed_string) {
            cell.value = text::windows1252_to_utf8(cell.value);
          }
          cell.available = true;
        } catch (const schema::ScalarError&) {
          cell.value = "<opaque>";
        }
      }
      cells.push_back(std::move(cell));
    }
  }
  for (const auto& field : schema::formlink_fields(record.header.signature)) {
    try {
      static_cast<void>(record_field_count(plugin, record.form_key, field));
    } catch (const schema::FormLinkFieldError&) {
      cells.push_back(SemanticFieldCell{std::string(field.path), "<opaque>", false, true});
      continue;
    }
    const schema::FieldShape shape{.subrecord = field.subrecord,
                                   .offset = field.offset,
                                   .width = sizeof(std::uint32_t),
                                   .max_form_version = field.max_form_version};
    const auto resolved = schema::resolve_field_slots(record, shape, field.address);
    if (resolved.misaligned) {
      return std::unexpected(SemanticFieldError{
          "FormLinkReadFailed",
          record.form_key.to_string() + ": invalid " + std::string(field.path) + " storage"});
    }
    const auto short_entry = [&](const auto& slot) {
      return slot.misfit && field.payload_sizes[1] != 0 &&
             record.subrecords[slot.subrecord].data.size() == field.payload_sizes[1];
    };
    if (std::any_of(resolved.slots.begin(), resolved.slots.end(),
                    [&](const auto& slot) { return slot.misfit && !short_entry(slot); })) {
      return std::unexpected(SemanticFieldError{
          "FormLinkReadFailed",
          record.form_key.to_string() + ": " + std::string(field.path) + " does not fit in the subrecord payload"});
    }
    const auto count = field.address.repeated || field.address.stride != 0
                           ? resolved.slots.size()
                           : std::max(std::size_t{1}, resolved.slots.size());
    if (field.address.repeated || field.address.stride != 0) {
      cells.push_back(SemanticFieldCell{std::string(field.path) + "\\Count", std::to_string(count), true, false, false});
    }
    for (std::size_t index = 0; index < count; ++index) {
      SemanticFieldCell cell{indexed_path(field.path, field.address, index), {}, false, true};
      if (index < resolved.slots.size() && (resolved.slots[index].valid || short_entry(resolved.slots[index]))) {
        const auto& slot = resolved.slots[index];
        std::uint32_t raw = 0;
        if (slot.valid) {
          std::memcpy(&raw, record.subrecords[slot.subrecord].data.data() + slot.offset, sizeof(raw));
        }
        try {
          cell.value =
              resolve_plugin_form_id(RawFormId{raw}, plugin_form_id_context(plugin)).to_string();
          cell.available = true;
        } catch (const FormIdError&) {
          cell.value = "<opaque>";
        }
      }
      cells.push_back(std::move(cell));
    }
  }
  for (const auto& descriptor : schema::k_generated_formlink_lists) {
    if (descriptor.record != record.header.signature) continue;
    try {
      const auto values = schema::record_formlink_list_values(plugin, record.form_key, descriptor);
      if (!values) continue;
      cells.push_back(SemanticFieldCell{std::string(descriptor.path) + "\\Count", std::to_string(values->size()), true, false, false});
      for (std::size_t index = 0; index < values->size(); ++index) {
        cells.push_back(SemanticFieldCell{std::string(descriptor.path) + "[" + std::to_string(index) + "]",
                                          (*values)[index].to_string(), true, true});
      }
    } catch (const schema::FormLinkListError& error) {
      if (descriptor.segment_count == 0) {
        return std::unexpected(SemanticFieldError{"FormLinkReadFailed", error.what()});
      }
      cells.push_back(SemanticFieldCell{std::string(descriptor.path), "<opaque>", false, true});
    } catch (const std::exception& error) {
      return std::unexpected(SemanticFieldError{"FormLinkReadFailed", error.what()});
    }
  }
  if (const auto constraint = schema::record_authoring_constraint(record.header.signature)) {
    for (const auto& rule : schema::authoring_subrecords(*constraint)) {
      const auto count = static_cast<std::size_t>(std::count_if(
          record.subrecords.begin(), record.subrecords.end(),
          [&](const auto& subrecord) { return subrecord.signature == rule.signature; }));
      const auto path = "Authoring\\" + rule.signature.to_string();
      cells.push_back(SemanticFieldCell{path, std::to_string(count), true, false, false});
      for (std::size_t index = 0; index < count; ++index) {
        cells.push_back(SemanticFieldCell{path + "[" + std::to_string(index) + "]", "present", true, false, false});
      }
    }
  }
  return cells;
}

}  // namespace fo4
