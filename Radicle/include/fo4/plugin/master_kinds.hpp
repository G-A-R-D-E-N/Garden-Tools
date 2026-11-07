#pragma once

#include "fo4/plugin/container.hpp"

#include <filesystem>
#include <span>

namespace fo4 {

void stamp_master_kinds_from_files(
    Plugin& plugin,
    std::span<const std::filesystem::path> master_paths);

}  // namespace fo4
