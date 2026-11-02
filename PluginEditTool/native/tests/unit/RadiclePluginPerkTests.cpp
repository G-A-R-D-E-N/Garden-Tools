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
    return out.bytes;
}

std::vector<std::byte> makePatchPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
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
