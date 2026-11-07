#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::hkx {

enum class PoseSpace { Local, Model };

class HkxService {
public:
    Result<nlohmann::json> skeleton(const std::filesystem::path& skeletonPath) const;
    Result<nlohmann::json> pose(const std::filesystem::path& skeletonPath,
                                const std::filesystem::path& animationPath,
                                float time,
                                PoseSpace space) const;
};

}
