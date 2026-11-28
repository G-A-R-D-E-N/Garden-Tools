#include "fo4/plugin/localized_flag.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"

#include <cstddef>
#include <span>
#include <system_error>

namespace fo4 {
namespace {

bool inline_shaped(std::span<const std::byte> data) {
  return data.size() == 4 && data[3] == std::byte{0} && data[0] != std::byte{0} &&
         data[1] != std::byte{0} && data[2] != std::byte{0};
}

bool conflicts(std::span<const std::byte> data, bool localized) {
  if (localized) return data.size() != 4 || inline_shaped(data);
  return data.size() == 4;
}

}  // namespace

LocalizedFlagCheck check_localized_flag_change(const Plugin& plugin, bool localized) {
  const auto conflict = localized ? LocalizedFlagConflict::inline_text : LocalizedFlagConflict::string_id;
  for (const auto* record : collect_records(plugin)) {
    const auto flagged = [&](const Record& at) {
      return LocalizedFlagCheck{conflict, at.form_key};
    };
    if (record->undecodable) return LocalizedFlagCheck{LocalizedFlagConflict::undecodable_record, record->form_key};
    for (const auto& row : record->subrecords) {
      if (!schema::is_localized_field(record->header.signature, row.signature)) continue;
      if (conflicts(row.data, localized)) return flagged(*record);
    }
    for (const auto& field : schema::scalar_fields(record->header.signature)) {
      if (!field.scalar.localized || field.scalar.storage != schema::ScalarStorage::zstring) continue;
      const auto resolved =
          schema::resolve_field_slots(*record, schema::scalar_field_shape(field.scalar), field.scalar.address);
      for (const auto& slot : resolved.slots) {
        if (!slot.valid) continue;
        if (conflicts(record->subrecords[slot.subrecord].data, localized)) return flagged(*record);
      }
    }
  }
  return {};
}

bool strings_folder_present(const std::filesystem::path& plugin_path) {
  if (plugin_path.parent_path().empty()) return false;
  std::error_code ec;
  return std::filesystem::is_directory(plugin_path.parent_path() / "Strings", ec);
}

}  // namespace fo4
