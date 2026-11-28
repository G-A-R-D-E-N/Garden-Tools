#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4/flags.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../../src/plugin/RadiclePluginJournalSession.hpp"
#include "plugin_bytes.hpp"

namespace {
std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_string_journal_" + id);
}
struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } };
void write(const std::filesystem::path& path, const std::vector<std::byte>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary); REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())); REQUIRE(stream.good());
}
std::vector<std::byte> bytes(std::string_view text) {
    const auto span = std::as_bytes(std::span{text.data(), text.size()}); return {span.begin(), span.end()};
}
void tables(const std::filesystem::path& root) {
    using Kind = fo4::localization::StringTableKind;
    std::filesystem::create_directories(root / "Strings");
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("English")}}}, root / "Strings" / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::strings, {{20, bytes("German")}}}, root / "Strings" / "Patch_de.STRINGS");
}
nlohmann::json loadArgs(const std::filesystem::path& plugin) {
    return {{"plugins", nlohmann::json::array({{{"name", "Patch.esp"}, {"path", plugin.string()},
        {"source", "StringJournal"}, {"resolved", true}}})}};
}
#ifdef __linux__
void interruptAfterWrite(std::size_t count) { if (count == 1) throw std::runtime_error("injected interruption"); }
struct ResetHook { ~ResetHook() { fo4ide::plugin::detail::setJournalCommitHook(nullptr); } };
#endif
}

// Detects: a standalone language save must persist only selected tables and preserve unrelated plugin and language edits.
TEST_CASE("String journal saves one language without saving other pending work") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto plugin = root / "Patch.esp";
    const auto original = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    write(plugin, original); tables(root);
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", loadArgs(plugin)).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "PendingQuest"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 10, "Saved EN"})).has_value());
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "de", "STRINGS", 20, "Pending DE"})).has_value());
    const auto saved = backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", "en"}));
    REQUIRE(saved.has_value());
    const auto savedPaths = nlohmann::json::parse(saved->get<std::string>()).at("saved");
    REQUIRE(savedPaths.size() == 1); CHECK(savedPaths[0] == (root / "Strings" / "Patch_en.STRINGS").string());
    const auto disk = fo4::read_plugin_file(plugin);
    REQUIRE(disk.original_data); CHECK(*disk.original_data == original);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse(key)) == nullptr);
    REQUIRE(backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key})).has_value());
    const auto en = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    const auto de = fo4::localization::read_string_table_file(root / "Strings" / "Patch_de.STRINGS", fo4::localization::StringTableKind::strings);
    CHECK(fo4::localization::find_string(en, 10)->text == bytes("Saved EN"));
    CHECK(fo4::localization::find_string(de, 20)->text == bytes("German"));
    const auto staged = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "de", "STRINGS"}));
    REQUIRE(staged.has_value()); CHECK(nlohmann::json::parse(staged->get<std::string>())["rows"][0]["text"] == "Pending DE");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(fo4::find_record(fo4::read_plugin_file(plugin), fo4::FormKey::parse(key)) != nullptr);
    const auto savedDe = fo4::localization::read_string_table_file(root / "Strings" / "Patch_de.STRINGS", fo4::localization::StringTableKind::strings);
    CHECK(fo4::localization::find_string(savedDe, 20)->text == bytes("Pending DE"));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: interrupted standalone string publication must recover before the plugin is reopened.
TEST_CASE("String journal recovers an interrupted standalone save") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto plugin = root / "Patch.esp";
    write(plugin, fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {})); tables(root);
    const auto args = loadArgs(plugin);
    {
        fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
        REQUIRE(backend.invoke("environment.LoadMo2", args).has_value());
        REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 10, "Recovered"})).has_value());
        ResetHook reset; fo4ide::plugin::detail::setJournalCommitHook(&interruptAfterWrite);
        const auto saved = backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", "en"}));
        fo4ide::plugin::detail::setJournalCommitHook(nullptr);
        REQUIRE_FALSE(saved.has_value()); CHECK(saved.error().type == "JournalRecoveryRequired");
    }
    fo4ide::plugin::RadiclePluginBackend restarted; restarted.testSetJournalRoot(root / "journal");
    REQUIRE(restarted.invoke("environment.LoadMo2", args).has_value());
    const auto table = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    CHECK(fo4::localization::find_string(table, 10)->text == bytes("Recovered"));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: sequential standalone saves must rebase retained allocation sources so another staged language can still save safely.
TEST_CASE("String journal rebases source snapshots between language saves") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto plugin = root / "Patch.esp";
    write(plugin, fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {})); tables(root);
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", loadArgs(plugin)).has_value());
    REQUIRE(backend.invoke("strings.AddString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", "Added EN"})).has_value());
    REQUIRE(backend.invoke("strings.AddString", nlohmann::json::array({"Patch.esp", "de", "STRINGS", "Added DE"})).has_value());
    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", "en"})).has_value());
    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", "de"})).has_value());
    const auto en = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    const auto de = fo4::localization::read_string_table_file(root / "Strings" / "Patch_de.STRINGS", fo4::localization::StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(en, 21) != nullptr);
    REQUIRE(fo4::localization::find_string(de, 22) != nullptr);
    CHECK(fo4::localization::find_string(en, 21)->text == bytes("Added EN"));
    CHECK(fo4::localization::find_string(de, 22)->text == bytes("Added DE"));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: standalone string save must refuse a language required by a pending plugin reference.
TEST_CASE("String journal refuses a plugin-dependent language") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto plugin = root / "Patch.esp";
    write(plugin, fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {})); tables(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", loadArgs(plugin)).has_value());
    REQUIRE(backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "Depends"}, {"name", "Title"}}})).has_value());
    const auto saved = backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", "en"}));
    REQUIRE_FALSE(saved.has_value()); CHECK(saved.error().type == "StringEditBlockedByPendingLocalization");
}
