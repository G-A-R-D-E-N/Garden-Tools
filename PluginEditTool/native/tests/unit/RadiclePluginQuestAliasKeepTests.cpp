#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

using SubrecordBytes = std::vector<std::pair<std::string, std::vector<std::byte>>>;

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_quest_alias_keep_" + std::to_string(id));
}

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
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void writeU32(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

void writeMasterRecord(fo4::test::ByteWriter& out,
                       std::string_view type,
                       std::uint32_t localId,
                       std::string_view edid) {
    const auto group = fo4::test::begin_group(out, type, 0);
    const auto record = fo4::test::begin_record(out, type, 0, localId);
    const std::string text = std::string(edid) + '\0';
    fo4::test::write_subrecord(out, "EDID", std::string_view{text});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
}

std::vector<std::byte> keepMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(14);
    hedr.u32(0x80E);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    writeMasterRecord(out, "KYWD", 0x800, "KeepKw");
    writeMasterRecord(out, "MISC", 0x801, "KeepItem");
    writeMasterRecord(out, "SPEL", 0x802, "KeepSpell");
    writeMasterRecord(out, "FACT", 0x803, "KeepFaction");
    writeMasterRecord(out, "PACK", 0x804, "KeepPack");
    writeMasterRecord(out, "NPC_", 0x805, "KeepNpc");
    writeMasterRecord(out, "LCTN", 0x806, "KeepLoc");
    writeMasterRecord(out, "REFR", 0x807, "KeepRef");
    writeMasterRecord(out, "LCRT", 0x808, "KeepRefType");
    writeMasterRecord(out, "FLST", 0x809, "KeepList");
    writeMasterRecord(out, "MESG", 0x80A, "KeepMsg");
    writeMasterRecord(out, "VTYP", 0x80B, "KeepVoice");
    writeMasterRecord(out, "LVLI", 0x80C, "KeepLvli");
    writeMasterRecord(out, "GLOB", 0x80D, "KeepGlob");
    return out.bytes;
}

template <typename Body>
std::vector<std::byte> keepQuestWith(Body&& body, bool withOwnItem = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(withOwnItem ? 0x802u : 0x801u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"KeepQuest\0", 10});
    body(out);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    if (withOwnItem) writeMasterRecord(out, "MISC", 0x801, "KeepOwnItem");
    return out.bytes;
}

std::vector<std::byte> keepQuestPlugin() {
    return keepQuestWith([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0x10);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    });
}

struct KeepFixture {
    std::filesystem::path root;
    std::filesystem::path masterPath;
    std::filesystem::path patchPath;

    KeepFixture()
        : root(tempRoot()), masterPath(root / "Master.esm"), patchPath(root / "Patch.esp") {
        writeBytes(masterPath, keepMasterPlugin());
        writeBytes(patchPath, keepQuestPlugin());
    }
    ~KeepFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    KeepFixture(const KeepFixture&) = delete;
    KeepFixture& operator=(const KeepFixture&) = delete;
};

void loadKeep(fo4ide::plugin::RadiclePluginBackend& backend, const KeepFixture& fixture) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", fixture.masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", fixture.patchPath.string()},
                {"source", "QuestTest"},
                {"resolved", true}
            }
        })}}).has_value());
}

nlohmann::json aliasArgs(const nlohmann::json& aliases) {
    return nlohmann::json::array({"Patch.esp", "000800:Patch.esp", aliases});
}

void setAliases(fo4ide::plugin::RadiclePluginBackend& backend, const nlohmann::json& aliases) {
    const auto result = backend.invokeBackend("SetQuestAliases", aliasArgs(aliases));
    REQUIRE(result.has_value());
}

