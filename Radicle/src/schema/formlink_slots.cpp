#include "fo4/schema/formlink_slots.hpp"

#include "fo4/schema/field_address.hpp"

#include <cstdint>

namespace fo4::schema {
namespace {

std::optional<std::size_t> ordinal_index(const Record& record, Signature subrecord, std::size_t ordinal) {
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != subrecord) continue;
    if (ordinal == 0) return i;
    --ordinal;
  }
  return std::nullopt;
}

}

std::optional<FormLinkFieldDescriptor> formlink_field_at(
    std::span<const FormLinkFieldDescriptor> fields,
    const Record& record,
    Signature subrecord,
    std::size_t ordinal,
    std::size_t offset) {
  for (const auto& field : fields) {
    if (field.subrecord != subrecord) continue;
    if (field.max_form_version != 0 && record.header.form_version >= field.max_form_version) continue;
    if (record.header.form_version < field.min_form_version) continue;
    const auto& address = field.address;
    if (address.when.count == 0) {
      const bool strided = address.stride != 0;
      const bool slot_matches =
          strided ? offset >= address.array_offset &&
                        (offset - address.array_offset) % address.stride == field.offset
                  : field.offset == offset;
      if (slot_matches) return field;
      continue;
    }
    FieldShape shape;
    shape.subrecord = field.subrecord;
    shape.offset = field.offset;
    shape.width = sizeof(std::uint32_t);
    const auto index = ordinal_index(record, subrecord, ordinal);
    if (!index) continue;
    const auto resolved = resolve_field_slots(record, shape, address);
    for (const auto& slot : resolved.slots) {
      if (slot.valid && slot.subrecord == *index && slot.offset == offset) return field;
    }
  }
  return std::nullopt;
}

}
