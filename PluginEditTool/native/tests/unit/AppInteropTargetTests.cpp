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
    std::filesystem::path trustedRoot;
    std::filesystem::path loadedInstance;
    bool failLoadMo2{false};
    bool failTrustedRoot{false};

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true, .readPlugins = true};
    }

    fo4ide::Result<void> setTrustedNewPluginRoot(const std::filesystem::path& root) override {
        if (failTrustedRoot) {
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
            if (failLoadMo2) {
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

const std::pair<std::string, nlohmann::json>* loadMo2Call(
    const std::vector<std::pair<std::string, nlohmann::json>>& calls) {
    const auto it = std::find_if(
        calls.begin(), calls.end(),
        [](const auto& call) { return call.first == "environment.LoadMo2"; });
    return it == calls.end() ? nullptr : &*it;
}

std::vector<std::string> snapshotNames(const fo4ide::mo2::ProfileSnapshot& snapshot) {
    std::vector<std::string> values;
    for (const auto& plugin : snapshot.plugins) values.push_back(plugin.name);
    return values;
}

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
    CHECK_FALSE(rows[1].at("Pending").get<bool>());
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
    const auto* load = loadMo2Call(plugins.calls);
    REQUIRE(load != nullptr);
    const auto& environment = load->second;
    CHECK(environment.at("profile") == "Test");
    CHECK(environment.at("dataFolder") == (root / "Game" / "Data").string());
    CHECK(environment.at("plugins").is_array());
    CHECK(environment.at("plugins").size() == 2);

    const auto result = target.invoke("GetPlugins", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Native.esp") != std::string::npos);
}

TEST_CASE("AppInterop GetMo2Status lists dirty plugins before reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    class ListedPendingBackend final : public FakePluginBackend {
    public:
        fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                              const nlohmann::json& arguments) override {
            calls.emplace_back(std::string(operation), arguments);
            if (operation == "appInterop.GetPlugins") {
                return nlohmann::json(std::string{
                    R"([{"Key":"Patch.esp","Pending":true},{"Key":"Clean.esp","Pending":false}])"});
            }
            return nlohmann::json(nullptr);
        }
    };

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    ListedPendingBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string(), true})).has_value());

    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    REQUIRE(status->at("pendingPlugins").is_array());
    CHECK(status->at("pendingPlugins") == nlohmann::json::array({"Patch.esp"}));
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
    CHECK_FALSE(status->at("staleConfig").get<bool>());
    CHECK_FALSE(status->at("staleLoadOrder").get<bool>());

    const auto pluginsTxt = root / "profiles" / "Test" / "plugins.txt";
    const auto previous = std::filesystem::last_write_time(pluginsTxt);
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previous + std::chrono::seconds(2));

    status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("stale").get<bool>());
    CHECK(status->at("staleLoadOrder").get<bool>());
    CHECK_FALSE(status->at("staleConfig").get<bool>());

    auto reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE(reloaded.has_value());
    CHECK_FALSE(reloaded->at("stale").get<bool>());
    CHECK_FALSE(reloaded->at("staleConfig").get<bool>());
    CHECK_FALSE(reloaded->at("staleLoadOrder").get<bool>());
}

TEST_CASE("AppInterop marks the MO2 snapshot stale when watched profile files change") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);
    writeFile(root / "profiles" / "Test" / "modlist.txt", "+Extra\n");
    writeFile(root / "profiles" / "Test" / "loadorder.txt", "Fallout4.esm\nExample.esp\n");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    const auto bump = [](const std::filesystem::path& path) {
        const auto previous = std::filesystem::last_write_time(path);
        std::filesystem::last_write_time(path, previous + std::chrono::seconds(2));
    };

    const auto ini = root / "ModOrganizer.ini";
    const std::vector<std::filesystem::path> loadOrderFiles{
        root / "profiles" / "Test" / "plugins.txt",
        root / "profiles" / "Test" / "modlist.txt",
        root / "profiles" / "Test" / "loadorder.txt",
    };

    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK_FALSE(status->at("stale").get<bool>());
    bump(ini);
    status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("stale").get<bool>());
    CHECK(status->at("staleConfig").get<bool>());
    CHECK_FALSE(status->at("staleLoadOrder").get<bool>());
    REQUIRE(target.invoke("ReloadMo2Profile", nlohmann::json::array()).has_value());

    for (const auto& path : loadOrderFiles) {
        status = target.invoke("GetMo2Status", nlohmann::json::array());
        REQUIRE(status.has_value());
        CHECK_FALSE(status->at("stale").get<bool>());
        bump(path);
        status = target.invoke("GetMo2Status", nlohmann::json::array());
        REQUIRE(status.has_value());
        CHECK(status->at("stale").get<bool>());
        CHECK(status->at("staleLoadOrder").get<bool>());
        CHECK_FALSE(status->at("staleConfig").get<bool>());
        auto reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
        REQUIRE(reloaded.has_value());
        CHECK_FALSE(reloaded->at("stale").get<bool>());
        CHECK_FALSE(reloaded->at("staleConfig").get<bool>());
        CHECK_FALSE(reloaded->at("staleLoadOrder").get<bool>());
    }
}

