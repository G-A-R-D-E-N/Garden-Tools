#pragma once

#include "fo4/plugin/container.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace fo4 {

Plugin read_plugin(std::span<const std::byte> bytes, std::filesystem::path path = {});
Plugin read_plugin_file(const std::filesystem::path& path);

std::vector<const Record*> collect_records(const Plugin& plugin);
std::vector<const Group*> collect_groups(const Plugin& plugin);

const Record* find_record(const Plugin& plugin, RawFormId form_id);
const Record* find_record(const Plugin& plugin, const FormKey& form_key);

PluginFormIdContext plugin_form_id_context(const Plugin& plugin);
void assign_record_form_keys(Plugin& plugin);
void apply_master_kinds(Plugin& plugin, std::span<const MasterDescriptor> known);
void stamp_master_kinds(Plugin& plugin, std::span<const Plugin> loaded_masters);

PluginHeader parse_tes4_header(const Record& tes4);

}  // namespace fo4
