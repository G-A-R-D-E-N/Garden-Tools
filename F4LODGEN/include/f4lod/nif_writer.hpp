#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct NifWriteBlock {
    std::string type;
    std::vector<unsigned char> data;
};

struct NifWriteDocument {
    std::string creator{"F4LODGen"};
    std::vector<std::string> strings;
    std::vector<NifWriteBlock> blocks;
    std::vector<std::int32_t> roots{0};
};

struct NifWriteResult {
    bool valid{};
    std::string error;
    std::uint64_t bytes_written{};
};

[[nodiscard]] NifWriteResult write_fallout4_nif(
    const std::filesystem::path& path,
    const NifWriteDocument& document);

} // namespace f4lod
