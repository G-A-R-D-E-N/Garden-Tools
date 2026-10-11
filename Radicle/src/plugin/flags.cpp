#include "fo4/plugin/writer.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"

#include <format>

namespace fo4 {

Plugin set_plugin_light_flag(Plugin plugin, bool light) {
  assign_record_form_keys(plugin);
  if (light) {
    const auto lowest = allows_hardcoded_range(plugin) ? k_first_light_local_id : k_first_full_local_id;
    for (const auto* record : collect_records(plugin)) {
      if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) {
        continue;
      }
      const auto local = record->form_key.local().value();
      if (local < lowest || local > k_light_local_mask) {
        throw FormIdError(std::format(
            "plugin cannot be flagged light while owned FormIDs fall outside 0x{:03X}-0xFFF",
            lowest));
      }
    }
    if (const auto lowered = lowered_light_next_form_id(plugin)) {
      set_hedr_next_form_id(plugin, *lowered);
    }
  }

  const auto current = plugin.tes4.header.flags.value();
  const auto next = light
      ? (current | RecordFlags::small_file)
      : (current & ~RecordFlags::small_file);

  plugin.tes4.header.flags = RecordFlags{next};
  plugin.header.tes4_flags = plugin.tes4.header.flags;

  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
  return plugin;
}

}  // namespace fo4
