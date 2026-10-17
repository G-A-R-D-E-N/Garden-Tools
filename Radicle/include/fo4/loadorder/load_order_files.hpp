#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::loadorder {

enum class OfficialTable { og_ng, ae };

struct PluginListEntry {
  std::string name;
  bool active = false;
  std::size_t line = 0;
};

struct PluginList {
  std::vector<PluginListEntry> entries;
  std::vector<PluginListEntry> duplicates;
  std::vector<std::size_t> invalid_lines;
};

struct LoadOrderEntry {
  std::string name;
  bool checked = false;
  bool master_flag = false;
  bool light = false;
};

struct LoadOrderFinding {
  std::string code;
  std::string plugin;
  std::string detail;
};

struct AssembledLoadOrder {
  std::vector<LoadOrderEntry> entries;
  std::vector<LoadOrderFinding> findings;
  bool failed = false;
};

PluginList parse_plugin_list(std::string_view text);
PluginList read_plugin_list(const std::filesystem::path& path);
std::vector<std::string> parse_ccc(std::string_view text);
std::vector<std::string> read_ccc(const std::filesystem::path& path);

AssembledLoadOrder assemble_load_order(const std::filesystem::path& data_dir,
                                       const PluginList& plugin_list,
                                       std::span<const std::string> ccc_names,
                                       OfficialTable table);
AssembledLoadOrder assemble_load_order(std::span<const std::filesystem::path> plugin_files,
                                       const PluginList& plugin_list,
                                       std::span<const std::string> ccc_names,
                                       OfficialTable table);

}  // namespace fo4::loadorder
