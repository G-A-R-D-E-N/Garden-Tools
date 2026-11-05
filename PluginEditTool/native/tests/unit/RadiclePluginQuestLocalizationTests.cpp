#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4/archive/ba2.hpp>
#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include "../helpers/GraphTestSupport.hpp"
#include "../helpers/TempRoot.hpp"
#include "../../src/plugin/RadiclePluginJournalSession.hpp"
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4ide/app/PluginBackendTarget.hpp>
#include "plugin_bytes.hpp"

namespace {
std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_quest_authoring_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::filesystem::path allocationMutationPath;
std::uint32_t allocationMutationId = 500;
void mutateAllocationSource() {
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{allocationMutationId, {std::byte{'X'}}}}},
        allocationMutationPath);
}
struct AllocationHookCleanup {
    ~AllocationHookCleanup() { fo4ide::plugin::setLocalizedAllocationAfterReadHook(nullptr); }
};
struct StringReadHookCleanup {
    ~StringReadHookCleanup() { fo4ide::plugin::setLocalizedStringAfterReadHook(nullptr); }
};


void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void writeU32(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

void writeLocalizationSet(const std::filesystem::path& root,
                           std::vector<fo4::localization::LocalizedStringEntry> strings,
                           std::vector<fo4::localization::LocalizedStringEntry> dlstrings,
                           std::vector<fo4::localization::LocalizedStringEntry> ilstrings,
                           std::string_view language = "en") {
    const auto directory = root / "Strings";
    std::filesystem::create_directories(directory);
    const auto stem = "Patch_" + std::string(language);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, std::move(strings)},
        directory / (stem + ".STRINGS"));
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, std::move(dlstrings)},
        directory / (stem + ".DLSTRINGS"));
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, std::move(ilstrings)},
        directory / (stem + ".ILSTRINGS"));
}

nlohmann::json localizedQuestContext(const std::filesystem::path& root,
                                    std::string_view language = "en",
                                    const std::filesystem::path& directory = {}) {
    return nlohmann::json{
        {"language", language},
        {"stringsDirectory", (directory.empty() ? root / "Strings" : directory).string()}
    };
}

std::vector<std::byte> questPlugin(std::uint32_t tes4Flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
    writeU32(out, "ANAM", 2);
    writeU32(out, "ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
    writeU32(out, "FNAM", 0x10);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "QuestTest"},
            {"resolved", true}
        }})}}).has_value());
}

void failAfterLocalizedBatchInsert() { throw std::runtime_error("injected localized table batch insertion failure"); }
struct LocalizedQuestHookCleanup {
    ~LocalizedQuestHookCleanup() { fo4ide::plugin::setLocalizedStringBatchAfterInsertHook(nullptr); }
};

}

// Detects: localized quest creation must reject a table changed after allocation instead of staging stale rows over the newer file.
TEST_CASE("Localized creation rejects a string table changed after it was read") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    allocationMutationPath = root / "Strings" / "Patch_en.STRINGS";
    allocationMutationId = 500;
    AllocationHookCleanup reset;
    fo4ide::plugin::setLocalizedAllocationAfterReadHook(&mutateAllocationSource);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "Race"}, {"name", "New"}}}));
    fo4ide::plugin::setLocalizedAllocationAfterReadHook(nullptr);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "LocalizedSidecarChangedOnDisk");
    const auto current = fo4::localization::read_string_table_file(
        allocationMutationPath, fo4::localization::StringTableKind::strings);
    REQUIRE(current.entries.size() == 1);
    CHECK(current.entries[0].id == 500);
}

// Detects: an ID allocation must reject a changed or newly introduced sibling language before staging a potentially colliding reference.
TEST_CASE("Localized creation rejects sibling language changes after allocation") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    allocationMutationId = 18;
    SECTION("existing sibling changes") {
        writeLocalizationSet(root, {{20, rawBytes("Deutsch")}}, {}, {}, "de");
        allocationMutationId = 21;
    }
    SECTION("new sibling appears") {}
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    allocationMutationPath = root / "Strings" / "Patch_de.STRINGS";
    AllocationHookCleanup reset;
    fo4ide::plugin::setLocalizedAllocationAfterReadHook(&mutateAllocationSource);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "RaceSibling"}, {"name", "New"}}}));
    fo4ide::plugin::setLocalizedAllocationAfterReadHook(nullptr);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "LocalizedSidecarChangedOnDisk");
    const auto current = fo4::localization::read_string_table_file(
        allocationMutationPath, fo4::localization::StringTableKind::strings);
    REQUIRE(current.entries.size() == 1);
    CHECK(current.entries[0].id == allocationMutationId);
}