nlohmann::json getAliases(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result = backend.invokeBackend(
        "GetQuestAliases", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE(result.has_value());
    return result->at("aliases");
}

void savePatch(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

SubrecordBytes questSubrecords(const std::filesystem::path& path) {
    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    SubrecordBytes out;
    for (const auto& sub : record->subrecords) {
        out.emplace_back(std::string(sub.signature.to_string()), sub.data);
    }
    return out;
}

SubrecordBytes withoutSignatures(const SubrecordBytes& subs,
                                 const std::vector<std::string>& skip) {
    SubrecordBytes out;
    for (const auto& sub : subs) {
        if (std::find(skip.begin(), skip.end(), sub.first) == skip.end()) out.push_back(sub);
    }
    return out;
}

std::size_t countSignature(const SubrecordBytes& subs, std::string_view signature) {
    return static_cast<std::size_t>(std::count_if(
        subs.begin(), subs.end(), [signature](const auto& sub) {
            return sub.first == std::string(signature);
        }));
}

std::vector<std::string> aliasMembers(const SubrecordBytes& subs, std::string_view start) {
    std::vector<std::string> members;
    bool inside = false;
    for (const auto& sub : subs) {
        if (sub.first == std::string(start)) inside = true;
        if (!inside) continue;
        members.push_back(sub.first);
        if (sub.first == "ALED") break;
    }
    return members;
}

fo4::forms::QuestAliases decodeKeep(const std::filesystem::path& path) {
    return fo4::forms::decode_quest_aliases(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
}

nlohmann::json expectGetterRoundTripIdentical(const KeepFixture& fixture,
                                              const nlohmann::json& written) {
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        loadKeep(backend, fixture);
        setAliases(backend, written);
        savePatch(backend);
    }
    const auto first = questSubrecords(fixture.patchPath);

    fo4ide::plugin::RadiclePluginBackend reloaded;
    loadKeep(reloaded, fixture);
    const auto payload = getAliases(reloaded);
    REQUIRE(payload.is_array());
    REQUIRE(payload.size() == written.size());
    setAliases(reloaded, payload);
    savePatch(reloaded);
    CHECK(questSubrecords(fixture.patchPath) == first);
    return payload;
}

}

TEST_CASE("Native QUST alias keep round trip is byte-identical for forcedReference") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Forced"}, {"flags", 0x10},
                       {"forceIntoAlias", 2}, {"forcedReference", "000807:Master.esm"}}
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    const auto* fill =
        std::get_if<fo4::forms::QuestSpecificReferenceFill>(&decoded.aliases[0].fill);
    REQUIRE(fill != nullptr);
    CHECK(fill->reference == std::optional<fo4::FormKey>{fo4::FormKey::parse("000807:Master.esm")});
    CHECK(decoded.aliases[0].force_into_alias == std::optional<std::int32_t>{2});
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for uniqueActor") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Actor"}, {"flags", 0},
                       {"uniqueActor", "000805:Master.esm"}}
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    const auto* fill = std::get_if<fo4::forms::QuestUniqueActorFill>(&decoded.aliases[0].fill);
    REQUIRE(fill != nullptr);
    CHECK(fill->actor == fo4::FormKey::parse("000805:Master.esm"));
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for specificLocation") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"flags", 0},
                       {"specificLocation", "000806:Master.esm"}, {"closestToAlias", 1}}
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 2u);
    CHECK(decoded.aliases[1].kind == fo4::forms::QuestAliasKind::location);
    const auto* fill =
        std::get_if<fo4::forms::QuestSpecificLocationFill>(&decoded.aliases[1].fill);
    REQUIRE(fill != nullptr);
    CHECK(fill->location == fo4::FormKey::parse("000806:Master.esm"));
    CHECK(decoded.aliases[1].closest_to_alias == std::optional<std::int32_t>{1});
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for createReference") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Made"}, {"flags", 0},
                       {"createReference", nlohmann::json{{"object", "000805:Master.esm"},
                                                          {"alias", -2}, {"create", "In"},
                                                          {"level", "Hard"}}}}
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    const auto* fill =
        std::get_if<fo4::forms::QuestCreateReferenceFill>(&decoded.aliases[0].fill);
    REQUIRE(fill != nullptr);
    CHECK(fill->alias == -2);
    CHECK(fill->create_mode == 0x8000u);
    CHECK(fill->level == 2u);
}

