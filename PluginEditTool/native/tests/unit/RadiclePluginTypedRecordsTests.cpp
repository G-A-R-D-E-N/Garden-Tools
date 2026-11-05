#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/faction.hpp>
#include <fo4/forms/npc.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path typedTempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_typed_records_" + id);
}

struct TypedCleanup {
    std::filesystem::path root;
    ~TypedCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void typedWriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> typedReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char ch : raw) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return bytes;
}

void writeHeader(fo4::test::ByteWriter& out,
                 std::initializer_list<std::string_view> masters,
                 std::uint32_t flags = 0) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        std::vector<std::byte> name;
        name.reserve(master.size() + 1);
        for (const char ch : master) {
            name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

std::vector<std::byte> basePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    return out.bytes;
}

std::vector<std::byte> npcPatchPlugin(std::uint32_t flags = 0) {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});

    const auto top = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", flags, 0x01000800u);
    fo4::test::ByteWriter snam;
    snam.u32(0x00000900u);
    snam.u8(3);
    fo4::test::write_subrecord(out, "SNAM", snam.bytes);
    fo4::test::ByteWriter prkz;
    prkz.u32(1);
    fo4::test::write_subrecord(out, "PRKZ", prkz.bytes);
    fo4::test::ByteWriter prkr;
    prkr.u32(0x00000901u);
    prkr.u8(1);
    fo4::test::write_subrecord(out, "PRKR", prkr.bytes);
    fo4::test::ByteWriter coct;
    coct.u32(1);
    fo4::test::write_subrecord(out, "COCT", coct.bytes);
    fo4::test::ByteWriter cnto;
    cnto.u32(0x00000902u);
    cnto.i32(2);
    fo4::test::write_subrecord(out, "CNTO", cnto.bytes);
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> factionPatchPlugin(bool localized = false) {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"}, localized ? fo4::RecordFlags::localized : 0u);

    const auto top = fo4::test::begin_group(out, "FACT", 0);
    const auto faction = fo4::test::begin_record(out, "FACT", 0, 0x01000810u);
    fo4::test::ByteWriter xnam;
    xnam.u32(0x00000900u);
    xnam.i32(5);
    xnam.u32(2);
    fo4::test::write_subrecord(out, "XNAM", xnam.bytes);
    fo4::test::ByteWriter rnam;
    rnam.u32(1);
    fo4::test::write_subrecord(out, "RNAM", rnam.bytes);
    if (localized) {
        fo4::test::ByteWriter mnam;
        mnam.u32(0x00001234u);
        fo4::test::write_subrecord(out, "MNAM", mnam.bytes);
    } else {
        fo4::test::write_subrecord(out, "MNAM", std::string_view{"Rank One\0", 9});
    }
    fo4::test::end_record(out, faction);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> weaponPatchPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});

    const auto npcTop = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x01000800u);
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, npcTop);

    const auto top = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x01000820u, 152);
    fo4::test::ByteWriter dama;
    dama.u32(0x00000900u);
    dama.u32(25);
    dama.u32(0x00000901u);
    fo4::test::write_subrecord(out, "DAMA", dama.bytes);
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, top);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "test"},
        {"resolved", true}
    };
}

void loadBaseAndPatch(fo4ide::plugin::RadiclePluginBackend& backend,
                      const std::filesystem::path& basePath,
                      const std::filesystem::path& patchPath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath),
            pluginEntry("Patch.esp", patchPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

bool pendingPlugin(fo4ide::plugin::RadiclePluginBackend& backend, std::size_t index) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(index).at("Pending").get<bool>();
}

}  // namespace

TEST_CASE("Native NPC typed decode reads factions, perks and inventory") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto npc = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(npc.has_value());
    REQUIRE(npc->at("factions").size() == 1);
    CHECK(npc->at("factions")[0].at("faction") == "000900:Base.esm");
    CHECK(npc->at("factions")[0].at("rank") == 3);
    REQUIRE(npc->at("perks").size() == 1);
    CHECK(npc->at("perks")[0].at("perk") == "000901:Base.esm");
    CHECK(npc->at("perks")[0].at("rank") == 1);
    REQUIRE(npc->at("inventory").size() == 1);
    CHECK(npc->at("inventory")[0].at("item") == "000902:Base.esm");
    CHECK(npc->at("inventory")[0].at("count") == 2);
    CHECK(npc->at("inventory")[0].at("extra").is_null());
}

