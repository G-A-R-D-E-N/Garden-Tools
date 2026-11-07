#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

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

std::filesystem::path staleLocalizationSibling;
fo4::localization::StringTableKind staleLocalizationKind;
bool staleLocalizationMutationPending = false;

void mutateStaleLocalizationSibling() {
    const auto table = fo4::localization::read_string_table_file(
        staleLocalizationSibling, staleLocalizationKind);
    auto entries = table.entries;
    entries.push_back({201, rawBytes("External")});
    fo4::localization::write_string_table_file(
        {staleLocalizationKind, std::move(entries)}, staleLocalizationSibling);
}

void mutateStaleLocalizationSiblingAfterTableParse() {
    if (!staleLocalizationMutationPending) return;
    staleLocalizationMutationPending = false;
    mutateStaleLocalizationSibling();
}

struct LocalizedQuestHookCleanup {
    ~LocalizedQuestHookCleanup() {
        fo4ide::plugin::setLocalizedQuestBeforeCommitHook(nullptr);
        fo4ide::plugin::setLocalizedQuestAfterTableParseHook(nullptr);
        staleLocalizationMutationPending = false;
    }
};

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

std::vector<std::byte> placedMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "REFR", 0);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PlacedRef\0", 10});
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> extraMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(8);
    hedr.u32(0x808);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    auto writeRecord = [&](std::string_view type, std::uint32_t formId, std::string_view edid) {
        const auto group = fo4::test::begin_group(out, type, 0);
        const auto record = fo4::test::begin_record(out, type, 0, formId);
        fo4::test::write_subrecord(out, "EDID", edid);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };
    writeRecord("KYWD", 0x00000800, std::string_view{"AliasKw\0", 8});
    writeRecord("MISC", 0x00000801, std::string_view{"AliasItem\0", 10});
    writeRecord("SPEL", 0x00000802, std::string_view{"AliasSpell\0", 11});
    writeRecord("FACT", 0x00000803, std::string_view{"AliasFact\0", 10});
    writeRecord("PACK", 0x00000804, std::string_view{"AliasPack\0", 10});
    writeRecord("ACTI", 0x00000805, std::string_view{"AliasActi\0", 10});
    writeRecord("NPC_", 0x00000806, std::string_view{"AliasNpc\0", 9});
    writeRecord("GLOB", 0x00000807, std::string_view{"AliasGlob\0", 10});
    return out.bytes;
}

std::vector<std::byte> conditionMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(3);
    hedr.u32(0x803);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    auto writeRecord = [&](std::string_view type, std::uint32_t formId, std::string_view edid) {
        const auto group = fo4::test::begin_group(out, type, 0);
        const auto record = fo4::test::begin_record(out, type, 0, formId);
        fo4::test::write_subrecord(out, "EDID", edid);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };
    writeRecord("NPC_", 0x00000800, std::string_view{"CondNpc\0", 8});
    writeRecord("GLOB", 0x00000801, std::string_view{"CondGlob\0", 9});
    writeRecord("REFR", 0x00000802, std::string_view{"CondRef\0", 8});
    return out.bytes;
}

std::vector<std::byte> memberMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(6);
    hedr.u32(0x806);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    auto writeRecord = [&](std::string_view type, std::uint32_t formId, std::string_view edid) {
        const auto group = fo4::test::begin_group(out, type, 0);
        const auto record = fo4::test::begin_record(out, type, 0, formId);
        fo4::test::write_subrecord(out, "EDID", edid);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };
    writeRecord("FLST", 0x00000800, std::string_view{"PkgList\0", 8});
    writeRecord("MESG", 0x00000801, std::string_view{"DispName\0", 9});
    writeRecord("VTYP", 0x00000802, std::string_view{"Voice\0", 6});
    writeRecord("LVLI", 0x00000803, std::string_view{"DeathIt\0", 8});
    writeRecord("KYWD", 0x00000804, std::string_view{"MemberKw\0", 9});
    writeRecord("SPEL", 0x00000805, std::string_view{"MemberSp\0", 9});
    return out.bytes;
}

std::vector<std::byte> fillMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    auto writeRecord = [&](std::string_view type, std::uint32_t formId, std::string_view edid) {
        const auto group = fo4::test::begin_group(out, type, 0);
        const auto record = fo4::test::begin_record(out, type, 0, formId);
        fo4::test::write_subrecord(out, "EDID", edid);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };
    writeRecord("LCRT", 0x00000800, std::string_view{"FillLcrt\0", 9});
    writeRecord("KYWD", 0x00000801, std::string_view{"FillKw\0", 7});
    return out.bytes;
}

std::vector<std::byte> npcMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"UniqueNpc\0", 10});
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeIndx(fo4::test::ByteWriter& out,
               std::uint16_t index,
               std::uint8_t flags,
               std::uint8_t unknown) {
    fo4::test::ByteWriter data;
    data.u16(index);
    data.u8(flags);
    data.u8(unknown);
    fo4::test::write_subrecord(out, "INDX", data.bytes);
}

void writeSimpleAlias(fo4::test::ByteWriter& out) {
    writeU32(out, "ANAM", 2);
    writeU32(out, "ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
    writeU32(out, "FNAM", 0x10);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
}

void writeVmadWstring(fo4::test::ByteWriter& out, std::string_view text) {
    out.u16(static_cast<std::uint16_t>(text.size()));
    out.raw(text);
}

std::vector<std::byte> vmadEmptyExtras() {
    fo4::test::ByteWriter vmad;
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(0);
    vmad.u8(3);
    vmad.u16(0);
    vmad.u16(0);
    vmad.u16(0);
    return vmad.bytes;
}

std::vector<std::byte> vmadStageFragments() {
    fo4::test::ByteWriter vmad;
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(0);
    vmad.u8(3);
    vmad.u16(1);
    vmad.u16(0);
    vmad.u16(10);
    vmad.u16(0);
    vmad.u32(0);
    vmad.u8(0);
    writeVmadWstring(vmad, "QF_Test");
    writeVmadWstring(vmad, "Fragment_10");
    vmad.u16(0);
    return vmad.bytes;
}

std::vector<std::byte> vmadAliasObjects() {
    fo4::test::ByteWriter vmad;
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(0);
    vmad.u8(3);
    vmad.u16(0);
    vmad.u16(0);
    vmad.u16(1);
    vmad.u16(0);
    vmad.u16(1);
    vmad.u32(0x00000014);
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(0);
    return vmad.bytes;
}

std::vector<std::byte> vmadTruncated() {
    fo4::test::ByteWriter vmad;
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(1);
    vmad.u16(20);
    vmad.raw(std::string_view{"x"});
    return vmad.bytes;
}

template <typename Body>
std::vector<std::byte> questPluginWith(std::uint32_t tes4Flags, Body&& body) {
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
    body(out);
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

}

TEST_CASE("Native SetQuestAliases replaces reference location and collection aliases") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json aliases = nlohmann::json::array({
        {
            {"kind", "reference"},
            {"id", 2},
            {"name", "NewRef"},
            {"flags", 0x10}
        },
        {
            {"kind", "location"},
            {"id", 3},
            {"flags", 0x40}
        },
        {
            {"kind", "collection"},
            {"id", 9},
            {"maxInitialFillCount", 2}
        }
    });
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", aliases}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Set 3 quest alias(es)") == 0);
    CHECK(readBytes(path) == before);
    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(result->get<std::string>().find("SavePlugin") != std::string::npos);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 3u);
    CHECK(decoded.aliases[0].kind == fo4::forms::QuestAliasKind::reference);
    CHECK(decoded.aliases[0].id == 2u);
    CHECK(decoded.aliases[0].name == std::optional<std::string>{"NewRef"});
    CHECK(decoded.aliases[1].kind == fo4::forms::QuestAliasKind::location);
    CHECK(decoded.aliases[1].id == 3u);
    CHECK(decoded.aliases[2].kind == fo4::forms::QuestAliasKind::collection);
    CHECK(decoded.next_alias_id == 4u);
}

TEST_CASE("Native SetQuestAliases refuses unknown fields without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 1}, {"flags", 0}, {"voiceTypes", nlohmann::json::array()}}
            })
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages writes multiple log entries with note and log text") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json stages = nlohmann::json::array({
        {
            {"index", 10},
            {"flags", 0},
            {"logEntry", "Find the thing"}
        },
        {
            {"index", 20},
            {"logEntries", nlohmann::json::array({
                {{"flags", 1}, {"logText", "Done"}, {"note", "ok"}},
                {{"flags", 2}, {"logText", "Failed"}}
            })}
        }
    });
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", stages}));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == before);

    const auto aliasResult = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{
                {"kind", "reference"},
                {"id", 2},
                {"name", "AfterStage"},
                {"flags", 0x20}
            }})
        }));
    REQUIRE(aliasResult.has_value());
    CHECK(readBytes(path) == before);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_stages(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 2u);
    CHECK(decoded.stages[0].index == 10u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries[0].log_text.has_value());
    CHECK(decoded.stages[0].log_entries[0].log_text->text ==
          std::optional<std::string>{"Find the thing"});
    REQUIRE(decoded.stages[1].log_entries.size() == 2u);
    CHECK(decoded.stages[1].log_entries[0].flags == 1u);
    CHECK(decoded.stages[1].log_entries[0].note == std::optional<std::string>{"ok"});
    CHECK(decoded.stages[1].log_entries[1].flags == 2u);

    const auto aliases = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(aliases.aliases.size() == 1u);
    CHECK(aliases.aliases[0].name == std::optional<std::string>{"AfterStage"});
}

TEST_CASE("Native SetQuestStages rejects unsupported fields and wrong types without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto conditions = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"index", 10}, {"conditions", nlohmann::json::array()}}
            })
        }));
    REQUIRE_FALSE(conditions.has_value());
    CHECK(conditions.error().type == "InvalidQuestStagePayload");
    CHECK(conditions.error().message.find("conditions") != std::string::npos);

    const auto logEntryType = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", 12}}})
        }));
    REQUIRE_FALSE(logEntryType.has_value());
    CHECK(logEntryType.error().type == "InvalidQuestStagePayload");

    const auto completeType = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"index", 10}, {"logEntry", "text"}, {"complete", 1}}
            })
        }));
    REQUIRE_FALSE(completeType.has_value());
    CHECK(completeType.error().type == "InvalidQuestStagePayload");

    const auto nam0 = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"NAM0", "000800:Patch.esp"}}})
        }));
    REQUIRE_FALSE(nam0.has_value());
    CHECK(nam0.error().type == "InvalidQuestStagePayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases validates Fill Type targets and numeric strings") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto unresolved = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"},
                 {"id", 1},
                 {"flags", 0},
                 {"uniqueActor", "000FFF:Patch.esp"}}
            })
        }));
    REQUIRE_FALSE(unresolved.has_value());
    CHECK(unresolved.error().type == "InvalidTypedFormKey");

    const auto mistyped = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"},
                 {"id", 1},
                 {"flags", 0},
                 {"uniqueActor", "000800:Patch.esp"}}
            })
        }));
    REQUIRE_FALSE(mistyped.has_value());
    CHECK(mistyped.error().type == "InvalidTypedFormKey");

    const auto junk = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"kind", "reference"}, {"id", "12junk"}, {"flags", 0}}})
        }));
    REQUIRE_FALSE(junk.has_value());
    CHECK(junk.error().type == "InvalidQuestAliasPayload");

    const auto negative = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"kind", "reference"}, {"id", "-1"}, {"flags", 0}}})
        }));
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error().type == "InvalidQuestAliasPayload");

    const auto tooWide = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", "4294967296"}, {"flags", 0}}
            })
        }));
    REQUIRE_FALSE(tooWide.has_value());
    CHECK(tooWide.error().type == "InvalidQuestAliasPayload");

    const auto kindType = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"kind", 1}, {"id", 1}, {"flags", 0}}})
        }));
    REQUIRE_FALSE(kindType.has_value());
    CHECK(kindType.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases adds a loaded uniqueActor master and remaps the fill") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, npcMasterPlugin());
    writeBytes(patchPath, questPlugin());
    const auto before = readBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());

    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {
                    {"kind", "reference"},
                    {"id", 1},
                    {"name", "ActorAlias"},
                    {"flags", 0},
                    {"uniqueActor", "000800:Master.esm"}
                }
            })
        }));
    REQUIRE(result.has_value());

    const auto pendingMastersRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(pendingMastersRaw.has_value());
    REQUIRE(pendingMastersRaw->is_string());
    const auto pendingMasters = nlohmann::json::parse(pendingMastersRaw->get<std::string>());
    REQUIRE(pendingMasters.at("masters").size() == 1u);
    CHECK(pendingMasters.at("masters")[0].at("name") == "Master.esm");
    CHECK(readBytes(patchPath) == before);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    const auto* quest = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(quest != nullptr);
    CHECK(quest->header.form_id == fo4::RawFormId{0x01000800});
    const auto aliases = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(aliases.aliases.size() == 1u);
    const auto* fill =
        std::get_if<fo4::forms::QuestUniqueActorFill>(&aliases.aliases[0].fill);
    REQUIRE(fill != nullptr);
    CHECK(fill->actor == fo4::FormKey::parse("000800:Master.esm"));
}

