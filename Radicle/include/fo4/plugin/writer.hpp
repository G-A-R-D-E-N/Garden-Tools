#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <filesystem>
#include <vector>

namespace fo4 {

std::vector<std::byte> write_plugin(const Plugin& plugin);
void write_plugin_file(const Plugin& plugin, const std::filesystem::path& path);

Plugin remap_plugin_masters(Plugin plugin, std::vector<PluginKey> new_masters);
Plugin remap_plugin_masters(Plugin plugin, std::vector<MasterDescriptor> new_masters);

}  // namespace fo4
