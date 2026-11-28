#include "fo4/schema/form_link_schema.hpp"

#include "fo4/schema/field_address.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "non_formid_slots.hpp"

#include <cstdint>

namespace fo4::schema {
namespace {

bool payload_size_allowed(const FormLinkFieldDescriptor& field, std::size_t size) {
  if (field.payload_sizes[0] == 0) return true;
  return size == field.payload_sizes[0] ||
         (field.payload_sizes[1] != 0 && size == field.payload_sizes[1]);
}

bool struct_or_mixed_handler_owns(const Record& record, const Subrecord& subrecord) {
  return struct_payload_formid_offsets(record, subrecord, false).has_value() ||
         mixed_formid_offsets(record, subrecord, false).has_value();
}

}  // namespace

bool qust_non_formid_signature(Signature signature) noexcept {
  return signature == sig_dnam || signature == sig_nam2 || signature == sig_cnam ||
         signature == sig_fnam || signature == sig_anam || signature == sig_nnam ||
         signature == sig_snam;
}

bool deliberate_non_formid_subrecord(const Record& record, const Subrecord& subrecord) noexcept {
  if (subrecord.signature == sig_xxxx) return true;
  if (record.header.signature == sig_race && subrecord.signature == sig_sraf) return true;
  if (record.header.signature == sig_qust && qust_non_formid_signature(subrecord.signature)) {
    return true;
  }
  return corpus_non_formid_slot(record, subrecord);
}

SubrecordFormLinkOffsets schema_formlink_offsets(const Record& record) {
  const auto fields = formlink_fields(record.header.signature);
  if (fields.empty() || record.header.signature == sig_info) return {};
  SubrecordFormLinkOffsets offsets(record.subrecords.size());
  for (const auto& field : fields) {
    const FieldShape shape{.subrecord = field.subrecord,
                           .offset = field.offset,
                           .width = sizeof(std::uint32_t),
                           .payload_size = 0,
                           .whole_payload = false,
                           .minimum_payload_size = 0,
                           .min_form_version = field.min_form_version,
                           .max_form_version = field.max_form_version};
    const auto resolved = resolve_field_slots(record, shape, field.address);
    for (const auto& slot : resolved.slots) {
      if (!slot.valid) continue;
      const auto& subrecord = record.subrecords[slot.subrecord];
      if (!payload_size_allowed(field, subrecord.data.size()) ||
          deliberate_non_formid_subrecord(record, subrecord) ||
          struct_or_mixed_handler_owns(record, subrecord)) {
        continue;
      }
      offsets[slot.subrecord].push_back(slot.offset);
    }
  }
  return offsets;
}

}  // namespace fo4::schema