TEST_CASE("Native SetQuestStages refuses new CNAM in a localized plugin without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Nope"}}})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedLogTextUnsupported");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages preserves unchanged localized CNAM IDs") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(fo4::RecordFlags::localized, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        writeU32(out, "CNAM", 42);
    }));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {{42, rawBytes("Existing")}},
        {{200, rawBytes("Other")}});
    const auto before = readBytes(path);
    const auto dlBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Existing"}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(result.has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_stages(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries[0].log_text.has_value());
    CHECK(decoded.stages[0].log_entries[0].log_text->id == 42u);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlBefore);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages reuses existing English CP1252 CNAM IDs") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(fo4::RecordFlags::localized, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        writeU32(out, "CNAM", 42);
    }));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {{42, {std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xe9}}}},
        {{200, rawBytes("Other")}});
    const auto before = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");
    const auto cafe = std::string{"Caf"} + "\xc3\xa9";

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", cafe}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(result.has_value());
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == before);
}

TEST_CASE("Native SetQuestStages refuses English CNAM text outside CP1252") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")}});
    const auto pluginBefore = readBytes(path);
    const auto dlstringsBefore = readBytes(root / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "\xe4\xb8\xad"}}}),
            localizedQuestContext(root)
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedLogEncodingUnsupported");
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.DLSTRINGS") == dlstringsBefore);
}

TEST_CASE("Native SetQuestStages rejects unsupported localized language tokens") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")}});
    const auto pluginBefore = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            localizedQuestContext(root, "xx")
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidLocalizedQuestContext");
    CHECK(readBytes(path) == pluginBefore);
}

TEST_CASE("Native SetQuestStages refuses stale localized allocation participants") {
    const auto run = [](fo4::localization::StringTableKind kind, std::string_view extension) {
        const auto root = tempRoot();
        Cleanup cleanup{root};
        const auto path = root / "Patch.esp";
        writeBytes(path, questPlugin(fo4::RecordFlags::localized));
        writeLocalizationSet(
            root,
            {{100, rawBytes("Other")}},
            {},
            {{200, rawBytes("Other")} });
        const auto pluginBefore = readBytes(path);
        const auto dlstringsPath = root / "Strings" / "Patch_en.DLSTRINGS";
        const auto dlstringsBefore = readBytes(dlstringsPath);
        staleLocalizationSibling = root / "Strings" / ("Patch_en" + std::string(extension));
        staleLocalizationKind = kind;

        LocalizedQuestHookCleanup hookCleanup;
        fo4ide::plugin::setLocalizedQuestBeforeCommitHook(&mutateStaleLocalizationSibling);

        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto result = backend.invokeBackend(
            "SetQuestStages",
            nlohmann::json::array({
                "Patch.esp",
                "000800:Patch.esp",
                nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
                localizedQuestContext(root)
            }));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
        CHECK(readBytes(path) == pluginBefore);
        CHECK(readBytes(dlstringsPath) == dlstringsBefore);
        const auto changed = fo4::localization::read_string_table_file(
            staleLocalizationSibling, staleLocalizationKind);
        CHECK(fo4::localization::find_string(changed, 201u) != nullptr);
    };

    SECTION("STRINGS") {
        run(fo4::localization::StringTableKind::strings, ".STRINGS");
    }
    SECTION("ILSTRINGS") {
        run(fo4::localization::StringTableKind::ilstrings, ".ILSTRINGS");
    }
}

TEST_CASE("Native SetQuestStages keeps localization parsing and snapshots coherent") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")} });
    const auto pluginBefore = readBytes(path);
    const auto dlstringsPath = root / "Strings" / "Patch_en.DLSTRINGS";
    const auto dlstringsBefore = readBytes(dlstringsPath);
    staleLocalizationSibling = root / "Strings" / "Patch_en.ILSTRINGS";
    staleLocalizationKind = fo4::localization::StringTableKind::ilstrings;
    staleLocalizationMutationPending = true;

    LocalizedQuestHookCleanup hookCleanup;
    fo4ide::plugin::setLocalizedQuestAfterTableParseHook(
        &mutateStaleLocalizationSiblingAfterTableParse);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            localizedQuestContext(root)
        }));
    if (result.has_value()) {
        const auto table = fo4::localization::read_string_table_file(
            dlstringsPath, fo4::localization::StringTableKind::dlstrings);
        CHECK(fo4::localization::find_string(table, 201u) != nullptr);
        CHECK(readBytes(dlstringsPath) != dlstringsBefore);
        CHECK(readBytes(path) != pluginBefore);
    }
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedSidecarChangedOnDisk");
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(dlstringsPath) == dlstringsBefore);
}

TEST_CASE("Native SetQuestStages refuses an untrusted localized sidecar root") {
    const auto pluginRoot = tempRoot();
    Cleanup pluginCleanup{pluginRoot};
    const auto sidecarRoot = tempRoot();
    Cleanup sidecarCleanup{sidecarRoot};
    const auto path = pluginRoot / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        sidecarRoot,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")}});
    const auto pluginBefore = readBytes(path);
    const auto sidecarBefore = readBytes(sidecarRoot / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(sidecarRoot);
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            localizedQuestContext(pluginRoot, "en", sidecarRoot / "Strings")
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedSidecarPathNotAllowed");
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(sidecarRoot / "Strings" / "Patch_en.DLSTRINGS") == sidecarBefore);
}

TEST_CASE("Native SetQuestStages refuses a localized sidecar outside trusted roots") {
    const auto pluginRoot = tempRoot();
    Cleanup pluginCleanup{pluginRoot};
    const auto sidecarRoot = tempRoot();
    Cleanup sidecarCleanup{sidecarRoot};
    const auto path = pluginRoot / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        sidecarRoot,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")} });
    const auto pluginBefore = readBytes(path);
    const auto sidecarBefore = readBytes(sidecarRoot / "Strings" / "Patch_en.DLSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(pluginRoot / "Data");
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            localizedQuestContext(pluginRoot, "en", sidecarRoot / "Strings")
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "LocalizedSidecarPathNotAllowed");
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(sidecarRoot / "Strings" / "Patch_en.DLSTRINGS") == sidecarBefore);
}

TEST_CASE("Native SetQuestStages fails closed when a localized sibling table is missing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    const auto stringsDirectory = root / "Strings";
    std::filesystem::create_directories(stringsDirectory);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{100, rawBytes("Other")}}},
        stringsDirectory / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, {}},
        stringsDirectory / "Patch_en.DLSTRINGS");
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            localizedQuestContext(root)
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("ILSTRINGS") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native logEntry sugar ORs complete and fail into QSDT flags") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {
                    {"index", 10},
                    {"logEntry", "Both"},
                    {"complete", true},
                    {"fail", true}
                }
            })
        }));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    CHECK(decoded.stages[0].log_entries[0].flags == 3u);
}

TEST_CASE("Native SetQuestStages preserves opaque INDX bytes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0x37);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"Original\0", 9});
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto beforeSave = readBytes(path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"flags", 4}, {"logEntry", "Updated"}}})
        }));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    CHECK(decoded.stages[0].unknown == 0x37u);
    CHECK(decoded.stages[0].flags == 4u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries[0].log_text.has_value());
    CHECK(decoded.stages[0].log_entries[0].log_text->text ==
          std::optional<std::string>{"Updated"});
}

TEST_CASE("Native SetQuestStages refuses existing conditioned log entries without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"CondQuest\0", 10});
    fo4::test::ByteWriter indx;
    indx.u16(10);
    indx.bytes.push_back(std::byte{0});
    indx.bytes.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "INDX", indx.bytes);
    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
    fo4::test::write_subrecord(out, "CTDA", std::vector<std::byte>(33, std::byte{0}));
    fo4::test::write_subrecord(out, "CNAM", std::string_view{"HasCondition\0", 13});
    writeU32(out, "ANAM", 0);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    writeBytes(path, out.bytes);
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("CTDA/CIS") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases refuses existing unsupported alias members without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"RichAlias\0", 10});
    writeU32(out, "ANAM", 2);
    writeU32(out, "ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
    writeU32(out, "FNAM", 0x10);
    writeU32(out, "KWDA", 0);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    writeBytes(path, out.bytes);
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("KWDA") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native QUST authoring refuses unrepresentable existing data without writing") {
    struct GuardCase {
        const char* name;
        bool aliases;
        std::function<std::vector<std::byte>()> make;
        const char* needle;
    };

    const GuardCase cases[] = {
        {
            "leading members before QSDT",
            false,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    writeIndx(out, 10, 0, 0);
                    fo4::test::write_subrecord(out, "NAM2", std::string_view{"Ahead\0", 6});
                    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
                    writeU32(out, "ANAM", 0);
                });
            },
            "members before the first QSDT"
        },
        {
            "existing localized CNAM",
            false,
            [] {
                return questPluginWith(fo4::RecordFlags::localized, [](fo4::test::ByteWriter& out) {
                    writeIndx(out, 10, 0, 0);
                    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
                    writeU32(out, "CNAM", 42);
                    writeU32(out, "ANAM", 0);
                });
            },
            "localized CNAM"
        },
        {
            "reversed CNAM before NAM2",
            false,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    writeIndx(out, 10, 0, 0);
                    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
                    fo4::test::write_subrecord(out, "CNAM", std::string_view{"Log\0", 4});
                    fo4::test::write_subrecord(out, "NAM2", std::string_view{"Note\0", 5});
                    writeU32(out, "ANAM", 0);
                });
            },
            "NAM2/CNAM order"
        },
        {
            "unresolved Create Reference ALCO",
            true,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    writeU32(out, "ANAM", 2);
                    writeU32(out, "ALST", 1);
                    fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
                    writeU32(out, "FNAM", 0x10);
                    writeU32(out, "ALCO", 0x00000123);
                    fo4::test::ByteWriter alca;
                    alca.u16(0);
                    alca.u16(0);
                    fo4::test::write_subrecord(out, "ALCA", alca.bytes);
                    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
                });
            },
            "does not resolve"
        },
        {
            "unresolved alias FormLink",
            true,
            [] {
                fo4::test::ByteWriter out;
                const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
                fo4::test::ByteWriter hedr;
                hedr.f32(1.0f);
                hedr.u32(1);
                hedr.u32(0x801);
                fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
                fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
                fo4::test::ByteWriter data;
                data.u32(0);
                data.u32(0);
                fo4::test::write_subrecord(out, "DATA", data.bytes);
                fo4::test::end_record(out, tes4);
                const auto group = fo4::test::begin_group(out, "QUST", 0);
                const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x01000800);
                fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
                writeU32(out, "ANAM", 2);
                writeU32(out, "ALST", 1);
                fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
                writeU32(out, "FNAM", 0x10);
                writeU32(out, "ALFR", 0x03000123);
                fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
                fo4::test::end_record(out, quest);
                fo4::test::end_group(out, group);
                return out.bytes;
            },
            "unresolved FormLink"
        },
        {
            "VMAD stage fragments",
            false,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    fo4::test::write_subrecord(out, "VMAD", vmadStageFragments());
                    writeSimpleAlias(out);
                });
            },
            "stage identity"
        },
        {
            "VMAD alias objects",
            true,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    fo4::test::write_subrecord(out, "VMAD", vmadAliasObjects());
                    writeSimpleAlias(out);
                });
            },
            "alias IDs"
        },
        {
            "truncated VMAD",
            true,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    fo4::test::write_subrecord(out, "VMAD", vmadTruncated());
                    writeSimpleAlias(out);
                });
            },
            "cannot be inspected"
        },
        {
            "empty first VMAD plus binding second VMAD",
            true,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    fo4::test::write_subrecord(out, "VMAD", vmadEmptyExtras());
                    fo4::test::write_subrecord(out, "VMAD", vmadAliasObjects());
                    writeSimpleAlias(out);
                });
            },
            "more than one VMAD"
        },
        {
            "valid empty QUST extras with trailing byte",
            true,
            [] {
                return questPluginWith(0, [](fo4::test::ByteWriter& out) {
                    auto vmad = vmadEmptyExtras();
                    vmad.push_back(std::byte{0});
                    fo4::test::write_subrecord(out, "VMAD", vmad);
                    writeSimpleAlias(out);
                });
            },
            "cannot be inspected"
        },
    };

    for (const auto& test : cases) {
        SECTION(test.name) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto path = root / "Patch.esp";
            writeBytes(path, test.make());
            const auto before = readBytes(path);

            fo4ide::plugin::RadiclePluginBackend backend;
            load(backend, path);
            const auto result = test.aliases
                ? backend.invokeBackend(
                      "SetQuestAliases",
                      nlohmann::json::array({
                          "Patch.esp",
                          "000800:Patch.esp",
                          nlohmann::json::array({
                              {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
                          })
                      }))
                : backend.invokeBackend(
                      "SetQuestStages",
                      nlohmann::json::array({
                          "Patch.esp",
                          "000800:Patch.esp",
                          nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
                      }));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
            CHECK(result.error().message.find(test.needle) != std::string::npos);
            CHECK(readBytes(path) == before);
        }
    }
}

