#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
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
                {{"kind", "reference"}, {"id", 1}, {"flags", 0}, {"keywords", nlohmann::json::array()}}
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
    REQUIRE_FALSE(aliases.aliases.empty());
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
    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    CHECK(decoded.stages[0].log_entries[0].flags == 3u);
}
