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

nlohmann::json tablesOf(fo4ide::plugin::RadiclePluginBackend& backend, nlohmann::json args) {
    return rowsOf(backend, "ListStringTables", std::move(args))["tables"];
}

#ifdef FO4IDE_TEST_HOOKS
fo4ide::plugin::RadiclePluginBackend* stringEditBackend = nullptr;
bool stringEditStagedInHook = false;

void stageStringEditInHook() {
    if (stringEditBackend == nullptr) return;
    stringEditStagedInHook = stringEditBackend->invoke(
        "strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Late edit"})).has_value();
    stringEditBackend = nullptr;
}
#endif

}

TEST_CASE("ListStringTables reports the tables present for the plugin language") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto english = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(english.size() == 2);
    CHECK(english[0]["kind"] == "STRINGS");
    CHECK(english[0]["count"] == 5);
    CHECK(english[0]["path"].get<std::string>().ends_with("Patch_en.STRINGS"));
    CHECK(english[1]["kind"] == "DLSTRINGS");
    CHECK(english[1]["count"] == 1);

    const auto german = tablesOf(backend, nlohmann::json::array({"Patch.esp", "de"}));
    REQUIRE(german.size() == 1);
    CHECK(german[0]["count"] == 1);

    const auto none = tablesOf(backend, nlohmann::json::array({"Patch.esp", "fr"}));
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

    const auto rows = tablesOf(backend, nlohmann::json::array({"mod.esp", "EN"}));
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

    const auto tables = tablesOf(backend, nlohmann::json::array({"Patch.esp", "es"}));
    REQUIRE(tables.size() == 1);
    CHECK(tables[0]["kind"] == "STRINGS");
    CHECK(tables[0]["source"] == "Patch - Main.ba2");
    CHECK(tables[0]["count"] == 1);

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "es", "STRINGS", "", 0, 200}));
    REQUIRE(page["rows"].size() == 1);
    CHECK(page["rows"][0]["text"] == "Manzana");

    const auto english = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
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

    const auto tables = tablesOf(backend, nlohmann::json::array({"Patch.esp", "es"}));
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

    const auto tables = tablesOf(backend, nlohmann::json::array({"Patch.esp", "es"}));
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

    const auto tables = tablesOf(backend, nlohmann::json::array({"Patch.esp", "es"}));
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

    const auto tables = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(tables.size() == 2);
    CHECK(tables[0]["source"] == "loose");
    CHECK(tables[1]["source"] == "loose");

    const auto skipped = rowsOf(backend, "ListStringTables", nlohmann::json::array({"Patch.esp", ""}))["skippedArchives"];
    REQUIRE(skipped.size() == 1);
    CHECK(skipped[0]["path"].get<std::string>().ends_with("Patch - Junk.ba2"));
    CHECK_FALSE(skipped[0]["reason"].get<std::string>().empty());
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
    const auto gone = tablesOf(backend, nlohmann::json::array({"Patch.esp", "es"}));
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
    CHECK(refused.error().message.find("Patch_en.STRINGS") != std::string::npos);
}

TEST_CASE("Staged string edits save as loose files and keep untouched ids byte-identical") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);
    const auto path = root / "Strings" / "Patch_en.STRINGS";
    const auto original = fo4::localization::read_string_table_file(path, StringTableKind::strings);

    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Apple tart"})).has_value());
    const auto added = rowsOf(backend, "AddString", nlohmann::json::array({"Patch.esp", "", "STRINGS", "Fig"}));
    CHECK(added["id"] == 1001);
    REQUIRE(backend.invoke("strings.RemoveString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 50})).has_value());

    const auto staged = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(staged.size() == 2);
    CHECK(staged[0]["pending"] == true);
    CHECK_FALSE(staged[1].contains("pending"));
    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(page["pending"] == true);
    REQUIRE(page["rows"].size() == 5);
    CHECK(page["rows"][0]["text"] == "Apple tart");
    CHECK(page["rows"][4]["id"] == 1001);

    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto saved = fo4::localization::read_string_table_file(path, StringTableKind::strings);
    REQUIRE(saved.entries.size() == 5);
    CHECK(fo4::localization::find_string(saved, 50) == nullptr);
    REQUIRE(fo4::localization::find_string(saved, 10) != nullptr);
    CHECK(fo4::localization::find_string(saved, 10)->text == rawBytes("Apple tart"));
    CHECK(fo4::localization::find_string(saved, 1001)->text == rawBytes("Fig"));
    for (const std::uint32_t id : {20u, 30u, 40u}) {
        REQUIRE(fo4::localization::find_string(original, id) != nullptr);
        REQUIRE(fo4::localization::find_string(saved, id) != nullptr);
        CHECK(fo4::localization::find_string(saved, id)->text == fo4::localization::find_string(original, id)->text);
    }
    const auto after = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
    CHECK_FALSE(after[0].contains("pending"));
}

TEST_CASE("Discard drops staged string edits so reads show the saved table again") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Edited"})).has_value());
    REQUIRE(backend.invoke("strings.DiscardStringEdits", nlohmann::json::array({"Patch.esp", ""})).has_value());

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(page["rows"][0]["text"] == "Apple pie");
    CHECK_FALSE(page.contains("pending"));
    const auto tables = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
    CHECK_FALSE(tables[0].contains("pending"));
}

