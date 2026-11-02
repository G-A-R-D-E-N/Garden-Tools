#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/perk.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_perk_" + std::to_string(id));
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

void writeNamedRecord(fo4::test::ByteWriter& out, std::string_view type, std::uint32_t recordId,
                      std::string_view editorId) {
    const auto group = fo4::test::begin_group(out, type, 0);
    const auto record = fo4::test::begin_record(out, type, 0, recordId);
    fo4::test::write_subrecord(out, "EDID", editorId);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
}

std::vector<std::byte> makeMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 1u, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(3);
    hedr.u32(0x500);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    writeNamedRecord(out, "QUST", 0x00000100, std::string_view{"MasterQuest\0", 12});
    writeNamedRecord(out, "SPEL", 0x00000123, std::string_view{"MasterSpell\0", 12});
    writeNamedRecord(out, "MISC", 0x00000456, std::string_view{"NotSpell\0", 9});
    writeNamedRecord(out, "AVIF", 0x00000789, std::string_view{"MasterActorValue\0", 17});
    writeNamedRecord(out, "LVLI", 0x00000A00, std::string_view{"MasterList\0", 11});
    writeNamedRecord(out, "SNDR", 0x00000B00, std::string_view{"MasterSound\0", 12});
    return out.bytes;
}

std::vector<std::byte> makePatchPlugin(std::uint32_t flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x910);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esp\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "Test Mod"},
        {"resolved", true}
    };
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& masterPath,
                 const std::filesystem::path& patchPath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esp", masterPath),
            pluginEntry("Patch.esp", patchPath)
        })}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 2);
}

nlohmann::json perkPayload(nlohmann::json effects) {
    return {
        {"editorId", "TestPerk"},
        {"full", "Test Perk"},
        {"desc", "Test description"},
        {"trait", 1},
        {"level", 5},
        {"ranks", 3},
        {"playable", 0},
        {"effects", std::move(effects)}
    };
}

nlohmann::json questEffect() {
    return {{"type", "quest_stage"}, {"rank", 1}, {"priority", 2},
            {"quest", "000100:Master.esp"}, {"questStage", 10}};
}

nlohmann::json abilityEffect() {
    return {{"type", "ability"}, {"ability", "000123:Master.esp"}};
}

std::string createPerk(fo4ide::plugin::RadiclePluginBackend& backend, const nlohmann::json& payload) {
    const auto created = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", payload}));
    REQUIRE(created.has_value());
    return nlohmann::json::parse(created->get<std::string>())["formKey"].get<std::string>();
}

nlohmann::json readEffects(fo4ide::plugin::RadiclePluginBackend& backend,
                           const std::string& formKey) {
    const auto got = backend.getPerkEffects(nlohmann::json::array({"Patch.esp", formKey}));
    REQUIRE(got.has_value());
    return got->at("effects");
}

}  // namespace

TEST_CASE("Native CreateRecord authors a PERK whose header and effects persist through save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        questEffect(), abilityEffect()
    })));

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 2u);
    CHECK(listed[0]["type"] == "quest_stage");
    CHECK(listed[0]["quest"] == "000100:Master.esp");
    CHECK(listed[0]["questStage"] == 10);
    CHECK(listed[1]["ability"] == "000123:Master.esp");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto header = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& subrecord) { return subrecord.signature.to_string() == "DATA"; });
    REQUIRE(header != record->subrecords.end());
    CHECK(header->data == std::vector<std::byte>{
        std::byte{1}, std::byte{5}, std::byte{3}, std::byte{0}, std::byte{0}});
    const auto effects = fo4::forms::read_perk_effects(plugin, *record);
    REQUIRE(effects.size() == 2u);
    CHECK(effects[0].quest == fo4::FormKey::parse("000100:Master.esp"));
    CHECK(effects[0].quest_stage == 10);
    CHECK(effects[1].ability == fo4::FormKey::parse("000123:Master.esp"));
}

TEST_CASE("Native CreateRecord refuses effect FormKeys of the wrong type or unknown records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);

    const auto wrongAbility = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", perkPayload(nlohmann::json::array({
            {{"type", "ability"}, {"ability", "000100:Master.esp"}}
        }))}));
    REQUIRE_FALSE(wrongAbility.has_value());
    CHECK(wrongAbility.error().type == "InvalidTypedFormKey");

    const auto unknownQuest = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", perkPayload(nlohmann::json::array({
            {{"type", "quest_stage"}, {"quest", "000999:Master.esp"}, {"questStage", 1}}
        }))}));
    REQUIRE_FALSE(unknownQuest.has_value());
    CHECK(unknownQuest.error().type == "InvalidTypedFormKey");
}

