#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4/flags.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/pex/pex.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "plugin_bytes.hpp"
#include <fo4/localization/strings.hpp>
#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "../helpers/GraphTestSupport.hpp"
#include "../../src/plugin/RadiclePluginJournalSession.hpp"
#include "../../src/plugin/RadiclePluginSupport.hpp"

namespace {
using namespace fo4ide::plugin::detail;
std::vector<std::byte> bytes(std::string_view value) {
    const auto data = std::as_bytes(std::span{value.data(), value.size()});
    return {data.begin(), data.end()};
}
void write(const std::filesystem::path& path, const std::vector<std::byte>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())); REQUIRE(out.good());
}
void interrupt(std::size_t count) { if (count == 3) throw std::runtime_error("interrupted"); }
std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_quest_author_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::filesystem::path questSiblingMutation;
void addQuestSiblingDuringCommit() {
    writeBytes(questSiblingMutation, fo4::localization::write_string_table(
        {fo4::localization::StringTableKind::strings, {{31, bytes("Sibling")}}}));
}
void mutateParsedQuestSibling() {
    writeBytes(questSiblingMutation, fo4::localization::write_string_table(
        {fo4::localization::StringTableKind::ilstrings, {{900, bytes("Changed")}}}));
}
struct QuestCommitHookCleanup {
    ~QuestCommitHookCleanup() { fo4ide::plugin::setLocalizedQuestBeforeCommitHook(nullptr); fo4ide::plugin::setLocalizedQuestAfterTableParseHook(nullptr); }
};

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::vector<std::byte> emptyPlugin(std::uint32_t tes4Flags) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

std::size_t journalInterruptAt = 0;
void interruptJournal(std::size_t committed) {
    if (committed == journalInterruptAt) throw std::runtime_error("injected interruption");
}
#ifdef __linux__
void terminateJournal(std::size_t committed) { if (committed == 1) ::_exit(91); }
std::vector<std::byte> journalBytes(std::string_view text) {
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}
#endif

}