TEST_CASE("Native QUST authoring allows mutation when VMAD extras have no alias or stage bindings") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::write_subrecord(out, "VMAD", vmadEmptyExtras());
        writeSimpleAlias(out);
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto aliases = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })
        }));
    REQUIRE(aliases.has_value());
    const auto stages = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "ok"}}})
        }));
    REQUIRE(stages.has_value());
}

TEST_CASE("Native SetQuestAliases is allowed when VMAD only binds stage fragments") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::write_subrecord(out, "VMAD", vmadStageFragments());
        writeSimpleAlias(out);
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })
        }));
    REQUIRE(result.has_value());
}

TEST_CASE("Native SetQuestAliases rewrites VMAD alias IDs by unique name") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::write_subrecord(out, "VMAD", vmadAliasObjects());
        writeSimpleAlias(out);
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto kept = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 1}, {"name", "Existing"}, {"flags", 0}}
            })
        }));
    REQUIRE(kept.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    std::vector<std::byte> beforeRename;
    std::size_t aliasOffset = 0;
    {
        const auto disk = fo4::read_plugin_file(path);
        const auto* quest = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
        REQUIRE(quest != nullptr);
        const fo4::Subrecord* vmad = nullptr;
        for (const auto& sub : quest->subrecords) {
            if (sub.signature.to_string() == "VMAD") vmad = &sub;
        }
        REQUIRE(vmad != nullptr);
        const auto layout = fo4::inspect_quest_vmad(vmad->data);
        REQUIRE(layout.aliases.size() == 1u);
        CHECK(layout.aliases[0].alias_id == 1);
        beforeRename = vmad->data;
        aliasOffset = layout.aliases[0].alias_id_offset;
    }

    const auto renamed = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 4}, {"name", "Existing"}, {"flags", 0}}
            })
        }));
    REQUIRE(renamed.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reread = fo4::read_plugin_file(path);
    const auto* quest = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(quest != nullptr);
    const fo4::Subrecord* vmad = nullptr;
    for (const auto& sub : quest->subrecords) {
        if (sub.signature.to_string() == "VMAD") vmad = &sub;
    }
    REQUIRE(vmad != nullptr);
    const auto layout = fo4::inspect_quest_vmad(vmad->data);
    REQUIRE(layout.aliases.size() == 1u);
    CHECK(layout.aliases[0].alias_id == 4);
    CHECK(layout.fragments.empty());
    REQUIRE(vmad->data.size() == beforeRename.size());
    for (std::size_t i = 0; i < vmad->data.size(); ++i) {
        if (i >= aliasOffset && i < aliasOffset + 2) continue;
        CHECK(vmad->data[i] == beforeRename[i]);
    }
}

TEST_CASE("Native SetQuestStages rewrites VMAD Stage and leaves alias extras") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::ByteWriter mixed;
        mixed.u16(6);
        mixed.u16(2);
        mixed.u16(0);
        mixed.u8(3);
        mixed.u16(1);
        mixed.u16(0);
        mixed.u16(10);
        mixed.u16(0);
        mixed.u32(7);
        mixed.u8(0);
        writeVmadWstring(mixed, "QF_Test");
        writeVmadWstring(mixed, "Fragment_10");
        mixed.u16(1);
        mixed.u16(0);
        mixed.u16(1);
        mixed.u32(0x00000800);
        mixed.u16(6);
        mixed.u16(2);
        mixed.u16(0);
        fo4::test::write_subrecord(out, "VMAD", mixed.bytes);
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"Log\0", 4});
        writeSimpleAlias(out);
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto same = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Log"}}})
        }));
    REQUIRE(same.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    {
        const auto disk = fo4::read_plugin_file(path);
        const auto* quest = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
        REQUIRE(quest != nullptr);
        const fo4::Subrecord* vmad = nullptr;
        for (const auto& sub : quest->subrecords) {
            if (sub.signature.to_string() == "VMAD") vmad = &sub;
        }
        REQUIRE(vmad != nullptr);
        const auto layout = fo4::inspect_quest_vmad(vmad->data);
        REQUIRE(layout.fragments.size() == 1u);
        CHECK(layout.fragments[0].stage == 10);
        CHECK(layout.fragments[0].stage_index == 7);
        REQUIRE(layout.aliases.size() == 1u);
        CHECK(layout.aliases[0].alias_id == 1);
    }

    const auto renumbered = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "Log"}}})
        }));
    REQUIRE(renumbered.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* quest = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(quest != nullptr);
    const fo4::Subrecord* vmad = nullptr;
    for (const auto& sub : quest->subrecords) {
        if (sub.signature.to_string() == "VMAD") vmad = &sub;
    }
    REQUIRE(vmad != nullptr);
    const auto layout = fo4::inspect_quest_vmad(vmad->data);
    REQUIRE(layout.fragments.size() == 1u);
    CHECK(layout.fragments[0].stage == 20);
    CHECK(layout.fragments[0].stage_index == 7);
    REQUIRE(layout.aliases.size() == 1u);
    CHECK(layout.aliases[0].alias_id == 1);

    const auto afterRenumber = readBytes(path);
    const auto dangling = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 9}, {"name", "Other"}, {"flags", 0}}
            })
        }));
    REQUIRE_FALSE(dangling.has_value());
    CHECK(dangling.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(readBytes(path) == afterRenumber);
    (void)before;
}

TEST_CASE("Native SetQuestStages remaps VMAD Stage by unique body not array position") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::ByteWriter vmad;
        vmad.u16(6);
        vmad.u16(2);
        vmad.u16(0);
        vmad.u8(3);
        vmad.u16(1);
        vmad.u16(0);
        vmad.u16(10);
        vmad.u16(0);
        vmad.u32(7);
        vmad.u8(0);
        writeVmadWstring(vmad, "QF_Test");
        writeVmadWstring(vmad, "Fragment_10");
        vmad.u16(0);
        fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"Alpha\0", 6});
        writeIndx(out, 20, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"Beta\0", 5});
        writeSimpleAlias(out);
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto beforeSave = readBytes(path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"index", 30}, {"logEntry", "Beta"}},
                {{"index", 40}, {"logEntry", "Alpha"}}
            })
        }));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* quest = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(quest != nullptr);
    const fo4::Subrecord* vmad = nullptr;
    for (const auto& sub : quest->subrecords) {
        if (sub.signature.to_string() == "VMAD") vmad = &sub;
    }
    REQUIRE(vmad != nullptr);
    const auto layout = fo4::inspect_quest_vmad(vmad->data);
    REQUIRE(layout.fragments.size() == 1u);
    CHECK(layout.fragments[0].stage == 40);
    CHECK(layout.fragments[0].stage_index == 7);
}

TEST_CASE("Native SetQuestStages refuses ambiguous VMAD stage remapping without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::write_subrecord(out, "VMAD", vmadStageFragments());
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"Log\0", 4});
        writeIndx(out, 20, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"Log\0", 4});
        writeSimpleAlias(out);
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"index", 30}, {"logEntry", "Log"}},
                {{"index", 40}, {"logEntry", "Log"}}
            })
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases refuses object format 1 VMAD rewrite without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::ByteWriter vmad;
        vmad.u16(6);
        vmad.u16(1);
        vmad.u16(0);
        vmad.u8(3);
        vmad.u16(0);
        vmad.u16(0);
        vmad.u16(1);
        vmad.u32(0x00000014);
        vmad.u16(1);
        vmad.u16(0);
        vmad.u16(6);
        vmad.u16(1);
        vmad.u16(0);
        fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
        writeSimpleAlias(out);
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 1}, {"name", "Existing"}, {"flags", 0}}
            })
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("object format 1") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages is allowed when VMAD only binds alias objects") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        fo4::test::write_subrecord(out, "VMAD", vmadAliasObjects());
        writeSimpleAlias(out);
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "ok"}}})
        }));
    REQUIRE(result.has_value());
}

std::vector<std::byte> makeCtdaPayload(std::uint16_t function,
                                       std::int32_t param1,
                                       float value,
                                       std::uint8_t flagsByte = 0,
                                       std::uint16_t unknown2 = 0,
                                       std::int32_t unknown3 = -1,
                                       std::uint32_t runOn = 0,
                                       std::uint32_t reference = 0) {
    fo4::test::ByteWriter ctda;
    ctda.u8(flagsByte);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    ctda.u32(bits);
    ctda.u16(function);
    ctda.u16(unknown2);
    ctda.u32(static_cast<std::uint32_t>(param1));
    ctda.u32(0);
    ctda.u32(runOn);
    ctda.u32(reference);
    ctda.u32(static_cast<std::uint32_t>(unknown3));
    return ctda.bytes;
}

std::vector<std::string> recordSignatures(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    const auto* record = fo4::find_record(plugin, key);
    REQUIRE(record != nullptr);
    std::vector<std::string> names;
    for (const auto& sub : record->subrecords) names.push_back(sub.signature.to_string());
    return names;
}

TEST_CASE("Native SetQuestStages writes per-log-entry CTDA and CIS strings") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const nlohmann::json stages = nlohmann::json::array({
        {
            {"index", 10},
            {"logEntries", nlohmann::json::array({
                {
                    {"flags", 0},
                    {"logText", "Start"},
                    {"note", "n1"},
                    {"conditions", nlohmann::json::array({
                        {
                            {"function", "GetGraphVariableInt"},
                            {"operator", "=="},
                            {"cis1", "KeywordOne"},
                            {"value", 1.0}
                        },
                        {
                            {"function", "GetIsID"},
                            {"operator", "!="},
                            {"param1", "000800:Patch.esp"},
                            {"value", 0.0}
                        }
                    })}
                },
                {
                    {"flags", 1},
                    {"logText", "Done"}
                }
            })}
        },
        {
            {"index", 20},
            {"logEntry", "Later"}
        }
    });
    const auto beforeSave = readBytes(path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", stages}));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_stages(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 2u);
    REQUIRE(decoded.stages[0].log_entries.size() == 2u);
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        decoded.stages[0].log_entries[0].members, conditions, consumed));
    REQUIRE(conditions.size() == 2u);
    CHECK(conditions[0].function == 675u);
    CHECK(conditions[0].param1 == 0);
    REQUIRE(conditions[0].parameter1_string.has_value());
    CHECK(*conditions[0].parameter1_string == "KeywordOne");
    CHECK_FALSE(conditions[0].parameter2_string.has_value());
    CHECK(conditions[1].compare_operator() == 1u);
    CHECK(consumed >= 3u);
    CHECK(decoded.stages[0].log_entries[0].members[consumed].signature.to_string() == "NAM2");
    CHECK(decoded.stages[0].log_entries[1].members.front().signature.to_string() == "CNAM");
    CHECK(decoded.stages[1].index == 20u);

    const auto aliases = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE_FALSE(aliases.aliases.empty());
}