TEST_CASE("AppInterop refuses profile reload while plugins have pending edits") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

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
    CHECK(loadMo2Call(plugins.calls) == nullptr);

    auto discarded = target.invoke("ReloadMo2Profile", nlohmann::json::array({true}));
    REQUIRE(discarded.has_value());
    const auto* load = loadMo2Call(plugins.calls);
    REQUIRE(load != nullptr);
    CHECK(load->second.at("discardPending").get<bool>());
}

TEST_CASE("AppInterop fails closed when pending plugin status cannot be inspected") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    class BrokenPendingBackend final : public FakePluginBackend {
    public:
        fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                              const nlohmann::json& arguments) override {
            calls.emplace_back(std::string(operation), arguments);
            if (operation == "appInterop.GetPlugins") {
                return std::unexpected(fo4ide::Error{"GetPluginsFailed", "plugin list unavailable"});
            }
            return nlohmann::json(nullptr);
        }
    };

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    BrokenPendingBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);

    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string(), true})).has_value());
    plugins.calls.clear();

    auto blocked = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().type == "GetPluginsFailed");
    CHECK(loadMo2Call(plugins.calls) == nullptr);
    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
}

TEST_CASE("AppInterop keeps the live MO2 snapshot until reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);
    writeFile(root / "Game" / "Data" / "Second.esp");
    writeFile(root / "mods" / "Extra" / "FromMod.esp");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    CHECK(loaded->enabledModsHighToLow.empty());

    const auto pluginsTxt = root / "profiles" / "Test" / "plugins.txt";
    const auto modlistTxt = root / "profiles" / "Test" / "modlist.txt";
    const auto previousPlugins = std::filesystem::last_write_time(pluginsTxt);
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previousPlugins + std::chrono::seconds(2));

    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("stale").get<bool>());
    CHECK(status->at("pluginCount").get<std::size_t>() == 2);

    writeFile(modlistTxt, "+Extra\n");
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n*FromMod.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previousPlugins + std::chrono::seconds(4));
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    CHECK(loaded->enabledModsHighToLow.empty());
    status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("stale").get<bool>());
    CHECK(status->at("pluginCount").get<std::size_t>() == 2);
}

TEST_CASE("AppInterop reload replaces the live MO2 snapshot after profile files change") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);
    writeFile(root / "Game" / "Data" / "Second.esp");
    writeFile(root / "mods" / "Extra" / "FromMod.esp");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});

    const auto pluginsTxt = root / "profiles" / "Test" / "plugins.txt";
    const auto previousPlugins = std::filesystem::last_write_time(pluginsTxt);
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previousPlugins + std::chrono::seconds(2));
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});

    plugins.calls.clear();
    auto reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE(reloaded.has_value());
    CHECK_FALSE(reloaded->at("stale").get<bool>());
    CHECK(reloaded->at("pluginCount").get<std::size_t>() == 3);
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp", "Second.esp"});
    const auto* firstLoad = loadMo2Call(plugins.calls);
    REQUIRE(firstLoad != nullptr);
    CHECK(firstLoad->second.at("plugins").size() == 3);

    writeFile(root / "profiles" / "Test" / "modlist.txt", "+Extra\n");
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n*FromMod.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previousPlugins + std::chrono::seconds(4));
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp", "Second.esp"});

    plugins.calls.clear();
    reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE(reloaded.has_value());
    CHECK_FALSE(reloaded->at("stale").get<bool>());
    CHECK(reloaded->at("pluginCount").get<std::size_t>() == 4);
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{
        "Fallout4.esm", "Example.esp", "Second.esp", "FromMod.esp"});
    REQUIRE_FALSE(loaded->enabledModsHighToLow.empty());
    CHECK(loaded->enabledModsHighToLow.front().name == "Extra");
    const auto* secondLoad = loadMo2Call(plugins.calls);
    REQUIRE(secondLoad != nullptr);
    CHECK(secondLoad->second.at("plugins").size() == 4);
}

