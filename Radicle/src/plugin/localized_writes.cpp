#include "fo4/plugin/mutation.hpp"

#include "record_edit.hpp"

#include "fo4/localization/strings.hpp"
#include "fo4/schema/localized_fields.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fo4 {
namespace {

using detail::find_mutable_record;
using detail::mark_record_modified;

std::vector<std::byte>* localized_payload(Record& record,
                                         Signature signature,
                                         std::size_t index) {
  std::size_t ordinal = 0;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (ordinal == index) return &subrecord.data;
    ++ordinal;
  }
  return nullptr;
}

std::array<std::byte, 4> encoded_string_id(std::uint32_t id) {
  std::array<std::byte, 4> out{};
  std::memcpy(out.data(), &id, out.size());
  return out;
}

}  // namespace

LocalizedValueStatus add_record_localized_string(Plugin& plugin,
                                                 const FormKey& record_key,
                                                 Signature subrecord_signature,
                                                 std::size_t index,
                                                 std::string_view text,
                                                 localization::LocalizedStringTable& table) {
  if (!plugin.header.tes4_flags.is_localized()) {
    return LocalizedValueStatus::plugin_not_localized;
  }
  assign_record_form_keys(plugin);
  auto* record = find_mutable_record(plugin, record_key);
  if (record == nullptr) return LocalizedValueStatus::record_missing;
  if (!schema::is_localized_field(record->header.signature, subrecord_signature)) {
    return LocalizedValueStatus::field_not_localized;
  }
  auto* payload = localized_payload(*record, subrecord_signature, index);
  if (payload == nullptr) return LocalizedValueStatus::subrecord_missing;
  if (payload->size() != 4) return LocalizedValueStatus::payload_not_an_id;

  const auto id = localization::add_string(table, text);
  if (id == 0) return LocalizedValueStatus::empty_text;

  const auto encoded = encoded_string_id(id);
  if (std::equal(payload->begin(), payload->end(), encoded.begin(), encoded.end())) {
    return LocalizedValueStatus::unchanged;
  }
  std::copy(encoded.begin(), encoded.end(), payload->begin());
  mark_record_modified(plugin, *record);
  return LocalizedValueStatus::updated;
}

}  // namespace fo4