TEST_CASE("Native QUST alias keep wizard fills survive a reload and getter round trip") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Actor"}, {"flags", 0},
                       {"uniqueActor", "000805:Master.esm"}},
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"flags", 0},
                       {"specificLocation", "000806:Master.esm"}},
        nlohmann::json{{"kind", "reference"}, {"id", 3}, {"name", "Made"}, {"flags", 0},
                       {"createReference", nlohmann::json{{"object", "000805:Master.esm"},
                                                          {"alias", -2}, {"create", "At"},
                                                          {"level", "None"}}}}
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 3u);
    CHECK(std::holds_alternative<fo4::forms::QuestUniqueActorFill>(decoded.aliases[0].fill));
    CHECK(std::holds_alternative<fo4::forms::QuestSpecificLocationFill>(decoded.aliases[1].fill));
    const auto* create =
        std::get_if<fo4::forms::QuestCreateReferenceFill>(&decoded.aliases[2].fill);
    REQUIRE(create != nullptr);
    CHECK(create->level == 4u);
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for keywords items linkedAliases spells factions and packages") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{
            {"kind", "reference"}, {"id", 1}, {"name", "Rich"}, {"flags", 2},
            {"keywords", nlohmann::json::array({"000800:Master.esm"})},
            {"items", nlohmann::json::array({
                nlohmann::json{{"item", "000801:Master.esm"}, {"count", 2},
                               {"extra", nlohmann::json{{"owner", "000805:Master.esm"},
                                                        {"global", "00080D:Master.esm"},
                                                        {"itemCondition", 0.5}}}},
                nlohmann::json{{"item", "000801:Master.esm"}, {"count", 1}}
            })},
            {"linkedAliases", nlohmann::json::array({
                nlohmann::json{{"keyword", "000800:Master.esm"}, {"alias", 4}}
            })},
            {"spells", nlohmann::json::array({"000802:Master.esm"})},
            {"factions", nlohmann::json::array({"000803:Master.esm"})},
            {"packages", nlohmann::json::array({"000804:Master.esm"})}
        }
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    const auto& alias = decoded.aliases[0];
    CHECK(alias.keywords.size() == 1u);
    REQUIRE(alias.items.size() == 2u);
    CHECK(alias.items[0].count == 2);
    REQUIRE(alias.items[0].extra.has_value());
    CHECK(alias.items[0].extra->global == std::optional<fo4::FormKey>{fo4::FormKey::parse("00080D:Master.esm")});
    REQUIRE(alias.linked_aliases.size() == 1u);
    CHECK(alias.linked_aliases[0].alias == 4);
    CHECK(alias.spells.size() == 1u);
    CHECK(alias.factions.size() == 1u);
    CHECK(alias.packages.size() == 1u);
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for conditions") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{
            {"kind", "reference"}, {"id", 1}, {"name", "Cond"}, {"flags", 0},
            {"conditions", nlohmann::json::array({
                nlohmann::json{{"function", "GetIsID"}, {"param1", "000805:Master.esm"},
                               {"compareGlobal", "00080D:Master.esm"}},
                nlohmann::json{{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}},
                nlohmann::json{{"function", "GetGraphVariableInt"}, {"cis1", "MyVar"},
                               {"value", 1.0}}
            })}
        }
    });
    const auto payload = expectGetterRoundTripIdentical(fixture, written);
    REQUIRE(payload.size() == 1u);
    CHECK(payload.at(0).at("conditions").size() == 3u);
    const auto subs = questSubrecords(fixture.patchPath);
    CHECK(countSignature(subs, "CTDA") == 3u);
    CHECK(countSignature(subs, "CIS1") == 1u);
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for display name voice types and package overrides") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{
            {"kind", "reference"}, {"id", 1}, {"name", "Lists"}, {"flags", 0},
            {"spectatorOverridePackageList", "000809:Master.esm"},
            {"observeDeadBodyOverridePackageList", "000809:Master.esm"},
            {"guardWarnOverridePackageList", "000809:Master.esm"},
            {"combatOverridePackageList", "000809:Master.esm"},
            {"displayName", "00080A:Master.esm"},
            {"forcedVoice", "00080B:Master.esm"},
            {"deathItem", "00080C:Master.esm"},
            {"voiceTypes", "00080B:Master.esm"}
        }
    });
    expectGetterRoundTripIdentical(fixture, written);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    const auto& alias = decoded.aliases[0];
    CHECK(alias.display_name == std::optional<fo4::FormKey>{fo4::FormKey::parse("00080A:Master.esm")});
    CHECK(alias.forced_voice == std::optional<fo4::FormKey>{fo4::FormKey::parse("00080B:Master.esm")});
    CHECK(alias.death_item == std::optional<fo4::FormKey>{fo4::FormKey::parse("00080C:Master.esm")});
    CHECK(alias.voice_types == std::optional<fo4::FormKey>{fo4::FormKey::parse("00080B:Master.esm")});
    CHECK(alias.spectator_override == std::optional<fo4::FormKey>{fo4::FormKey::parse("000809:Master.esm")});
}

