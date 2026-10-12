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

TEST_CASE("AppInterop failed reload does not commit settings or trusted output root") {
    const auto first = tempRoot();
    Cleanup firstCleanup{first};
    makeProfile(first);
    const auto second = tempRoot();
    Cleanup secondCleanup{second};
    makeProfile(second);
    writeFile(second / "Game" / "Data" / "Other.esp", minimalPlugin(0, "other"));
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.snapshot().mo2InstancePath;
    const auto firstData = settings.snapshot().dataFolder;
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

    CHECK(settings.snapshot().mo2InstancePath == firstInstance);
    CHECK(settings.snapshot().dataFolder == firstData);
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
    writeFile(second / "Game" / "Data" / "Other.esp", minimalPlugin(0, "other"));
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.snapshot().mo2InstancePath;
    const auto firstData = settings.snapshot().dataFolder;
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

    CHECK(settings.snapshot().mo2InstancePath == firstInstance);
    CHECK(settings.snapshot().dataFolder == firstData);
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
    writeFile(second / "Game" / "Data" / "Other.esp", minimalPlugin(0, "other"));
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.snapshot().mo2InstancePath;
    const auto firstData = settings.snapshot().dataFolder;
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

    CHECK(settings.snapshot().mo2InstancePath == firstInstance);
    CHECK(settings.snapshot().dataFolder == firstData);
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

TEST_CASE("AppInterop surfaces rollback LoadMo2 failure and still restores the trusted root") {
    const auto first = tempRoot();
    Cleanup firstCleanup{first};
    makeProfile(first);
    const auto second = tempRoot();
    Cleanup secondCleanup{second};
    makeProfile(second);
    writeFile(second / "Game" / "Data" / "Other.esp", minimalPlugin(0, "other"));
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.snapshot().mo2InstancePath;
    const auto firstData = settings.snapshot().dataFolder;
    const auto firstRoot = plugins.trustedRoot;
    const auto loadsBefore = plugins.loadMo2Calls;
    const auto rootsBefore = plugins.trustedRootCalls;
    REQUIRE(loadsBefore == 1);
    REQUIRE(rootsBefore == 1);

    const auto blocker = std::filesystem::path(store.path().string() + ".tmp");
    std::filesystem::create_directory(blocker);
    plugins.failLoadMo2At = loadsBefore + 1;
    plugins.calls.clear();
    auto failed = target.invoke("OpenMo2Profile", nlohmann::json::array({second.string()}));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "SettingsWriteFailed");
    CHECK(failed.error().message.find("rollback failed") != std::string::npos);
    CHECK(failed.error().message.find("LoadMo2Failed") != std::string::npos);
    CHECK(plugins.loadMo2Calls == loadsBefore + 2);
    CHECK(plugins.trustedRootCalls == rootsBefore + 2);
    CHECK(plugins.trustedRoot == firstRoot);
    CHECK(plugins.loadedInstance != firstInstance.string());
    CHECK(settings.snapshot().mo2InstancePath == firstInstance);
    CHECK(settings.snapshot().dataFolder == firstData);
    auto rolled = target.snapshot();
    REQUIRE(rolled.has_value());
    CHECK(rolled->instance.instancePath == firstInstance);
    auto persistedAfter = store.load();
    REQUIRE(persistedAfter.has_value());
    CHECK(persistedAfter->mo2InstancePath == firstInstance);
}

TEST_CASE("AppInterop surfaces rollback trusted-root failure after backend restore") {
    const auto first = tempRoot();
    Cleanup firstCleanup{first};
    makeProfile(first);
    const auto second = tempRoot();
    Cleanup secondCleanup{second};
    makeProfile(second);
    writeFile(second / "Game" / "Data" / "Other.esp", minimalPlugin(0, "other"));
    writeFile(second / "profiles" / "Test" / "plugins.txt", "*Other.esp\n");

    fo4ide::settings::Store store(first / "settings.json");
    fo4ide::settings::SharedSettings settings;
    FakePlatform platform;
    FakePluginBackend plugins;
    fo4ide::app::AppInteropTarget target(store, settings, platform, plugins);
    REQUIRE(target.invoke("OpenMo2Profile", nlohmann::json::array({first.string()})).has_value());

    const auto firstInstance = settings.snapshot().mo2InstancePath;
    const auto firstData = settings.snapshot().dataFolder;
    const auto firstLoaded = plugins.loadedInstance;
    const auto firstRoot = plugins.trustedRoot;
    const auto loadsBefore = plugins.loadMo2Calls;
    const auto rootsBefore = plugins.trustedRootCalls;

    const auto blocker = std::filesystem::path(store.path().string() + ".tmp");
    std::filesystem::create_directory(blocker);
    plugins.failTrustedRootAt = rootsBefore + 1;
    plugins.calls.clear();
    auto failed = target.invoke("OpenMo2Profile", nlohmann::json::array({second.string()}));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().type == "SettingsWriteFailed");
    CHECK(failed.error().message.find("rollback failed") != std::string::npos);
    CHECK(failed.error().message.find("NewPluginRootInvalid") != std::string::npos);
    CHECK(plugins.loadMo2Calls == loadsBefore + 2);
    CHECK(plugins.trustedRootCalls == rootsBefore + 2);
    CHECK(plugins.loadedInstance == firstLoaded);
    CHECK(plugins.trustedRoot != firstRoot);
    CHECK(settings.snapshot().mo2InstancePath == firstInstance);
    CHECK(settings.snapshot().dataFolder == firstData);
    auto rolled = target.snapshot();
    REQUIRE(rolled.has_value());
    CHECK(rolled->instance.instancePath == firstInstance);
}
