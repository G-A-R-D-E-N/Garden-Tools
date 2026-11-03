#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/assets.hpp>
#include <fo4/archive/ba2.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4ide/plugin/RadiclePluginStringLanguages.hpp>

namespace {

std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() /
           ("fo4ide_string_languages_" + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeFile(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).put('x');
}

void writeArchive(const std::filesystem::path& path, std::string_view entryPath,
                  bool names = true) {
    fo4::archive::Ba2GeneralArchive archive;
    archive.has_string_table = names;
    fo4::archive::Ba2GeneralEntry entry;
    entry.path = std::string(entryPath);
    archive.entries.push_back(std::move(entry));
    std::filesystem::create_directories(path.parent_path());
    fo4::archive::write_ba2_general_file(archive, path);
}

// Detects: language discovery must use the same Unicode filename folding as the table resolver.
TEST_CASE("string language discovery case-folds Unicode plugin stems") {
    Cleanup cleanup{tempRoot()};
    const auto directory = cleanup.root / "Strings";
    writeFile(directory / fo4::io::from_utf8("\xC3\xA4" "Patch_de.STRINGS"));
    const std::vector<std::filesystem::path> directories{directory};
    const std::vector<std::string_view> staged{};
    const auto found = fo4ide::plugin::discoverPluginStringLanguages(
        "\xC3\x84" "Patch.esp", directories, nullptr, staged);
    REQUIRE(found);
    CHECK(*found == std::set<std::string>{"de"});
}

// Detects: an archive entry whose Unicode case cannot be resolved by the asset lookup must not be treated as an available language.
TEST_CASE("string language discovery refuses Unicode archive case mismatches") {
    Cleanup cleanup{tempRoot()};
    const auto archive = cleanup.root / "Provider.ba2";
    writeArchive(archive, "Strings/\xC3\xA4" "Patch_de.STRINGS");
    const std::vector<std::filesystem::path> archives{archive}, directories{};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root, archives);
    const std::vector<std::string_view> staged{};
    const auto found = fo4ide::plugin::discoverPluginStringLanguages(
        "\xC3\x84" "Patch.esp", directories, &assets, staged);
    REQUIRE_FALSE(found);
    CHECK(found.error().type == "LocalizedSourcesUnavailable");
}

TEST_CASE("string language discovery combines approved folders, applicable archives, and staged tokens") {
    // Detects: discovery must retain locales supplied by local and Data files, BA2 entries, and staged edits.
    Cleanup cleanup{tempRoot()};
    const auto local = cleanup.root / "mod" / "Strings";
    const auto data = cleanup.root / "Data" / "Strings";
    writeFile(local / "patch_EN.STRINGS");
    writeFile(data / "Patch_de.DLSTRINGS");
    writeFile(local / "Other_fr.STRINGS");
    const auto archivePath = cleanup.root / "Data" / "Patch - Main.ba2";
    writeArchive(archivePath, "sTrInGs\\PATCH_it.IlStRiNgS");
    const std::vector<std::filesystem::path> archives{archivePath};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root / "Data", archives);
    const std::vector<std::filesystem::path> dirs{local, data};
    const std::vector<std::string_view> staged{"ja"};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, &assets, staged);
    REQUIRE(languages);
    CHECK(*languages == std::set<std::string>{"de", "en", "it", "ja"});
}

TEST_CASE("missing optional string directories and a null resolver are empty sources") {
    // Detects: absent optional sources must be treated as empty.
    Cleanup cleanup{tempRoot()};
    const std::vector<std::filesystem::path> dirs{cleanup.root / "missing"};
    const std::vector<std::string_view> staged{};
    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, nullptr, staged);
    REQUIRE(languages);
    CHECK(languages->empty());
}

TEST_CASE("a nameless applicable archive fails closed") {
    // Detects: discovery must refuse an archive without entry names.
    Cleanup cleanup{tempRoot()};
    const auto archivePath = cleanup.root / "Patch - Main.ba2";
    writeArchive(archivePath, "", false);
    const std::vector<std::filesystem::path> archives{archivePath};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root, archives);
    const std::vector<std::filesystem::path> dirs{};
    const std::vector<std::string_view> staged{};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, &assets, staged);
    REQUIRE_FALSE(languages);
    CHECK(languages.error().type == "LocalizedSourcesUnavailable");
}

