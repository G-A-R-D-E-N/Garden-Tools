#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::settings {

struct Settings {
    std::filesystem::path mo2InstancePath;
    std::filesystem::path dataFolder;
    std::filesystem::path nifToolPath;
    std::filesystem::path texconvPath;
    std::filesystem::path papyrusCompilerPath;
    std::filesystem::path papyrusBaseImports;
    std::filesystem::path ckWikiPath;
    std::filesystem::path ffmpegPath;
    std::filesystem::path xwmaEncodePath;
    std::filesystem::path archive2Path;
    nlohmann::json extra{nlohmann::json::object()};
};

void to_json(nlohmann::json& json, const Settings& settings);
void from_json(const nlohmann::json& json, Settings& settings);

class Store {
public:
    explicit Store(std::filesystem::path path);

    Result<Settings> load() const;
    Result<void> save(const Settings& settings) const;
    const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
};

}