// Detects: recovery must retain localized table changes and fragment artifacts while preserving a verified sibling table.
TEST_CASE("Journal recovers plugin multiple string tables and quest scripts together") {
#ifdef __linux__
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("fo4ide_multi_journal_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto plugin = root / "Patch.esp", strings = root / "Custom" / "Strings";
    const auto psc = root / "Scripts" / "QF_Test.psc", pex = root / "Scripts" / "QF_Test.pex";
    const auto oldPlugin = fo4::test::make_tes4_plugin(0, 1.0f, {});
    const auto newPlugin = fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {});
    const auto oldText = fo4::localization::write_string_table({fo4::localization::StringTableKind::strings, {{1, bytes("Old")}}});
    const auto newText = fo4::localization::write_string_table({fo4::localization::StringTableKind::strings, {{1, bytes("Old")}, {2, bytes("Title")}}});
    const auto oldDialogue = fo4::localization::write_string_table({fo4::localization::StringTableKind::dlstrings, {{1, bytes("Old")}}});
    const auto newDialogue = fo4::localization::write_string_table({fo4::localization::StringTableKind::dlstrings, {{1, bytes("Old")}, {2, bytes("Objective")}}});
    const auto sibling = fo4::localization::write_string_table({fo4::localization::StringTableKind::strings, {{3, bytes("Sibling")}}});
    const auto oldPsc = bytes("old source"), newPsc = bytes("new source"), oldPex = bytes("old pex"), newPex = bytes("new pex");
    write(plugin, oldPlugin); write(strings / "Patch_en.STRINGS", oldText); write(strings / "Patch_en.DLSTRINGS", oldDialogue);
    write(strings / "Patch_de.STRINGS", sibling);
    write(psc, oldPsc); write(pex, oldPex);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    const auto make = [&](std::string kind, std::string name, const auto& before, const auto& after, std::vector<std::string> dirs) {
        JournalInput input{std::move(kind), std::move(name), true, fo4::io::sha256_hex(before), before.size(), after};
        if (input.kind != "plugin") { input.rootSelector = root; input.directories = std::move(dirs); }
        return input;
    };
    auto observed = make("strings", "de", sibling, std::vector<std::byte>{}, {"Custom", "Strings"}); observed.verifyOnly = true;
    const std::vector<JournalInput> files{make("plugin", "", oldPlugin, newPlugin, {}),
        make("strings", "en", oldText, newText, {"Custom", "Strings"}),
        make("dlstrings", "en", oldDialogue, newDialogue, {"Custom", "Strings"}),
        make("psc", "QF_Test", oldPsc, newPsc, {"Scripts"}), make("pex", "QF_Test", oldPex, newPex, {"Scripts"}), observed};
    { JournalSession journal(root / "journal"); setJournalCommitHook(interrupt);
      CHECK_THROWS_AS(journal.publish(roots, files), JournalRecoveryRequired); setJournalCommitHook(nullptr); }
    { JournalSession restarted(root / "journal"); CHECK_NOTHROW(restarted.recover({roots})); }
    const auto resultText = fo4::localization::read_string_table_file(strings / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    const auto resultDialogue = fo4::localization::read_string_table_file(strings / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    CHECK(fo4::localization::find_string(resultText, 2) != nullptr); CHECK(fo4::localization::find_string(resultDialogue, 2) != nullptr);
    auto matches = [](const fs::path& path, const auto& expected) { std::ifstream in(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}) == std::string(reinterpret_cast<const char*>(expected.data()), expected.size()); };
    CHECK(matches(plugin, newPlugin)); CHECK(fo4::read_plugin_file(plugin).header.tes4_flags.is_localized());
    CHECK(matches(strings / "Patch_de.STRINGS", sibling));
    CHECK(matches(psc, newPsc)); CHECK(matches(pex, newPex));
    std::error_code ec; fs::remove_all(root, ec);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: a generic edit made after a staged quest log must be saved with the quest's newly allocated localized text.
TEST_CASE("Journal saves later generic edits with a staged localized quest log") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", directory = root / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    std::filesystem::create_directories(directory);
    using Kind = fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, directory / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Old log")}}}, directory / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, directory / "Patch_en.ILSTRINGS");
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
        nlohmann::json{{"editorId", "Coalesce"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", directory.string()}}})).has_value());
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "DLSTRINGS", 20, "Edited"})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::localization::read_string_table_file(directory / "Patch_en.DLSTRINGS", Kind::dlstrings);
    const auto* edited = fo4::localization::find_string(saved, 20);
    const auto* log = fo4::localization::find_string(saved, 31);
    REQUIRE(edited != nullptr); REQUIRE(log != nullptr);
    CHECK(edited->text == bytes("Edited")); CHECK(log->text == bytes("New log"));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: a sibling language appearing after quest ID allocation must prevent staging a colliding log or objective reference.
TEST_CASE("Localized quest text rejects a sibling added after its ID scan") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", strings = root / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    std::filesystem::create_directories(strings);
    using Kind = fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, strings / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Log")}}}, strings / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, strings / "Patch_en.ILSTRINGS");
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "SiblingRace"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    std::string method, readMethod, field; nlohmann::json rows; bool afterParse = false;
    SECTION("stage log") {
        method = "SetQuestStages"; readMethod = "GetQuestStages"; field = "stages";
        rows = nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}});
    }
    SECTION("objective") {
        method = "SetQuestObjectives"; readMethod = "GetQuestObjectives"; field = "objectives";
        rows = nlohmann::json::array({{{"index", 1}, {"displayText", "New objective"}, {"targets", nlohmann::json::array()}}});
    }
    SECTION("sibling bytes change after parse") {
        method = "SetQuestStages"; readMethod = "GetQuestStages"; field = "stages"; afterParse = true;
        rows = nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}});
    }
    questSiblingMutation = strings / (afterParse ? "Patch_en.ILSTRINGS" : "Patch_de.STRINGS"); QuestCommitHookCleanup reset;
    if (afterParse) fo4ide::plugin::setLocalizedQuestAfterTableParseHook(&mutateParsedQuestSibling);
    else fo4ide::plugin::setLocalizedQuestBeforeCommitHook(&addQuestSiblingDuringCommit);
    const auto result = backend.invokeBackend(method, nlohmann::json::array({"Patch.esp", key, rows,
        nlohmann::json{{"language", "en"}, {"stringsDirectory", strings.string()}}}));
    fo4ide::plugin::setLocalizedQuestBeforeCommitHook(nullptr);
    fo4ide::plugin::setLocalizedQuestAfterTableParseHook(nullptr);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
    const auto read = backend.invokeBackend(readMethod, nlohmann::json::array({"Patch.esp", key}));
    REQUIRE(read.has_value()); CHECK(read->at(field).empty());
}