// Detects: localized ID allocation must refuse an unreadable applicable archive instead of overlooking a language table it may contain.
TEST_CASE("Localized creation refuses an unreadable string archive") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", data = root / "Data";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    writeBytes(data / "Patch - Main.ba2", rawBytes("invalid archive"));
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); load(backend, plugin);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "ArchiveRace"}, {"name", "New"}}}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "LocalizedSourcesUnavailable");
}

// Detects: localized title allocation must avoid IDs used only by Data or archive-backed sibling languages.
TEST_CASE("Localized creation accounts for fallback sibling language IDs") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", data = root / "Data";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    SECTION("Data directory") { writeLocalizationSet(data, {{300, rawBytes("Deutsch")}}, {}, {}, "de"); }
    SECTION("named archive") {
        fo4::archive::Ba2GeneralEntry entry; entry.path = "Strings\\Patch_de.STRINGS";
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(fo4::localization::write_string_table(
            {fo4::localization::StringTableKind::strings, {{300, rawBytes("Deutsch")}}})));
        fo4::archive::Ba2GeneralArchive archive; archive.entries.push_back(std::move(entry));
        std::filesystem::create_directories(data);
        fo4::archive::write_ba2_general_file(archive, data / "Patch - Main.ba2");
    }
    fo4ide::plugin::RadiclePluginBackend backend; backend.setDataDirectory(data); load(backend, plugin);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FallbackLocale"}, {"name", "New"}}}));
    REQUIRE(created.has_value());
    const auto exported = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
    REQUIRE(exported.has_value());
    const auto rows = nlohmann::json::parse(exported->get<std::string>()).at("rows");
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 301; }));
}

// Detects: case variants of the same language must share pending edits and remain protected from read-to-stage changes.
TEST_CASE("Localized creation composes mixed-case pending language edits") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "EN", "STRINGS", 17, "Edited"})).has_value());
    SECTION("successful create") {
        REQUIRE(backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
            nlohmann::json{{"editorId", "MixedCase"}, {"language", "eN"}, {"name", "New"}}})).has_value());
        const auto exported = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
        REQUIRE(exported.has_value());
        const auto rows = nlohmann::json::parse(exported->get<std::string>()).at("rows");
        CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 17 && row.at("text") == "Edited"; }));
        CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 18 && row.at("text") == "New"; }));
    }
    SECTION("source changes before staging") {
        allocationMutationPath = root / "Strings" / "Patch_en.STRINGS"; allocationMutationId = 18;
        AllocationHookCleanup reset;
        fo4ide::plugin::setLocalizedAllocationAfterReadHook(&mutateAllocationSource);
        const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
            nlohmann::json{{"editorId", "MixedRace"}, {"language", "eN"}, {"name", "New"}}}));
        fo4ide::plugin::setLocalizedAllocationAfterReadHook(nullptr);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().type == "LocalizedSidecarChangedOnDisk");
    }
}

// Detects: an edit must not stage a second file when the existing table has different filename casing on a case-sensitive filesystem.
TEST_CASE("String editing rejects an unresolved destination case mismatch") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp", directory = root / "Strings";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    std::filesystem::rename(directory / "Patch_en.STRINGS", directory / "Patch_EN.STRINGS");
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    const auto changed = backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 17, "Edited"}));
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "StringTableDestinationConflict");
    CHECK_FALSE(std::filesystem::exists(directory / "Patch_en.STRINGS"));
    const auto table = fo4::localization::read_string_table_file(directory / "Patch_EN.STRINGS",
        fo4::localization::StringTableKind::strings);
    REQUIRE(table.entries.size() == 1);
    CHECK(table.entries[0].text == rawBytes("Original"));