namespace {

nlohmann::json fullReference() {
    return nlohmann::json::array({
        nlohmann::json{
            {"kind", "reference"}, {"id", 1}, {"name", "Full"}, {"flags", 0x10},
            {"forceIntoAlias", 3}, {"forcedReference", "000807:Master.esm"},
            {"keywords", nlohmann::json::array({"000800:Master.esm"})},
            {"items", nlohmann::json::array({
                nlohmann::json{{"item", "000801:Master.esm"}, {"count", 2}}
            })},
            {"linkedAliases", nlohmann::json::array({
                nlohmann::json{{"keyword", "000800:Master.esm"}, {"alias", 4}}
            })},
            {"spells", nlohmann::json::array({"000802:Master.esm"})},
            {"factions", nlohmann::json::array({"000803:Master.esm"})},
            {"packages", nlohmann::json::array({"000804:Master.esm"})},
            {"displayName", "00080A:Master.esm"},
            {"conditions", nlohmann::json::array({
                nlohmann::json{{"function", "GetAngle"}, {"param1", 1}, {"value", 1.0}}
            })}
        }
    });
}

}

TEST_CASE("Native QUST alias keep web-style payload keeps fill keywords items and conditions") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, fullReference());
    savePatch(backend);
    const auto before = questSubrecords(fixture.patchPath);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Renamed"}, {"flags", 0x20}}
    }));
    savePatch(backend);
    const auto after = questSubrecords(fixture.patchPath);

    CHECK(withoutSignatures(after, {"ALID", "FNAM"}) == withoutSignatures(before, {"ALID", "FNAM"}));
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    CHECK(decoded.aliases[0].name == std::optional<std::string>{"Renamed"});
    CHECK(decoded.aliases[0].flags == std::optional<std::uint32_t>{0x20u});
    CHECK(std::holds_alternative<fo4::forms::QuestSpecificReferenceFill>(decoded.aliases[0].fill));
    CHECK(decoded.aliases[0].keywords.size() == 1u);
    CHECK(decoded.aliases[0].items.size() == 1u);
    CHECK(countSignature(after, "CTDA") == 1u);
}

TEST_CASE("Native QUST alias keep omitted keys keep data while an explicit empty array clears one key") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, fullReference());
    savePatch(backend);
    const auto before = questSubrecords(fixture.patchPath);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Full"}, {"flags", 0x10},
                       {"keywords", nlohmann::json::array()}}
    }));
    savePatch(backend);
    const auto after = questSubrecords(fixture.patchPath);

    CHECK(withoutSignatures(after, {"KSIZ", "KWDA"}) == withoutSignatures(before, {"KSIZ", "KWDA"}));
    CHECK(countSignature(after, "KWDA") == 0u);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    CHECK(decoded.aliases[0].keywords.empty());
    CHECK(decoded.aliases[0].items.size() == 1u);
    CHECK(decoded.aliases[0].spells.size() == 1u);
    CHECK(std::holds_alternative<fo4::forms::QuestSpecificReferenceFill>(decoded.aliases[0].fill));
}

