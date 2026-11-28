#pragma once

#include "fo4/plugin/container.hpp"

#include <filesystem>

namespace fo4 {

struct TextExportOptions {
  bool schema_labels{false};
};

std::filesystem::path export_plugin_text(const Plugin& plugin,
                                         const std::filesystem::path& parent_dir,
                                         const TextExportOptions& options = {});
Plugin import_plugin_text(const std::filesystem::path& fo4txt_dir);

}  // namespace fo4
