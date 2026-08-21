#include "fo4/plugin/writer.hpp"

namespace fo4 {

Plugin set_plugin_light_flag(Plugin plugin, bool light) {
  const auto current = plugin.tes4.header.flags.value();
  const auto next = light
      ? (current | RecordFlags::small_file)
      : (current & ~RecordFlags::small_file);

  plugin.tes4.header.flags = RecordFlags{next};
  plugin.header.tes4_flags = plugin.tes4.header.flags;

  plugin.original_bytes.clear();
  plugin.tes4.original_bytes.clear();
  return plugin;
}

}  // namespace fo4
