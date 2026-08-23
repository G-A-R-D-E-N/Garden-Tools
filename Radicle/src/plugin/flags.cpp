#include "fo4/plugin/writer.hpp"

#include "fo4/plugin/reader.hpp"

namespace fo4 {

Plugin set_plugin_light_flag(Plugin plugin, bool light) {
  assign_record_form_keys(plugin);
  if (light) {
    for (const auto* record : collect_records(plugin)) {
      if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) {
        continue;
      }
      if (record->form_key.local().value() > k_light_local_mask) {
        throw FormIdError(
            "plugin cannot be flagged light while owned FormIDs exceed 0xFFF");
      }
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
