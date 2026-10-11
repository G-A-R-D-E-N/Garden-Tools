#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace fo4 {

std::vector<std::byte> write_plugin(const Plugin& plugin);
void write_plugin_file(const Plugin& plugin, const std::filesystem::path& path);
Plugin apply_extension_flags(Plugin plugin, const std::filesystem::path& path);

Plugin set_plugin_light_flag(Plugin plugin, bool light);
Plugin remap_plugin_masters(Plugin plugin, std::vector<PluginKey> new_masters);
Plugin remap_plugin_masters(Plugin plugin, std::vector<MasterDescriptor> new_masters);
Plugin sort_plugin_masters(Plugin plugin, std::span<const PluginKey> load_order);
Plugin make_empty_plugin(std::string_view filename);

}  // namespace fo4