// Detects: a reference staged in a custom directory must not block editing the same ID in a different table destination.
TEST_CASE("Localized reference guards distinguish table destinations") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", normal = root / "Strings", custom = root / "Custom" / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    using Kind = fo4::localization::StringTableKind;
    for (const auto& directory : {normal, custom}) {
        std::filesystem::create_directories(directory);
        fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, directory / "Patch_en.STRINGS");
        fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Log")}}}, directory / "Patch_en.DLSTRINGS");
        fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, directory / "Patch_en.ILSTRINGS");
    }
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "CustomLocale"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", custom.string()}}})).has_value());
    REQUIRE(backend.invoke("strings.ImportStrings", nlohmann::json::array({"Patch.esp", "en", "DLSTRINGS",
        R"({"rows":[{"id":31,"text":"Other table"}]})", "merge"})).has_value());
    REQUIRE(backend.invoke("strings.RemoveString", nlohmann::json::array({"Patch.esp", "en", "DLSTRINGS", 31})).has_value());
#else
    SUCCEED("this fixture uses case-sensitive temporary paths");
#endif
}

// Detects: repeated quest edits must read and preserve pending text staged in a custom Strings directory.
TEST_CASE("Custom localized quest table is read and edited before journal save") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", custom = root / "Custom" / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    std::filesystem::create_directories(custom);
    using Kind = fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, custom / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Log")}}}, custom / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, custom / "Patch_en.ILSTRINGS");
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "CustomEdit"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    const auto set = [&](std::string_view text) { return backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", text}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", custom.string()}}})); };
    REQUIRE(set("First").has_value());
    const auto first = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key}));
    REQUIRE(first.has_value());
    CHECK(first->at("stages")[0]["logEntries"][0]["logTextId"] == 31);
    REQUIRE(set("Second").has_value());
    const auto second = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key}));
    REQUIRE(second.has_value());
    CHECK(second->at("stages")[0]["logEntries"][0]["logTextId"] == 32);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::localization::read_string_table_file(custom / "Patch_en.DLSTRINGS", Kind::dlstrings);
    REQUIRE(fo4::localization::find_string(saved, 31) != nullptr);
    REQUIRE(fo4::localization::find_string(saved, 32) != nullptr);
    CHECK(fo4::localization::find_string(saved, 31)->text == bytes("First"));
    CHECK(fo4::localization::find_string(saved, 32)->text == bytes("Second"));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: custom-directory sibling languages must influence IDs and stop staging or replay when their inventory changes.
TEST_CASE("Custom localized source inventory protects quest IDs") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", custom = root / "Custom" / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized)); std::filesystem::create_directories(custom);
    using Kind = fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, custom / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Log")}}}, custom / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, custom / "Patch_en.ILSTRINGS");
    const auto loadArgs = nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}};
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", loadArgs).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "CustomSources"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    const auto args = nlohmann::json::array({"Patch.esp", key, nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", custom.string()}}});
    SECTION("existing sibling ID") {
        fo4::localization::write_string_table_file({Kind::strings, {{500, bytes("Deutsch")}}}, custom / "Patch_de.STRINGS");
        REQUIRE(backend.invokeBackend("SetQuestStages", args).has_value());
        const auto read = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key}));
        REQUIRE(read.has_value()); CHECK(read->at("stages")[0]["logEntries"][0]["logTextId"] == 501);
    }
    SECTION("sibling appears before staging") {
        questSiblingMutation = custom / "Patch_de.STRINGS"; QuestCommitHookCleanup reset;
        fo4ide::plugin::setLocalizedQuestBeforeCommitHook(&addQuestSiblingDuringCommit);
        const auto result = backend.invokeBackend("SetQuestStages", args);
        fo4ide::plugin::setLocalizedQuestBeforeCommitHook(nullptr);
        REQUIRE_FALSE(result.has_value()); CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
    }
    SECTION("failed second edit preserves the first") {
        REQUIRE(backend.invokeBackend("SetQuestStages", args).has_value());
        auto changed = args; changed[2][0]["logEntry"] = "Changed log";
        questSiblingMutation = custom / "Patch_de.STRINGS"; QuestCommitHookCleanup reset;
        fo4ide::plugin::setLocalizedQuestBeforeCommitHook(&addQuestSiblingDuringCommit);
        const auto result = backend.invokeBackend("SetQuestStages", changed);
        fo4ide::plugin::setLocalizedQuestBeforeCommitHook(nullptr);
        REQUIRE_FALSE(result.has_value()); CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
        const auto read = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key}));
        REQUIRE(read.has_value()); CHECK(read->at("stages")[0]["logEntries"][0]["logTextId"] == 31);
        const auto text = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key, args[3]}));
        REQUIRE(text.has_value()); CHECK(text->at("stages")[0]["logEntries"][0]["logText"] == "New log");
    }
    SECTION("sibling appears before replay") {
        REQUIRE(backend.invokeBackend("SetQuestStages", args).has_value());
        journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        setJournalCommitHook(nullptr);
        REQUIRE_FALSE(saved.has_value()); CHECK(saved.error().type == "JournalRecoveryRequired");
        questSiblingMutation = custom / "Patch_de.STRINGS"; addQuestSiblingDuringCommit();
        fo4ide::plugin::RadiclePluginBackend restarted; restarted.testSetJournalRoot(root / "journal");
        const auto warned = restarted.invoke("environment.LoadMo2", loadArgs);
        REQUIRE(warned.has_value()); CHECK_FALSE(warned->at("warnings").empty());
        CHECK(readBytes(plugin) == emptyPlugin(fo4::RecordFlags::localized));
        std::filesystem::remove(questSiblingMutation);
        REQUIRE(restarted.invoke("environment.LoadMo2", loadArgs).has_value());
    }
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: case variants naming the same Windows table must share staged plugin reference guards.
TEST_CASE("Windows localized reference guard recognizes alternate path casing") {
#ifdef _WIN32
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", strings = root / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    std::filesystem::create_directories(strings);
    using Kind = fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, strings / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Log")}}}, strings / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, strings / "Patch_en.ILSTRINGS");
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "CaseAlias"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", (root / "sTRINGS").string()}}})).has_value());
    const auto first = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key}));
    REQUIRE(first.has_value()); CHECK(first->at("stages")[0]["logEntries"][0]["logTextId"] == 31);
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Second log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", strings.string()}}})).has_value());
    const auto second = backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", key}));
    REQUIRE(second.has_value()); CHECK(second->at("stages")[0]["logEntries"][0]["logTextId"] == 32);
    const auto removed = backend.invoke("strings.RemoveString", nlohmann::json::array({"Patch.esp", "en", "DLSTRINGS", 32}));
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().type == "LocalizedStringInUse");
#else
    SUCCEED("Windows case alias fixture runs on Windows");