TEST_CASE("Native NPC faction add round-trips through save and re-read") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto added = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "add", nullptr,
        nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", -1}}
    }));
    REQUIRE(added.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::read_plugin_file(patchPath);
    const auto factions = fo4::forms::decode_npc_factions(reread, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(factions.size() == 2);
    CHECK(factions[0].rank == 3);
    CHECK(factions[1].faction == fo4::FormKey::parse("000901:Base.esm"));
    CHECK(factions[1].rank == -1);
}

TEST_CASE("Native NPC faction set refuses an out-of-range index and leaves the plugin bytes unchanged") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin());
    const auto before = typedReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto refused = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "set", 9,
        nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", 1}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "NpcWriteFailed");

    const auto npc = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(npc.has_value());
    REQUIRE(npc->at("factions").size() == 1);
    CHECK(npc->at("factions")[0].at("rank") == 3);
    const bool unchanged = typedReadBytes(patchPath) == before;
    CHECK(unchanged);
    CHECK_FALSE(pendingPlugin(backend, 1));
}

TEST_CASE("Native FACT typed decode reads relations and ranks") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto faction = backend.getFaction(nlohmann::json::array({"000810:Patch.esp"}));
    REQUIRE(faction.has_value());
    REQUIRE(faction->at("relations").size() == 1);
    CHECK(faction->at("relations")[0].at("faction") == "000900:Base.esm");
    CHECK(faction->at("relations")[0].at("modifier") == 5);
    CHECK(faction->at("relations")[0].at("reaction") == "ally");
    REQUIRE(faction->at("ranks").size() == 1);
    CHECK(faction->at("ranks")[0].at("number") == 1);
    CHECK(faction->at("ranks")[0].at("maleTitle") == "Rank One");
    CHECK(faction->at("ranks")[0].at("femaleTitle").is_null());
    CHECK(faction->at("ranks")[0].at("hasInsignia") == false);
}

TEST_CASE("Native FACT rank set keeps the male title and round-trips through save and re-read") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto edited = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", "000810:Patch.esp", "ranks", "set", 0,
        nlohmann::json{{"number", 2}, {"femaleTitle", "Second"}}
    }));
    REQUIRE(edited.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::read_plugin_file(patchPath);
    const auto ranks = fo4::forms::decode_faction_ranks(reread, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(ranks.size() == 1);
    CHECK(ranks[0].number == 2);
    REQUIRE(ranks[0].male_title.has_value());
    CHECK(ranks[0].male_title->text.value_or("") == "Rank One");
    REQUIRE(ranks[0].female_title.has_value());
    CHECK(ranks[0].female_title->text.value_or("") == "Second");
}

TEST_CASE("Native FACT relation set refuses an out-of-range index and leaves the plugin bytes unchanged") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin());
    const auto before = typedReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto refused = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", "000810:Patch.esp", "relations", "set", 4,
        nlohmann::json{{"faction", "000900:Base.esm"}, {"modifier", 0}, {"reaction", "neutral"}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "FactionWriteFailed");

    const auto faction = backend.getFaction(nlohmann::json::array({"000810:Patch.esp"}));
    REQUIRE(faction.has_value());
    REQUIRE(faction->at("relations").size() == 1);
    CHECK(faction->at("relations")[0].at("modifier") == 5);
    const bool unchanged = typedReadBytes(patchPath) == before;
    CHECK(unchanged);
    CHECK_FALSE(pendingPlugin(backend, 1));
}

TEST_CASE("Native WEAP typed decode reads damage types from the winning record") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, weaponPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto weapon = backend.getWeaponDamageTypes(nlohmann::json::array({"000820:Patch.esp"}));
    REQUIRE(weapon.has_value());
    REQUIRE(weapon->at("damageTypes").size() == 1);
    CHECK(weapon->at("damageTypes")[0].at("type") == "000900:Base.esm");
    CHECK(weapon->at("damageTypes")[0].at("amount") == 25);
    CHECK(weapon->at("damageTypes")[0].at("curveTable") == "000901:Base.esm");
}

TEST_CASE("Native WEAP damage read refuses a non-WEAP record and leaves the plugin bytes unchanged") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, weaponPatchPlugin());
    const auto before = typedReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto refused = backend.getWeaponDamageTypes(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "WeaponReadFailed");
    const bool unchanged = typedReadBytes(patchPath) == before;
    CHECK(unchanged);
    CHECK_FALSE(pendingPlugin(backend, 1));
}