TEST_CASE("Staged string edits block a LoadMo2 or OpenPlugin reload without discardPending instead of dropping them (regression: silent loss of staged strings on reload)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Edited"})).has_value());

    const auto path = root / "Patch.esp";
    auto environment = nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"},
        {"path", path.string()},
        {"source", "PluginStrings"},
        {"resolved", true}
    }})}};
    const auto reloaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE_FALSE(reloaded.has_value());
    CHECK(reloaded.error().type == "PluginHasPendingChanges");
    const auto reopened = backend.invoke("backend.OpenPlugin", nlohmann::json::array({path.string()}));
    REQUIRE_FALSE(reopened.has_value());
    CHECK(reopened.error().type == "PluginHasPendingChanges");
    const auto kept = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(kept["rows"][0]["text"] == "Edited");
    CHECK(kept["pending"] == true);
    const auto listed = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(listed.has_value());
    CHECK(nlohmann::json::parse(listed->get<std::string>())[0]["Pending"] == true);

    environment["discardPending"] = true;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    const auto discarded = tablesOf(backend, nlohmann::json::array({"Patch.esp", ""}));
    CHECK_FALSE(discarded[0].contains("pending"));
}

TEST_CASE("LoadMo2 discardPending refuses a string edit staged after consent instead of dropping it (regression: staged string edit made after consent was silently discarded)") {
#ifdef FO4IDE_TEST_HOOKS
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    auto environment = nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"},
        {"path", (root / "Patch.esp").string()},
        {"source", "PluginStrings"},
        {"resolved", true}
    }})}};
    environment["discardPending"] = true;

    stringEditBackend = &backend;
    fo4ide::plugin::setLoadMo2AfterPendingCheckHook(&stageStringEditInHook);
    const auto reloaded = backend.invoke("environment.LoadMo2", environment);
    fo4ide::plugin::setLoadMo2AfterPendingCheckHook(nullptr);

    CHECK(stringEditStagedInHook);
    REQUIRE_FALSE(reloaded.has_value());
    CHECK(reloaded.error().type == "PluginHasPendingChanges");
    const auto kept = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(kept["pending"] == true);
    CHECK(kept["rows"][0]["text"] == "Late edit");
#else
    SUCCEED("FO4IDE_TEST_HOOKS is required for the LoadMo2 string edit consent race");
#endif
}

TEST_CASE("DiscardPlugin on a clean plugin drops its staged string edits (regression: discardPlugin returned early and left the string edit pending)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Edited"})).has_value());

    const auto discarded = backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(discarded.has_value());

    const auto listed = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(listed.has_value());
    CHECK(nlohmann::json::parse(listed->get<std::string>())[0]["Pending"] == false);
    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(page["rows"][0]["text"] == "Apple pie");
    CHECK_FALSE(page.contains("pending"));
}

TEST_CASE("Saving string tables into the backend Data folder is refused and leaves the file unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "Data";
    writeStrings(data);
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    loadPlugin(backend, data);
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Edited"})).has_value());

    const auto refused = backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "StringSaveProtected");
    const auto onDisk = fo4::localization::read_string_table_file(data / "Strings" / "Patch_en.STRINGS", StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(onDisk, 10) != nullptr);
    CHECK(fo4::localization::find_string(onDisk, 10)->text == rawBytes("Apple pie"));
}

TEST_CASE("English string text outside Windows-1252 is refused, and Windows-1252 text is saved as one byte") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const auto refused = backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "\xCE\xA9 mega"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "StringNotEncodable");
    const auto unchanged = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(unchanged["rows"][0]["text"] == "Apple pie");
    CHECK_FALSE(unchanged.contains("pending"));

    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "", "STRINGS", 10, "Caf\xC3\xA9"})).has_value());
    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto saved = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.STRINGS", StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(saved, 10) != nullptr);
    auto expected = rawBytes("Caf");
    expected.push_back(std::byte{0xE9});
    CHECK(fo4::localization::find_string(saved, 10)->text == expected);
}

TEST_CASE("Import replace with a duplicate id is rejected and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const std::string listing = R"({"rows":[{"id":10,"text":"A"},{"id":10,"text":"B"}]})";
    const auto refused = backend.invoke("strings.ImportStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", listing, "replace"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ImportDuplicateId");

    const auto page = rowsOf(backend, "GetStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", "", 0, 200}));
    CHECK(page["total"] == 5);
    CHECK(page["rows"][0]["text"] == "Apple pie");
    CHECK_FALSE(page.contains("pending"));
}

TEST_CASE("Import merge keeps explicit ids below the next free id and exports rows in id order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeStrings(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root);

    const std::string listing = R"({"rows":[{"id":7,"text":"Seven"},{"id":10,"text":"Apple tart"}]})";
    REQUIRE(backend.invoke("strings.ImportStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS", listing, "merge"})).has_value());

    const auto exported = rowsOf(backend, "ExportStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS"}));
    CHECK(exported["kind"] == "STRINGS");
    REQUIRE(exported["rows"].size() == 6);
    CHECK(exported["rows"][0]["id"] == 7);
    CHECK(exported["rows"][0]["text"] == "Seven");
    CHECK(exported["rows"][1]["text"] == "Apple tart");
    CHECK(exported["rows"][2]["text"] == "Banana");

    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto saved = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.STRINGS", StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(saved, 7) != nullptr);
    CHECK(fo4::localization::find_string(saved, 7)->text == rawBytes("Seven"));
    REQUIRE(fo4::localization::find_string(saved, 10) != nullptr);
    CHECK(fo4::localization::find_string(saved, 10)->text == rawBytes("Apple tart"));
}