TEST_CASE("Native SetPerkEffects replaces the effect list and leaves the record unchanged on refusal") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        questEffect(), abilityEffect()
    })));

    const auto refused = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({
            {{"type", "ability"}, {"ability", "000456:Master.esp"}}
        })
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidTypedFormKey");
    CHECK(readEffects(backend, formKey).size() == 2u);

    const auto replaced = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({
            {{"type", "entry_point"}, {"entryPoint", 3}, {"function", 5},
             {"parameters", {{"type", 4}, {"entryId", 7}, {"scriptFlags", 1}, {"data", "23010000"}}}}
        })
    }));
    REQUIRE(replaced.has_value());

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 1u);
    CHECK(listed[0]["type"] == "entry_point");
    CHECK(listed[0]["entryPoint"] == 3);
    CHECK(listed[0]["function"] == 5);
    CHECK(listed[0]["parameters"]["entryId"] == 7);
    CHECK(listed[0]["parameters"]["data"] == "23010000");
}

TEST_CASE("Native SetPerkEffects refuses EPFD data whose length does not match its EPFT type and leaves the record unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        questEffect(), abilityEffect()
    })));

    const auto refused = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({
            {{"type", "entry_point"}, {"entryPoint", 3}, {"function", 5},
             {"parameters", {{"type", 4}, {"data", "2301"}}}}
        })
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidPerkEffects");

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 2u);
    CHECK(listed[0]["type"] == "quest_stage");
    CHECK(listed[1]["type"] == "ability");
}

TEST_CASE("Native SetField edits PERK Level and Playable header fields and persists them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, nlohmann::json{{"editorId", "BarePerk"}});

    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", formKey, "PERK\\Level", "9"
    })).has_value());
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", formKey, "PERK\\Playable", "0"
    })).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto header = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& subrecord) { return subrecord.signature.to_string() == "DATA"; });
    REQUIRE(header != record->subrecords.end());
    CHECK(header->data == std::vector<std::byte>{
        std::byte{0}, std::byte{9}, std::byte{1}, std::byte{0}, std::byte{0}});
}

namespace {

nlohmann::json entryPoint(int function, nlohmann::json parameters) {
    return {{"type", "entry_point"}, {"entryPoint", 1}, {"function", function}, {"parameters", std::move(parameters)}};
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

void writeStringTable(const std::filesystem::path& path,
                      fo4::localization::StringTableKind kind,
                      std::uint32_t id,
                      std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    fo4::localization::write_string_table_file({kind, {{id, rawBytes(text)}}}, path);
}

}  // namespace

// Detects: typed EPFD fields must encode to the EPFD bytes for EPFT 1, 2, 5, 6 and 8 and read back as the same typed fields.
TEST_CASE("Native typed EPFD fields encode per EPFT and read back with their hex") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        entryPoint(1, {{"type", 1}, {"value", 2.5}}),
        entryPoint(4, {{"type", 2}, {"value", 1.5}, {"value2", -2.0}}),
        entryPoint(5, {{"type", 5}, {"form", "MasterSpell"}}),
        entryPoint(6, {{"type", 6}, {"text", "Hi"}}),
        entryPoint(5, {{"type", 8}, {"actorValue", "MasterActorValue"}, {"value", 3.0}}),
    })));

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 5u);
    CHECK(listed[0]["parameters"]["data"] == "00002040");
    CHECK(listed[0]["parameters"]["value"] == 2.5);
    CHECK(listed[1]["parameters"]["data"] == "0000c03f000000c0");
    CHECK(listed[1]["parameters"]["value"] == 1.5);
    CHECK(listed[1]["parameters"]["value2"] == -2.0);
    CHECK(listed[2]["parameters"]["data"] == "23010000");
    CHECK(listed[2]["parameters"]["form"] == "000123:Master.esp");
    CHECK(listed[3]["parameters"]["data"] == "486900");
    CHECK(listed[3]["parameters"]["text"] == "Hi");
    CHECK(listed[4]["parameters"]["data"] == "8907000000004040");
    CHECK(listed[4]["parameters"]["actorValue"] == "000789:Master.esp");
    CHECK(listed[4]["parameters"]["value"] == 3.0);
}

// Detects: typed parameters must refuse hex data with typed fields, typed fields that do not apply to the EPFT, and forms of the wrong record type.
TEST_CASE("Native typed EPFD refuses mixed, misplaced and wrong-typed fields") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array()));
    const auto refusedType = [&](nlohmann::json parameters) {
        const auto result = backend.setPerkEffects(nlohmann::json::array({
            "Patch.esp", formKey, nlohmann::json::array({entryPoint(5, std::move(parameters))})
        }));
        REQUIRE_FALSE(result.has_value());
        return result.error().type;
    };
    CHECK(refusedType({{"type", 1}, {"data", "00002040"}, {"value", 1.0}}) == "InvalidPerkEffects");
    CHECK(refusedType({{"type", 1}, {"form", "MasterSpell"}, {"value", 1.0}}) == "InvalidPerkEffects");
    CHECK(refusedType({{"type", 8}, {"value", 1.0}}) == "InvalidPerkEffects");
    CHECK(refusedType({{"type", 5}, {"form", "MasterList"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"type", 3}, {"form", "MasterSpell"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"type", 5}, {"form", "NoSuchRecord"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"type", 8}, {"actorValue", "MasterSpell"}, {"value", 1.0}}) == "InvalidTypedFormKey");
}