TEST_CASE("Native FACT localized title reads as a string ID") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin(true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto faction = backend.getFaction(nlohmann::json::array({"000810:Patch.esp"}));
    REQUIRE(faction.has_value());
    REQUIRE(faction->at("ranks").size() == 1);
    CHECK(faction->at("ranks")[0].at("maleTitle") == nlohmann::json{{"stringId", 0x1234}});
}

TEST_CASE("Native FACT localized rank set and add keep a string ID title through save and re-read") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin(true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const nlohmann::json title{{"stringId", 9000}};
    const auto edited = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", "000810:Patch.esp", "ranks", "set", 0,
        nlohmann::json{{"number", 2}, {"maleTitle", title}}
    }));
    REQUIRE(edited.has_value());
    const auto added = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", "000810:Patch.esp", "ranks", "add", nullptr,
        nlohmann::json{{"number", 3}, {"maleTitle", title}}
    }));
    REQUIRE(added.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::read_plugin_file(patchPath);
    const auto ranks = fo4::forms::decode_faction_ranks(reread, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(ranks.size() == 2);
    REQUIRE(ranks[0].male_title.has_value());
    CHECK(ranks[0].male_title->id.value_or(0) == 9000u);
    REQUIRE(ranks[1].male_title.has_value());
    CHECK(ranks[1].male_title->id.value_or(0) == 9000u);
}

TEST_CASE("Native FACT rank add refuses a rawId title and leaves the plugin bytes unchanged") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin(true));
    const auto before = typedReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto refused = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", "000810:Patch.esp", "ranks", "add", nullptr,
        nlohmann::json{{"number", 3}, {"maleTitle", nlohmann::json{{"rawId", "01020304"}}}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidFactionEntry");
    const bool unchanged = typedReadBytes(patchPath) == before;
    CHECK(unchanged);
    CHECK_FALSE(pendingPlugin(backend, 1));
}

TEST_CASE("Native FACT rank add refuses a string ID title on a non-localized plugin and leaves the plugin bytes unchanged") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, factionPatchPlugin(false));
    const auto before = typedReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto refused = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", "000810:Patch.esp", "ranks", "add", nullptr,
        nlohmann::json{{"number", 3}, {"maleTitle", nlohmann::json{{"stringId", 9000}}}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "FactionWriteFailed");
    const bool unchanged = typedReadBytes(patchPath) == before;
    CHECK(unchanged);
    CHECK_FALSE(pendingPlugin(backend, 1));
}

TEST_CASE("Native NPC reads and edits refuse a deleted record") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin(fo4::RecordFlags::deleted));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto npc = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE_FALSE(npc.has_value());
    CHECK(npc.error().type == "RecordDeleted");

    const auto edited = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "add", nullptr,
        nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", 1}}
    }));
    REQUIRE_FALSE(edited.has_value());
    CHECK(edited.error().type == "RecordDeleted");
}

TEST_CASE("Native NPC set with unchanged values reports no change and stages nothing") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);

    const auto unchanged = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "set", 0,
        nlohmann::json{{"faction", "000900:Base.esm"}, {"rank", 3}}
    }));
    REQUIRE(unchanged.has_value());
    CHECK(unchanged->get<std::string>() == "No change for factions entry in 000800:Patch.esp in Patch.esp.");
    CHECK_FALSE(pendingPlugin(backend, 1));
}

TEST_CASE("Native typed NPC edit is refused on a protected plugin and leaves it unchanged") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin());
    const auto before = typedReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath),
            nlohmann::json{
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "data"},
                {"resolved", true}
            }
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto refused = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "add", nullptr,
        nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", -1}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PluginReadOnly");
    CHECK_FALSE(pendingPlugin(backend, 1));
    CHECK(typedReadBytes(patchPath) == before);
}

TEST_CASE("Native typed NPC edit is refused when the plugin changed on disk after load") {
    const auto root = typedTempRoot();
    TypedCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    typedWriteBytes(basePath, basePlugin());
    typedWriteBytes(patchPath, npcPatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBaseAndPatch(backend, basePath, patchPath);
    const auto loadedTime = std::filesystem::last_write_time(patchPath);

    typedWriteBytes(patchPath, basePlugin());
    std::filesystem::last_write_time(patchPath, loadedTime + std::chrono::seconds(2));

    const auto refused = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "add", nullptr,
        nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", -1}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PluginChangedOnDisk");
    CHECK_FALSE(pendingPlugin(backend, 1));
}