#else
    SUCCEED("case-sensitive path fixture runs on Linux");
#endif
}

// Detects: arbitrary string edits must reject a source changed after parsing instead of staging stale rows over it.
TEST_CASE("String editing rejects changes after its source was read") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    allocationMutationPath = root / "Strings" / "Patch_en.STRINGS"; allocationMutationId = 500;
    StringReadHookCleanup reset;
    fo4ide::plugin::setLocalizedStringAfterReadHook(&mutateAllocationSource);
    const auto changed = backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 17, "Edited"}));
    fo4ide::plugin::setLocalizedStringAfterReadHook(nullptr);
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "LocalizedSidecarChangedOnDisk");
    const auto table = fo4::localization::read_string_table_file(allocationMutationPath,
        fo4::localization::StringTableKind::strings);
    REQUIRE(table.entries.size() == 1);
    CHECK(table.entries[0].id == 500);
}

// Detects: AddString, RemoveString and ImportStrings must reject source changes before staging, including after ID allocation.
TEST_CASE("String additions removals and imports reject read-to-stage changes") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    std::string operation;
    nlohmann::json arguments;
    SECTION("add after set-wide ID scan") { operation = "strings.AddString"; arguments = nlohmann::json::array({"Patch.esp", "en", "STRINGS", "Added"}); }
    SECTION("remove") { operation = "strings.RemoveString"; arguments = nlohmann::json::array({"Patch.esp", "en", "STRINGS", 17}); }
    SECTION("import") { operation = "strings.ImportStrings"; arguments = nlohmann::json::array({"Patch.esp", "en", "STRINGS", R"({"rows":[{"id":17,"text":"Imported"}]})", "merge"}); }
    allocationMutationPath = root / "Strings" / "Patch_en.STRINGS"; allocationMutationId = 500;
    StringReadHookCleanup reset;
    fo4ide::plugin::setLocalizedStringAfterReadHook(&mutateAllocationSource);
    const auto result = backend.invoke(operation, arguments);
    fo4ide::plugin::setLocalizedStringAfterReadHook(nullptr);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
    const auto table = fo4::localization::read_string_table_file(allocationMutationPath,
        fo4::localization::StringTableKind::strings);
    REQUIRE(table.entries.size() == 1);
    CHECK(table.entries[0].id == 500);
}

// Detects: AddString must retain sibling-language allocation preconditions through both staging and journal publication.
TEST_CASE("String addition rejects sibling collisions during staging and save") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    writeLocalizationSet(root, {{20, rawBytes("Deutsch")}}, {}, {}, "de");
    const auto pluginBefore = readBytes(plugin);
    const auto stringsBefore = readBytes(root / "Strings" / "Patch_en.STRINGS");
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal"); load(backend, plugin);
    allocationMutationPath = root / "Strings" / "Patch_de.STRINGS"; allocationMutationId = 21;
    SECTION("sibling changes after ID scan") {
        StringReadHookCleanup reset;
        fo4ide::plugin::setLocalizedStringAfterReadHook(&mutateAllocationSource);
        const auto result = backend.invoke("strings.AddString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", "Added"}));
        fo4ide::plugin::setLocalizedStringAfterReadHook(nullptr);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
    }
    SECTION("sibling changes after staging") {
        REQUIRE(backend.invoke("strings.AddString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", "Added"})).has_value());
        mutateAllocationSource();
        const auto result = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
    }
    CHECK(readBytes(plugin) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.STRINGS") == stringsBefore);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: non-ASCII plugin filenames must resolve the same staged string-table save path used by reads on Windows.
TEST_CASE("Windows localized string staging preserves a Unicode plugin filename") {
#ifdef _WIN32
    const auto root = tempRoot(); Cleanup cleanup{root};
    const std::string pluginName = "\xC3\x84" "Patch.esp";
    const auto plugin = root / fo4::io::from_utf8(pluginName);
    const auto tablePath = root / "Strings" / fo4::io::from_utf8("\xC3\x84" "Patch_en.STRINGS");
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    std::filesystem::create_directories(tablePath.parent_path());
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{17, rawBytes("Original")}}}, tablePath);
    fo4::localization::write_string_table_file({fo4::localization::StringTableKind::dlstrings, {{20, rawBytes("Old")}}}, tablePath.parent_path() / fo4::io::from_utf8("\xC3\x84" "Patch_en.DLSTRINGS"));
    fo4::localization::write_string_table_file({fo4::localization::StringTableKind::ilstrings, {{30, rawBytes("Other")}}}, tablePath.parent_path() / fo4::io::from_utf8("\xC3\x84" "Patch_en.ILSTRINGS"));
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", pluginName}, {"path", fo4::io::utf8(plugin)}, {"source", "QuestTest"}, {"resolved", true}}})}}).has_value());
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({pluginName, "en", "STRINGS", 17, "Edited"})).has_value());
    const auto exported = backend.invoke("strings.ExportStrings", nlohmann::json::array({pluginName, "en", "STRINGS"}));
    REQUIRE(exported.has_value());
    const auto rows = nlohmann::json::parse(exported->get<std::string>()).at("rows");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].at("text") == "Edited");
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({pluginName, "000800:" + pluginName,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}), nlohmann::json{{"language", "en"}, {"stringsDirectory", fo4::io::utf8(tablePath.parent_path())}}})).has_value());
    const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({pluginName}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "LocalizedSaveUnavailable");