TEST_CASE("Native SetQuestStages adds a loaded condition FormLink master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, npcMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());

    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {
                    {"index", 10},
                    {"logEntries", nlohmann::json::array({
                        {
                            {"logText", "Actor"},
                            {"conditions", nlohmann::json::array({
                                {
                                    {"function", "GetIsID"},
                                    {"param1", "000800:Master.esm"},
                                    {"value", 1.0}
                                }
                            })}
                        }
                    })}
                }
            })
        }));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    const auto decoded = fo4::forms::decode_quest_stages(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        decoded.stages[0].log_entries[0].members, conditions, consumed));
    REQUIRE(conditions.size() == 1u);
    const auto raw = fo4::RawFormId{static_cast<std::uint32_t>(conditions[0].param1)};
    CHECK(fo4::resolve_plugin_form_id(raw, fo4::plugin_form_id_context(disk)) ==
          fo4::FormKey::parse("000800:Master.esm"));
}

TEST_CASE("Native SetQuestStages preserves objectives aliases and tail when writing conditions") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 5, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"KeepMe\0", 7});
        fo4::test::ByteWriter qobj;
        qobj.u16(1);
        fo4::test::write_subrecord(out, "QOBJ", qobj.bytes);
        writeU32(out, "FNAM", 0);
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Objective\0", 10});
        writeSimpleAlias(out);
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {
                    {"index", 10},
                    {"logEntries", nlohmann::json::array({
                        {
                            {"logText", "New"},
                            {"conditions", nlohmann::json::array({
                                {{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}}
                            })}
                        }
                    })}
                }
            })
        }));
    REQUIRE(result.has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto names = recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp"));
    const auto qobj = std::find(names.begin(), names.end(), "QOBJ");
    REQUIRE(qobj != names.end());
    CHECK(std::find(names.begin(), qobj, "INDX") != qobj);
    CHECK(std::find(qobj, names.end(), "ALST") != names.end());
    CHECK(names.back() == "NNAM");
    CHECK(fo4::forms::decode_quest_objectives(
              disk, fo4::FormKey::parse("000800:Patch.esp")).objectives.size() == 1u);
    CHECK(fo4::forms::decode_quest_aliases(
              disk, fo4::FormKey::parse("000800:Patch.esp")).aliases.size() == 1u);
}

TEST_CASE("Native SetQuestStages replaces a lossless existing CTDA run") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CTDA", makeCtdaPayload(8, 1, 1.0f));
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"HasCondition\0", 13});
        writeU32(out, "ANAM", 0);
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto beforeSave = readBytes(path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
        }));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    CHECK(decoded.stages[0].index == 20u);
}

namespace {

using QuestMember = std::pair<std::string, std::vector<std::byte>>;

std::vector<QuestMember> questMembers(const std::filesystem::path& path) {
    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    std::vector<QuestMember> members;
    for (const auto& sub : record->subrecords) members.emplace_back(sub.signature.to_string(), sub.data);
    return members;
}

nlohmann::json conditionedLogEntries() {
    return nlohmann::json::array({
        {
            {"flags", 0},
            {"logText", "Start"},
            {"note", "n1"},
            {"conditions", nlohmann::json::array({
                {
                    {"function", "GetGraphVariableInt"},
                    {"operator", "=="},
                    {"cis1", "KeywordOne"},
                    {"value", 1.0}
                },
                {
                    {"function", "GetIsID"},
                    {"operator", "!="},
                    {"param1", "000800:Patch.esp"},
                    {"value", 0.0}
                }
            })}
        },
        {
            {"flags", 2},
            {"conditions", nlohmann::json::array({
                {
                    {"function", "GetIsID"},
                    {"operator", "=="},
                    {"param1", "000800:Patch.esp"},
                    {"value", 1.0}
                }
            })}
        },
        {
            {"flags", 1},
            {"logText", "Done"}
        }
    });
}

nlohmann::json conditionedStageTen() {
    return nlohmann::json{{"index", 10}, {"flags", 0}, {"logEntries", conditionedLogEntries()}};
}

nlohmann::json editorStageTen() {
    return nlohmann::json{{"index", 10}, {"flags", 0}, {"logEntries", nlohmann::json::array({
        {{"flags", 0}, {"logText", "Start"}, {"note", "n1"}},
        {{"flags", 2}},
        {{"flags", 1}, {"logText", "Done"}}
    })}};
}

void writeConditionedQuest(const std::filesystem::path& path) {
    writeBytes(path, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", nlohmann::json::array({conditionedStageTen()})})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

void writeObjectiveWithKeyword(fo4::test::ByteWriter& out, bool localizedText) {
    fo4::test::ByteWriter qobj;
    qobj.u16(10);
    fo4::test::write_subrecord(out, "QOBJ", qobj.bytes);
    writeU32(out, "FNAM", 1);
    if (localizedText) {
        writeU32(out, "NNAM", 42);
    } else {
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Find the courier\0", 17});
    }
    fo4::test::ByteWriter qsta;
    qsta.u32(1);
    qsta.u32(2);
    qsta.u32(0x801);
    fo4::test::write_subrecord(out, "QSTA", qsta.bytes);
    writeSimpleAlias(out);
}

void writeObjectiveQuest(const std::filesystem::path& path, std::uint32_t flags, bool localizedText) {
    writeBytes(path, questPluginWith(flags, [localizedText](fo4::test::ByteWriter& out) {
        writeObjectiveWithKeyword(out, localizedText);
    }));
}

}

TEST_CASE("Native quest stage round trip does not rewrite log-entry CTDA or CIS members") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeConditionedQuest(path);
    const auto before = questMembers(path);
    CHECK(std::count_if(before.begin(), before.end(), [](const QuestMember& member) {
        return member.first == "CTDA";
    }) == 3);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto read = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE(read.has_value());
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", read->at("stages")})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(questMembers(path) == before);
}

TEST_CASE("Native SetQuestStages keeps log-entry conditions when the payload omits conditions") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeConditionedQuest(path);
    const auto before = questMembers(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", nlohmann::json::array({editorStageTen()})})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(questMembers(path) == before);
}

TEST_CASE("Native SetQuestStages clears log-entry conditions only for an explicit empty or null conditions value") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeConditionedQuest(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const nlohmann::json cleared{{"index", 10}, {"flags", 0}, {"logEntries", nlohmann::json::array({
        {{"flags", 0}, {"logText", "Start"}, {"note", "n1"}, {"conditions", nlohmann::json::array()}},
        {{"flags", 2}, {"conditions", nlohmann::json::array()}},
        {{"flags", 1}, {"logText", "Done"}, {"conditions", nullptr}}
    })}};
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", nlohmann::json::array({cleared})})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto names = recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(std::find(names.begin(), names.end(), "CTDA") == names.end());
    CHECK(std::find(names.begin(), names.end(), "CIS1") == names.end());
    CHECK(std::find(names.begin(), names.end(), "CNAM") != names.end());
}

TEST_CASE("Native SetQuestStages refuses a log-entry count change on a conditioned stage without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeConditionedQuest(path);
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{
                {"index", 10},
                {"logEntries", nlohmann::json::array({{{"flags", 0}, {"logText", "Start"}}})}
            }})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestStagePayload");
    CHECK(result.error().message.find("log entry count") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages gives a new stage index no conditions when its log entries omit conditions") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeConditionedQuest(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const nlohmann::json newStage{{"index", 20}, {"logEntries", nlohmann::json::array({
        {{"flags", 0}, {"logText", "Later"}}
    })}};
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({editorStageTen(), newStage})
        })).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 2u);
    CHECK(decoded.stages[0].index == 10u);
    std::vector<fo4::CtdaCondition> kept;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        decoded.stages[0].log_entries[0].members, kept, consumed));
    CHECK(kept.size() == 2u);
    CHECK(decoded.stages[1].index == 20u);
    REQUIRE(decoded.stages[1].log_entries.size() == 1u);
    CHECK(std::none_of(decoded.stages[1].log_entries[0].members.begin(),
                       decoded.stages[1].log_entries[0].members.end(),
                       [](const fo4::Subrecord& member) {
                           return member.signature.to_string() == "CTDA";
                       }));
}

TEST_CASE("Native SetQuestStages refuses a new condition owner master while carried conditions exist without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto path = root / "Patch.esp";
    writeBytes(masterPath, npcMasterPlugin());
    writeConditionedQuest(path);
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", path.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());

    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{
                {"index", 10},
                {"logEntries", nlohmann::json::array({
                    {{"flags", 0}, {"logText", "Start"}, {"note", "n1"}},
                    {{"flags", 2}, {"conditions", nlohmann::json::array({
                        {{"function", "GetIsID"}, {"param1", "000800:Master.esm"}, {"value", 1.0}}
                    })}},
                    {{"flags", 1}, {"logText", "Done"}}
                })}
            }})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestStagePayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native GetQuestStages reports an unreadable log-entry condition run without failing the read") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto checkStage = [&path](const std::vector<std::byte>& ctda) {
        writeBytes(path, questPluginWith(0, [&ctda](fo4::test::ByteWriter& out) {
            writeIndx(out, 10, 0, 0);
            fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
            fo4::test::write_subrecord(out, "CTDA", ctda);
            fo4::test::write_subrecord(out, "CNAM", std::string_view{"HasCondition\0", 13});
            writeU32(out, "ANAM", 0);
        }));
        const auto before = readBytes(path);

        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto read = backend.invokeBackend(
            "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
        REQUIRE(read.has_value());
        const auto& entry = read->at("stages").at(0).at("logEntries").at(0);
        CHECK(entry.at("conditionsUnreadable") == true);
        CHECK_FALSE(entry.contains("conditions"));

        const auto result = backend.invokeBackend(
            "SetQuestStages",
            nlohmann::json::array({
                "Patch.esp",
                "000800:Patch.esp",
                nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
            }));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(readBytes(path) == before);
    };
    checkStage(std::vector<std::byte>(33, std::byte{0}));
    checkStage(makeCtdaPayload(8, 1, std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("Native SetQuestStages refuses a localized logTextId without logText without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(root, {{42, rawBytes("Existing")}}, {}, {});
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{
                {"index", 10},
                {"logEntries", nlohmann::json::array({{{"flags", 0}, {"logTextId", 42}}})}
            }}),
            localizedQuestContext(root)
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestStagePayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native quest stage round trip writes no CNAM or NAM2 for absent logText or note") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        REQUIRE(backend.invokeBackend(
            "SetQuestStages",
            nlohmann::json::array({
                "Patch.esp",
                "000800:Patch.esp",
                nlohmann::json::array({{{"index", 10}, {"logEntries", nlohmann::json::array({{{"flags", 1}}})}}})
            })).has_value());
        REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }
    const auto before = questMembers(path);
    CHECK(std::none_of(before.begin(), before.end(), [](const QuestMember& member) {
        return member.first == "CNAM" || member.first == "NAM2";
    }));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto read = backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE(read.has_value());
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", read->at("stages")})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(questMembers(path) == before);
}

TEST_CASE("Native quest objective round trip keeps flagNames, aliasName, keyword and display text byte-identical") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeObjectiveQuest(path, 0, false);
    const auto before = questMembers(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto read = backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE(read.has_value());
    const auto& objective = read->at("objectives").at(0);
    CHECK(objective.at("displayText") == "Find the courier");
    CHECK(objective.at("flagNames") == nlohmann::json::array({"ORedWithPrevious"}));
    const auto& target = objective.at("targets").at(0);
    CHECK(target.at("aliasName") == "Existing");
    CHECK(target.at("keyword") == "000801:Patch.esp");

    REQUIRE(backend.invokeBackend(
        "SetQuestObjectives",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", read->at("objectives")})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(questMembers(path) == before);
}

TEST_CASE("Native SetQuestObjectives refuses a target keyword that differs from the saved keyword without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeObjectiveQuest(path, 0, false);
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestObjectives",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{
                {"index", 10},
                {"flags", 1},
                {"displayText", "Find the courier"},
                {"targets", nlohmann::json::array({{
                    {"alias", 1},
                    {"flags", 2},
                    {"keyword", "000999:Patch.esp"}
                }})}
            }})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestObjectivePayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native quest objective localized displayTextId survives a get and set round trip") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeObjectiveQuest(path, fo4::RecordFlags::localized, true);
    writeLocalizationSet(root, {{42, rawBytes("Find the courier")}}, {}, {});

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto context = localizedQuestContext(root);
    const auto read = backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context}));
    REQUIRE(read.has_value());
    const auto objectives = read->at("objectives");
    REQUIRE(objectives.size() == 1u);
    CHECK(objectives.at(0).at("displayTextId") == 42);
    CHECK(objectives.at(0).at("displayText") == "Find the courier");

    REQUIRE(backend.invokeBackend(
        "SetQuestObjectives",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", objectives, context})).has_value());
    const auto again = backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context}));
    REQUIRE(again.has_value());
    CHECK(again->at("objectives") == objectives);
}

