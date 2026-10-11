#include "fo4/plugin/load_order.hpp"

#include <set>
#include <utility>

namespace fo4 {

RuntimeFormIdContext build_runtime_form_id_context(
    std::span<const Plugin* const> load_order) {
  RuntimeFormIdContext context;
  context.full_plugins.reserve(load_order.size());
  context.light_plugins.reserve(load_order.size());
  std::set<PluginKey> seen;

  for (const auto* plugin : load_order) {
    if (plugin == nullptr || plugin->key.empty()) {
      continue;
    }
    if (!seen.insert(plugin->key).second) {
      throw FormIdError("duplicate plugin in runtime load order: " +
                        std::string{plugin->key.filename()});
    }

    MasterDescriptor descriptor{
        plugin->key,
        classify_plugin_kind(plugin->header.tes4_flags, plugin->key),
    };
    if (descriptor.kind == PluginKind::light) {
      if (context.light_plugins.size() >= 0x1000u) {
        throw FormIdError(
            "runtime light-plugin table exceeds 4096 FE slots (0x000-0xFFF)");
      }
      context.light_plugins.push_back(std::move(descriptor));
    } else {
      if (context.full_plugins.size() >= 0xFEu) {
        throw FormIdError(
            "runtime full-plugin table exceeds 254 compile indexes (0x00-0xFD); 0xFE and 0xFF are reserved");
      }
      context.full_plugins.push_back(std::move(descriptor));
    }
  }

  return context;
}

}  // namespace fo4
