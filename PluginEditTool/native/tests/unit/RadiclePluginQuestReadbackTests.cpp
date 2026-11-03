#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_quest_readback_" + std::to_string(id));
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

void writeU32Subrecord(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> localizedQuestPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::localized, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ReadbackQuest\0", 14});
    fo4::test::ByteWriter index;
    index.u16(10);
    index.u8(0);
    index.u8(0);
    fo4::test::write_subrecord(out, "INDX", index.bytes);
    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
    writeU32Subrecord(out, "CNAM", 42);

    fo4::test::ByteWriter objective;
    objective.u16(20);
    fo4::test::write_subrecord(out, "QOBJ", objective.bytes);
    writeU32Subrecord(out, "FNAM", 0);
    writeU32Subrecord(out, "NNAM", 84);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeTables(const std::filesystem::path& root) {
    const auto strings = root / "Strings";
    std::filesystem::create_directories(strings);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{84, {std::byte{'O'}, std::byte{'l'}, std::byte{'d'}}}}},
        strings / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, {{42, {std::byte{'O'}, std::byte{'l'}, std::byte{'d'}}}}},
        strings / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, {}},
        strings / "Patch_en.ILSTRINGS");
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    nlohmann::json plugin{{"name", "Patch.esp"}, {"path", path.string()},
                          {"source", "QuestReadback"}, {"resolved", true}};
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", {plugin}}}).has_value());
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    return result->is_string() ? nlohmann::json::parse(result->get<std::string>()) : *result;
}

}  // namespace

// Detects: localized quest reads must resolve staged and saved text while preserving the existing no-context response.
TEST_CASE("Localized quest readback resolves stage DLSTRINGS and objective STRINGS while preserving ID-only reads") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, localizedQuestPlugin());
    writeTables(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto stagesWithoutContext = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    REQUIRE(stagesWithoutContext.at("stages").size() == 1u);
    CHECK(stagesWithoutContext["stages"][0]["logEntries"][0]["logTextId"] == 42);

    const auto objectivesWithoutContext = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    REQUIRE(objectivesWithoutContext.at("objectives").size() == 1u);
    CHECK_FALSE(objectivesWithoutContext["objectives"][0].contains("displayTextId"));
    CHECK(objectivesWithoutContext["objectives"][0]["displayText"] == "");

    const nlohmann::json context{{"language", "en"}, {"stringsDirectory", (root / "Strings").string()}};
    const auto stages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(stages["stages"][0]["logEntries"][0]["logText"] == "Old");

    const auto objectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(objectives["objectives"][0]["displayText"] == "Old");

    const nlohmann::json mixedCaseContext{{"language", "EN"},
                                          {"stringsDirectory", (root / "Strings").string()}};
    const auto mixedCaseStages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", mixedCaseContext})));
    CHECK(mixedCaseStages["stages"][0]["logEntries"][0]["logText"] == "Old");
    const auto mixedCaseObjectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", mixedCaseContext})));
    CHECK(mixedCaseObjectives["objectives"][0]["displayText"] == "Old");

    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({
        "Patch.esp", "en", "DLSTRINGS", 42, "Pending stage"})).has_value());
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({
        "Patch.esp", "en", "STRINGS", 84, "Pending objective"})).has_value());
    const auto staged = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(staged["stages"][0]["logEntries"][0]["logText"] == "Pending stage");
    const auto stagedObjectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(stagedObjectives["objectives"][0]["displayText"] == "Pending objective");

#ifdef __linux__
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend reloaded;
    loadPatch(reloaded, path);
    const auto saved = parseResult(reloaded.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(saved["stages"][0]["logEntries"][0]["logText"] == "Pending stage");
    const auto savedObjectives = parseResult(reloaded.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(savedObjectives["objectives"][0]["displayText"] == "Pending objective");
#endif
}

// Detects: localized quest readback must reject malformed tables and untrusted explicit directories.
TEST_CASE("Localized quest readback rejects malformed tables and untrusted explicit directories") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, localizedQuestPlugin());
    writeTables(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto context = nlohmann::json{{"language", "en"}, {"stringsDirectory", (root / "Strings").string()}};
    REQUIRE(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})).has_value());

    const auto invalid = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", nlohmann::json{{"language", "xx"}}}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidLocalizedQuestContext");

    const auto untrustedRoot = tempRoot();
    Cleanup untrustedCleanup{untrustedRoot};
    const auto untrusted = backend.invokeBackend("GetQuestStages", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", nlohmann::json{{"language", "en"},
            {"stringsDirectory", (untrustedRoot / "Strings").string()}}}));
    REQUIRE_FALSE(untrusted.has_value());
    CHECK(untrusted.error().type == "UntrustedStringTableDirectory");

    std::filesystem::remove(root / "Strings" / "Patch_en.DLSTRINGS");
    const auto missing = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context}));
    REQUIRE_FALSE(missing.has_value());

    writeBytes(root / "Strings" / "Patch_en.DLSTRINGS", {std::byte{0}});
    const auto malformed = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context}));
    REQUIRE_FALSE(malformed.has_value());
    writeTables(root);
    std::filesystem::remove(root / "Strings" / "Patch_en.STRINGS");
    REQUIRE_FALSE(backend.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})).has_value());
    writeBytes(root / "Strings" / "Patch_en.STRINGS", {std::byte{0}});
    REQUIRE_FALSE(backend.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})).has_value());
}

// Detects: localized stage and objective reads must decode staged non-ASCII English text without corrupting it.
TEST_CASE("Localized quest readback preserves non-ASCII English text") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp"; writeBytes(plugin, localizedQuestPlugin()); writeTables(root);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, plugin);
    const std::string text = "Caf\xC3\xA9";
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "EN", "DLSTRINGS", 42, text})).has_value());
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 84, text})).has_value());
    const auto context = nlohmann::json{{"language", "En"}};
    const auto stage = parseResult(backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    const auto objective = parseResult(backend.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(stage["stages"][0]["logEntries"][0]["logText"] == text);
    CHECK(objective["objectives"][0]["displayText"] == text);
}

// Detects: custom-directory quest reads must resolve staged text from exact tables and refuse saved-file reads without held-root authority.
TEST_CASE("Localized quest readback overlays staged custom tables") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", custom = root / "Custom";
    writeBytes(plugin, localizedQuestPlugin()); writeTables(root); writeTables(custom);
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal"); loadPatch(backend, plugin);
    const auto context = nlohmann::json{{"language", "en"}, {"stringsDirectory", (custom / "Strings").string()}};
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp",
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Custom staged log"}}}), context})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp",
        nlohmann::json::array({{{"index", 20}, {"displayText", "Custom staged objective"}, {"targets", nlohmann::json::array()}}}), context})).has_value());
    const auto stages = parseResult(backend.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    const auto objectives = parseResult(backend.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    CHECK(stages["stages"][0]["logEntries"][0]["logText"] == "Custom staged log");
    CHECK(objectives["objectives"][0]["displayText"] == "Custom staged objective");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend reloaded; reloaded.testSetJournalRoot(root / "journal"); loadPatch(reloaded, plugin);
    const auto savedStages = reloaded.invokeBackend("GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context}));
    const auto savedObjectives = reloaded.invokeBackend("GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context}));
    REQUIRE_FALSE(savedStages.has_value()); CHECK(savedStages.error().type == "LocalizedSourceReadUnavailable");
    REQUIRE_FALSE(savedObjectives.has_value()); CHECK(savedObjectives.error().type == "LocalizedSourceReadUnavailable");
#else
    SUCCEED("custom journal readback fixture uses held Linux destinations");
#endif
}