nlohmann::json oneConditionStage(const nlohmann::json& condition) {
    return nlohmann::json::array({
        {
            {"index", 10},
            {"logEntries", nlohmann::json::array({
                {{"logText", "New"}, {"conditions", nlohmann::json::array({condition})}}
            })}
        }
    });
}

TEST_CASE("Native SetQuestStages refuses GetEventData JSON without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({{"function", "GetEventData"}, {"value", 1.0}})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestStagePayload");
    CHECK(result.error().message.find("GetEventData") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages enforces condition parameter types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto distanceInt = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({{"function", "GetDistance"}, {"param1", 1}, {"value", 1.0}})
        }));
    REQUIRE_FALSE(distanceInt.has_value());
    CHECK(distanceInt.error().type == "InvalidQuestStagePayload");

    const auto angleKey = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetAngle"},
                {"param1", "000800:Patch.esp"},
                {"value", 1.0}
            })
        }));
    REQUIRE_FALSE(angleKey.has_value());
    CHECK(angleKey.error().type == "InvalidQuestStagePayload");

    const auto mistyped = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetDistance"},
                {"param1", "000800:Patch.esp"},
                {"value", 1.0}
            })
        }));
    REQUIRE_FALSE(mistyped.has_value());
    CHECK(mistyped.error().type == "InvalidTypedFormKey");

    const auto subjectRef = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetAngle"},
                {"param1", 1},
                {"runOn", "Subject"},
                {"reference", "000800:Patch.esp"},
                {"value", 1.0}
            })
        }));
    REQUIRE_FALSE(subjectRef.has_value());
    CHECK(subjectRef.error().type == "InvalidQuestStagePayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages accepts alias-index GetDistance and RunOn Reference FormKeys") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, placedMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());

    const auto beforeSave = readBytes(patchPath);
    const auto aliasIndex = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetDistance"},
                {"flags", "ParametersUseAliases"},
                {"param1", 2},
                {"value", 1.0}
            })
        }));
    REQUIRE(aliasIndex.has_value());
    CHECK(readBytes(patchPath) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto referenced = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetDistance"},
                {"param1", "000800:Master.esm"},
                {"runOn", "Reference"},
                {"reference", "000800:Master.esm"},
                {"value", 1.0}
            })
        }));
    REQUIRE(referenced.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        fo4::forms::decode_quest_stages(disk, fo4::FormKey::parse("000800:Patch.esp"))
            .stages[0]
            .log_entries[0]
            .members,
        conditions,
        consumed));
    REQUIRE(conditions.size() == 1u);
    CHECK(conditions[0].run_on == 2u);
    const auto raw = fo4::RawFormId{static_cast<std::uint32_t>(conditions[0].param1)};
    CHECK(fo4::resolve_plugin_form_id(raw, fo4::plugin_form_id_context(disk)) ==
          fo4::FormKey::parse("000800:Master.esm"));
}

TEST_CASE("Native SetQuestStages refuses existing unsupported operator and unknown CTDA fields") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    auto tryRefuse = [&](std::vector<std::byte> ctda, const char* needle) {
        writeBytes(path, questPluginWith(0, [&](fo4::test::ByteWriter& out) {
            writeIndx(out, 10, 0, 0);
            fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
            fo4::test::write_subrecord(out, "CTDA", ctda);
            writeU32(out, "ANAM", 0);
        }));
        const auto before = readBytes(path);
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto result = backend.invokeBackend(
            "SetQuestStages",
            nlohmann::json::array({
                "Patch.esp",
                "000800:Patch.esp",
                nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
            }));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(result.error().message.find(needle) != std::string::npos);
        CHECK(readBytes(path) == before);
    };

    tryRefuse(makeCtdaPayload(8, 1, 1.0f, static_cast<std::uint8_t>(6u << 5)), "operator");
    tryRefuse(makeCtdaPayload(8, 1, 1.0f, 0, 1), "unknown fields");
}

TEST_CASE("Native SetQuestStages refuses existing GetAngle CTDA with CIS1 without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CTDA", makeCtdaPayload(8, 1, 1.0f));
        fo4::test::write_subrecord(out, "CIS1", std::string_view{"Nope\0", 5});
        writeU32(out, "ANAM", 0);
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("CIS1") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestStages ties CIS strings to String/Event parameter slots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto angleCis1 = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetAngle"},
                {"param1", 1},
                {"cis1", "Nope"},
                {"value", 1.0}
            })
        }));
    REQUIRE_FALSE(angleCis1.has_value());
    CHECK(angleCis1.error().type == "InvalidQuestStagePayload");

    const auto angleCis2 = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetAngle"},
                {"param1", 1},
                {"cis2", "Nope"},
                {"value", 1.0}
            })
        }));
    REQUIRE_FALSE(angleCis2.has_value());
    CHECK(angleCis2.error().type == "InvalidQuestStagePayload");
    CHECK(readBytes(path) == before);

    const auto beforeSave = readBytes(path);
    const auto graphVar = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetGraphVariableInt"},
                {"cis1", "MyVar"},
                {"value", 1.0}
            })
        }));
    REQUIRE(graphVar.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    {
        const auto decoded = fo4::forms::decode_quest_stages(
            fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
        std::vector<fo4::CtdaCondition> conditions;
        std::size_t consumed = 0;
        REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
            decoded.stages[0].log_entries[0].members, conditions, consumed));
        REQUIRE(conditions.size() == 1u);
        CHECK(conditions[0].function == 675u);
        REQUIRE(conditions[0].parameter1_string.has_value());
        CHECK(*conditions[0].parameter1_string == "MyVar");
        CHECK(conditions[0].param1 == 0);
    }

    const auto questVar = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            oneConditionStage({
                {"function", "GetVMQuestVariable"},
                {"param1", "000800:Patch.esp"},
                {"cis2", "QuestVar"},
                {"value", 1.0}
            })
        }));
    REQUIRE(questVar.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        decoded.stages[0].log_entries[0].members, conditions, consumed));
    REQUIRE(conditions.size() == 1u);
    CHECK(conditions[0].function == 629u);
    REQUIRE(conditions[0].parameter2_string.has_value());
    CHECK(*conditions[0].parameter2_string == "QuestVar");
    CHECK_FALSE(conditions[0].parameter1_string.has_value());
}

TEST_CASE("Native SetQuestStages refuses existing CIS or nonzero raw data on text slots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CTDA", makeCtdaPayload(675, 1, 1.0f));
        fo4::test::write_subrecord(out, "CIS1", std::string_view{"MyVar\0", 6});
        writeU32(out, "ANAM", 0);
    }));
    auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("nonzero raw data") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases writes keywords items packages spells factions and linked aliases") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());

    const auto beforeSave = readBytes(patchPath);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {
                    {"kind", "reference"},
                    {"id", 1},
                    {"name", "Rich"},
                    {"flags", 2},
                    {"keywords", nlohmann::json::array({"000800:Master.esm"})},
                    {"items", nlohmann::json::array({
                        {{"item", "000801:Master.esm"}, {"count", 2}}
                    })},
                    {"linkedAliases", nlohmann::json::array({
                        {{"keyword", "000800:Master.esm"}, {"alias", 4}}
                    })},
                    {"spells", nlohmann::json::array({"000802:Master.esm"})},
                    {"factions", nlohmann::json::array({"000803:Master.esm"})},
                    {"packages", nlohmann::json::array({"000804:Master.esm"})}
                }
            })
        }));
    REQUIRE(result.has_value());
    CHECK(readBytes(patchPath) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    const auto names = recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp"));
    const auto alst = std::find(names.begin(), names.end(), "ALST");
    REQUIRE(alst != names.end());
    std::vector<std::string> extras;
    for (auto it = alst; it != names.end() && *it != "ALED"; ++it) extras.push_back(*it);
    extras.push_back("ALED");
    const std::vector<std::string> expected{
        "ALST", "ALID", "FNAM", "KSIZ", "KWDA", "COCT", "CNTO", "ALLA",
        "ALSP", "ALFC", "ALPC", "ALED"};
    CHECK(extras == expected);
    {
        const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
        REQUIRE(record != nullptr);
        const fo4::Subrecord* alla = nullptr;
        std::size_t allaCount = 0;
        for (const auto& sub : record->subrecords) {
            if (sub.signature.to_string() != "ALLA") continue;
            ++allaCount;
            alla = &sub;
        }
        REQUIRE(allaCount == 1u);
        REQUIRE(alla != nullptr);
        CHECK(alla->data.size() == 8u);
    }
    {
        const auto decoded = fo4::forms::decode_quest_aliases(
            disk, fo4::FormKey::parse("000800:Patch.esp"));
        REQUIRE(decoded.aliases.size() == 1u);
        const auto& alias = decoded.aliases[0];
        REQUIRE(alias.keywords.size() == 1u);
        CHECK(alias.keywords[0] == fo4::FormKey::parse("000800:Master.esm"));
        REQUIRE(alias.items.size() == 1u);
        CHECK(alias.items[0].item == fo4::FormKey::parse("000801:Master.esm"));
        CHECK(alias.items[0].count == 2);
        REQUIRE(alias.linked_aliases.size() == 1u);
        CHECK(alias.linked_aliases[0].keyword == fo4::FormKey::parse("000800:Master.esm"));
        CHECK(alias.linked_aliases[0].alias == 4);
        REQUIRE(alias.spells.size() == 1u);
        CHECK(alias.spells[0] == fo4::FormKey::parse("000802:Master.esm"));
        REQUIRE(alias.factions.size() == 1u);
        CHECK(alias.factions[0] == fo4::FormKey::parse("000803:Master.esm"));
        REQUIRE(alias.packages.size() == 1u);
        CHECK(alias.packages[0] == fo4::FormKey::parse("000804:Master.esm"));
        REQUIRE(alias.flags.has_value());
        CHECK((*alias.flags & 0x2u) != 0u);
    }

    const auto locationExtras = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {
                    {"kind", "location"},
                    {"id", 2},
                    {"flags", 0},
                    {"keywords", nlohmann::json::array({"000800:Master.esm"})}
                }
            })
        }));
    REQUIRE_FALSE(locationExtras.has_value());
    CHECK(locationExtras.error().type == "InvalidQuestAliasPayload");
}

TEST_CASE("Native SetQuestAliases refuses existing extra members that are out of order or incomplete") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0x10);
        writeU32(out, "KWDA", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(readBytes(path) == before);
}

const fo4::Subrecord* findQuestSub(const fo4::Plugin& plugin, std::string_view signature) {
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    const fo4::Subrecord* found = nullptr;
    std::size_t count = 0;
    for (const auto& sub : record->subrecords) {
        if (sub.signature.to_string() != signature) continue;
        ++count;
        found = &sub;
    }
    CHECK(count <= 1u);
    return found;
}

void loadExtraMaster(fo4ide::plugin::RadiclePluginBackend& backend,
                     const std::filesystem::path& masterPath,
                     const std::filesystem::path& patchPath) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());
}

nlohmann::json setAliasesArgs(const nlohmann::json& aliases) {
    return nlohmann::json::array({"Patch.esp", "000800:Patch.esp", aliases});
}