#endif
}

// Detects: Windows hard-link aliases must be recognized as the same file without being treated as the same destination spelling.
TEST_CASE("Windows localized path identity detects hard-link aliases") {
#ifdef _WIN32
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto source = root / "Original.STRINGS", alias = root / "Alias.STRINGS";
    writeBytes(source, bytes("same underlying file"));
    std::filesystem::create_hard_link(source, alias);
    CHECK(fo4ide::plugin::support::sameLocalizedPath(source, alias));
    CHECK_FALSE(fo4ide::plugin::support::sameLocalizedSpelling(source, alias));
#else
    SUCCEED("Windows hard-link fixture runs on Windows");
#endif
}

// Detects: recovery must stop before additional writes when a new sibling language appears after journal publication.
TEST_CASE("Journal recovery rejects an unexpected sibling language") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", directory = root / "Strings";
    const auto oldPlugin = emptyPlugin(0), newPlugin = emptyPlugin(fo4::RecordFlags::localized);
    const auto oldTable = bytes("old table"), newTable = bytes("new table");
    writeBytes(plugin, oldPlugin); writeBytes(directory / "Patch_en.STRINGS", oldTable);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    table.rootSelector = root; table.directories = {"Strings"};
    JournalSourceSet sources{"strings", root, {"Strings"}, {"Patch_en.STRINGS"}};
    {
        JournalSession session(root / "journal"); journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, {target, table}, {sources}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    writeBytes(directory / "Patch_de.STRINGS", bytes("new sibling"));
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(readBytes(plugin) == oldPlugin);
        CHECK(readBytes(directory / "Patch_en.STRINGS") == newTable);
        CHECK(readBytes(directory / "Patch_de.STRINGS") == bytes("new sibling"));
        std::filesystem::remove(directory / "Patch_de.STRINGS");
        CHECK_NOTHROW(restarted.recover({roots}));
    }
    CHECK(readBytes(plugin) == newPlugin);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: a newly introduced archive provider must stop recovery before it publishes a plugin with potentially colliding IDs.
TEST_CASE("Journal recovery rejects an archive provider added after publication") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", directory = root / "Strings";
    const auto oldPlugin = emptyPlugin(0), newPlugin = emptyPlugin(fo4::RecordFlags::localized);
    const auto oldTable = bytes("old table"), newTable = bytes("new table");
    writeBytes(plugin, oldPlugin); writeBytes(directory / "Patch_en.STRINGS", oldTable);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    table.rootSelector = root; table.directories = {"Strings"};
    JournalSourceSet emptyArchives{"archive-empty", root, {}, {}};
    {
        JournalSession session(root / "journal"); journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, {target, table}, {emptyArchives}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    const auto archive = root / "Patch - Main.ba2"; writeBytes(archive, bytes("new provider"));
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(readBytes(plugin) == oldPlugin);
        CHECK(readBytes(directory / "Patch_en.STRINGS") == newTable);
        std::filesystem::remove(archive);
        CHECK_NOTHROW(restarted.recover({roots}));
    }
    CHECK(readBytes(plugin) == newPlugin);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: recovery must accept a newly created journal destination already written before interruption while still checking its expected bytes.
