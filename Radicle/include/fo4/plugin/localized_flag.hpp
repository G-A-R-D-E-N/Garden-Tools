#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <filesystem>
#include <optional>

namespace fo4 {

enum class LocalizedFlagConflict { none, inline_text, string_id, undecodable_record };

struct LocalizedFlagCheck {
  LocalizedFlagConflict conflict{LocalizedFlagConflict::none};
  std::optional<FormKey> record;
};

LocalizedFlagCheck check_localized_flag_change(const Plugin& plugin, bool localized);

bool strings_folder_present(const std::filesystem::path& plugin_path);

}  // namespace fo4
