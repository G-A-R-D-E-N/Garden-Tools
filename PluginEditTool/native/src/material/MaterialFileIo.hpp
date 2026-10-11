#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::material {

std::string lower(std::string_view value);
Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path);
Result<void> writeFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

}
