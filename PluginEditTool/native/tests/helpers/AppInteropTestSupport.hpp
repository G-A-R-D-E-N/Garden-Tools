#pragma once

#include <algorithm>
#include "TempRoot.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <fo4ide/app/AppInteropTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "PluginFixtures.hpp"

namespace app_interop_test {

inline std::filesystem::path tempRoot() {
    static std::atomic<unsigned> counter{0};
    const auto id = fo4ide_test::uniqueTempName();
    const auto salt = std::random_device{}();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_appinterop_test_" + id + "_" + std::to_string(salt) + "_" + std::to_string(counter++));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

inline void writeFile(const std::filesystem::path& path, std::string_view contents = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
}

class FakePlatform final : public fo4ide::platform::Services {
public:
    std::filesystem::path folder;

    fo4ide::Result<std::filesystem::path> pickFile(const fo4ide::platform::FileDialogOptions&) override {
        return std::filesystem::path{};
    }

    fo4ide::Result<std::filesystem::path> pickFolder(const fo4ide::platform::FileDialogOptions&) override {
        return folder;
    }

    fo4ide::Result<std::filesystem::path> pickSavePath(const fo4ide::platform::FileDialogOptions&) override {
        return std::filesystem::path{};
    }

    fo4ide::Result<void> openExternal(std::string_view) override {
        return {};
    }

    fo4ide::Result<void> openFolder(const std::filesystem::path&) override {
        return {};
    }
};

class FakePluginBackend : public fo4ide::plugin::IPluginBackend {
public:
    std::vector<std::pair<std::string, nlohmann::json>> calls;
    std::filesystem::path trustedRoot;
    std::filesystem::path loadedInstance;
    std::size_t loadMo2Calls{0};
    std::size_t trustedRootCalls{0};
    bool failLoadMo2{false};
    bool failTrustedRoot{false};
    std::optional<std::size_t> failLoadMo2At;
    std::optional<std::size_t> failTrustedRootAt;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true, .readPlugins = true};
    }

    fo4ide::Result<void> setTrustedNewPluginRoot(const std::filesystem::path& root) override {
        const auto index = trustedRootCalls++;
        if (failTrustedRoot || failTrustedRootAt == index) {
            return std::unexpected(fo4ide::Error{
                "NewPluginRootInvalid",
                "deliberate trusted-root failure"
            });
        }
        trustedRoot = root;
        return {};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json& arguments) override {
        calls.emplace_back(std::string(operation), arguments);
        if (operation == "environment.LoadMo2") {
            const auto index = loadMo2Calls++;
            if (failLoadMo2 || failLoadMo2At == index) {
                return std::unexpected(fo4ide::Error{"LoadMo2Failed", "deliberate backend load failure"});
            }
            loadedInstance = arguments.value("instancePath", std::string{});
            return nlohmann::json(nullptr);
        }
        if (operation == "appInterop.GetPlugins") {
            return nlohmann::json(R"([{"Key":"Native.esp","ConflictStatus":0,"IsRecordNode":false,"HasChildren":true,"Pending":false}])");
        }
        return nlohmann::json(nullptr);
    }
};

class PendingPluginBackend final : public FakePluginBackend {
public:
    bool pending{false};
    std::string pendingName{"Patch.esp"};

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json& arguments) override {
        calls.emplace_back(std::string(operation), arguments);
        if (operation == "appInterop.GetPlugins") {
            return nlohmann::json(pending
                ? nlohmann::json::array({
                      {{"Key", pendingName}, {"Pending", true}}
                  }).dump()
                : nlohmann::json::array({
                      {{"Key", pendingName}, {"Pending", false}}
                  }).dump());
        }
        return nlohmann::json(nullptr);
    }
};

inline const std::pair<std::string, nlohmann::json>* loadMo2Call(
    const std::vector<std::pair<std::string, nlohmann::json>>& calls) {
    const auto it = std::find_if(
        calls.begin(), calls.end(),
        [](const auto& call) { return call.first == "environment.LoadMo2"; });
    return it == calls.end() ? nullptr : &*it;
}

inline std::vector<std::string> snapshotNames(const fo4ide::mo2::ProfileSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& plugin : snapshot.plugins) values.push_back(plugin.name);
    return values;
}

inline void makeProfile(const std::filesystem::path& root) {
    const auto game = root / "Game";
    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(root / "profiles" / "Test" / "plugins.txt", "*Example.esp\n");
    writeFile(game / "Data" / "Fallout4.esm", plugin_fixtures_test::minimalPlugin(0x1, "fallout4"));
    writeFile(game / "Data" / "Example.esp", plugin_fixtures_test::minimalPlugin(0, "example"));
}

}
