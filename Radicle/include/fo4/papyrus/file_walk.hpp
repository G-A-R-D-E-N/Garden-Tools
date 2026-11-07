#pragma once

#include <filesystem>
#include <vector>

namespace fo4::papyrus {

std::vector<std::filesystem::path> enumeratePapyrusFiles(const std::filesystem::path& root);

}
