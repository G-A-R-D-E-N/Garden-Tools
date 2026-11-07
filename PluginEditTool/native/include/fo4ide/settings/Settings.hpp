#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::settings {

struct Settings {
    std::string aiProvider;
    std::string anthropicApiKey;
    std::string model;
    std::string geminiApiKey;
    std::string geminiModel;
    std::filesystem::path claudeCodePath;
    std::string ollamaUrl;
    std::string ollamaModel;
    std::filesystem::path outputFolder;
    std::filesystem::path fallout4Path;
    std::filesystem::path dataFolder;
    std::filesystem::path mo2InstancePath;
    std::filesystem::path ckWikiPath;
    std::filesystem::path texconvPath;
    std::filesystem::path papyrusCompilerPath;
    std::filesystem::path papyrusBaseImports;
    std::filesystem::path nifToolPath;
    std::filesystem::path ffmpegPath;
    std::filesystem::path xwmaEncodePath;
    std::filesystem::path archive2Path;
    bool readLargePluginsIntoMemory{false};
    nlohmann::json extra{nlohmann::json::object()};
};

void to_json(nlohmann::json& json, const Settings& settings);
void from_json(const nlohmann::json& json, Settings& settings);

class SharedSettings {
public:
    explicit SharedSettings(Settings value = {}) : value_(std::move(value)) {}

    Settings snapshot() const {
        std::lock_guard lock(mutex_);
        return value_;
    }

    template <class F>
    decltype(auto) update(F&& fn) {
        std::lock_guard lock(mutex_);
        return std::forward<F>(fn)(value_);
    }

private:
    mutable std::mutex mutex_;
    Settings value_;
};

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
