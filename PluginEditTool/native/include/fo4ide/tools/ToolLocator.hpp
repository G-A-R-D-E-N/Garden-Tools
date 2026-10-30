#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include <fo4ide/settings/Settings.hpp>

namespace fo4ide::tools {

enum class Tool {
    Texconv,
    PapyrusCompiler,
    Archive2,
    CkWiki,
    Ffmpeg,
    XwmaEncode,
    BmlFuzEncode,
    BmlFuzDecode,
    ClaudeCode
};

class Locator {
public:
    Locator(const settings::SharedSettings& settings, std::filesystem::path executablePath);

    std::filesystem::path fallout4Root() const;
    std::filesystem::path resolve(Tool tool) const;
    std::vector<std::filesystem::path> papyrusBaseImports() const;
    std::string_view describe(Tool tool) const noexcept;

private:
    std::filesystem::path appPath(std::initializer_list<std::string_view> parts) const;
    std::filesystem::path firstFile(std::initializer_list<std::filesystem::path> candidates) const;
    std::filesystem::path firstDirectory(std::initializer_list<std::filesystem::path> candidates) const;
    std::filesystem::path onPath(std::initializer_list<std::string_view> names) const;
    std::filesystem::path environmentPath(const char* name) const;

    const settings::SharedSettings& settings_;
    std::filesystem::path executablePath_;
    std::filesystem::path appDirectory_;
};

std::filesystem::path resolveCommand(const std::filesystem::path& command);

}