TEST_CASE("Native QUST alias keep explicit null and empty values clear carried reference data") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, fullReference());
    savePatch(backend);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{
            {"kind", "reference"}, {"id", 1}, {"name", nullptr}, {"flags", nullptr},
            {"forceIntoAlias", nullptr}, {"forcedReference", nullptr},
            {"keywords", nlohmann::json::array()}, {"items", nlohmann::json::array()},
            {"linkedAliases", nlohmann::json::array()}, {"spells", nlohmann::json::array()},
            {"factions", nlohmann::json::array()}, {"packages", nlohmann::json::array()},
            {"spectatorOverridePackageList", nullptr}, {"displayName", nullptr},
            {"conditions", nlohmann::json::array()}
        }
    }));
    savePatch(backend);

    const auto subs = questSubrecords(fixture.patchPath);
    CHECK(aliasMembers(subs, "ALST") == std::vector<std::string>{"ALST", "ALID", "FNAM", "ALED"});
    CHECK(countSignature(subs, "CTDA") == 0u);
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    CHECK(decoded.aliases[0].name == std::optional<std::string>{"Alias1"});
    CHECK(decoded.aliases[0].flags == std::optional<std::uint32_t>{0u});
    CHECK(decoded.aliases[0].force_into_alias == std::nullopt);
    CHECK(std::holds_alternative<std::monostate>(decoded.aliases[0].fill));
    CHECK(decoded.aliases[0].keywords.empty());
    CHECK(decoded.aliases[0].items.empty());
    CHECK(decoded.aliases[0].display_name == std::nullopt);
}

TEST_CASE("Native QUST alias keep explicit null name and flags clear a location alias") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"name", "LocName"}, {"flags", 0x40},
                       {"specificLocation", "000806:Master.esm"}}
    }));
    savePatch(backend);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"name", nullptr}, {"flags", nullptr},
                       {"specificLocation", nullptr}}
    }));
    savePatch(backend);

    const auto subs = questSubrecords(fixture.patchPath);
    CHECK(aliasMembers(subs, "ALLS") == std::vector<std::string>{"ALLS", "FNAM", "ALED"});
    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 2u);
    CHECK(decoded.aliases[1].name == std::nullopt);
    CHECK(decoded.aliases[1].flags == std::optional<std::uint32_t>{0u});
    CHECK(std::holds_alternative<std::monostate>(decoded.aliases[1].fill));
}

TEST_CASE("Native QUST alias keep refuses kind change when the previous Fill Type cannot carry") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"name", "Loc"}, {"flags", 0},
                       {"specificLocation", "000806:Master.esm"}}
    }));
    savePatch(backend);
    const auto before = readBytes(fixture.patchPath);

    const auto result = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "reference"}, {"id", 2}, {"name", "Loc"}, {"flags", 0}}
    })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(fixture.patchPath) == before);
}

TEST_CASE("Native QUST alias keep refuses a location to reference change that drops closestToAlias") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"name", "Loc"}, {"flags", 0},
                       {"closestToAlias", 1}}
    }));
    savePatch(backend);
    const auto before = readBytes(fixture.patchPath);

    const auto result = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 2}, {"name", "Ref"}, {"flags", 0}}
    })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestAliasPayload");
    CHECK(result.error().message.find("closestToAlias") != std::string::npos);
    CHECK(readBytes(fixture.patchPath) == before);
}

TEST_CASE("Native QUST alias keep refuses a reference to location change with carried reference data") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"keywords", nlohmann::json::array({"000800:Master.esm"})}}
    }));
    savePatch(backend);
    const auto before = readBytes(fixture.patchPath);

    const auto result = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
        nlohmann::json{{"kind", "location"}, {"id", 1}, {"flags", 0}}
    })));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestAliasPayload");
    CHECK(result.error().message.find("reference-only") != std::string::npos);
    CHECK(readBytes(fixture.patchPath) == before);
}

