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
    std::filesystem::create_directories(root / "Strings");
    fo4::localization::write_string_table_file(
        {StringTableKind::strings, {{7, rawBytes("Folded")}}},
        root / "Strings" / "mod_EN.strings");
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

TEST_CASE("ListStringTables and GetStrings read archives that sit beside the plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeArchive(root / "Patch - Main.ba2", "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Manzana")}}}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto tables = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "es"}));
    REQUIRE(tables.size() == 1);
    CHECK(tables[0]["source"] == "Patch - Main.ba2");

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(page["rows"].size() == 1);
    CHECK(page["rows"][0]["text"] == "Manzana");
}

TEST_CASE("A plugin folder archive shadows the data directory copy of the same name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "Data";
    writeArchive(data / "Patch - Main.ba2", "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Data copy")}}}));
    writeArchive(root / "Patch - Main.ba2", "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Manzana")}}}));
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    loadPlugin(backend, root);

    const auto tables = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "es"}));
    REQUIRE(tables.size() == 1);
    CHECK(tables[0]["source"] == "Patch - Main.ba2");

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(page["rows"].size() == 1);
    CHECK(page["rows"][0]["text"] == "Manzana");
}

TEST_CASE("A plugin folder archive shadows a data directory copy whose name differs only in case") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "Data";
    writeArchive(data / "Patch - Main.ba2", "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Data copy")}}}));
    writeArchive(root / "PATCH - MAIN.ba2", "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Manzana")}}}));
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    loadPlugin(backend, root);

    const auto tables = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "es"}));
    REQUIRE(tables.size() == 1);
    CHECK(tables[0]["source"] == "PATCH - MAIN.ba2");

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(page["rows"].size() == 1);
    CHECK(page["rows"][0]["text"] == "Manzana");
}

TEST_CASE("ListStringTables still lists loose tables when a data directory archive is unreadable") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    const auto data = root / "Data";
    std::filesystem::create_directories(data);
    {
        std::ofstream junk(data / "Patch - Junk.ba2", std::ios::binary);
        junk << "not an archive";
    }
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    loadPlugin(backend, root);

    const auto tables = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(tables.size() == 2);
    CHECK(tables[0]["source"] == "loose");
    CHECK(tables[1]["source"] == "loose");
}

TEST_CASE("String RPC arguments are validated with distinct error types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    for (const std::string language : {"e n", "../x", "en-US"}) {
        const auto refused = backend.invoke("strings.ListStringTables",
                                            nlohmann::json::array({"Patch.esp", language}));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "InvalidStringLanguage");
    }

    const auto noName = backend.invoke("strings.ListStringTables", nlohmann::json::array({"", "en"}));
    REQUIRE_FALSE(noName.has_value());
    CHECK(noName.error().type == "PluginMissing");

    const auto notLoaded = backend.invoke("strings.ListStringTables", nlohmann::json::array({"Missing.esp", ""}));
    REQUIRE_FALSE(notLoaded.has_value());
    CHECK(notLoaded.error().type == "PluginNotLoaded");

    const auto absent = backend.invoke("strings.GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "ILSTRINGS", "", 0, 200}));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().type == "StringTableUnavailable");
    CHECK(absent.error().message.find("No ILSTRINGS table for Patch.esp in language en") != std::string::npos);

    const auto badIds = nlohmann::json::array({"x", -1, 4294967296, 1.5, nullptr});
    for (const auto& id : badIds) {
        const auto refused = backend.invoke("strings.LookupString",
                                            nlohmann::json::array({"Patch.esp", "", id}));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "InvalidStringId");
    }

    const auto badLanguage = backend.invoke("strings.LookupString",
                                            nlohmann::json::array({"Patch.esp", "a b", 7}));
    REQUIRE_FALSE(badLanguage.has_value());
    CHECK(badLanguage.error().type == "InvalidStringLanguage");
}

TEST_CASE("GetStrings clamps offset and limit and falls back on non-integer paging values") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto low = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "", -5, 0}));
    CHECK(low["offset"] == 0);
    CHECK(low["limit"] == 1);
    CHECK(low["total"] == 5);
    REQUIRE(low["rows"].size() == 1);

    const auto high = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "", 2000000, -7}));
    CHECK(high["offset"] == 1000000);
    CHECK(high["limit"] == 1);
    CHECK(high["total"] == 5);
    CHECK(high["rows"].empty());

    const auto fallback = rowsOf(backend, "GetStrings", nlohmann::json::array(
        {"Patch.esp", "", "STRINGS", "", 1.5, "9"}));
    CHECK(fallback["offset"] == 0);
    CHECK(fallback["limit"] == 200);
    CHECK(fallback["rows"].size() == 5);
}

TEST_CASE("String archive cache follows the archive when it changes or disappears") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "Data";
    const auto archive = data / "Patch - Main.ba2";
    writeArchive(archive, "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Manzana")}}}));
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    loadPlugin(backend, root);

    const auto first = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(first["rows"].size() == 1);
    CHECK(first["rows"][0]["text"] == "Manzana");

    const auto original = std::filesystem::last_write_time(archive);
    writeArchive(archive, "Strings\\Patch_es.STRINGS",
                 fo4::localization::write_string_table(
                     {StringTableKind::strings, {{10, rawBytes("Pera de agua")}}}));
    std::filesystem::last_write_time(archive, original + std::chrono::seconds(2));
    const auto changed = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(changed["rows"].size() == 1);
    CHECK(changed["rows"][0]["text"] == "Pera de agua");

    std::filesystem::remove(archive);
    const auto gone = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", "es"}));
    CHECK(gone.empty());
}

TEST_CASE("A corrupt loose string table is reported as StringTableInvalid") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "Strings");
    {
        std::ofstream stream(root / "Strings" / "Patch_en.STRINGS", std::ios::binary);
        stream.write("\xE8\x03\x00\x00\x00\x00\x00\x00", 8);
    }
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto refused = backend.invoke("strings.ListStringTables", nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "StringTableInvalid");
    CHECK(refused.error().message.starts_with("could not read string tables: "));
}