#else
    SUCCEED("Windows filename conversion fixture runs on Windows");
#endif
}

// Invariant: a localized quest save whose plugin sits outside the configured Data directory must publish through the held read sources, so the new string rows reach the plugin's own STRINGS table.
TEST_CASE("Windows localized quest save publishes held sources with a Data directory set") {
#ifdef _WIN32
    // Requires: a root whose ancestors grant no untrusted write access, which the Windows journal checks.
    const char* profile = std::getenv("USERPROFILE");
    const auto base = (profile && *profile) ? std::filesystem::path(profile)
                                            : std::filesystem::temp_directory_path();
    const auto root = base / ("fo4ide_held_save_" + fo4ide_test::uniqueTempName());
    Cleanup cleanup{root};
    const auto mod = root / "mod";
    const auto plugin = mod / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(mod, {{17, rawBytes("Original")}}, {}, {});
    const auto data = root / "Data";
    std::filesystem::create_directories(data);
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    backend.testSetJournalRoot(root / "journal");
    load(backend, plugin);
    const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "HeldSave"}, {"name", "Title"}}}));
    REQUIRE(created.has_value());
    const auto quest = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}}),
        localizedQuestContext(mod)})).has_value());
    const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
    if (!saved.has_value()) FAIL("SavePlugin failed: " << saved.error().type << ": " << saved.error().message);
    const auto savedPlugin = fo4::read_plugin_file(plugin);
    const auto stages = fo4::forms::decode_quest_stages(savedPlugin, fo4::FormKey::parse(quest));
    REQUIRE(stages.stages.size() == 1u);
    REQUIRE(stages.stages[0].log_entries.size() == 1u);
    REQUIRE(stages.stages[0].log_entries[0].log_text.has_value());
    const auto textId = stages.stages[0].log_entries[0].log_text->id;
    REQUIRE(textId.has_value());
    const auto table = fo4::localization::read_string_table_file(
        mod / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(table, *textId) != nullptr);
#else
    SUCCEED("held Windows journal destinations are not implemented on this platform");
#endif
}

// Invariant: a localized plugin that has no Strings directory yet must still read its quest stages.
TEST_CASE("Reading quest stages of a localized plugin with no Strings directory succeeds") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, plugin);
    const auto stages = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE(stages.has_value());
    CHECK(stages->at("stages").empty());
    const auto withContext = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", localizedQuestContext(root)}));
    if (!withContext.has_value()) FAIL("context read failed: " << withContext.error().type);
    CHECK(withContext->at("stages").empty());
}
// Detects: mixed-case locale tokens must select the same pending quest stage and objective string tables.
TEST_CASE("Localized quest stage and objective accept mixed-case language tokens") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {{20, rawBytes("Log")}}, {{30, rawBytes("Other")}});
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp",
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}), localizedQuestContext(root, "EN")})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp",
        nlohmann::json::array({{{"index", 1}, {"displayText", "New objective"},
            {"targets", nlohmann::json::array({{{"alias", "Existing"}}})}}}), localizedQuestContext(root, "eN")})).has_value());
}

