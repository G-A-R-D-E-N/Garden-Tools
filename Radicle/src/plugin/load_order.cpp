#include "fo4/plugin/load_order.hpp"

namespace fo4 {

RuntimeFormIdContext build_runtime_form_id_context(
    std::span<const Plugin* const> load_order) {
  RuntimeFormIdContext context;
  context.full_plugins.reserve(load_order.size());
  context.light_plugins.reserve(load_order.size());

  for (const auto* plugin : load_order) {
    if (plugin == nullptr || plugin->key.empty()) {
      continue;
    }

    MasterDescriptor descriptor{
        plugin->key,
        plugin->header.tes4_flags.is_small_file() ? PluginKind::light
                                                  : PluginKind::full,
    };
    if (descriptor.kind == PluginKind::light) {
      context.light_plugins.push_back(std::move(descriptor));
    } else {
      context.full_plugins.push_back(std::move(descriptor));
    }
  }

  return context;
}

}  // namespace fo4
