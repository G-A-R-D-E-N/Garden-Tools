#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

namespace fo4ide::archive {

class IArchiveBackend {
public:
    virtual ~IArchiveBackend() = default;
    virtual bool available() const noexcept = 0;
    virtual Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json& arguments) = 0;
};

// Placeholder backend used when no real archive implementation is wired up.
// It is genuinely null: every operation reports ArchiveBackendUnavailable.
class NullArchiveBackend final : public IArchiveBackend {
public:
    NullArchiveBackend() = default;

    bool available() const noexcept override { return false; }
    Result<nlohmann::json> invoke(std::string_view operation,
                                  const nlohmann::json& arguments) override;
};

class ArchiveService {
public:
    ArchiveService(process::Runner& runner,
                   const tools::Locator& tools,
                   IArchiveBackend& backend);

    Result<nlohmann::json> list(const std::filesystem::path& archivePath,
                                std::string filter,
                                int limit,
                                std::string filterMode);
    Result<std::string> extractFile(const std::filesystem::path& archivePath,
                                    std::string innerPath,
                                    const std::filesystem::path& outputPath);
    Result<std::string> extractSelected(const std::filesystem::path& archivePath,
                                        const std::vector<std::string>& innerPaths,
                                        const std::filesystem::path& outputDirectory);
    Result<std::string> extractAll(const std::filesystem::path& archivePath,
                                   const std::filesystem::path& outputDirectory,
                                   std::string filter,
                                   int limit,
                                   std::string filterMode);
    Result<nlohmann::json> compare(const std::filesystem::path& archiveA,
                                   const std::filesystem::path& archiveB);
    Result<std::string> pack(const std::vector<std::filesystem::path>& sources,
                             std::filesystem::path outputBa2,
                             std::string format,
                             const std::filesystem::path& rootDirectory,
                             bool compress) const;

    bool readBackendAvailable() const noexcept;

private:
    process::Runner& runner_;
    const tools::Locator& tools_;
    IArchiveBackend& backend_;
};

}
