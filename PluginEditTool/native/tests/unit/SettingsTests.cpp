#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/settings/Settings.hpp>

namespace {

std::filesystem::path tempSettingsPath() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
        ("fo4ide_settings_test_" + id) / "settings.json";
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

}

TEST_CASE("Missing settings file produces defaults") {
    const auto path = tempSettingsPath();
    Cleanup cleanup{path.parent_path()};
    fo4ide::settings::Store store(path);

    auto result = store.load();
    REQUIRE(result.has_value());
    CHECK(result->dataFolder.empty());
    CHECK_FALSE(result->readLargePluginsIntoMemory);
}

TEST_CASE("Settings preserve existing PascalCase keys and unknown values") {
    const auto path = tempSettingsPath();
    Cleanup cleanup{path.parent_path()};
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream stream(path);
        stream << R"({
  "Model": "claude-3-5-sonnet-20241022",
  "DataFolder": "/game/Data",
  "ReadLargePluginsIntoMemory": true,
  "FutureSetting": {"keep": true},
  "NiftoolPath": "/old/niftool.exe"
})";
    }

    fo4ide::settings::Store store(path);
    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->model == "claude-sonnet-4-6");
    CHECK(loaded->dataFolder == std::filesystem::path("/game/Data"));
    CHECK(loaded->readLargePluginsIntoMemory);

    // Invariant: a settings file written by an older build still loads, and its niftool key is kept rather than dropped.
    REQUIRE(loaded->extra.at("NiftoolPath") == "/old/niftool.exe");

    REQUIRE(store.save(*loaded).has_value());
    auto reloaded = store.load();
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->extra.at("FutureSetting").at("keep") == true);
    CHECK(reloaded->extra.at("NiftoolPath") == "/old/niftool.exe");
}

TEST_CASE("Settings round-trip the previs output folder and default it to blank") {
    const auto path = tempSettingsPath();
    Cleanup cleanup{path.parent_path()};
    fo4ide::settings::Store store(path);

    auto defaults = store.load();
    REQUIRE(defaults.has_value());
    CHECK(defaults->previsOutputFolder.empty());

    fo4ide::settings::Settings settings;
    settings.previsOutputFolder = path.parent_path() / "Previs Out";
    REQUIRE(store.save(settings).has_value());
    auto reloaded = store.load();
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->previsOutputFolder == path.parent_path() / "Previs Out");
    CHECK(reloaded->extra.at("PrevisOutputFolder") == (path.parent_path() / "Previs Out").string());
}

// Constraint: a hand-edited relative PrevisOutputFolder must never reach the generate output root, where it would resolve against the working directory.
TEST_CASE("Settings load ignores a relative previs output folder") {
    const auto path = tempSettingsPath();
    Cleanup cleanup{path.parent_path()};
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream stream(path);
        stream << R"({"PrevisOutputFolder": "relative/Previs"})";
    }

    fo4ide::settings::Store store(path);
    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->previsOutputFolder.empty());
}

TEST_CASE("Malformed settings fail explicitly") {
    const auto path = tempSettingsPath();
    Cleanup cleanup{path.parent_path()};
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream stream(path);
        stream << "{ definitely not json";
    }

    fo4ide::settings::Store store(path);
    auto result = store.load();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "SettingsReadFailed");
}

TEST_CASE("Settings save keeps an existing destination when the rename fails") {
    const auto path = tempSettingsPath();
    Cleanup cleanup{path.parent_path()};
    std::filesystem::create_directories(path);

    fo4ide::settings::Store store(path);
    fo4ide::settings::Settings settings;
    settings.model = "replacement";
    const auto saved = store.save(settings);
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "SettingsWriteFailed");
    CHECK(std::filesystem::is_directory(path));
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
}