// Detects: create_record must write conditions, nextPerk and sound with their type checks, and get_perk_links must read them back.
TEST_CASE("Native CreateRecord writes PERK conditions, next perk and sound and refuses other types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto nextKey = createPerk(backend, nlohmann::json{{"editorId", "NextPerk"}});
    const nlohmann::json condition{{"function", "GetStage"}, {"operator", ">="}, {"value", 5}, {"param1", 2}, {"runOn", "Subject"}};
    const auto formKey = createPerk(backend, nlohmann::json{
        {"editorId", "LinkedPerk"},
        {"conditions", nlohmann::json::array({condition})},
        {"nextPerk", nextKey},
        {"sound", "000B00:Master.esp"},
        {"swf", "Interface\\Perk.swf"}
    });

    const auto links = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", formKey}));
    REQUIRE(links.has_value());
    CHECK((*links)["nextPerk"] == nextKey);
    CHECK((*links)["sound"] == "000B00:Master.esp");
    CHECK((*links)["swf"] == "Interface\\Perk.swf");
    REQUIRE((*links)["conditions"].size() == 1u);
    CHECK((*links)["conditions"][0]["function"] == "GetStage");

    const auto refusedType = [&](nlohmann::json payload) {
        const auto result = backend.invokeBackend(
            "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", payload}));
        REQUIRE_FALSE(result.has_value());
        return result.error().type;
    };
    CHECK(refusedType({{"editorId", "BadNext"}, {"nextPerk", "000123:Master.esp"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"editorId", "BadSound"}, {"sound", "000100:Master.esp"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"editorId", "BadNextText"}, {"nextPerk", "NextPerk"}}) == "InvalidFormKey");
}

// Detects: set_perk_next must refuse a target that is not a PERK by name and leave the record unchanged.
TEST_CASE("Native SetPerkNext links a PERK and refuses a non-PERK target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto perk = createPerk(backend, nlohmann::json{{"editorId", "LinkingPerk"}});
    const auto next = createPerk(backend, nlohmann::json{{"editorId", "FollowingPerk"}});

    const auto refused = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "000123:Master.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidTypedFormKey");
    const auto unlinked = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(unlinked.has_value());
    CHECK((*unlinked)["nextPerk"].is_null());

    REQUIRE(backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, next})).has_value());
    const auto linked = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(linked.has_value());
    CHECK((*linked)["nextPerk"] == next);

    const auto invalid = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "FollowingPerk"}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidPerkNext");
}

// Detects: a localized PERK must store FULL and DESC as string IDs after the set-wide maximum and never renumber existing IDs.
TEST_CASE("Native CreateRecord writes localized PERK text as string IDs past the existing maximum") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin(0x80));
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 7, "Existing name");
    writeStringTable(root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings, 9, "Existing description");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, nlohmann::json{
        {"editorId", "LocalPerk"}, {"full", "New name"}, {"desc", "New description"}
    });
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto subrecordData = [record](std::string_view signature) {
        const auto found = std::find_if(record->subrecords.begin(), record->subrecords.end(),
            [signature](const fo4::Subrecord& subrecord) { return subrecord.signature.to_string() == signature; });
        REQUIRE(found != record->subrecords.end());
        return found->data;
    };
    CHECK(subrecordData("FULL") == std::vector<std::byte>{std::byte{10}, std::byte{0}, std::byte{0}, std::byte{0}});
    CHECK(subrecordData("DESC") == std::vector<std::byte>{std::byte{11}, std::byte{0}, std::byte{0}, std::byte{0}});

    const auto names = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS"}));
    REQUIRE(names.has_value());
    const auto nameRows = nlohmann::json::parse(names->get<std::string>())["rows"];
    REQUIRE(nameRows.size() == 2u);
    CHECK(nameRows[0] == nlohmann::json{{"id", 7}, {"text", "Existing name"}});
    CHECK(nameRows[1] == nlohmann::json{{"id", 10}, {"text", "New name"}});
    const auto descriptions = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "", "DLSTRINGS"}));
    REQUIRE(descriptions.has_value());
    const auto descriptionRows = nlohmann::json::parse(descriptions->get<std::string>())["rows"];
    REQUIRE(descriptionRows.size() == 2u);
    CHECK(descriptionRows[0] == nlohmann::json{{"id", 9}, {"text", "Existing description"}});
    CHECK(descriptionRows[1] == nlohmann::json{{"id", 11}, {"text", "New description"}});
}
