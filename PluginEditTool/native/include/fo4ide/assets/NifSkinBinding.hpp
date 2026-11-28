#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

// Requires a readable NIF and a skeleton .hkx; skeletonNifPath may be empty. Reads only, and an empty block reports every skinned shape.
Result<std::string> readSkinBinding(const std::filesystem::path& nifPath,
                                    std::optional<std::uint32_t> block,
                                    const std::filesystem::path& skeletonPath,
                                    const std::filesystem::path& skeletonNifPath);

}
