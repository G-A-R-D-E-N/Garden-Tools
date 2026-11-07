#pragma once

#include "fo4/loadorder/load_order_files.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::loadorder {

struct Mo2Instance {
  std::filesystem::path instance_dir;
  std::string selected_profile;
  std::filesystem::path game_path;
  std::filesystem::path data_dir;
  std::filesystem::path base_dir;
  std::filesystem::path mods_dir;
  std::filesystem::path overwrite_dir;
  std::filesystem::path profiles_dir;
};

struct Mo2Mod {
  std::string name;
  bool enabled = false;
  bool is_separator = false;
};

struct Mo2Profile {
  std::string name;
  std::vector<Mo2Mod> mods_high_to_low;
  PluginList plugins;
};

using Mo2PathTranslator = std::function<std::filesystem::path(std::string)>;

Mo2Instance read_mo2_instance(const std::filesystem::path& instance_dir, const Mo2PathTranslator& translate = {});
Mo2Profile read_mo2_profile(const Mo2Instance& instance, std::string_view profile);
std::vector<std::filesystem::path> mo2_asset_roots_low_to_high(const Mo2Instance& instance, const Mo2Profile& profile);
std::map<std::string, std::filesystem::path> mo2_resolve_plugin_paths(const Mo2Instance& instance,
                                                                      const Mo2Profile& profile);

}  // namespace fo4::loadorder
