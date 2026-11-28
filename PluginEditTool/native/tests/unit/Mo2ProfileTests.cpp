#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/mo2/Mo2Profile.hpp>

#include "../helpers/PluginFixtures.hpp"
#include "../helpers/TempRoot.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace plugin_fixtures_test;

namespace {

std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("fo4ide_mo2_test_" + fo4ide_test::uniqueTempName());
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

std::string withHedrVersion(std::string bytes, std::uint32_t floatBits) {
    const auto pos = bytes.find("HEDR");
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[pos + 6 + i] = static_cast<char>((floatBits >> (8 * i)) & 0xFFu);
    }
    return bytes;
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
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "data"));
    writeFile(low / "Example.esp", minimalPlugin(0, "low"));
    writeFile(high / "Example.esp", minimalPlugin(0, "high"));
    writeFile(high / "OnlyHigh.esl", minimalPlugin(0x200, "high only"));
    writeFile(overwrite / "Example.esp", minimalPlugin(0, "overwrite"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->loadOrder.size() == 3);
    CHECK(snapshot->loadOrder[0] == "Fallout4.esm");
    CHECK(snapshot->loadOrder[1] == "OnlyHigh.esl");
    CHECK(snapshot->loadOrder[2] == "Example.esp");

    REQUIRE(snapshot->enabledModsHighToLow.size() == 2);
    CHECK(snapshot->enabledModsHighToLow[0].name == "High");
    CHECK(snapshot->enabledModsHighToLow[1].name == "Low");

    REQUIRE(snapshot->plugins.size() == 3);
    CHECK(snapshot->plugins[0].path == gameData / "Fallout4.esm");
    CHECK(snapshot->plugins[1].path == high / "OnlyHigh.esl");
    CHECK(snapshot->plugins[1].source == "High");
    CHECK(snapshot->plugins[2].path == overwrite / "Example.esp");
    CHECK(snapshot->plugins[2].source == "overwrite");
    CHECK(snapshot->missingPlugins.empty());

    REQUIRE(snapshot->assetRootsLowToHigh.size() == 4);
    CHECK(snapshot->assetRootsLowToHigh[0] == gameData);
    CHECK(snapshot->assetRootsLowToHigh[1] == low);
    CHECK(snapshot->assetRootsLowToHigh[2] == high);
    CHECK(snapshot->assetRootsLowToHigh[3] == overwrite);
}