// Detects: staged quest log and objective IDs must remain present when generic table edits remove other rows.
TEST_CASE("Localized quest references survive generic row removal") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {{20, rawBytes("Log")}}, {{30, rawBytes("Other")}});
    fo4ide::plugin::RadiclePluginBackend backend; load(backend, plugin);
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp",
        nlohmann::json::array({{{"index", 10}, {"logEntry", "New log"}}}), localizedQuestContext(root)})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp",
        nlohmann::json::array({{{"index", 1}, {"displayText", "New objective"},
            {"targets", nlohmann::json::array({{{"alias", "Existing"}}})}}}), localizedQuestContext(root)})).has_value());
    for (const auto& [kind, id] : std::vector<std::pair<std::string, int>>{{"DLSTRINGS", 31}, {"STRINGS", 32}}) {
        const auto result = backend.invoke("strings.RemoveString", nlohmann::json::array({"Patch.esp", "en", kind, id}));
        REQUIRE_FALSE(result.has_value()); CHECK(result.error().type == "LocalizedStringInUse");
    }
    REQUIRE(backend.invoke("strings.RemoveString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 17})).has_value());
}

// Detects: plugin publication must reject a sibling table changed after localized allocation and leave the pending plugin and sidecar on disk untouched.
TEST_CASE("Localized save rejects sibling language changes after staging") {
#ifdef __linux__
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto plugin = root / "Patch.esp";
    writeBytes(plugin, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{17, rawBytes("Original")}}, {}, {});
    writeLocalizationSet(root, {{20, rawBytes("Deutsch")}}, {}, {}, "de");
    const auto originalPlugin = readBytes(plugin);
    const auto originalStrings = readBytes(root / "Strings" / "Patch_en.STRINGS");
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal"); load(backend, plugin);
    REQUIRE(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "SaveRace"}, {"name", "New"}}})).has_value());
    const auto discarded = backend.invoke("strings.DiscardStringEdits", nlohmann::json::array({"Patch.esp", "en"}));
    REQUIRE_FALSE(discarded.has_value());
    CHECK(discarded.error().type == "LocalizedPendingPluginEdit");
    allocationMutationPath = root / "Strings" / "Patch_de.STRINGS";
    allocationMutationId = 21;
    mutateAllocationSource();
    const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "LocalizedSidecarChangedOnDisk");
    CHECK(readBytes(plugin) == originalPlugin);
    CHECK(readBytes(root / "Strings" / "Patch_en.STRINGS") == originalStrings);
    const auto german = fo4::localization::read_string_table_file(
        allocationMutationPath, fo4::localization::StringTableKind::strings);
    REQUIRE(german.entries.size() == 1);
    CHECK(german.entries[0].id == 21);
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}

