#pragma once

#include "fo4/archive/ba2.hpp"

#include <filesystem>
#include <optional>

namespace fo4::test {

inline const fo4::archive::Ba2GeneralArchive* shared_meshes_archive(const std::filesystem::path& data_dir) {
  static const std::optional<fo4::archive::Ba2GeneralArchive> archive = [&]() -> std::optional<fo4::archive::Ba2GeneralArchive> {
    const auto path = data_dir / "Fallout4 - Meshes.ba2";
    if (!std::filesystem::exists(path)) return std::nullopt;
    return fo4::archive::read_ba2_general_file(path);
  }();
  return archive ? &*archive : nullptr;
}

}  // namespace fo4::test