TEST_CASE("AppInterop reload replaces the previous MO2 snapshot instead of merging it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);
    writeFile(root / "Game" / "Data" / "Second.esp");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});

    const auto pluginsTxt = root / "profiles" / "Test" / "plugins.txt";
    const auto previousPlugins = std::filesystem::last_write_time(pluginsTxt);
    writeFile(pluginsTxt, "*Second.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previousPlugins + std::chrono::seconds(2));
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});

    plugins.calls.clear();
    auto reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE(reloaded.has_value());
    CHECK_FALSE(reloaded->at("stale").get<bool>());
    CHECK(reloaded->at("pluginCount").get<std::size_t>() == 2);
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Second.esp"});
    const auto* replacedLoad = loadMo2Call(plugins.calls);
    REQUIRE(replacedLoad != nullptr);
    std::vector<std::string> loadNames;
    for (const auto& plugin : replacedLoad->second.at("plugins")) {
        loadNames.push_back(plugin.at("name").get<std::string>());
    }
    CHECK(loadNames == snapshotNames(*loaded));
}

TEST_CASE("AppInterop keeps the old snapshot when reload is blocked by pending edits") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);
    writeFile(root / "Game" / "Data" / "Second.esp");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    PendingPluginBackend plugins;
    plugins.pendingName = "Example.esp";
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    const auto pluginsTxt = root / "profiles" / "Test" / "plugins.txt";
    const auto previousPlugins = std::filesystem::last_write_time(pluginsTxt);
    writeFile(pluginsTxt, "*Example.esp\n*Second.esp\n");
    std::filesystem::last_write_time(pluginsTxt, previousPlugins + std::chrono::seconds(2));
    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});

    plugins.pending = true;
    plugins.calls.clear();
    auto blocked = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().type == "PluginHasPendingChanges");
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("stale").get<bool>());
    CHECK(status->at("pluginCount").get<std::size_t>() == 2);
    CHECK(status->at("pendingPlugins") == nlohmann::json::array({"Example.esp"}));
    CHECK(loadMo2Call(plugins.calls) == nullptr);

    auto discarded = target.invoke("ReloadMo2Profile", nlohmann::json::array({true}));
    REQUIRE(discarded.has_value());
    CHECK_FALSE(discarded->at("stale").get<bool>());
    CHECK(discarded->at("pluginCount").get<std::size_t>() == 3);
    loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp", "Second.esp"});
}

TEST_CASE("AppInterop failed reload does not commit settings or trusted output root") {
    const auto first = tempRoot();
    Cleanup firstCleanup{first};
    makeProfile(first);
    const auto second = tempRoot();
    Cleanup secondCleanup{second};
    makeProfile(second);
    writeFile(second / "Game" / "Data" / "Other.esp");
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.mo2InstancePath;
    const auto firstData = settings.dataFolder;
    const auto firstRoot = plugins.trustedRoot;
    REQUIRE_FALSE(firstInstance.empty());
    REQUIRE_FALSE(firstRoot.empty());
    auto persisted = store.load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->mo2InstancePath == firstInstance);

    plugins.failLoadMo2 = true;
    plugins.calls.clear();
    auto failed = target.invoke("OpenMo2Profile", nlohmann::json::array({second.string()}));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "LoadMo2Failed");
    CHECK(loadMo2Call(plugins.calls) != nullptr);

    CHECK(settings.mo2InstancePath == firstInstance);
    CHECK(settings.dataFolder == firstData);
    CHECK(plugins.trustedRoot == firstRoot);
    CHECK(plugins.loadedInstance == firstInstance);
    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    persisted = store.load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->mo2InstancePath == firstInstance);
    CHECK(persisted->dataFolder == firstData);
}