TEST_CASE("a named unrelated archive can provide a plugin string table") {
    // Detects: discovery must include localized tables supplied by differently named archives.
    Cleanup cleanup{tempRoot()};
    const auto archivePath = cleanup.root / "Shared - Textures.ba2";
    writeArchive(archivePath, "Strings\\Patch_pt.STRINGS");
    const std::vector<std::filesystem::path> archives{archivePath};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root, archives);
    const std::vector<std::filesystem::path> dirs{};
    const std::vector<std::string_view> staged{};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, &assets, staged);
    REQUIRE(languages);
    CHECK(*languages == std::set<std::string>{"pt"});
}

TEST_CASE("a nameless unrelated archive fails closed") {
    // Detects: discovery must refuse uninspectable archives that could supply a table.
    Cleanup cleanup{tempRoot()};
    const auto archivePath = cleanup.root / "Shared - Textures.ba2";
    writeArchive(archivePath, "", false);
    const std::vector<std::filesystem::path> archives{archivePath};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root, archives);
    const std::vector<std::filesystem::path> dirs{};
    const std::vector<std::string_view> staged{};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, &assets, staged);
    REQUIRE_FALSE(languages);
    CHECK(languages.error().type == "LocalizedSourcesUnavailable");
}

TEST_CASE("an archive that becomes unreadable before discovery fails closed") {
    // Detects: discovery must reject an archive that became unreadable.
    Cleanup cleanup{tempRoot()};
    const auto archivePath = cleanup.root / "Shared - Textures.ba2";
    writeArchive(archivePath, "Strings\\Patch_pt.STRINGS");
    const std::vector<std::filesystem::path> archives{archivePath};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root, archives);
    std::ofstream(archivePath, std::ios::binary | std::ios::trunc).put('x');
    const std::vector<std::filesystem::path> dirs{};
    const std::vector<std::string_view> staged{};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, &assets, staged);
    REQUIRE_FALSE(languages);
    CHECK(languages.error().type == "LocalizedSourcesUnavailable");
}

TEST_CASE("a replaced named archive fails closed against the resolver snapshot") {
    // Detects: discovery must reject an archive index replaced after resolver creation.
    Cleanup cleanup{tempRoot()};
    const auto archivePath = cleanup.root / "Shared - Textures.ba2";
    writeArchive(archivePath, "Strings\\Patch_pt.STRINGS");
    const std::vector<std::filesystem::path> archives{archivePath};
    const auto assets = fo4::archive::AssetResolver::open(cleanup.root, archives);
    writeArchive(archivePath, "Strings\\Patch_sv.STRINGS");
    const std::vector<std::filesystem::path> dirs{};
    const std::vector<std::string_view> staged{};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, &assets, staged);
    REQUIRE_FALSE(languages);
    CHECK(languages.error().type == "LocalizedSourcesUnavailable");
}

TEST_CASE("case-folded duplicate tables in one strings directory fail closed") {
    // Detects: discovery must reject case-insensitive duplicate table paths.
    Cleanup cleanup{tempRoot()};
    const auto strings = cleanup.root / "Strings";
    writeFile(strings / "Patch_en.STRINGS");
    writeFile(strings / "patch_EN.strings");
    std::error_code ec;
    if (std::filesystem::equivalent(strings / "Patch_en.STRINGS", strings / "patch_EN.strings", ec)) {
        SKIP("the filesystem cannot represent case-distinct entries in one directory");
    }
    const std::vector<std::filesystem::path> dirs{strings};
    const std::vector<std::string_view> staged{};

    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, nullptr, staged);
    REQUIRE_FALSE(languages);
    CHECK(languages.error().type == "LocalizedSourcesUnavailable");
}

TEST_CASE("invalid staged language tokens fail closed") {
    // Detects: discovery must reject staged locale tokens unsafe for table filenames.
    const std::vector<std::filesystem::path> dirs{};
    const std::vector<std::string_view> staged{"en-US"};
    const auto languages = fo4ide::plugin::discoverPluginStringLanguages(
        "Patch.esp", dirs, nullptr, staged);
    REQUIRE_FALSE(languages);
    CHECK(languages.error().type == "LocalizedSourcesUnavailable");
}

}
