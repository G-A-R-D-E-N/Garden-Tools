#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/AppInteropTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_appinterop_test_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeFile(const std::filesystem::path& path, std::string_view contents = {}) {
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

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true, .readPlugins = true};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json& arguments) override {
        calls.emplace_back(std::string(operation), arguments);
        if (operation == "appInterop.GetPlugins") {
            return nlohmann::json(R"([{"Key":"Native.esp","ConflictStatus":0,"IsRecordNode":false,"HasChildren":true}])");
        }
        return nlohmann::json(nullptr);
    }
};

void makeProfile(const std::filesystem::path& root) {
    const auto game = root / "Game";
    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(root / "profiles" / "Test" / "plugins.txt", "*Example.esp\n");
    writeFile(game / "Data" / "Fallout4.esm");
    writeFile(game / "Data" / "Example.esp");
}

}

TEST_CASE("AppInterop validates selected MO2 instance folders") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "profiles");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend plugins;
    platform.folder = root;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);

    auto result = target.invoke("BrowseForMo2Folder", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(*result == root.string());
}

TEST_CASE("AppInterop opens MO2 metadata and returns top-level plugin rows") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend nativePlugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, nativePlugins);

    auto opened = target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()}));
    REQUIRE(opened.has_value());
    CHECK(settings.mo2InstancePath == std::filesystem::absolute(root));
    CHECK(settings.dataFolder == root / "Game" / "Data");

    auto plugins = target.invoke("GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    const auto rows = nlohmann::json::parse(plugins->get<std::string>());
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].at("Key") == "Fallout4.esm");
    CHECK(rows[1].at("Key") == "Example.esp");
    CHECK(rows[1].at("FilePath") == (root / "Game" / "Data" / "Example.esp").string());
}

TEST_CASE("AppInterop keeps profile metadata available for the future plugin backend") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);

    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());
    const auto snapshot = target.snapshot();
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->plugins.size() == 2);
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("AppInterop sends one normalized MO2 environment to the native plugin backend") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);

    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());
    REQUIRE_FALSE(plugins.calls.empty());
    const auto load = std::find_if(
        plugins.calls.begin(), plugins.calls.end(),
        [](const auto& call) { return call.first == "environment.LoadMo2"; });
    REQUIRE(load != plugins.calls.end());
    const auto& environment = load->second;
    CHECK(environment.at("profile") == "Test");
    CHECK(environment.at("dataFolder") == (root / "Game" / "Data").string());
    CHECK(environment.at("plugins").is_array());
    CHECK(environment.at("plugins").size() == 2);

    const auto result = target.invoke("GetPlugins", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Native.esp") != std::string::npos);
}

TEST_CASE("AppInterop reports MO2 profile staleness and reload clears it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK_FALSE(status->at("stale").get<bool>());

    const auto pluginsTxt = root / "profiles" / "Test" / "plugins.txt";
    const auto previous = std::filesystem::last_write_time(pluginsTxt);
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previous + std::chrono::seconds(2));

    status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("stale").get<bool>());

    auto reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE(reloaded.has_value());
    CHECK_FALSE(reloaded->at("stale").get<bool>());
}

TEST_CASE("AppInterop refuses profile reload while plugins have pending edits") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    class PendingPluginBackend final : public FakePluginBackend {
    public:
        bool pending{false};

        fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                              const nlohmann::json& arguments) override {
            calls.emplace_back(std::string(operation), arguments);
            if (operation == "backend.GetActivePlugins") {
                return nlohmann::json(pending
                    ? std::string{R"([{"Name":"Patch.esp","Pending":true}])"}
                    : std::string{R"([{"Name":"Patch.esp","Pending":false}])"});
            }
            return nlohmann::json(nullptr);
        }
    };

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    PendingPluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);

    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());
    plugins.pending = true;
    plugins.calls.clear();

    auto blocked = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().type == "PluginHasPendingChanges");
    CHECK(blocked.error().message.find("Patch.esp") != std::string::npos);
    CHECK(std::none_of(
        plugins.calls.begin(), plugins.calls.end(),
        [](const auto& call) { return call.first == "environment.LoadMo2"; }));

    auto discarded = target.invoke("ReloadMo2Profile", nlohmann::json::array({true}));
    REQUIRE(discarded.has_value());
    const auto load = std::find_if(
        plugins.calls.begin(), plugins.calls.end(),
        [](const auto& call) { return call.first == "environment.LoadMo2"; });
    REQUIRE(load != plugins.calls.end());
    CHECK(load->second.at("discardPending").get<bool>());
}
