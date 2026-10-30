#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/process/ProcessRunner.hpp>

namespace fo4ide::assets {

struct NifImportOptions {
    std::string material;
    std::string diffuseTexture;
    std::string normalTexture;
    bool collisionBox{false};
    bool fromBlender{false};
};

class NifService {
public:
    // Requires in-process execution: the configured tool path and runner are unused.
    NifService(process::Runner&, std::filesystem::path);

    Result<std::string> inspect(const std::filesystem::path& nifPath) const;
    Result<std::string> geometry(const std::filesystem::path& nifPath) const;
    Result<std::string> verify(const std::filesystem::path& nifPath) const;
    Result<std::string> tree(const std::filesystem::path& nifPath) const;
    Result<std::string> fix(const std::filesystem::path& nifPath,
                            const std::filesystem::path& outputPath) const;
    Result<std::string> importObj(const std::filesystem::path& objPath,
                                  const std::filesystem::path& outputPath,
                                  const NifImportOptions& options = {}) const;
    Result<std::string> applyEdits(const std::filesystem::path& nifPath,
                                   const nlohmann::json& edits,
                                   const std::filesystem::path& outputPath = {}) const;

private:
    Result<void> requireFile(const std::filesystem::path& path, std::string_view label) const;
};

}
