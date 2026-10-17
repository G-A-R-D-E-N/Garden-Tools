#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/AppInteropTarget.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../helpers/AppInteropTestSupport.hpp"
#include "../helpers/PluginFixtures.hpp"

using namespace app_interop_test;
using namespace plugin_fixtures_test;

TEST_CASE("AppInterop validates selected MO2 instance folders") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "profiles");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend nativePlugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, nativePlugins);

    auto opened = target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()}));
    REQUIRE(opened.has_value());
    CHECK(settings.snapshot().mo2InstancePath == std::filesystem::absolute(root));
    CHECK(settings.snapshot().dataFolder == root / "Game" / "Data");

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
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
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
    fo4ide::settings::SharedSettings settings;
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
    writeFile(root / "Game" / "Data" / "Second.esp", minimalPlugin(0, "second"));
    writeFile(root / "mods" / "Extra" / "FromMod.esp", minimalPlugin(0, "from mod"));

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
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
    writeFile(root / "Game" / "Data" / "Second.esp", minimalPlugin(0, "second"));
    writeFile(root / "mods" / "Extra" / "FromMod.esp", minimalPlugin(0, "from mod"));

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
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
    writeFile(root / "Game" / "Data" / "Second.esp", minimalPlugin(0, "second"));

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
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
    writeFile(root / "Game" / "Data" / "Second.esp", minimalPlugin(0, "second"));

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
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

TEST_CASE("AppInterop GetMo2Status exposes load order warnings") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);
    writeFile(root / "Game" / "Fallout4.ccc", "ccGone.esl\n");

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());

    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("warnings") == nlohmann::json::array({"ccGone.esl: listed in Fallout4.ccc but not in Data"}));

    const auto clean = root / "clean";
    makeProfile(clean);
    fo4ide::settings::Store cleanStore(clean / "settings.json");
    fo4ide::settings::SharedSettings cleanSettings;
    fo4ide::app::AppInteropTarget cleanTarget(cleanStore, cleanSettings, platform, plugins);
    REQUIRE(cleanTarget.invoke("OpenMo2Profile", nlohmann::json::array({clean.string()})).has_value());

    auto cleanStatus = cleanTarget.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(cleanStatus.has_value());
    REQUIRE(cleanStatus->at("warnings").is_array());
    CHECK(cleanStatus->at("warnings").empty());
}

TEST_CASE("AppInterop ReloadMo2Profile keeps the loaded profile when the new load order is invalid") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    makeProfile(root);

    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({root.string()})).has_value());
    REQUIRE(plugins.loadMo2Calls == 1);

    auto status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    const auto pluginCount = status->at("pluginCount").get<std::size_t>();

    writeFile(root / "Game" / "Data" / "Mid.esm", minimalPlugin(0x1, "mid", {"Absent.esm"}));
    writeFile(root / "Game" / "Data" / "Example.esp", minimalPlugin(0, "example", {"Mid.esm"}));

    auto reloaded = target.invoke("ReloadMo2Profile", nlohmann::json::array());
    REQUIRE_FALSE(reloaded.has_value());
    CHECK(reloaded.error().type == "Mo2LoadOrderInvalid");
    CHECK(plugins.loadMo2Calls == 1);

    status = target.invoke("GetMo2Status", nlohmann::json::array());
    REQUIRE(status.has_value());
    CHECK(status->at("pluginCount").get<std::size_t>() == pluginCount);
}