TEST_CASE("Native QUST alias keep accepts a kind change when the fill and closestToAlias are sent as null") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "location"}, {"id", 2}, {"name", "Loc"}, {"flags", 0},
                       {"specificLocation", "000806:Master.esm"}, {"closestToAlias", 1}}
    }));
    savePatch(backend);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}},
        nlohmann::json{{"kind", "reference"}, {"id", 2}, {"name", "Loc"}, {"flags", 0},
                       {"specificLocation", nullptr}, {"closestToAlias", nullptr}}
    }));
    savePatch(backend);

    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 2u);
    CHECK(decoded.aliases[1].kind == fo4::forms::QuestAliasKind::reference);
    CHECK(std::holds_alternative<std::monostate>(decoded.aliases[1].fill));
    CHECK(decoded.aliases[1].closest_to_alias == std::nullopt);
}

TEST_CASE("Native QUST alias keep round trip is byte-identical for a plain multi-entry collection") {
    KeepFixture fixture;
    const auto written = nlohmann::json::array({
        nlohmann::json{{"kind", "collection"}, {"entries", nlohmann::json::array({
            nlohmann::json{{"id", 5}, {"maxInitialFillCount", 2}},
            nlohmann::json{{"id", 7}}
        })}},
        nlohmann::json{{"kind", "reference"}, {"id", 2}, {"name", "After"}, {"flags", 0}}
    });
    const auto payload = expectGetterRoundTripIdentical(fixture, written);
    REQUIRE(payload.size() == 2u);
    CHECK(payload.at(0).at("kind") == "collection");
    CHECK(payload.at(0).at("entries").size() == 2u);

    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 2u);
    REQUIRE(decoded.aliases[0].collection_entries.size() == 2u);
    CHECK(decoded.aliases[0].collection_entries[0].id == 5u);
    CHECK(decoded.aliases[0].collection_entries[1].id == 7u);
}

TEST_CASE("Native QUST alias keep accepts a collection payload with name null and refuses a non-null collection name") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "collection"}, {"name", nullptr}, {"entries", nlohmann::json::array({
            nlohmann::json{{"id", 5}}
        })}}
    }));
    savePatch(backend);
    const auto accepted = decodeKeep(fixture.patchPath);
    REQUIRE(accepted.aliases.size() == 1u);
    CHECK(accepted.aliases[0].kind == fo4::forms::QuestAliasKind::collection);

    const auto before = readBytes(fixture.patchPath);
    const auto refused = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
        nlohmann::json{{"kind", "collection"}, {"name", "Group"}, {"entries", nlohmann::json::array({
            nlohmann::json{{"id", 5}}
        })}}
    })));
    CHECK_FALSE(refused.has_value());
    CHECK(readBytes(fixture.patchPath) == before);
}