TEST_CASE("MO2 profile loads official masters and Creation Club plugins missing from plugins.txt") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n");
    writeFile(game / "Fallout4.ccc", "ccTest.esl\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "DLCRobot.esm", minimalPlugin(0x1, "robot"));
    writeFile(gameData / "ccTest.esl", minimalPlugin(0x200, "cc"));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm", "DLCRobot.esm", "ccTest.esl", "Example.esp"};
    CHECK(snapshot->loadOrder == expected);
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("MO2 profile assembles an OG-style install from the official DLC masters alone") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "DLCRobot.esm", minimalPlugin(0x1, "robot", {"Fallout4.esm"}));
    writeFile(gameData / "DLCworkshop01.esm", minimalPlugin(0x1, "workshop01", {"Fallout4.esm"}));
    writeFile(gameData / "DLCCoast.esm", minimalPlugin(0x1, "coast", {"Fallout4.esm"}));
    writeFile(gameData / "DLCworkshop02.esm", minimalPlugin(0x1, "workshop02", {"Fallout4.esm"}));
    writeFile(gameData / "DLCworkshop03.esm", minimalPlugin(0x1, "workshop03", {"Fallout4.esm"}));
    writeFile(gameData / "DLCNukaWorld.esm", minimalPlugin(0x1, "nuka", {"Fallout4.esm"}));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example", {"Fallout4.esm"}));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
        "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm", "Example.esp"};
    CHECK(snapshot->loadOrder == expected);
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("MO2 profile assembles an OG-style install with a partial DLC set listed in plugins.txt") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*DLCRobot.esm\n*DLCCoast.esm\n*Example.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "DLCRobot.esm", minimalPlugin(0x1, "robot", {"Fallout4.esm"}));
    writeFile(gameData / "DLCworkshop01.esm", minimalPlugin(0x1, "workshop01", {"Fallout4.esm"}));
    writeFile(gameData / "DLCCoast.esm", minimalPlugin(0x1, "coast", {"Fallout4.esm"}));
    writeFile(gameData / "DLCworkshop02.esm", minimalPlugin(0x1, "workshop02", {"Fallout4.esm"}));
    writeFile(gameData / "DLCworkshop03.esm", minimalPlugin(0x1, "workshop03", {"Fallout4.esm"}));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example", {"Fallout4.esm"}));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
        "DLCworkshop02.esm", "DLCworkshop03.esm", "Example.esp"};
    CHECK(snapshot->loadOrder == expected);
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("MO2 profile keeps inactive plugin list entries unloaded") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "Inactive.esp\n*Active.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Inactive.esp", minimalPlugin(0, "inactive"));
    writeFile(gameData / "Active.esp", minimalPlugin(0, "active"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm", "Active.esp"};
    CHECK(snapshot->loadOrder == expected);
    CHECK(snapshot->plugins.size() == 2);
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("MO2 profile resolves plugins in non-ANSI mod folders") {
#if defined(_WIN32)
    REQUIRE(GetACP() == CP_UTF8);
#endif
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
    writeFile(root / "Game" / "Data" / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(root / "Game" / "Data" / std::filesystem::path(name + u".esp"), minimalPlugin(0, "data"));
    writeFile(mod / "Ru.esp", minimalPlugin(0, "ru"));

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
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->missingPlugins.size() == 1);
    CHECK(snapshot->missingPlugins[0] == "Missing.esp");
}

TEST_CASE("MO2 profile reports an active plugin whose master is absent") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example", {"Absent.esm"}));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->missingPlugins.size() == 1);
    CHECK(snapshot->missingPlugins[0] == "Example.esp");
    const std::vector<std::string> expected{"Fallout4.esm"};
    CHECK(snapshot->loadOrder == expected);
}

TEST_CASE("MO2 profile refuses a load order when a checked master is missing its own master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Mid.esm", minimalPlugin(0x1, "mid", {"Absent.esm"}));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example", {"Mid.esm"}));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().type == "Mo2LoadOrderInvalid");
    CHECK(snapshot.error().message.find("Mid.esm: master Absent.esm is not in Data") != std::string::npos);
}

TEST_CASE("MO2 profile keeps the higher-priority winner when names differ only by non-ASCII case") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";
    const auto high = root / "mods" / "High";
    const auto low = root / "mods" / "Low";
    const std::filesystem::path upper(std::u16string{0x00C4, u'.', u'e', u's', u'p'});
    const std::filesystem::path lower(std::u16string{0x00E4, u'.', u'e', u's', u'p'});

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*\xC4.esp\n");
    writeFile(profile / "modlist.txt", "+High\n+Low\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(high / upper, minimalPlugin(0, "high"));
    writeFile(low / lower, minimalPlugin(0, "low"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->plugins.size() == 2);
    CHECK(snapshot->plugins[1].path == high / upper);
    CHECK(snapshot->plugins[1].source == "High");
    CHECK(snapshot->missingPlugins.empty());
}

TEST_CASE("MO2 profile checks a plugins.txt name that differs from the file only by non-ASCII case") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";
    const std::filesystem::path upper(std::u16string{0x00C4, u'.', u'e', u's', u'p'});

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*\xE4.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / upper, minimalPlugin(0, "upper"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->missingPlugins.empty());
    const std::vector<std::string> expected{"Fallout4.esm", "\xC3\x84.esp"};
    CHECK(snapshot->loadOrder == expected);
}