TEST_CASE("Native SetQuestAliases packs linked aliases into one ALLA array") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto none = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"keywords", nlohmann::json::array({"000800:Master.esm"})}
            }
        })));
    REQUIRE(none.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(findQuestSub(fo4::read_plugin_file(patchPath), "ALLA") == nullptr);

    const auto two = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"linkedAliases", nlohmann::json::array({
                    {{"keyword", "000800:Master.esm"}, {"alias", 4}},
                    {{"keyword", "000800:Master.esm"}, {"alias", 7}}
                })}
            }
        })));
    REQUIRE(two.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    auto disk = fo4::read_plugin_file(patchPath);
    const auto* alla = findQuestSub(disk, "ALLA");
    REQUIRE(alla != nullptr);
    CHECK(alla->data.size() == 16u);
    std::int32_t aliasA = 0;
    std::int32_t aliasB = 0;
    std::memcpy(&aliasA, alla->data.data() + 4, 4);
    std::memcpy(&aliasB, alla->data.data() + 12, 4);
    CHECK(aliasA == 4);
    CHECK(aliasB == 7);

    auto remapped = fo4::remap_plugin_masters(
        std::move(disk),
        {fo4::PluginKey::from_filename("Dummy.esm"),
         fo4::PluginKey::from_filename("Master.esm")});
    const auto* remappedAlla = findQuestSub(remapped, "ALLA");
    REQUIRE(remappedAlla != nullptr);
    REQUIRE(remappedAlla->data.size() == 16u);
    std::uint32_t keywordA = 0;
    std::uint32_t keywordB = 0;
    std::int32_t remappedAliasA = 0;
    std::int32_t remappedAliasB = 0;
    std::memcpy(&keywordA, remappedAlla->data.data(), 4);
    std::memcpy(&remappedAliasA, remappedAlla->data.data() + 4, 4);
    std::memcpy(&keywordB, remappedAlla->data.data() + 8, 4);
    std::memcpy(&remappedAliasB, remappedAlla->data.data() + 12, 4);
    CHECK(keywordA == 0x01000800u);
    CHECK(keywordB == 0x01000800u);
    CHECK(remappedAliasA == 4);
    CHECK(remappedAliasB == 7);
    const auto context = fo4::plugin_form_id_context(remapped);
    CHECK(fo4::resolve_plugin_form_id(fo4::RawFormId{keywordA}, context) ==
          fo4::FormKey::parse("000800:Master.esm"));
}

TEST_CASE("Native SetQuestAliases accepts sigBaseObjects items and rejects nested extra keys") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto acti = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"items", nlohmann::json::array({
                    {{"item", "000805:Master.esm"}, {"count", 1}}
                })}
            }
        })));
    REQUIRE(acti.has_value());

    const auto before = readBytes(patchPath);
    const auto owner = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"items", nlohmann::json::array({
                    {{"item", "000801:Master.esm"}, {"count", 1}, {"owner", "x"}}
                })}
            }
        })));
    REQUIRE_FALSE(owner.has_value());
    CHECK(owner.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(patchPath) == before);

    const auto typo = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"linkedAliases", nlohmann::json::array({
                    {{"keyword", "000800:Master.esm"}, {"alias", 4}, {"typo", 1}}
                })}
            }
        })));
    REQUIRE_FALSE(typo.has_value());
    CHECK(typo.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(patchPath) == before);
}

TEST_CASE("Native SetQuestAliases binds mixed item COED to the preceding CNTO") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto beforeSave = readBytes(patchPath);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"items", nlohmann::json::array({
                    {{"item", "000801:Master.esm"}, {"count", 1}},
                    {
                        {"item", "000801:Master.esm"},
                        {"count", 2},
                        {"extra", {
                            {"owner", "000803:Master.esm"},
                            {"requiredRank", 3},
                            {"itemCondition", 1.0}
                        }}
                    },
                    {
                        {"item", "000805:Master.esm"},
                        {"count", 1},
                        {"extra", {
                            {"owner", "000806:Master.esm"},
                            {"global", "000807:Master.esm"},
                            {"itemCondition", 0.5}
                        }}
                    }
                })}
            }
        })));
    REQUIRE(result.has_value());
    CHECK(readBytes(patchPath) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(patchPath);
    const auto names = recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp"));
    const auto alst = std::find(names.begin(), names.end(), "ALST");
    REQUIRE(alst != names.end());
    std::vector<std::string> extras;
    for (auto it = alst; it != names.end() && *it != "ALED"; ++it) extras.push_back(*it);
    extras.push_back("ALED");
    const std::vector<std::string> expected{
        "ALST", "ALID", "FNAM", "COCT", "CNTO", "CNTO", "COED", "CNTO", "COED", "ALED"};
    CHECK(extras == expected);

    const auto decoded = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 1u);
    REQUIRE(decoded.aliases[0].items.size() == 3u);
    CHECK_FALSE(decoded.aliases[0].items[0].extra.has_value());
    REQUIRE(decoded.aliases[0].items[1].extra.has_value());
    CHECK(decoded.aliases[0].items[1].extra->owner ==
          std::optional<fo4::FormKey>{fo4::FormKey::parse("000803:Master.esm")});
    CHECK_FALSE(decoded.aliases[0].items[1].extra->required_rank.has_value());
    CHECK_FALSE(decoded.aliases[0].items[1].extra->global.has_value());
    CHECK(decoded.aliases[0].items[1].extra->item_condition == 1.0f);
    REQUIRE(decoded.aliases[0].items[2].extra.has_value());
    CHECK(decoded.aliases[0].items[2].extra->owner ==
          std::optional<fo4::FormKey>{fo4::FormKey::parse("000806:Master.esm")});
    CHECK_FALSE(decoded.aliases[0].items[2].extra->required_rank.has_value());
    CHECK(decoded.aliases[0].items[2].extra->item_condition == 0.5f);
    {
        const auto* quest = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
        REQUIRE(quest != nullptr);
        std::vector<const fo4::Subrecord*> coeds;
        for (const auto& sub : quest->subrecords) {
            if (sub.signature.to_string() == "COED") coeds.push_back(&sub);
        }
        REQUIRE(coeds.size() == 2u);
        std::int32_t rank = 0;
        std::memcpy(&rank, coeds[0]->data.data() + 4, 4);
        CHECK(rank == 3);
        std::uint32_t glob = 0;
        std::memcpy(&glob, coeds[1]->data.data() + 4, 4);
        CHECK(glob == 0x00000807u);
    }

    const auto before = readBytes(patchPath);
    const auto both = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"items", nlohmann::json::array({
                    {
                        {"item", "000801:Master.esm"},
                        {"count", 1},
                        {"extra", {
                            {"owner", "000803:Master.esm"},
                            {"global", "000807:Master.esm"},
                            {"requiredRank", 1}
                        }}
                    }
                })}
            }
        })));
    REQUIRE_FALSE(both.has_value());
    CHECK(both.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(patchPath) == before);

    const auto factGlobal = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"items", nlohmann::json::array({
                    {
                        {"item", "000801:Master.esm"},
                        {"count", 1},
                        {"extra", {
                            {"owner", "000803:Master.esm"},
                            {"global", "000807:Master.esm"}
                        }}
                    }
                })}
            }
        })));
    REQUIRE_FALSE(factGlobal.has_value());
    CHECK(factGlobal.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(patchPath) == before);

    const auto huge = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"items", nlohmann::json::array({
                    {
                        {"item", "000801:Master.esm"},
                        {"count", 1},
                        {"extra", {
                            {"owner", "000803:Master.esm"},
                            {"requiredRank", 1},
                            {"itemCondition", 1e40}
                        }}
                    }
                })}
            }
        })));
    REQUIRE_FALSE(huge.has_value());
    CHECK(huge.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(patchPath) == before);
}

template <typename Body>
std::vector<std::byte> questPatchWithMaster(Body&& body) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x01000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
    body(out);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeCnto(fo4::test::ByteWriter& out, std::uint32_t item, std::int32_t count) {
    fo4::test::ByteWriter data;
    data.u32(item);
    data.u32(static_cast<std::uint32_t>(count));
    fo4::test::write_subrecord(out, "CNTO", data.bytes);
}

void writeAlla(fo4::test::ByteWriter& out, std::uint32_t keyword, std::int32_t alias) {
    fo4::test::ByteWriter data;
    data.u32(keyword);
    data.u32(static_cast<std::uint32_t>(alias));
    fo4::test::write_subrecord(out, "ALLA", data.bytes);
}

void writeCoed(fo4::test::ByteWriter& out,
               std::uint32_t owner,
               std::uint32_t ownerData,
               float itemCondition) {
    fo4::test::ByteWriter data;
    data.u32(owner);
    data.u32(ownerData);
    data.f32(itemCondition);
    fo4::test::write_subrecord(out, "COED", data.bytes);
}

TEST_CASE("Native SetQuestAliases refuses existing extras that fail authoring type rules") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    writeBytes(masterPath, extraMasterPlugin());

    auto refuse = [&](std::vector<std::byte> bytes) {
        const auto path = root / "Patch.esp";
        writeBytes(path, std::move(bytes));
        const auto before = readBytes(path);
        fo4ide::plugin::RadiclePluginBackend backend;
        loadExtraMaster(backend, masterPath, path);
        const auto result = backend.invokeBackend(
            "SetQuestAliases",
            setAliasesArgs(nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"flags", 0}}
            })));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(readBytes(path) == before);
    };

    auto aliasPrefix = [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
    };
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeU32(out, "ALSP", 0x00000803);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeU32(out, "KSIZ", 1);
        writeU32(out, "KWDA", 0x00000804);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeU32(out, "COCT", 1);
        writeCnto(out, 0x00000803, 1);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeU32(out, "COCT", 1);
        writeCnto(out, 0x00000801, 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeU32(out, "COCT", 1);
        writeCnto(out, 0x00000801, 1);
        writeCoed(out, 0x00000802, 0, 1.0f);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeCoed(out, 0x00000803, 0, 1.0f);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeU32(out, "COCT", 1);
        writeCnto(out, 0x00000801, 1);
        writeCoed(out, 0x00000803, 0, std::numeric_limits<float>::infinity());
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    refuse(questPatchWithMaster([&](fo4::test::ByteWriter& out) {
        aliasPrefix(out);
        writeAlla(out, 0, 1);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
}

TEST_CASE("Native SetQuestAliases still rewrites reconstructable extra members") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto path = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(path, questPatchWithMaster([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "KSIZ", 1);
        writeU32(out, "KWDA", 0x00000800);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE(result.has_value());
}

TEST_CASE("Native SetQuestAliases rewrites reconstructable mixed item COED") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto path = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(path, questPatchWithMaster([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "COCT", 2);
        writeCnto(out, 0x00000801, 1);
        writeCnto(out, 0x00000801, 2);
        writeCoed(out, 0x00000803, 3, 1.0f);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE(result.has_value());
}

TEST_CASE("Native SetQuestAliases writes CTDA/CIS before extras") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, extraMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Cond"},
                {"flags", 0},
                {"keywords", nlohmann::json::array({"000800:Master.esm"})},
                {"conditions", nlohmann::json::array({
                    {
                        {"function", "GetGraphVariableInt"},
                        {"cis1", "MyVar"},
                        {"value", 1.0}
                    }
                })}
            }
        })));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(patchPath);
    const auto names = recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp"));
    const auto alst = std::find(names.begin(), names.end(), "ALST");
    REQUIRE(alst != names.end());
    std::vector<std::string> members;
    for (auto it = alst; it != names.end() && *it != "ALED"; ++it) members.push_back(*it);
    members.push_back("ALED");
    const std::vector<std::string> expected{
        "ALST", "ALID", "FNAM", "CTDA", "CIS1", "KSIZ", "KWDA", "ALED"};
    CHECK(members == expected);

    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    const auto ctda = std::find_if(
        record->subrecords.begin(), record->subrecords.end(), [](const fo4::Subrecord& sub) {
            return sub.signature.to_string() == "CTDA";
        });
    REQUIRE(ctda != record->subrecords.end());
    const std::span<const fo4::Subrecord> run{&*ctda, 2};
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(run, conditions, consumed));
    REQUIRE(conditions.size() == 1u);
    CHECK(conditions[0].function == 675u);
    REQUIRE(conditions[0].parameter1_string.has_value());
    CHECK(*conditions[0].parameter1_string == "MyVar");
}

