#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/mo2/Mo2Profile.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_mo2_test_" + std::to_string(id));
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
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

}

TEST_CASE("MO2 instance info reads selected profile and game data") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    std::filesystem::create_directories(game / "Data");
    std::filesystem::create_directories(root / "overwrite");
    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n"
        "base_directory=%BASE_DIR%\n"
        "overwrite_directory=%BASE_DIR%/overwrite\n");

    auto info = fo4ide::mo2::readInstanceInfo(root);
    REQUIRE(info.has_value());
    CHECK(info->profile == "Test");
    CHECK(info->gameDataFolder == game / "Data");
    CHECK(info->overwriteFolder.filename() == "overwrite");
    CHECK(info->modsFolder == root / "mods");
}

TEST_CASE("MO2 profile resolves plugin winners in virtual data priority") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto gameData = root / "Game" / "Data";
    const auto profile = root / "profiles" / "Test";
    const auto high = root / "mods" / "High";
    const auto low = root / "mods" / "Low";
    const auto overwrite = root / "overwrite";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + (root / "Game").string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n*OnlyHigh.esl\n");
    writeFile(profile / "modlist.txt", "+High\n+Low\n+Tools_separator\n-Disabled\n");
    writeFile(gameData / "Fallout4.esm");
    writeFile(gameData / "Example.esp", "data");
    writeFile(low / "Example.esp", "low");
    writeFile(high / "Example.esp", "high");
    writeFile(high / "OnlyHigh.esl", "high only");
    writeFile(overwrite / "Example.esp", "overwrite");

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->loadOrder.size() == 3);
    CHECK(snapshot->loadOrder[0] == "Fallout4.esm");
    CHECK(snapshot->loadOrder[1] == "Example.esp");
    CHECK(snapshot->loadOrder[2] == "OnlyHigh.esl");

    REQUIRE(snapshot->enabledModsHighToLow.size() == 2);
    CHECK(snapshot->enabledModsHighToLow[0].name == "High");
    CHECK(snapshot->enabledModsHighToLow[1].name == "Low");

    REQUIRE(snapshot->plugins.size() == 3);
    CHECK(snapshot->plugins[0].path == gameData / "Fallout4.esm");
    CHECK(snapshot->plugins[1].path == overwrite / "Example.esp");
    CHECK(snapshot->plugins[1].source == "overwrite");
    CHECK(snapshot->plugins[2].path == high / "OnlyHigh.esl");
    CHECK(snapshot->plugins[2].source == "High");
    CHECK(snapshot->missingPlugins.empty());

    REQUIRE(snapshot->assetRootsLowToHigh.size() == 4);
    CHECK(snapshot->assetRootsLowToHigh[0] == gameData);
    CHECK(snapshot->assetRootsLowToHigh[1] == low);
    CHECK(snapshot->assetRootsLowToHigh[2] == high);
    CHECK(snapshot->assetRootsLowToHigh[3] == overwrite);
}

TEST_CASE("MO2 profile resolves plugins in non-ANSI mod folders") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const std::u16string name{0x041C, 0x043E, 0x0434, 0x044B};
    const auto mod = root / "mods" / std::filesystem::path(name);
    const auto text = std::filesystem::path(name).u8string();

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + (root / "Game").string() + "\n");
    writeFile(root / "profiles" / "Test" / "plugins.txt", "*Ru.esp\n");
    writeFile(root / "profiles" / "Test" / "modlist.txt", "+" + std::string(text.begin(), text.end()) + "\n");
    writeFile(root / "Game" / "Data" / "Fallout4.esm");
    writeFile(root / "Game" / "Data" / std::filesystem::path(name + u".esp"));
    writeFile(mod / "Ru.esp", "ru");

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->plugins.size() == 2);
    CHECK(snapshot->plugins[1].path == mod / "Ru.esp");
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("MO2 profile reports unresolved enabled plugins without loading records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto gameData = root / "Game" / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + (root / "Game").string() + "\n");
    writeFile(profile / "plugins.txt", "*Missing.esp\n");
    writeFile(gameData / "Fallout4.esm");

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->missingPlugins.size() == 1);
    CHECK(snapshot->missingPlugins[0] == "Missing.esp");
}