TEST_CASE("Native QUST alias keep refuses a quest with a zero-valued optional FormLink") {
    KeepFixture fixture;
    const auto refuseWith = [&](std::vector<std::byte> bytes) {
        writeBytes(fixture.patchPath, bytes);
        fo4ide::plugin::RadiclePluginBackend backend;
        loadKeep(backend, fixture);
        const auto before = readBytes(fixture.patchPath);
        const auto result = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
            nlohmann::json{{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
        })));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(readBytes(fixture.patchPath) == before);
    };

    refuseWith(keepQuestWith([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0x10);
        writeU32(out, "ALDN", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));

    refuseWith(keepQuestWith([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0x10);
        writeU32(out, "KSIZ", 1);
        writeU32(out, "KWDA", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));

    refuseWith(keepQuestWith([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALLS", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0x10);
        writeU32(out, "ALFA", 1);
        writeU32(out, "KNAM", 0);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }));
}

TEST_CASE("Native QUST alias keep own-plugin CTDA FormKey still resolves after a keyword adds a master") {
    KeepFixture fixture;
    writeBytes(fixture.patchPath, keepQuestWith([](fo4::test::ByteWriter& out) {
        writeU32(out, "ANAM", 2);
        writeU32(out, "ALST", 1);
        fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
        writeU32(out, "FNAM", 0x10);
        fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    }, true));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Own"}, {"flags", 0},
                       {"conditions", nlohmann::json::array({
                           nlohmann::json{{"function", "GetIsID"},
                                          {"param1", "000801:Patch.esp"}, {"value", 1.0}}
                       })}}
    }));
    savePatch(backend);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Own"}, {"flags", 0}},
        nlohmann::json{{"kind", "reference"}, {"id", 2}, {"name", "Kw"}, {"flags", 0},
                       {"keywords", nlohmann::json::array({"000800:Master.esm"})}}
    }));
    savePatch(backend);

    fo4ide::plugin::RadiclePluginBackend reloaded;
    loadKeep(reloaded, fixture);
    const auto payload = getAliases(reloaded);
    REQUIRE(payload.size() == 2u);
    REQUIRE(payload.at(0).at("conditions").size() == 1u);
    CHECK(fo4::FormKey::parse(payload.at(0).at("conditions").at(0).at("param1").get<std::string>()) ==
          fo4::FormKey::parse("000801:Patch.esp"));
    CHECK(payload.at(1).at("keywords").size() == 1u);
}

TEST_CASE("Native QUST alias keep reference to location accepts explicit null and empty extras") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 1}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}}
    }));
    savePatch(backend);

    setAliases(backend, nlohmann::json::array({
        nlohmann::json{
            {"kind", "location"}, {"id", 1}, {"name", "Loc"}, {"flags", 0},
            {"specificLocation", "000806:Master.esm"},
            {"keywords", nullptr}, {"items", nlohmann::json::array()},
            {"linkedAliases", nlohmann::json::array()}, {"spells", nlohmann::json::array()},
            {"factions", nlohmann::json::array()}, {"packages", nlohmann::json::array()},
            {"spectatorOverridePackageList", nullptr},
            {"observeDeadBodyOverridePackageList", nullptr},
            {"guardWarnOverridePackageList", nullptr}, {"combatOverridePackageList", nullptr},
            {"displayName", nullptr}, {"forcedVoice", nullptr}, {"deathItem", nullptr},
            {"voiceTypes", nullptr}
        }
    }));
    savePatch(backend);

    const auto decoded = decodeKeep(fixture.patchPath);
    REQUIRE(decoded.aliases.size() == 1u);
    CHECK(decoded.aliases[0].kind == fo4::forms::QuestAliasKind::location);
    CHECK(std::holds_alternative<fo4::forms::QuestSpecificLocationFill>(decoded.aliases[0].fill));
    CHECK(decoded.aliases[0].keywords.empty());
    CHECK(decoded.aliases[0].display_name == std::nullopt);
}

TEST_CASE("Native QUST alias keep refuses converting an existing reference alias with a fill to a collection") {
    KeepFixture fixture;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadKeep(backend, fixture);
    setAliases(backend, nlohmann::json::array({
        nlohmann::json{{"kind", "reference"}, {"id", 5}, {"name", "Ref"}, {"flags", 0},
                       {"forcedReference", "000807:Master.esm"}}
    }));
    savePatch(backend);
    const auto before = readBytes(fixture.patchPath);

    const auto entries = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
        nlohmann::json{{"kind", "collection"}, {"entries", nlohmann::json::array({
            nlohmann::json{{"id", 5}}
        })}}
    })));
    REQUIRE_FALSE(entries.has_value());
    CHECK(entries.error().type == "InvalidQuestAliasPayload");

    const auto sugar = backend.invokeBackend("SetQuestAliases", aliasArgs(nlohmann::json::array({
        nlohmann::json{{"kind", "collection"}, {"id", 5}}
    })));
    REQUIRE_FALSE(sugar.has_value());
    CHECK(sugar.error().type == "InvalidQuestAliasPayload");
    CHECK(readBytes(fixture.patchPath) == before);
}