TEST_CASE("Native SetQuestAliases writes conditions on location aliases") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "location"},
                {"id", 2},
                {"flags", 0},
                {"closestToAlias", 1},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetGraphVariableInt"}, {"cis1", "LocVar"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto names = recordSignatures(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    const auto alls = std::find(names.begin(), names.end(), "ALLS");
    REQUIRE(alls != names.end());
    std::vector<std::string> members;
    for (auto it = alls; it != names.end() && *it != "ALED"; ++it) members.push_back(*it);
    members.push_back("ALED");
    const std::vector<std::string> expected{"ALLS", "FNAM", "CTDA", "CIS1", "ALCC", "ALED"};
    CHECK(members == expected);
}

TEST_CASE("Native SetQuestAliases refuses collection conditions and invalid nested keys") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto collection = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "collection"},
                {"id", 3},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetGraphVariableInt"}, {"cis1", "X"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE_FALSE(collection.has_value());
    CHECK(collection.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);

    const auto unknown = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetGraphVariableInt"}, {"cis1", "X"}, {"typo", 1}}
                })}
            }
        })));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);

    const auto angle = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetAngle"}, {"param1", 1}, {"cis1", "Nope"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE_FALSE(angle.has_value());
    CHECK(angle.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases refuses existing alias CIS on a non-text slot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        fo4::test::write_subrecord(out, "CTDA", makeCtdaPayload(8, 1, 1.0f));
        fo4::test::write_subrecord(out, "CIS1", std::string_view{"Nope\0", 5});
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("CIS1") != std::string::npos);
    CHECK(readBytes(path) == before);
}

std::vector<fo4::CtdaCondition> readFirstAliasConditions(const std::filesystem::path& path) {
    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE_FALSE(decoded.aliases.empty());
    const auto& members = decoded.aliases[0].subrecords;
    auto begin = members.begin();
    while (begin != members.end() && begin->signature.to_string() != "CTDA") ++begin;
    REQUIRE(begin != members.end());
    auto end = begin;
    while (end != members.end()) {
        const auto name = end->signature.to_string();
        if (name != "CTDA" && name != "CIS1" && name != "CIS2") break;
        ++end;
    }
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    const std::span<const fo4::Subrecord> run{
        &*begin, static_cast<std::size_t>(end - begin)};
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(run, conditions, consumed));
    REQUIRE(consumed == run.size());
    return conditions;
}

TEST_CASE("Native SetQuestAliases writes zero, multiple, scalar, and CIS2 conditions") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto empty = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 1}, {"name", "None"}, {"flags", 0}}
        })));
    REQUIRE(empty.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    {
        const auto names = recordSignatures(
            fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
        CHECK(std::find(names.begin(), names.end(), "CTDA") == names.end());
        CHECK(std::find(names.begin(), names.end(), "CIS1") == names.end());
        CHECK(names.back() == "NNAM");
    }

    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Many"},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {
                        {"function", "GetGraphVariableInt"},
                        {"operator", "=="},
                        {"cis1", "KeywordOne"},
                        {"value", 1.0}
                    },
                    {
                        {"function", "GetAngle"},
                        {"operator", "!="},
                        {"param1", 1},
                        {"value", 0.0}
                    },
                    {
                        {"function", "GetVMQuestVariable"},
                        {"param1", "000800:Patch.esp"},
                        {"cis2", "QuestVar"},
                        {"value", 1.0}
                    }
                })}
            }
        })));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto names = recordSignatures(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    const auto alst = std::find(names.begin(), names.end(), "ALST");
    REQUIRE(alst != names.end());
    std::vector<std::string> members;
    for (auto it = alst; it != names.end() && *it != "ALED"; ++it) members.push_back(*it);
    members.push_back("ALED");
    const std::vector<std::string> expected{
        "ALST", "ALID", "FNAM", "CTDA", "CIS1", "CTDA", "CTDA", "CIS2", "ALED"};
    CHECK(members == expected);
    CHECK(names.back() == "NNAM");

    const auto conditions = readFirstAliasConditions(path);
    REQUIRE(conditions.size() == 3u);
    CHECK(conditions[0].function == 675u);
    REQUIRE(conditions[0].parameter1_string.has_value());
    CHECK(*conditions[0].parameter1_string == "KeywordOne");
    CHECK(conditions[1].function == 8u);
    CHECK(conditions[1].param1 == 1);
    CHECK(conditions[1].compare_operator() == 1u);
    CHECK_FALSE(conditions[1].parameter1_string.has_value());
    CHECK(conditions[2].function == 629u);
    REQUIRE(conditions[2].parameter2_string.has_value());
    CHECK(*conditions[2].parameter2_string == "QuestVar");
    CHECK_FALSE(conditions[2].parameter1_string.has_value());
}

TEST_CASE("Native SetQuestAliases encodes FormKey compareGlobal and RunOn Reference") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, conditionMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Links"},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {
                        {"function", "GetIsID"},
                        {"param1", "000800:Master.esm"},
                        {"compareGlobal", "000801:Master.esm"}
                    },
                    {
                        {"function", "GetDistance"},
                        {"param1", "000802:Master.esm"},
                        {"runOn", "Reference"},
                        {"reference", "000802:Master.esm"},
                        {"value", 1.0}
                    },
                    {
                        {"function", "GetDistance"},
                        {"flags", "ParametersUseAliases"},
                        {"param1", 2},
                        {"value", 1.0}
                    }
                })}
            }
        })));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    const auto context = fo4::plugin_form_id_context(disk);
    const auto conditions = readFirstAliasConditions(patchPath);
    REQUIRE(conditions.size() == 3u);
    CHECK((conditions[0].flags_byte & fo4::k_ctda_flag_use_global) != 0);
    CHECK(fo4::resolve_plugin_form_id(
              fo4::RawFormId{static_cast<std::uint32_t>(conditions[0].param1)}, context) ==
          fo4::FormKey::parse("000800:Master.esm"));
    CHECK(fo4::resolve_plugin_form_id(
              fo4::RawFormId{conditions[0].comparison_raw}, context) ==
          fo4::FormKey::parse("000801:Master.esm"));
    CHECK(conditions[1].run_on == 2u);
    CHECK(fo4::resolve_plugin_form_id(
              fo4::RawFormId{static_cast<std::uint32_t>(conditions[1].param1)}, context) ==
          fo4::FormKey::parse("000802:Master.esm"));
    CHECK(fo4::resolve_plugin_form_id(
              fo4::RawFormId{conditions[1].reference}, context) ==
          fo4::FormKey::parse("000802:Master.esm"));
    CHECK((conditions[2].flags_byte & fo4::k_ctda_flag_aliases) != 0);
    CHECK(conditions[2].param1 == 2);

    fo4ide::plugin::RadiclePluginBackend reloaded;
    loadExtraMaster(reloaded, masterPath, patchPath);
    const auto again = readFirstAliasConditions(patchPath);
    REQUIRE(again.size() == 3u);
    CHECK(again[0].function == conditions[0].function);
    CHECK(again[1].run_on == 2u);
    CHECK(again[2].param1 == 2);
}

TEST_CASE("Native SetQuestAliases preserves stages and tail when writing conditions") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 5, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CNAM", std::string_view{"KeepMe\0", 7});
        writeSimpleAlias(out);
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto beforeSave = readBytes(path);
    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 4},
                {"name", "Cond"},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE(written.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto names = recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(std::find(names.begin(), names.end(), "INDX") != names.end());
    CHECK(std::find(names.begin(), names.end(), "QSDT") != names.end());
    CHECK(names.back() == "NNAM");
    CHECK(fo4::forms::decode_quest_stages(
              disk, fo4::FormKey::parse("000800:Patch.esp")).stages.size() == 1u);
    const auto aliases = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(aliases.aliases.size() == 1u);
    CHECK(aliases.aliases[0].id == 4u);
    CHECK(readFirstAliasConditions(path).size() == 1u);
}

TEST_CASE("Native SetQuestAliases refuses invalid condition FormKeys and GetEventData JSON") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto eventData = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetEventData"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE_FALSE(eventData.has_value());
    CHECK(eventData.error().type == "InvalidQuestAliasPayload");
    CHECK(eventData.error().message.find("GetEventData") != std::string::npos);

    const auto mistyped = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetDistance"}, {"param1", "000800:Patch.esp"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE_FALSE(mistyped.has_value());
    CHECK(mistyped.error().type == "InvalidTypedFormKey");

    const auto unresolved = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetIsID"}, {"param1", "000800:Missing.esm"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE_FALSE(unresolved.has_value());
    CHECK(readBytes(path) == before);
}

std::vector<std::string> aliasMembersUntilAled(const std::vector<std::string>& names,
                                               std::string_view start) {
    const auto first = std::find(names.begin(), names.end(), start);
    REQUIRE(first != names.end());
    std::vector<std::string> members;
    for (auto it = first; it != names.end() && *it != "ALED"; ++it) members.push_back(*it);
    members.push_back("ALED");
    return members;
}

TEST_CASE("Native SetQuestAliases writes remaining reference fill types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, fillMasterPlugin());
    writeBytes(patchPath, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto created = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Create"},
                {"flags", 0},
                {"createReference", {{"object", "000801:Master.esm"}, {"alias", -2}, {"create", "In"}}},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE(created.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{
              "ALST", "ALID", "FNAM", "ALCO", "ALCA", "ALCL", "CTDA", "ALED"});
    CHECK(fo4::read_plugin_file(patchPath).header.masters.size() == 1u);

    const auto locRef = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "LocRef"},
                {"flags", 0},
                {"locationAliasReference", {{"alias", 3}, {"referenceType", "000800:Master.esm"}}},
                {"conditions", nlohmann::json::array()}
            }
        })));
    REQUIRE(locRef.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALFA", "ALRT", "ALED"});

    const auto external = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Ext"},
                {"flags", 0},
                {"externalAlias", {{"quest", "000800:Patch.esp"}, {"alias", 4}}},
                {"conditions", nlohmann::json::array()}
            }
        })));
    REQUIRE(external.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALEQ", "ALEA", "ALED"});

    const auto near = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Near"},
                {"flags", 0},
                {"findMatchingNearAlias", {{"alias", -3}, {"type", 1}}},
                {"conditions", nlohmann::json::array()}
            }
        })));
    REQUIRE(near.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALNA", "ALNT", "ALED"});

    const auto fromEvent = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Evt"},
                {"flags", 0},
                {"findMatchingFromEvent", {{"event", 17}, {"data", 4}}},
                {"conditions", nlohmann::json::array()}
            }
        })));
    REQUIRE(fromEvent.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALFE", "ALFD", "ALED"});

    const auto closest = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Close"},
                {"flags", 0},
                {"findMatchingClosest", {{"alias", 2}}},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE(closest.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALCC", "CTDA", "ALED"});
}

TEST_CASE("Native SetQuestAliases writes remaining location fill types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, fillMasterPlugin());
    writeBytes(patchPath, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto refLoc = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "location"},
                {"id", 2},
                {"flags", 0},
                {"referenceAliasLocation", {{"alias", 1}, {"keyword", "000801:Master.esm"}}},
                {"closestToAlias", -5}
            }
        })));
    REQUIRE(refLoc.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALLS") ==
          std::vector<std::string>{"ALLS", "FNAM", "ALFA", "KNAM", "ALCC", "ALED"});

    const auto extLoc = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "location"},
                {"id", 2},
                {"flags", 0},
                {"externalAliasLocation", {{"quest", "000800:Patch.esp"}, {"alias", 8}}},
                {"closestToAlias", nullptr}
            }
        })));
    REQUIRE(extLoc.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALLS") ==
          std::vector<std::string>{"ALLS", "FNAM", "ALEQ", "ALEA", "ALED"});

    const auto matchLoc = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "location"},
                {"id", 2},
                {"flags", 0},
                {"findMatchingLocation", {{"event", 9}, {"data", 2}}},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetGraphVariableInt"}, {"cis1", "LocVar"}, {"value", 1.0}}
                })},
                {"closestToAlias", 1}
            }
        })));
    REQUIRE(matchLoc.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALLS") ==
          std::vector<std::string>{"ALLS", "FNAM", "ALFE", "ALFD", "CTDA", "CIS1", "ALCC", "ALED"});
}