// Detects: combined localized objective, stage and existing string edits must survive save.
TEST_CASE("Native SetQuestObjectives stages localized NNAM text in STRINGS until SavePlugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {{42, rawBytes("Existing")}},
        {{200, rawBytes("Other")}});
    const auto pluginBefore = readBytes(path);
    const auto stringsPath = root / "Strings" / "Patch_en.STRINGS";
    const auto stringsBefore = readBytes(stringsPath);
    const auto dlstringsBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");
    const auto ilstringsBefore = readBytes(root / "Strings" / "Patch_en.ILSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke(
        "strings.SetString",
        nlohmann::json::array({"Patch.esp", "en", "STRINGS", 100, "Edited before"})).has_value());
    REQUIRE(backend.invoke(
        "strings.SetString",
        nlohmann::json::array({"Patch.esp", "en", "DLSTRINGS", 42, "Edited log"})).has_value());
    const auto result = backend.invokeBackend(
        "SetQuestObjectives",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 1}, {"displayText", "Objective"},
                                   {"targets", nlohmann::json::array({{{"alias", "Existing"}}})}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(stringsPath) == stringsBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlstringsBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.ILSTRINGS") == ilstringsBefore);
    REQUIRE(backend.invoke(
        "strings.SetString",
        nlohmann::json::array({"Patch.esp", "en", "STRINGS", 100, "Edited after"})).has_value());

    const auto stageEdit = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Log"}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(stageEdit.has_value());
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(stringsPath) == stringsBefore);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto objectives = fo4::forms::decode_quest_objectives(
        saved, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(objectives.objectives.size() == 1u);
    REQUIRE(objectives.objectives[0].display_text.has_value());
    CHECK(objectives.objectives[0].display_text->id == 201u);
    const auto stages = fo4::forms::decode_quest_stages(
        saved, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(stages.stages.size() == 1u);
    REQUIRE(stages.stages[0].log_entries.size() == 1u);
    REQUIRE(stages.stages[0].log_entries[0].log_text.has_value());
    CHECK(stages.stages[0].log_entries[0].log_text->id == 202u);
    const auto strings = fo4::localization::read_string_table_file(
        stringsPath, fo4::localization::StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(strings, 201u) != nullptr);
    CHECK(fo4::localization::find_string(strings, 201u)->text == rawBytes("Objective"));
    CHECK(fo4::localization::find_string(strings, 100u)->text == rawBytes("Edited after"));
    const auto dlstrings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(dlstrings, 202u) != nullptr);
    CHECK(fo4::localization::find_string(dlstrings, 202u)->text == rawBytes("Log"));
    CHECK(fo4::localization::find_string(dlstrings, 42u)->text == rawBytes("Edited log"));
    CHECK(readBytes(root / "Strings" / "Patch_en.ILSTRINGS") == ilstringsBefore);
}

// Detects: a failed batch insertion must leave both plugin and prior table edits intact.
TEST_CASE("Native localized objective staging failure after map insertion publishes no state") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{100, rawBytes("Other")}}, {}, {{200, rawBytes("Other")} });
    const auto stringsPath = root / "Strings" / "Patch_en.STRINGS";
    LocalizedQuestHookCleanup hookCleanup;

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke(
        "strings.SetString",
        nlohmann::json::array({"Patch.esp", "en", "STRINGS", 100, "Pending"})).has_value());
    fo4ide::plugin::setLocalizedStringBatchAfterInsertHook(&failAfterLocalizedBatchInsert);
    const auto result = backend.invokeBackend(
        "SetQuestObjectives",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 1}, {"displayText", "Objective"},
                                   {"targets", nlohmann::json::array({{{"alias", "Existing"}}})}}}),
            localizedQuestContext(root)
        }));
    REQUIRE_FALSE(result.has_value());

    fo4ide::plugin::setLocalizedStringBatchAfterInsertHook(nullptr);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto objectives = fo4::forms::decode_quest_objectives(
        saved, fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(objectives.objectives.empty());
    const auto strings = fo4::localization::read_string_table_file(
        stringsPath, fo4::localization::StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(strings, 100u) != nullptr);
    CHECK(fo4::localization::find_string(strings, 100u)->text == rawBytes("Pending"));
    CHECK(fo4::localization::find_string(strings, 201u) == nullptr);
}

// Detects: objective and log sidecars must remain distinct and persist together.
TEST_CASE("Native SetQuestObjectives combines with a pending localized log sidecar") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{100, rawBytes("Other")}}, {}, {{200, rawBytes("Other")}});
    const auto pluginBefore = readBytes(path);
    const auto stringsBefore = readBytes(root / "Strings" / "Patch_en.STRINGS");
    const auto dlstringsBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Log"}}}),
            localizedQuestContext(root)
        })).has_value());

    const auto objectiveEdit = backend.invokeBackend(
        "SetQuestObjectives",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 1}, {"displayText", "Objective"},
                                   {"targets", nlohmann::json::array({{{"alias", "Existing"}}})}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(objectiveEdit.has_value());
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.STRINGS") == stringsBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlstringsBefore);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto objectives = fo4::forms::decode_quest_objectives(
        saved, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(objectives.objectives.size() == 1u);
    REQUIRE(objectives.objectives[0].display_text.has_value());
    CHECK(objectives.objectives[0].display_text->id == 202u);
    const auto stages = fo4::forms::decode_quest_stages(
        saved, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(stages.stages.size() == 1u);
    REQUIRE(stages.stages[0].log_entries.size() == 1u);
    REQUIRE(stages.stages[0].log_entries[0].log_text.has_value());
    CHECK(stages.stages[0].log_entries[0].log_text->id == 201u);
    const auto strings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    REQUIRE(fo4::localization::find_string(strings, 202u) != nullptr);
    const auto dlstrings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(dlstrings, 201u) != nullptr);
}


