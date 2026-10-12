#include "fo4/schema/formlink_lists.hpp"
#include "fo4/plugin/reader.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace fo4::schema {

std::optional<std::vector<FormKey>> record_formlink_list_values(
    const Plugin& plugin,
    const FormKey& record_key,
    const FormLinkListDescriptor& descriptor) {
  const auto count = record_formlink_list_size(plugin, record_key, descriptor);
  if (!count) return std::nullopt;
  const auto* record = find_record(plugin, record_key);
  FieldShape shape;
  shape.subrecord = descriptor.subrecord;
  shape.width = sizeof(std::uint32_t);
  shape.whole_payload = descriptor.storage == FormLinkListStorage::per_subrecord;
  FieldAddress address;
  address.repeated = shape.whole_payload;
  address.stride = shape.whole_payload ? 0 : sizeof(std::uint32_t);
  const auto resolved = resolve_field_slots(*record, shape, address);
  if (resolved.duplicate || resolved.malformed || resolved.misaligned || resolved.slots.size() != *count) {
    throw FormLinkListError(std::string(descriptor.path) + " has invalid list storage");
  }
  std::vector<FormKey> values;
  values.reserve(*count);
  const auto context = plugin_form_id_context(plugin);
  for (const auto& slot : resolved.slots) {
    if (!slot.valid) throw FormLinkListError(std::string(descriptor.path) + " has invalid list element storage");
    std::uint32_t raw = 0;
    std::memcpy(&raw, record->subrecords[slot.subrecord].data.data() + slot.offset, sizeof(raw));
    values.push_back(resolve_plugin_form_id(RawFormId{raw}, context));
  }
  return values;
}

}