TEST_CASE("Journal recovery accepts its own newly created string table") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", directory = root / "Strings";
    const auto oldPlugin = emptyPlugin(0), newPlugin = emptyPlugin(fo4::RecordFlags::localized);
    const auto newTable = bytes("new table"); writeBytes(plugin, oldPlugin);
    std::filesystem::create_directories(directory);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", false, "", 0, newTable};
    table.rootSelector = root; table.directories = {"Strings"};
    JournalSourceSet sources{"strings", root, {"Strings"}, {}};
    {
        JournalSession session(root / "journal"); journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, {target, table}, {sources}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    REQUIRE(readBytes(directory / "Patch_en.STRINGS") == newTable);
    { JournalSession restarted(root / "journal"); CHECK_NOTHROW(restarted.recover({roots})); }
    CHECK(readBytes(plugin) == newPlugin);
    CHECK(readBytes(directory / "Patch_en.STRINGS") == newTable);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: an initially missing source directory must remain absent during recovery or publication must stop before further writes.
TEST_CASE("Journal recovery rejects an appearing source directory") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", missing = root / "OtherStrings";
    const auto oldPlugin = emptyPlugin(0), newPlugin = emptyPlugin(fo4::RecordFlags::localized);
    const auto oldTable = bytes("old table"), newTable = bytes("new table");
    writeBytes(plugin, oldPlugin); writeBytes(root / "Strings" / "Patch_en.STRINGS", oldTable);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    table.rootSelector = root; table.directories = {"Strings"};
    JournalSourceSet absent{"strings", root, {"OtherStrings"}, {}, false};
    {
        JournalSession session(root / "journal"); journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, {target, table}, {absent}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    writeBytes(missing / "Patch_de.STRINGS", bytes("new sibling"));
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
        CHECK(readBytes(plugin) == oldPlugin);
        CHECK(readBytes(root / "Strings" / "Patch_en.STRINGS") == newTable);
        std::filesystem::remove_all(missing);
        CHECK_NOTHROW(restarted.recover({roots}));
    }
    CHECK(readBytes(plugin) == newPlugin);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: a missing or replaced authorized root must not be accepted as an absent descendant directory.
TEST_CASE("Journal source directory absence requires the original authorized root") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto approved = root / "approved", moved = root / "moved";
    std::filesystem::create_directories(approved);
    const auto identity = JournalDestination::captureRoot(approved);
    std::filesystem::rename(approved, moved);
    SECTION("missing root") {}
    SECTION("replaced root") { std::filesystem::create_directories(approved); }
    bool rejected = false;
    try { static_cast<void>(JournalDestination::listDirectory(approved, {"Missing"}, identity)); }
    catch (const JournalDirectoryAbsent&) { FAIL("root replacement was treated as an absent descendant"); }
    catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: recovery must not write the plugin or redirect sidecars when an authorized source root disappears or is replaced.
TEST_CASE("Journal recovery rejects missing or replaced authorized source roots") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", approved = root / "approved", moved = root / "moved";
    const auto oldPlugin = emptyPlugin(0), newPlugin = emptyPlugin(fo4::RecordFlags::localized);
    const auto oldTable = bytes("old table"), newTable = bytes("new table");
    writeBytes(plugin, oldPlugin); writeBytes(approved / "Strings" / "Patch_en.STRINGS", oldTable);
    JournalRoots roots; roots.plugin = plugin; roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({approved, JournalDestination::captureRoot(approved)});
    JournalInput target{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput table{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    table.rootSelector = approved; table.directories = {"Strings"};
    JournalSourceSet sources{"strings", approved, {"Strings"}, {"Patch_en.STRINGS"}};
    {
        JournalSession session(root / "journal"); journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, {target, table}, {sources}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    std::filesystem::rename(approved, moved);
    SECTION("missing root") {}
    SECTION("replaced root") { std::filesystem::create_directories(approved / "Strings"); }
    { JournalSession restarted(root / "journal"); CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired); }
    CHECK(readBytes(plugin) == oldPlugin);
    CHECK(readBytes(moved / "Strings" / "Patch_en.STRINGS") == newTable);
    CHECK_FALSE(std::filesystem::exists(approved / "Strings" / "Patch_en.STRINGS"));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: interruption must roll forward matching source, PEX and plugin files before reopening the quest.
TEST_CASE("Quest journal recovers every interrupted publication phase") {
#ifdef __linux__
    using namespace fo4ide::plugin::detail;
    for (std::size_t interrupt = 0; interrupt <= 3; ++interrupt) {
        const auto root = tempRoot(); Cleanup cleanup{root};
        const auto plugin = root / "Patch.esp";
        const auto source = root / "QF_Test.psc";
        const auto pex = root / "QF_Test.pex";
        const auto oldPlugin = emptyPlugin(0u), newPlugin = emptyPlugin(0x80u);
        const auto oldSource = journalBytes("old source"), newSource = journalBytes("new source");
        const auto oldPex = journalBytes("old pex"), newPex = journalBytes("new pex");
        writeBytes(plugin, oldPlugin); writeBytes(source, oldSource); writeBytes(pex, oldPex);
        JournalRoots roots; roots.plugin = plugin;
        roots.scriptSourceDirectory = root; roots.scriptCompiledDirectory = root;
        roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
        roots.scriptSourceDirectoryIdentity = roots.pluginDirectoryIdentity;
        roots.scriptCompiledDirectoryIdentity = roots.pluginDirectoryIdentity;
        const auto entry = [](std::string kind, std::string name, const auto& before, const auto& after) {
            return JournalInput{std::move(kind), std::move(name), true, fo4::io::sha256_hex(before), before.size(), after};
        };
        const std::vector<JournalInput> files{entry("plugin", "", oldPlugin, newPlugin),
            entry("psc", "QF_Test", oldSource, newSource), entry("pex", "QF_Test", oldPex, newPex)};
        {
            JournalSession session(root / "journal");
            journalInterruptAt = interrupt; setJournalCommitHook(interruptJournal);
            CHECK_THROWS_AS(session.publish(roots, files), JournalRecoveryRequired);
            setJournalCommitHook(nullptr);
        }
        {
            JournalSession restarted(root / "journal");
            CHECK_NOTHROW(restarted.recover({roots}));
            CHECK_NOTHROW(restarted.recover({roots}));
        }
        CHECK(readBytes(plugin) == newPlugin);
        CHECK(readBytes(source) == newSource);
        CHECK(readBytes(pex) == newPex);
        JournalRoot journal(root / "journal");
        CHECK(journal.listFiles() == std::vector<std::string>{"global.lock"});
    }
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: process death must release the journal lock and permit recovery before plugin reopening.
TEST_CASE("Quest journal survives a process exit during publication") {
#ifdef __linux__
    using namespace fo4ide::plugin::detail;
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", source = root / "QF_Test.psc", pex = root / "QF_Test.pex";
    const auto oldPlugin = emptyPlugin(0u), newPlugin = emptyPlugin(0x80u);
    const auto oldSource = journalBytes("old source"), newSource = journalBytes("new source");
    const auto oldPex = journalBytes("old pex"), newPex = journalBytes("new pex");
    writeBytes(plugin, oldPlugin); writeBytes(source, oldSource); writeBytes(pex, oldPex);
    JournalRoots roots; roots.plugin = plugin; roots.scriptSourceDirectory = root; roots.scriptCompiledDirectory = root;
    roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.scriptSourceDirectoryIdentity = roots.pluginDirectoryIdentity;
    roots.scriptCompiledDirectoryIdentity = roots.pluginDirectoryIdentity;
    const auto entry = [](std::string kind, std::string name, const auto& before, const auto& after) {
        return JournalInput{std::move(kind), std::move(name), true, fo4::io::sha256_hex(before), before.size(), after};
    };
    const std::vector<JournalInput> files{entry("plugin", "", oldPlugin, newPlugin),
        entry("psc", "QF_Test", oldSource, newSource), entry("pex", "QF_Test", oldPex, newPex)};
    const auto child = ::fork(); REQUIRE(child >= 0);
    if (child == 0) {
        try { JournalSession session(root / "journal"); setJournalCommitHook(terminateJournal); session.publish(roots, files); }
        catch (...) { ::_exit(92); }
        ::_exit(93);
    }
    int status = 0; REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status)); REQUIRE(WEXITSTATUS(status) == 91);
    CHECK(readBytes(plugin) == oldPlugin);
    { JournalSession restarted(root / "journal"); CHECK_NOTHROW(restarted.recover({roots})); }
    CHECK(readBytes(plugin) == newPlugin); CHECK(readBytes(source) == newSource); CHECK(readBytes(pex) == newPex);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: recovery must refuse an externally changed script before publishing any remaining artifact.
TEST_CASE("Quest journal refuses a changed source during recovery") {
#ifdef __linux__
    using namespace fo4ide::plugin::detail;
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", source = root / "QF_Test.psc", pex = root / "QF_Test.pex";
    const auto oldPlugin = emptyPlugin(0u), newPlugin = emptyPlugin(0x80u);
    const auto oldSource = journalBytes("old source"), newSource = journalBytes("new source");
    const auto oldPex = journalBytes("old pex"), newPex = journalBytes("new pex");
    writeBytes(plugin, oldPlugin); writeBytes(source, oldSource); writeBytes(pex, oldPex);
    JournalRoots roots; roots.plugin = plugin; roots.scriptSourceDirectory = root; roots.scriptCompiledDirectory = root;
    roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.scriptSourceDirectoryIdentity = roots.pluginDirectoryIdentity;
    roots.scriptCompiledDirectoryIdentity = roots.pluginDirectoryIdentity;
    const auto entry = [](std::string kind, std::string name, const auto& before, const auto& after) {
        return JournalInput{std::move(kind), std::move(name), true, fo4::io::sha256_hex(before), before.size(), after};
    };
    const std::vector<JournalInput> files{entry("plugin", "", oldPlugin, newPlugin),
        entry("psc", "QF_Test", oldSource, newSource), entry("pex", "QF_Test", oldPex, newPex)};
    {
        JournalSession session(root / "journal");
        journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, files), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    writeBytes(source, journalBytes("external"));
    {
        JournalSession restarted(root / "journal");
        CHECK_THROWS_AS(restarted.recover({roots}), JournalRecoveryRequired);
    }
    CHECK(readBytes(plugin) == oldPlugin);
    CHECK(readBytes(pex) == oldPex);
    CHECK(readBytes(source) == journalBytes("external"));
    JournalRoot journal(root / "journal");
    const auto filesLeft = journal.listFiles();
    CHECK(std::any_of(filesLeft.begin(), filesLeft.end(), [](const auto& name) { return name.ends_with(".manifest"); })); CHECK(std::any_of(filesLeft.begin(), filesLeft.end(), [](const auto& name) { return name.ends_with(".payload"); }));
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: recovery must resolve arbitrary sidecar subdirectories only under an independently approved held root.
TEST_CASE("Quest journal confines recorded sidecar directories to approved roots") {
#ifdef __linux__
    using namespace fo4ide::plugin::detail;
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", folder = root / "Custom Assets" / "Strings";
    const auto table = folder / "Patch_en.STRINGS";
    const auto oldPlugin = emptyPlugin(0u), newPlugin = emptyPlugin(0x80u);
    const auto oldTable = journalBytes("old table"), newTable = journalBytes("new table");
    writeBytes(plugin, oldPlugin); writeBytes(table, oldTable);
    JournalRoots roots; roots.plugin = plugin;
    roots.pluginDirectoryIdentity = JournalDestination::captureRoot(root);
    roots.approvedRoots.push_back({root, *roots.pluginDirectoryIdentity});
    JournalInput pluginFile{"plugin", "", true, fo4::io::sha256_hex(oldPlugin), oldPlugin.size(), newPlugin};
    JournalInput sidecar{"strings", "en", true, fo4::io::sha256_hex(oldTable), oldTable.size(), newTable};
    sidecar.rootSelector = root; sidecar.directories = {"Custom Assets", "Strings"};
    {
        JournalSession session(root / "journal");
        auto escaped = sidecar; escaped.directories = {"..", "outside"};
        CHECK_THROWS(session.publish(roots, {pluginFile, escaped}));
        auto unauthorized = sidecar; unauthorized.rootSelector = root.parent_path();
        CHECK_THROWS(session.publish(roots, {pluginFile, unauthorized}));
        CHECK(readBytes(plugin) == oldPlugin); CHECK(readBytes(table) == oldTable);
        journalInterruptAt = 0; setJournalCommitHook(interruptJournal);
        CHECK_THROWS_AS(session.publish(roots, {pluginFile, sidecar}), JournalRecoveryRequired);
        setJournalCommitHook(nullptr);
    }
    { JournalSession restarted(root / "journal"); CHECK_NOTHROW(restarted.recover({roots})); }
    CHECK(readBytes(plugin) == newPlugin); CHECK(readBytes(table) == newTable);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

namespace {
void stageQuestFragment(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& root) {
    const auto plugin = root / "Patch.esp", imports = root / "imports";
    writeBytes(plugin, emptyPlugin(0u));
    std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
        nlohmann::json{{"editorId", "CoWelcome"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
    const auto actions = nlohmann::json::array({{{"kind", "stage"}, {"value", 10}}});
    REQUIRE(backend.invokeBackend("SetQuestFragment", nlohmann::json::array({"Patch.esp", key, 10, 0, "QF_CoWelcome",
        root.string(), imports.string(), actions})).has_value());
}
}

TEST_CASE("Journal publishes a staged quest fragment script and PEX") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    stageQuestFragment(backend, root);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    REQUIRE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    REQUIRE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
    const auto psc = readBytes(root / "QF_CoWelcome.psc");
    const std::string source(reinterpret_cast<const char*>(psc.data()), psc.size());
    CHECK(source.find("Function Fragment_Stage_0010_Item_00") != std::string::npos);
    CHECK(source.find("SetStage(10)") != std::string::npos);
    const auto pexBytes = readBytes(root / "QF_CoWelcome.pex");
    std::vector<std::uint8_t> pex;
    pex.reserve(pexBytes.size());
    for (const auto byte : pexBytes) pex.push_back(std::to_integer<std::uint8_t>(byte));
    const auto file = fo4::pex::PexFile::fromBytes(pex);
    REQUIRE(file.objects.size() == 1);
    CHECK(file.objects[0].name == "QF_CoWelcome");
}

TEST_CASE("Quest fragment save without a journal root is refused") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    stageQuestFragment(backend, root);
    const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "QuestFragmentSaveUnavailable");
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
}

TEST_CASE("Journal load with an absent journal root succeeds and creates nothing") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    writeBytes(root / "Patch.esp", emptyPlugin(0u));
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "absent" / "journal");
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", (root / "Patch.esp").string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    CHECK_FALSE(std::filesystem::exists(root / "absent"));
}

TEST_CASE("Journal load ignores a leftover manifest for a plugin outside the profile") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    {
        fo4ide::plugin::RadiclePluginBackend writer; writer.testSetJournalRoot(root / "journal");
        stageQuestFragment(writer, root);
        journalInterruptAt = 1; setJournalCommitHook(interruptJournal);
        const auto saved = writer.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        setJournalCommitHook(nullptr);
        REQUIRE_FALSE(saved.has_value());
    }
    REQUIRE(journalHasRecoveryFiles(root / "journal"));
    const auto plugin = root / "Other.esp", directory = root / "Strings";
    writeBytes(plugin, emptyPlugin(fo4::RecordFlags::localized));
    std::filesystem::create_directories(directory);
    using Kind = fo4::localization::StringTableKind;
    fo4::localization::write_string_table_file({Kind::strings, {{10, bytes("Name")}}}, directory / "Other_en.STRINGS");
    fo4::localization::write_string_table_file({Kind::dlstrings, {{20, bytes("Log")}}}, directory / "Other_en.DLSTRINGS");
    fo4::localization::write_string_table_file({Kind::ilstrings, {{30, bytes("Other")}}}, directory / "Other_en.ILSTRINGS");
    fo4ide::plugin::RadiclePluginBackend loader; loader.testSetJournalRoot(root / "journal");
    REQUIRE(loader.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Other.esp"}, {"path", plugin.string()}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    const auto created = loader.invokeBackend("CreateRecord", nlohmann::json::array({"Other.esp", "QUST",
        nlohmann::json{{"editorId", "CleanQuest"}}}));
    REQUIRE(created.has_value());
    const auto key = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(loader.invokeBackend("SetQuestStages", nlohmann::json::array({"Other.esp", key,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}),
        nlohmann::json{{"language", "en"}, {"stringsDirectory", directory.string()}}})).has_value());
    REQUIRE(loader.invoke("backend.SavePlugin", nlohmann::json::array({"Other.esp"})).has_value());
    CHECK(journalHasRecoveryFiles(root / "journal"));
}

TEST_CASE("Journal save sweeps orphaned authority sidecars without a manifest") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    stageQuestFragment(backend, root);
    std::vector<std::filesystem::path> orphans;
    {
        JournalRoot journal(root / "journal");
        for (char digit = '0'; digit <= '7'; ++digit) {
            const auto name = std::string(31, '0') + digit + ".authority";
            journal.writeFile(name, bytes("orphan"));
            orphans.push_back(root / "journal" / name);
        }
    }
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    for (const auto& orphan : orphans) CHECK_FALSE(std::filesystem::exists(orphan));
}