// Detects: localized quest names, objectives and fragments must survive publication and interrupted recovery together.
TEST_CASE("Localized quest with stage fragment survives save and recovery") {
#ifdef __linux__
    using fo4ide::plugin::detail::setJournalCommitHook;
    for (const int scenario : {0, 1, 2}) {
        const auto root = tempRoot(); Cleanup cleanup{root}; const auto pluginPath = root / "Patch.esp";
        writeBytes(pluginPath, fo4::test::make_tes4_plugin(fo4::RecordFlags::localized, 1.0f, {}));
        writeLocalizationSet(root, {{100, rawBytes("Old")}}, {{42, rawBytes("Log")}}, {{200, rawBytes("Other")}});
        const auto imports = root / "imports"; std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);
        std::filesystem::create_directories(root / "Scripts");
        const auto otherPath = root / "Other.esp";
        writeBytes(otherPath, fo4::test::make_tes4_plugin(0, 1.0f, {}));
        std::string questKey;
        {
            fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal"); load(backend, pluginPath);
            REQUIRE(backend.invoke("backend.OpenPlugin", nlohmann::json::array({otherPath.string()})).has_value());
            REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "en", "STRINGS", 100, "Edited"})).has_value());
            const auto created = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
                nlohmann::json{{"editorId", "WelcomeQuest"}, {"name", "Welcome"}, {"language", "en"}}}));
            REQUIRE(created.has_value()); REQUIRE(created->is_string()); questKey = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
            REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
                nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}}), localizedQuestContext(root)})).has_value());
            REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey,
                nlohmann::json::array({{{"index", 1}, {"displayText", "Objective"}, {"targets", nlohmann::json::array()}}}), localizedQuestContext(root)})).has_value());
            const auto actions = nlohmann::json::array({{{"kind", "display"}, {"value", 1}}, {{"kind", "stage"}, {"value", 10}}});
            REQUIRE(backend.testSetQuestFragment(nlohmann::json::array({"Patch.esp", questKey, 10, 0, "QF_Welcome", (root / "Scripts").string(), imports.string(), actions})).has_value());
            if (scenario == 1) setJournalCommitHook(+[](std::size_t count) { if (count == 1) throw std::runtime_error("interrupted"); });
            if (scenario == 2) fo4ide::plugin::detail::setJournalTimestampHook(+[] { return std::make_error_code(std::errc::io_error); });
            const auto saved = backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"}));
            setJournalCommitHook(nullptr); fo4ide::plugin::detail::setJournalTimestampHook(nullptr);
            if (scenario == 1) {
                REQUIRE_FALSE(saved.has_value()); CHECK(saved.error().type == "JournalRecoveryRequired");
                CHECK_FALSE(backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({"Patch.esp", "QUST", 10, 0})).has_value());
                fo4ide::app::PluginBackendTarget target(backend, "backend");
                const auto directRead = target.invoke("GetNpc", nlohmann::json::array({questKey}));
                REQUIRE_FALSE(directRead.has_value()); CHECK(directRead.error().type == "JournalRecoveryRequired");
                CHECK_FALSE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
                CHECK_FALSE(backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
                CHECK_FALSE(backend.invoke("backend.OpenPlugin", nlohmann::json::array({pluginPath.string()})).has_value());
                std::error_code aliasError; std::filesystem::create_symlink(pluginPath, root / "Alias.esp", aliasError);
                if (!aliasError) CHECK_FALSE(backend.invoke("backend.OpenPlugin", nlohmann::json::array({(root / "Alias.esp").string()})).has_value());
                CHECK(backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({"Other.esp", "QUST", 10, 0})).has_value());
                const nlohmann::json environment{{"discardPending", true}, {"plugins", nlohmann::json::array({
                    {{"name", "Patch.esp"}, {"path", pluginPath.string()}, {"source", "QuestTest"}, {"resolved", true}}})}};
                REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
                CHECK(backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({"Patch.esp", "QUST", 10, 0})).has_value());
            } else REQUIRE(saved.has_value());
        }
        fo4ide::plugin::RadiclePluginBackend restarted; restarted.testSetJournalRoot(root / "journal"); load(restarted, pluginPath);
        const auto parsed = fo4::read_plugin_file(pluginPath); const auto key = fo4::FormKey::parse(questKey);
        const auto* record = fo4::find_record(parsed, key); REQUIRE(record != nullptr);
        const fo4::Subrecord* full = nullptr; const fo4::Subrecord* vmad = nullptr;
        for (const auto& sub : record->subrecords) { if (sub.signature == fo4::Signature::from_string("FULL")) full = &sub; if (sub.signature == fo4::Signature::from_string("VMAD")) vmad = &sub; }
        REQUIRE(full != nullptr); REQUIRE(vmad != nullptr);
        const auto title = fo4::schema::read_localized_string(parsed, fo4::sig_qust, fo4::Signature::from_string("FULL"), *full);
        REQUIRE(title.id.has_value());
        const auto objectives = fo4::forms::decode_quest_objectives(parsed, key); REQUIRE(objectives.objectives.size() == 1);
        REQUIRE(objectives.objectives[0].display_text.has_value()); REQUIRE(objectives.objectives[0].display_text->id.has_value());
        const auto strings = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
        REQUIRE(fo4::localization::find_string(strings, *title.id) != nullptr);
        CHECK(fo4::localization::find_string(strings, *title.id)->text == rawBytes("Welcome"));
        const auto objective = fo4::localization::find_string(strings, *objectives.objectives[0].display_text->id);
        REQUIRE(objective != nullptr); CHECK(objective->text == rawBytes("Objective"));
        REQUIRE(fo4::localization::find_string(strings, 100) != nullptr); CHECK(fo4::localization::find_string(strings, 100)->text == rawBytes("Edited"));
        const auto stages = fo4::forms::decode_quest_stages(parsed, key); REQUIRE(stages.stages.size() == 1);
        REQUIRE(stages.stages[0].log_entries.size() == 1); REQUIRE(stages.stages[0].log_entries[0].log_text.has_value());
        REQUIRE(stages.stages[0].log_entries[0].log_text->id.has_value());
        const auto dialogue = fo4::localization::read_string_table_file(root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
        const auto log = fo4::localization::find_string(dialogue, *stages.stages[0].log_entries[0].log_text->id);
        REQUIRE(log != nullptr); CHECK(log->text == rawBytes("Begin"));
        const auto scripts = fo4::read_vmad_scripts(vmad->data);
        const auto extras = fo4::read_quest_vmad_extras(std::span<const std::byte>(vmad->data).last(scripts.trailing_bytes), scripts.object_format);
        REQUIRE(extras.fragments.size() == 1); CHECK(extras.fragments[0].stage == 10);
        CHECK(extras.fragments[0].script == "QF_Welcome"); CHECK(extras.fragments[0].function == "Fragment_Stage_0010_Item_00");
        const auto source = readBytes(root / "Scripts" / "QF_Welcome.psc"); REQUIRE_FALSE(source.empty());
        const std::string sourceText(reinterpret_cast<const char*>(source.data()), source.size());
        CHECK(sourceText.find("Function Fragment_Stage_0010_Item_00()") != std::string::npos);
        CHECK(sourceText.find("SetObjectiveDisplayed(1)") != std::string::npos); CHECK(sourceText.find("SetStage(10)") != std::string::npos);
        const auto binary = readBytes(root / "Scripts" / "QF_Welcome.pex"); std::vector<std::uint8_t> pex; pex.reserve(binary.size());
        for (const auto byte : binary) pex.push_back(std::to_integer<std::uint8_t>(byte));
        CHECK_NOTHROW(fo4ide::papyrus::PexFile::fromBytes(pex));
    }
#else
    SUCCEED("held journal destinations are not implemented on this platform");
#endif
}
