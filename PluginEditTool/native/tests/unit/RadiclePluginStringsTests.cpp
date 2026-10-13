#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/archive/ba2.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

using fo4::localization::StringTableKind;

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_plugin_strings_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

void writeStrings(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Strings");
    fo4::localization::write_string_table_file(
        {StringTableKind::strings,
         {{10, rawBytes("Apple pie")},
          {20, rawBytes("Banana")},
          {30, rawBytes("Cherry APPLE")},
          {40, rawBytes("Date")},
          {50, rawBytes("Elder")}}},
        root / "Strings" / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {StringTableKind::dlstrings,
         {{1000, rawBytes("Long description")}}},
        root / "Strings" / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {StringTableKind::strings,
         {{10, rawBytes("Apfel")}}},
        root / "Strings" / "Patch_de.STRINGS");
}

void writeArchive(const std::filesystem::path& path,
                  std::string_view innerPath,
                  const std::vector<std::byte>& bytes) {
    fo4::archive::Ba2GeneralArchive archive;
    fo4::archive::Ba2GeneralEntry entry;
    entry.path = std::string(innerPath);
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes));
    archive.entries.push_back(std::move(entry));
    std::filesystem::create_directories(path.parent_path());
    fo4::archive::write_ba2_general_file(archive, path);
}

void loadPlugin(fo4ide::plugin::RadiclePluginBackend& backend,
                const std::filesystem::path& root,
                const std::string& name = "Patch.esp") {
    const auto path = root / name;
    std::filesystem::create_directories(root);
    {
        const auto bytes = fo4::test::make_tes4_plugin(0x80, 1.0f, {});
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", name},
            {"path", path.string()},
            {"source", "PluginStrings"},
            {"resolved", true}
        }})}}).has_value());
}

nlohmann::json rowsOf(fo4ide::plugin::RadiclePluginBackend& backend,
                      const std::string& method,
                      nlohmann::json args) {
    const auto value = backend.invoke("strings." + method, args);
    REQUIRE(value.has_value());
    return nlohmann::json::parse(value->get<std::string>());
}

}

TEST_CASE("ListStringTables reports the tables present for the plugin language") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto english = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(english.size() == 2);
    CHECK(english[0]["kind"] == "STRINGS");
    CHECK(english[0]["count"] == 5);
    CHECK(english[0]["path"].get<std::string>().ends_with("Patch_en.STRINGS"));
    CHECK(english[1]["kind"] == "DLSTRINGS");
    CHECK(english[1]["count"] == 1);

    const auto german = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "de"}));
    REQUIRE(german.size() == 1);
    CHECK(german[0]["count"] == 1);

    const auto none = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "fr"}));
    CHECK(none.empty());

    CHECK_FALSE(backend.invoke("strings.ListStringTables",
                               nlohmann::json::array({"Patch.esp", "e n"})).has_value());
    CHECK_FALSE(backend.invoke("strings.ListStringTables",
                               nlohmann::json::array({"Missing.esp", ""})).has_value());
}

TEST_CASE("GetStrings filters by text or id, pages, and caps the limit") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto byText = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "apple", 0, 200}));
    CHECK(byText["total"] == 2);
    REQUIRE(byText["rows"].size() == 2);
    CHECK(byText["rows"][0]["id"] == 10);
    CHECK(byText["rows"][0]["text"] == "Apple pie");
    CHECK(byText["rows"][1]["id"] == 30);

    const auto byId = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "40", 0, 200}));
    CHECK(byId["total"] == 1);
    REQUIRE(byId["rows"].size() == 1);
    CHECK(byId["rows"][0]["text"] == "Date");

    const auto paged = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "", 1, 2}));
    CHECK(paged["total"] == 5);
    CHECK(paged["offset"] == 1);
    REQUIRE(paged["rows"].size() == 2);
    CHECK(paged["rows"][0]["id"] == 20);
    CHECK(paged["rows"][1]["id"] == 30);

    const auto capped = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "", 0, 100000}));
    CHECK(capped["limit"] == 1000);

    const auto dl = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "DLSTRINGS", "", 0, 200}));
    REQUIRE(dl["rows"].size() == 1);
    CHECK(dl["rows"][0]["text"] == "Long description");

    CHECK_FALSE(backend.invoke("strings.GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "ILSTRINGS", "", 0, 200})).has_value());
}

TEST_CASE("LookupString returns the text for an id in each table that has it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto found = rowsOf(backend, "LookupString", nlohmann::json::array({"Patch.esp", "", 20}));
    REQUIRE(found.size() == 1);
    CHECK(found[0]["kind"] == "STRINGS");
    CHECK(found[0]["text"] == "Banana");

    const auto german = rowsOf(backend, "LookupString", nlohmann::json::array({"Patch.esp", "de", 10}));
    REQUIRE(german.size() == 1);
    CHECK(german[0]["text"] == "Apfel");

    const auto missing = rowsOf(backend, "LookupString", nlohmann::json::array({"Patch.esp", "", 999}));
    CHECK(missing.empty());

    CHECK_FALSE(backend.invoke("strings.LookupString",
                               nlohmann::json::array({"Patch.esp", "", "x"})).has_value());
}

TEST_CASE("ListStringTables matches loose table names case-insensitively") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "strings");
    fo4::localization::write_string_table_file(
        {StringTableKind::strings, {{7, rawBytes("Folded")}}},
        root / "strings" / "mod_EN.strings");
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root, "mod.esp");

    const auto rows = rowsOf(backend, "ListStringTables", nlohmann::json::array({"mod.esp", "EN"}));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0]["kind"] == "STRINGS");
    CHECK(rows[0]["source"] == "loose");
    CHECK(rows[0]["path"].get<std::string>().ends_with("mod_EN.strings"));

    const auto text = rowsOf(backend, "LookupString", nlohmann::json::array({"mod.esp", "EN", 7}));
    REQUIRE(text.size() == 1);
    CHECK(text[0]["text"] == "Folded");
}

TEST_CASE("ListStringTables and GetStrings read tables packed in the data directory's archives") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "Data";
    writeArchive(data / "Patch - Main.ba2", "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Manzana")}}}));
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    loadPlugin(backend, root);

    const auto tables = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "es"}));
    REQUIRE(tables.size() == 1);
    CHECK(tables[0]["kind"] == "STRINGS");
    CHECK(tables[0]["source"] == "Patch - Main.ba2");
    CHECK(tables[0]["count"] == 1);

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(page["rows"].size() == 1);
    CHECK(page["rows"][0]["text"] == "Manzana");

    const auto english = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", ""}));
    CHECK(english.empty());
}
