#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

namespace fo4ide::audio {

class AudioService {
public:
    AudioService(process::Runner& runner, const tools::Locator& tools);

    Result<std::string> convertToXwm(const std::filesystem::path& source,
                                     const std::filesystem::path& output = {},
                                     std::optional<int> bitrateBps = {}) const;
    Result<std::string> convertFromXwm(const std::filesystem::path& source,
                                       const std::filesystem::path& output = {},
                                       std::string targetExtension = "wav") const;
    Result<std::string> makeFuz(const std::filesystem::path& audioSource,
                                const std::filesystem::path& lipPath,
                                const std::filesystem::path& fuzOutput,
                                bool noLip) const;
    Result<std::string> extractFuz(const std::filesystem::path& fuzPath,
                                   const std::filesystem::path& xwmOutput = {},
                                   const std::filesystem::path& lipOutput = {},
                                   bool alsoWav = false) const;

private:
    Result<std::string> convertOneToXwm(const std::filesystem::path& source,
                                        const std::filesystem::path& output,
                                        std::optional<int> bitrateBps) const;
    Result<std::filesystem::path> normalizeToPcmWav(const std::filesystem::path& source) const;
    Result<process::Output> runTool(const std::filesystem::path& executable,
                                    std::vector<std::string> arguments,
                                    std::chrono::milliseconds timeout) const;
    bool isPcmWav(const std::filesystem::path& path) const;

    process::Runner& runner_;
    const tools::Locator& tools_;
};

}