TEST_CASE("AppInterop trusted-root failure after LoadMo2 restores the previous backend") {
    const auto first = tempRoot();
    Cleanup firstCleanup{first};
    makeProfile(first);
    const auto second = tempRoot();
    Cleanup secondCleanup{second};
    makeProfile(second);
    writeFile(second / "Game" / "Data" / "Other.esp");
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.mo2InstancePath;
    const auto firstData = settings.dataFolder;
    const auto firstRoot = plugins.trustedRoot;
    const auto firstLoaded = plugins.loadedInstance;
    REQUIRE_FALSE(firstInstance.empty());
    REQUIRE_FALSE(firstRoot.empty());
    auto beforeStatus = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(beforeStatus.has_value());
    const auto loadedAt = beforeStatus->at("loadedAtUnixMs").get<long long>();
    const auto pluginsTxt = first / "profiles" / "Test" / "plugins.txt";
    const auto watchStamp = std::filesystem::last_write_time(pluginsTxt);
    auto persisted = store.load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->mo2InstancePath == firstInstance);

    plugins.failTrustedRoot = true;
    plugins.calls.clear();
    auto failed = target.invoke("OpenMo2Profile", nlohmann::json::array({second.string()}));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "NewPluginRootInvalid");
    CHECK(loadMo2Call(plugins.calls) != nullptr);

    CHECK(settings.mo2InstancePath == firstInstance);
    CHECK(settings.dataFolder == firstData);
    CHECK(plugins.trustedRoot == firstRoot);
    CHECK(plugins.loadedInstance == firstLoaded);
    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(loaded->instance.instancePath == firstInstance);
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    persisted = store.load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->mo2InstancePath == firstInstance);
    CHECK(persisted->dataFolder == firstData);
    CHECK(std::filesystem::last_write_time(pluginsTxt) == watchStamp);
    auto afterStatus = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(afterStatus.has_value());
    CHECK(afterStatus->at("instancePath").get<std::string>() == firstInstance.string());
    CHECK(afterStatus->at("pluginCount").get<std::size_t>() == 2);
    CHECK(afterStatus->at("loadedAtUnixMs").get<long long>() == loadedAt);
    CHECK_FALSE(afterStatus->at("stale").get<bool>());
}

TEST_CASE("AppInterop settings-store failure after LoadMo2 restores the previous backend") {
    const auto first = tempRoot();
    Cleanup firstCleanup{first};
    makeProfile(first);
    const auto second = tempRoot();
    Cleanup secondCleanup{second};
    makeProfile(second);
    writeFile(second / "Game" / "Data" / "Other.esp");
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::Settings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.mo2InstancePath;
    const auto firstData = settings.dataFolder;
    const auto firstRoot = plugins.trustedRoot;
    const auto firstLoaded = plugins.loadedInstance;
    auto beforeStatus = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(beforeStatus.has_value());
    const auto loadedAt = beforeStatus->at("loadedAtUnixMs").get<long long>();
    const auto pluginsTxt = first / "profiles" / "Test" / "plugins.txt";
    const auto watchStamp = std::filesystem::last_write_time(pluginsTxt);
    auto persisted = store.load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->mo2InstancePath == firstInstance);

    const auto blocker = std::filesystem::path(store.path().string() + ".tmp");
    std::filesystem::create_directory(blocker);
    plugins.calls.clear();
    auto failed = target.invoke("OpenMo2Profile", nlohmann::json::array({second.string()}));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "SettingsWriteFailed");
    CHECK(loadMo2Call(plugins.calls) != nullptr);

    CHECK(settings.mo2InstancePath == firstInstance);
    CHECK(settings.dataFolder == firstData);
    CHECK(plugins.trustedRoot == firstRoot);
    CHECK(plugins.loadedInstance == firstLoaded);
    auto loaded = target.snapshot();
    REQUIRE(loaded.has_value());
    CHECK(loaded->instance.instancePath == firstInstance);
    CHECK(snapshotNames(*loaded) == std::vector<std::string>{"Fallout4.esm", "Example.esp"});
    persisted = store.load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->mo2InstancePath == firstInstance);
    CHECK(persisted->dataFolder == firstData);
    CHECK(std::filesystem::last_write_time(pluginsTxt) == watchStamp);
    auto afterStatus = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(afterStatus.has_value());
    CHECK(afterStatus->at("instancePath").get<std::string>() == firstInstance.string());
    CHECK(afterStatus->at("pluginCount").get<std::size_t>() == 2);
    CHECK(afterStatus->at("loadedAtUnixMs").get<long long>() == loadedAt);
    CHECK_FALSE(afterStatus->at("stale").get<bool>());
}