TEST_CASE("Native SetQuestAliases refuses invalid remaining fills without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto twoFills = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"uniqueActor", "000800:Patch.esp"},
                {"createReference", {{"alias", 0}, {"create", "At"}}}
            }
        })));
    REQUIRE_FALSE(twoFills.has_value());
    CHECK(twoFills.error().type == "InvalidQuestAliasPayload");

    const auto unknown = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"createReference", {{"alias", 0}, {"typo", 1}}}
            }
        })));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "InvalidQuestAliasPayload");

    const auto badMode = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"createReference", {{"alias", 0}, {"create", 7}}}
            }
        })));
    REQUIRE_FALSE(badMode.has_value());
    CHECK(badMode.error().type == "InvalidQuestAliasPayload");

    const auto badLevel = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"createReference", {{"alias", 0}, {"level", 7}}}
            }
        })));
    REQUIRE_FALSE(badLevel.has_value());
    CHECK(badLevel.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases writes and reconstructs Create Reference Level") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "ALCO", 0);
        writeU32(out, "ALCA", 0x80000000);
        writeU32(out, "ALCL", 1);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Create"},
                {"flags", 0},
                {"createReference", {{"alias", 0}, {"create", "At"}, {"level", "VeryHard"}}}
            }
        })));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(aliasMembersUntilAled(
              recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALCO", "ALCA", "ALCL", "ALED"});
    const auto decoded = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 1u);
    const auto* fill = std::get_if<fo4::forms::QuestCreateReferenceFill>(
        &decoded.aliases[0].fill);
    REQUIRE(fill != nullptr);
    CHECK(fill->level == 3u);
    CHECK(recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp")).back() == "NNAM");
}

TEST_CASE("Native SetQuestAliases refuses existing invalid ALCL without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "ALCO", 0);
        writeU32(out, "ALCA", 0);
        writeU32(out, "ALCL", 9);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto blocked = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases replaces existing Create Reference without ALCL") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "ALCO", 0);
        writeU32(out, "ALCA", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto replaced = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 3},
                {"name", "Near"},
                {"flags", 0},
                {"findMatchingNearAlias", {{"alias", 1}, {"type", 0}}}
            }
        })));
    REQUIRE(replaced.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto names = recordSignatures(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(names.back() == "NNAM");
    CHECK(aliasMembersUntilAled(names, "ALST") ==
          std::vector<std::string>{"ALST", "ALID", "FNAM", "ALNA", "ALNT", "ALED"});
}

TEST_CASE("Native SetQuestAliases refuses existing invalid ALCA create mode and ALNT type") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    auto tryRefuse = [&](auto body, const char* needle) {
        writeBytes(path, questPluginWith(0, body));
        const auto before = readBytes(path);
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto result = backend.invokeBackend(
            "SetQuestAliases",
            setAliasesArgs(nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(result.error().message.find(needle) != std::string::npos);
        CHECK(readBytes(path) == before);
    };

    tryRefuse(
        [](fo4::test::ByteWriter& out) {
            writeU32(out, "ANAM", 2);
            writeU32(out, "ALST", 1);
            fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
            writeU32(out, "FNAM", 0);
            writeU32(out, "ALCO", 0);
            fo4::test::ByteWriter alca;
            alca.u16(0);
            alca.u16(7);
            fo4::test::write_subrecord(out, "ALCA", alca.bytes);
            fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        },
        "create mode");

    tryRefuse(
        [](fo4::test::ByteWriter& out) {
            writeU32(out, "ANAM", 2);
            writeU32(out, "ALST", 1);
            fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
            writeU32(out, "FNAM", 0);
            writeU32(out, "ALNA", 0);
            writeU32(out, "ALNT", 2);
            fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        },
        "type");
}

TEST_CASE("Native SetQuestAliases refuses existing null ALFR without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "ALFR", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("forcedReference is null") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases refuses Fill Type members after alias CTDA") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        fo4::test::write_subrecord(out, "CTDA", makeCtdaPayload(8, 1, 1.0f));
        writeU32(out, "ALNA", 0);
        writeU32(out, "ALNT", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    auto before = readBytes(path);
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto result = backend.invokeBackend(
            "SetQuestAliases",
            setAliasesArgs(nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(result.error().message.find("out of order") != std::string::npos);
        CHECK(readBytes(path) == before);
    }

    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALLS", 1);
        writeU32(out, "FNAM", 0);
        fo4::test::write_subrecord(out, "CTDA", makeCtdaPayload(8, 1, 1.0f));
        writeU32(out, "ALFE", 9);
        writeU32(out, "ALFD", 2);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
    before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto location = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE_FALSE(location.has_value());
    CHECK(location.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(location.error().message.find("out of order") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases reports alias payload errors for condition helpers") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"flags", 0},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetAngle"}, {"param1", 1}, {"operator", "~~"}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestAliasPayload");
    CHECK(result.error().message.find("operator") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases writes remaining members in Mutagen order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, memberMasterPlugin());
    writeBytes(patchPath, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadExtraMaster(backend, masterPath, patchPath);

    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Rich"},
                {"flags", 0},
                {"keywords", nlohmann::json::array({"000804:Master.esm"})},
                {"spectatorOverridePackageList", "000800:Master.esm"},
                {"linkedAliases", nlohmann::json::array({
                    {{"keyword", "000804:Master.esm"}, {"alias", 2}}
                })},
                {"displayName", "000801:Master.esm"},
                {"forcedVoice", "000802:Master.esm"},
                {"deathItem", "000803:Master.esm"},
                {"spells", nlohmann::json::array({"000805:Master.esm"})},
                {"voiceTypes", "000802:Master.esm"},
                {"conditions", nlohmann::json::array({
                    {{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}}
                })}
            }
        })));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(aliasMembersUntilAled(
              recordSignatures(fo4::read_plugin_file(patchPath),
                               fo4::FormKey::parse("000800:Patch.esp")),
              "ALST") ==
          std::vector<std::string>{
              "ALST", "ALID", "FNAM", "CTDA", "KSIZ", "KWDA", "SPOR", "ALLA", "ALDN",
              "ALFV", "ALDI", "ALSP", "VTCK", "ALED"});
    CHECK(fo4::read_plugin_file(patchPath).header.masters.size() == 1u);
}

TEST_CASE("Native SetQuestAliases refuses remaining members on location aliases") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "location"},
                {"id", 2},
                {"flags", 0},
                {"displayName", "000800:Patch.esp"}
            }
        })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases refuses existing remaining-member coverage gaps") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    auto tryRefuse = [&](auto body, const char* needle) {
        writeBytes(path, questPluginWith(0, body));
        const auto before = readBytes(path);
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto result = backend.invokeBackend(
            "SetQuestAliases",
            setAliasesArgs(nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(result.error().message.find(needle) != std::string::npos);
        CHECK(readBytes(path) == before);
    };

    tryRefuse(
        [](fo4::test::ByteWriter& out) {
            writeU32(out, "ANAM", 2);
            writeU32(out, "ALST", 1);
            fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
            writeU32(out, "FNAM", 0);
            writeU32(out, "SPOR", 0);
            fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        },
        "zero-valued");

    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(3);
    hedr.u32(0x803);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    {
        const auto group = fo4::test::begin_group(out, "FLST", 0);
        const auto record = fo4::test::begin_record(out, "FLST", 0, 0x00000801);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"List\0", 5});
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    }
    {
        const auto group = fo4::test::begin_group(out, "MESG", 0);
        const auto record = fo4::test::begin_record(out, "MESG", 0, 0x00000802);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Msg\0", 4});
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    }
    {
        const auto group = fo4::test::begin_group(out, "QUST", 0);
        const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0);
        writeU32(out, "ALDN", 0x00000802);
        writeU32(out, "SPOR", 0x00000801);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        fo4::test::end_record(out, quest);
        fo4::test::end_group(out, group);
    }
    writeBytes(path, out.bytes);
    const auto beforeOrder = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto ordered = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
    REQUIRE_FALSE(ordered.has_value());
    CHECK(ordered.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(ordered.error().message.find("out of order") != std::string::npos);
    CHECK(readBytes(path) == beforeOrder);
}

TEST_CASE("Native SetQuestAliases writes multi-entry collections and preserves tail") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto beforeSave = readBytes(path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "collection"},
                {"entries", nlohmann::json::array({
                    {{"id", 5}, {"maxInitialFillCount", 2}},
                    {{"id", 7}}
                })}
            },
            {
                {"kind", "reference"},
                {"id", 2},
                {"name", "AfterCollection"},
                {"flags", 0}
            }
        })));
    REQUIRE(result.has_value());
    CHECK(readBytes(path) == beforeSave);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_aliases(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 2u);
    REQUIRE(decoded.aliases[0].collection_entries.size() == 2u);
    CHECK(decoded.aliases[0].collection_entries[0].id == 5u);
    CHECK(decoded.aliases[0].collection_entries[0].max_initial_fill_count ==
          std::optional<std::uint8_t>{std::uint8_t{2}});
    CHECK(decoded.aliases[0].collection_entries[1].id == 7u);
    CHECK_FALSE(decoded.aliases[0].collection_entries[1].max_initial_fill_count.has_value());
    CHECK(decoded.aliases[1].id == 2u);
    CHECK(decoded.next_alias_id == 3u);
    CHECK(recordSignatures(disk, fo4::FormKey::parse("000800:Patch.esp")).back() == "NNAM");
}

TEST_CASE("Native SetQuestAliases rewrites existing multi-entry collections") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 10);
        writeU32(out, "ALCS", 5);
        fo4::test::write_subrecord(out, "ALMI", std::vector<std::byte>{std::byte{2}});
        writeU32(out, "ALCS", 7);
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    }));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "collection"},
                {"entries", nlohmann::json::array({
                    {{"id", 8}, {"maxInitialFillCount", 1}},
                    {{"id", 9}, {"maxInitialFillCount", 4}}
                })}
            }
        })));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto decoded = fo4::forms::decode_quest_aliases(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 1u);
    REQUIRE(decoded.aliases[0].collection_entries.size() == 2u);
    CHECK(decoded.aliases[0].collection_entries[0].id == 8u);
    CHECK(decoded.aliases[0].collection_entries[1].id == 9u);
    CHECK(decoded.next_alias_id == 10u);
}

TEST_CASE("Native SetQuestAliases does not allocate reference IDs from collection entries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestAliases",
        setAliasesArgs(nlohmann::json::array({
            {
                {"kind", "collection"},
                {"entries", nlohmann::json::array({
                    {{"id", 100}},
                    {{"id", 101}}
                })}
            },
            {
                {"kind", "reference"},
                {"name", "AutoRef"},
                {"flags", 0}
            }
        })));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto decoded = fo4::forms::decode_quest_aliases(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 2u);
    REQUIRE(decoded.aliases[0].collection_entries.size() == 2u);
    CHECK(decoded.aliases[0].collection_entries[0].id == 100u);
    CHECK(decoded.aliases[1].kind == fo4::forms::QuestAliasKind::reference);
    CHECK(decoded.aliases[1].id == 0u);
    CHECK(decoded.next_alias_id == 2u);
}

TEST_CASE("Native SetQuestAliases refuses invalid collection payloads without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin());
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    auto refuse = [&](const nlohmann::json& aliases) {
        const auto result = backend.invokeBackend("SetQuestAliases", setAliasesArgs(aliases));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "InvalidQuestAliasPayload");
        CHECK(readBytes(path) == before);
    };

    refuse(nlohmann::json::array({
        {
            {"kind", "collection"},
            {"entries", nlohmann::json::array({
                {{"id", 1}},
                {{"id", 2}}
            })}
        },
        {
            {"kind", "collection"},
            {"entries", nlohmann::json::array({
                {{"id", 3}}
            })}
        }
    }));
    refuse(nlohmann::json::array({
        {
            {"kind", "collection"},
            {"id", 1},
            {"entries", nlohmann::json::array({{{"id", 1}}})}
        }
    }));
    refuse(nlohmann::json::array({
        {
            {"kind", "collection"},
            {"entries", nlohmann::json::array()}
        }
    }));
    refuse(nlohmann::json::array({
        {
            {"kind", "collection"},
            {"entries", nlohmann::json::array({
                {{"id", 1}},
                {{"id", 1}}
            })}
        }
    }));
    refuse(nlohmann::json::array({
        {
            {"kind", "collection"},
            {"entries", nlohmann::json::array({
                {{"id", 1}, {"alias", 2}}
            })}
        }
    }));
}