TEST_CASE("MO2 profile surfaces a plugins.txt control-character line as a warning and keeps it unloaded") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*MyMod.esp\t\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "MyMod.esp", minimalPlugin(0, "mymod"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    bool reported = false;
    for (const auto& warning : snapshot->warnings) {
        reported = reported || warning.find("line 1 has a control character") != std::string::npos;
    }
    CHECK(reported);
    const std::vector<std::string> expected{"Fallout4.esm"};
    CHECK(snapshot->loadOrder == expected);
}

TEST_CASE("MO2 test roots are distinct and start absent so cases never share files") {
    std::set<std::filesystem::path> seen;
    for (int i = 0; i < 1000; ++i) {
        const auto root = tempRoot();
        CHECK_FALSE(std::filesystem::exists(root));
        CHECK(seen.insert(root).second);
    }
}

TEST_CASE("MO2 profile refuses a profile without plugins.txt") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "modlist.txt", "+High\n");

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().type == "Mo2PluginsMissing");
    CHECK(snapshot.error().message == "plugins.txt not found for profile 'Test'");
}

TEST_CASE("MO2 profile refuses a load order whose checked plugin has a master with HEDR version above 1.0") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Mid.esm", withHedrVersion(minimalPlugin(0x1, "mid"), 0x3FC00000u));
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example", {"Mid.esm"}));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().type == "Mo2LoadOrderInvalid");
    CHECK(snapshot.error().message.find("Example.esp: master Mid.esm has HEDR version above 1.0") != std::string::npos);
}

TEST_CASE("MO2 profile warns and leaves unchecked a plugin with HEDR version above 1.0") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Big.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Big.esp", withHedrVersion(minimalPlugin(0, "big"), 0x3FC00000u));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> warnings{"Big.esp: HEDR version is above 1.0; unchecked"};
    CHECK(snapshot->warnings == warnings);
    const std::vector<std::string> loadOrder{"Fallout4.esm"};
    CHECK(snapshot->loadOrder == loadOrder);
    const std::vector<std::string> missing{"Big.esp"};
    CHECK(snapshot->missingPlugins == missing);
}

TEST_CASE("MO2 profile reports a plugin with an unreadable header as a warning") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Broken.esp\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "Broken.esp", "garbage");

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> warnings{"Broken.esp: unable to read the TES4 record header"};
    CHECK(snapshot->warnings == warnings);
    const std::vector<std::string> loadOrder{"Fallout4.esm"};
    CHECK(snapshot->loadOrder == loadOrder);
    const std::vector<std::string> missing{"Broken.esp"};
    CHECK(snapshot->missingPlugins == missing);
}

TEST_CASE("MO2 profile refuses masters that form a cycle") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*A.esm\n*B.esm\n");
    writeFile(gameData / "Fallout4.esm", minimalPlugin(0x1, "base"));
    writeFile(gameData / "A.esm", minimalPlugin(0x1, "a", {"B.esm"}));
    writeFile(gameData / "B.esm", minimalPlugin(0x1, "b", {"A.esm"}));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE_FALSE(snapshot.has_value());
    CHECK(snapshot.error().type == "Mo2LoadOrderInvalid");
    CHECK(snapshot.error().message.find("masters form a cycle") != std::string::npos);
}

TEST_CASE("MO2 profile reports Fallout4.esm missing when no root has it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto game = root / "Game";
    const auto gameData = game / "Data";
    const auto profile = root / "profiles" / "Test";

    writeFile(root / "ModOrganizer.ini",
        "selected_profile=Test\n"
        "gamePath=" + game.string() + "\n");
    writeFile(profile / "plugins.txt", "*Example.esp\n");
    writeFile(gameData / "Example.esp", minimalPlugin(0, "example"));

    auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> missing{"Fallout4.esm"};
    CHECK(snapshot->missingPlugins == missing);
    REQUIRE(snapshot->plugins.size() == 2);
    CHECK(snapshot->plugins[0].name == "Fallout4.esm");
    CHECK(snapshot->plugins[0].path.empty());
    CHECK_FALSE(snapshot->plugins[0].resolved);
}
