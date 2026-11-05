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

#include <fo4/flags.hpp>
#include <fo4/forms/faction.hpp>
#include <fo4/forms/npc.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path entryEditTempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_typed_entry_edit_" + id);
}

struct EntryEditCleanup {
    std::filesystem::path root;
    ~EntryEditCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void entryEditWriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> entryEditReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char ch : raw) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return bytes;
}

void entryEditHeader(fo4::test::ByteWriter& out, std::initializer_list<std::string_view> masters) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
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

std::vector<std::byte> entryEditBasePlugin() {
    fo4::test::ByteWriter out;
    entryEditHeader(out, {});
    return out.bytes;
}

std::vector<std::byte> entryEditNpcPatch() {
    fo4::test::ByteWriter out;
    entryEditHeader(out, {"Base.esm"});

    const auto top = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x01000800u);
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

std::vector<std::byte> entryEditUndecodableNpcPatch() {
    fo4::test::ByteWriter out;
    entryEditHeader(out, {"Base.esm"});

    const auto top = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", fo4::RecordFlags::compressed, 0x01000800u, 131);
    out.u32(32);
    out.raw(std::string_view{"not-zlib-data-at-all"});
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> entryEditFactionPatch() {
    fo4::test::ByteWriter out;
    entryEditHeader(out, {"Base.esm"});

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
    fo4::test::write_subrecord(out, "MNAM", std::string_view{"Rank One\0", 9});
    fo4::test::end_record(out, faction);
    fo4::test::end_group(out, top);
    return out.bytes;
}

nlohmann::json entryEditPluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "test"},
        {"resolved", true}
    };
}

void entryEditLoad(fo4ide::plugin::RadiclePluginBackend& backend,
                   const std::filesystem::path& basePath,
                   const std::filesystem::path& patchPath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            entryEditPluginEntry("Base.esm", basePath),
            entryEditPluginEntry("Patch.esp", patchPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

bool entryEditPending(fo4ide::plugin::RadiclePluginBackend& backend, std::size_t index) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(index).at("Pending").get<bool>();
}

}  // namespace

TEST_CASE("Native NPC perk add, set and remove round-trip through save and re-read") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditNpcPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);

    const auto added = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "perks", "add", nullptr,
        nlohmann::json{{"perk", "000903:Base.esm"}, {"rank", 2}}
    }));
    REQUIRE(added.has_value());
    const auto afterAdd = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(afterAdd.has_value());
    REQUIRE(afterAdd->at("perks").size() == 2);
    CHECK(afterAdd->at("perks")[0].at("perk") == "000901:Base.esm");
    CHECK(afterAdd->at("perks")[1].at("perk") == "000903:Base.esm");
    CHECK(afterAdd->at("perks")[1].at("rank") == 2);

    const auto set = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "perks", "set", 0,
        nlohmann::json{{"perk", "000904:Base.esm"}, {"rank", 4}}
    }));
    REQUIRE(set.has_value());
    const auto afterSet = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(afterSet.has_value());
    REQUIRE(afterSet->at("perks").size() == 2);
    CHECK(afterSet->at("perks")[0].at("perk") == "000904:Base.esm");
    CHECK(afterSet->at("perks")[0].at("rank") == 4);
    CHECK(afterSet->at("perks")[1].at("perk") == "000903:Base.esm");

    const auto removed = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "perks", "remove", 1, nullptr
    }));
    REQUIRE(removed.has_value());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto key = fo4::FormKey::parse("000800:Patch.esp");
    const auto reread = fo4::read_plugin_file(patchPath);
    const auto perks = fo4::forms::decode_npc_perks(reread, key);
    REQUIRE(perks.size() == 1);
    CHECK(perks[0].perk == fo4::FormKey::parse("000904:Base.esm"));
    CHECK(perks[0].rank == 4);
    const auto factions = fo4::forms::decode_npc_factions(reread, key);
    REQUIRE(factions.size() == 1);
    CHECK(factions[0].faction == fo4::FormKey::parse("000900:Base.esm"));
    CHECK(factions[0].rank == 3);
    const auto inventory = fo4::forms::decode_npc_inventory(reread, key);
    REQUIRE(inventory.size() == 1);
    CHECK(inventory[0].item == fo4::FormKey::parse("000902:Base.esm"));
    CHECK(inventory[0].count == 2);
}

TEST_CASE("Native NPC inventory add, set and remove round-trip including extra data") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditNpcPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);

    REQUIRE(backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "inventory", "add", nullptr,
        nlohmann::json{{"item", "000905:Base.esm"}, {"count", 5}}
    })).has_value());
    REQUIRE(backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "inventory", "add", nullptr,
        nlohmann::json{
            {"item", "000906:Base.esm"},
            {"count", 1},
            {"extra", {{"owner", "000900:Base.esm"}, {"globalOrRank", 3}, {"condition", 0.5}}}
        }
    })).has_value());

    const auto afterAdd = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(afterAdd.has_value());
    REQUIRE(afterAdd->at("inventory").size() == 3);
    CHECK(afterAdd->at("inventory")[0].at("item") == "000902:Base.esm");
    CHECK(afterAdd->at("inventory")[0].at("extra").is_null());
    CHECK(afterAdd->at("inventory")[1].at("item") == "000905:Base.esm");
    CHECK(afterAdd->at("inventory")[1].at("count") == 5);
    CHECK(afterAdd->at("inventory")[2].at("item") == "000906:Base.esm");
    CHECK(afterAdd->at("inventory")[2].at("extra").at("owner") == "000900:Base.esm");
    CHECK(afterAdd->at("inventory")[2].at("extra").at("globalOrRank") == 3);
    CHECK(afterAdd->at("inventory")[2].at("extra").at("condition") == 0.5);

    REQUIRE(backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "inventory", "set", 1,
        nlohmann::json{{"item", "000907:Base.esm"}, {"count", -2}}
    })).has_value());
    const auto afterSet = backend.getNpc(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(afterSet.has_value());
    CHECK(afterSet->at("inventory")[1].at("item") == "000907:Base.esm");
    CHECK(afterSet->at("inventory")[1].at("count") == -2);

    REQUIRE(backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "inventory", "remove", 0, nullptr
    })).has_value());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto inventory = fo4::forms::decode_npc_inventory(
        fo4::read_plugin_file(patchPath), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(inventory.size() == 2);
    CHECK(inventory[0].item == fo4::FormKey::parse("000907:Base.esm"));
    CHECK(inventory[0].count == -2);
    CHECK_FALSE(inventory[0].extra_data.has_value());
    CHECK(inventory[1].item == fo4::FormKey::parse("000906:Base.esm"));
    REQUIRE(inventory[1].extra_data.has_value());
    CHECK(inventory[1].extra_data->owner == fo4::FormKey::parse("000900:Base.esm"));
    CHECK(inventory[1].extra_data->global_or_rank == 3);
    CHECK(inventory[1].extra_data->condition == 0.5f);
}

TEST_CASE("Native FACT relation add, set and remove round-trip and report no change") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditFactionPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);
    const std::string key = "000810:Patch.esp";

    REQUIRE(backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", key, "relations", "add", nullptr,
        nlohmann::json{{"faction", "000901:Base.esm"}, {"modifier", -10}, {"reaction", "enemy"}}
    })).has_value());
    const auto afterAdd = backend.getFaction(nlohmann::json::array({key}));
    REQUIRE(afterAdd.has_value());
    REQUIRE(afterAdd->at("relations").size() == 2);
    CHECK(afterAdd->at("relations")[0].at("faction") == "000900:Base.esm");
    CHECK(afterAdd->at("relations")[0].at("modifier") == 5);
    CHECK(afterAdd->at("relations")[0].at("reaction") == "ally");
    CHECK(afterAdd->at("relations")[1].at("faction") == "000901:Base.esm");
    CHECK(afterAdd->at("relations")[1].at("modifier") == -10);
    CHECK(afterAdd->at("relations")[1].at("reaction") == "enemy");

    const nlohmann::json friendly{{"faction", "000900:Base.esm"}, {"modifier", 7}, {"reaction", "friendly"}};
    const auto set = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", key, "relations", "set", 0, friendly
    }));
    REQUIRE(set.has_value());
    const auto afterSet = backend.getFaction(nlohmann::json::array({key}));
    REQUIRE(afterSet.has_value());
    CHECK(afterSet->at("relations")[0].at("modifier") == 7);
    CHECK(afterSet->at("relations")[0].at("reaction") == "friendly");

    const auto unchanged = backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", key, "relations", "set", 0, friendly
    }));
    REQUIRE(unchanged.has_value());
    CHECK(unchanged->get<std::string>() == "No change for relations entry in 000810:Patch.esp in Patch.esp.");

    REQUIRE(backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", key, "relations", "remove", 1, nullptr
    })).has_value());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto relations = fo4::forms::decode_faction_relations(
        fo4::read_plugin_file(patchPath), fo4::FormKey::parse(key));
    REQUIRE(relations.size() == 1);
    CHECK(relations[0].faction == fo4::FormKey::parse("000900:Base.esm"));
    CHECK(relations[0].modifier == 7);
    CHECK(relations[0].reaction == fo4::forms::FactionCombatReaction::friendly);
}

TEST_CASE("Native FACT rank add and remove keep other ranks") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditFactionPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);
    const std::string key = "000810:Patch.esp";

    REQUIRE(backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", key, "ranks", "add", nullptr,
        nlohmann::json{{"number", 2}, {"maleTitle", "Rank Two"}, {"femaleTitle", "Rank Two F"}}
    })).has_value());
    const auto afterAdd = backend.getFaction(nlohmann::json::array({key}));
    REQUIRE(afterAdd.has_value());
    REQUIRE(afterAdd->at("ranks").size() == 2);
    CHECK(afterAdd->at("ranks")[0].at("number") == 1);
    CHECK(afterAdd->at("ranks")[0].at("maleTitle") == "Rank One");
    CHECK(afterAdd->at("ranks")[0].at("hasInsignia") == false);
    CHECK(afterAdd->at("ranks")[1].at("number") == 2);
    CHECK(afterAdd->at("ranks")[1].at("maleTitle") == "Rank Two");
    CHECK(afterAdd->at("ranks")[1].at("femaleTitle") == "Rank Two F");
    CHECK(afterAdd->at("ranks")[1].at("hasInsignia") == false);

    REQUIRE(backend.editFactionEntries(nlohmann::json::array({
        "Patch.esp", key, "ranks", "remove", 1, nullptr
    })).has_value());
    const auto afterRemove = backend.getFaction(nlohmann::json::array({key}));
    REQUIRE(afterRemove.has_value());
    REQUIRE(afterRemove->at("ranks").size() == 1);
    CHECK(afterRemove->at("ranks")[0].at("number") == 1);
    CHECK(afterRemove->at("ranks")[0].at("maleTitle") == "Rank One");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto ranks = fo4::forms::decode_faction_ranks(
        fo4::read_plugin_file(patchPath), fo4::FormKey::parse(key));
    REQUIRE(ranks.size() == 1);
    CHECK(ranks[0].number == 1);
}

TEST_CASE("Native NPC entry edits refuse malformed arguments without staging") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditNpcPatch());
    const auto before = entryEditReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);

    const auto refuses = [&](const nlohmann::json& args, const char* type) {
        const auto result = backend.editNpcEntries(args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
    };
    const auto npcArgs = [](const char* list, const char* op, const nlohmann::json& index, const nlohmann::json& value) {
        return nlohmann::json::array({"Patch.esp", "000800:Patch.esp", list, op, index, value});
    };
    const nlohmann::json faction{{"faction", "000901:Base.esm"}, {"rank", 1}};
    const nlohmann::json inventory{{"item", "000902:Base.esm"}, {"count", 1}};

    refuses(npcArgs("factions", "replace", 0, nlohmann::json::object()), "InvalidNpcOperation");
    refuses(npcArgs("keywords", "add", nullptr, nlohmann::json::object()), "InvalidNpcList");
    refuses(npcArgs("factions", "set", nullptr, faction), "InvalidNpcEntry");
    refuses(npcArgs("factions", "remove", "0", nullptr), "InvalidNpcEntry");
    refuses(npcArgs("factions", "remove", -1, nullptr), "InvalidNpcEntry");
    refuses(npcArgs("factions", "add", nullptr, nlohmann::json{{"faction", "000901:Base.esm"}}), "InvalidNpcEntry");
    refuses(npcArgs("factions", "add", nullptr, nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", 128}}), "InvalidNpcEntry");
    refuses(npcArgs("factions", "add", nullptr, nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", -129}}), "InvalidNpcEntry");
    refuses(npcArgs("factions", "add", nullptr, nlohmann::json{{"faction", "zz"}, {"rank", 1}}), "InvalidNpcEntry");
    refuses(npcArgs("factions", "add", nullptr, nullptr), "InvalidNpcEntry");
    refuses(npcArgs("perks", "add", nullptr, nlohmann::json{{"perk", "000901:Base.esm"}, {"rank", 256}}), "InvalidNpcEntry");
    refuses(npcArgs("perks", "add", nullptr, nlohmann::json{{"perk", "000901:Base.esm"}, {"rank", -1}}), "InvalidNpcEntry");
    refuses(npcArgs("inventory", "add", nullptr, nlohmann::json{{"item", "000902:Base.esm"}, {"count", 2147483648}}), "InvalidNpcEntry");
    refuses(npcArgs("inventory", "add", nullptr, nlohmann::json{
        {"item", "000902:Base.esm"}, {"count", 1}, {"extra", {{"globalOrRank", 0}}}
    }), "InvalidNpcEntry");
    refuses(npcArgs("inventory", "add", nullptr, nlohmann::json{
        {"item", "000902:Base.esm"}, {"count", 1},
        {"extra", {{"owner", "zz"}, {"globalOrRank", 0}, {"condition", 1.0}}}
    }), "InvalidFormKey");
    refuses(npcArgs("perks", "remove", 5, nullptr), "NpcWriteFailed");
    refuses(nlohmann::json::array({"Patch.esp", "zz", "factions", "add", nullptr, faction}), "InvalidFormKey");
    refuses(nlohmann::json::array({"", "000800:Patch.esp", "factions", "add", nullptr, faction}), "PluginMissing");
    refuses(nlohmann::json::array({"Nope.esp", "000800:Patch.esp", "factions", "add", nullptr, faction}), "PluginNotLoaded");
    refuses(nlohmann::json::array({"Patch.esp", "000999:Patch.esp", "factions", "add", nullptr, faction}), "RecordNotFound");

    CHECK_FALSE(entryEditPending(backend, 1));
    const bool unchanged = entryEditReadBytes(patchPath) == before;
    CHECK(unchanged);
}

TEST_CASE("Native FACT entry edits refuse malformed arguments without staging") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditFactionPatch());
    const auto before = entryEditReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);
    const std::string key = "000810:Patch.esp";

    const auto refuses = [&](const nlohmann::json& args, const char* type) {
        const auto result = backend.editFactionEntries(args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
    };
    const auto factionArgs = [&](const char* list, const char* op, const nlohmann::json& index, const nlohmann::json& value) {
        return nlohmann::json::array({"Patch.esp", key, list, op, index, value});
    };
    const nlohmann::json relation{{"faction", "000900:Base.esm"}, {"modifier", 0}, {"reaction", "neutral"}};

    refuses(factionArgs("relations", "replace", 0, nlohmann::json::object()), "InvalidFactionOperation");
    refuses(factionArgs("perks", "add", nullptr, nlohmann::json::object()), "InvalidFactionList");
    refuses(factionArgs("relations", "set", nullptr, relation), "InvalidFactionEntry");
    refuses(factionArgs("relations", "add", nullptr, nlohmann::json{{"faction", "000900:Base.esm"}, {"modifier", 0}, {"reaction", "hostile"}}), "InvalidFactionEntry");
    refuses(factionArgs("relations", "add", nullptr, nlohmann::json{{"faction", "000900:Base.esm"}, {"modifier", 2147483648}, {"reaction", "neutral"}}), "InvalidFactionEntry");
    refuses(factionArgs("relations", "add", nullptr, nlohmann::json{{"modifier", 0}, {"reaction", "neutral"}}), "InvalidFactionEntry");
    refuses(factionArgs("ranks", "add", nullptr, nlohmann::json{{"maleTitle", "x"}}), "InvalidFactionEntry");
    refuses(factionArgs("ranks", "add", nullptr, nlohmann::json{{"number", -1}}), "InvalidFactionEntry");
    refuses(factionArgs("ranks", "add", nullptr, nlohmann::json{{"number", 1}, {"maleTitle", 5}}), "InvalidFactionEntry");
    refuses(factionArgs("ranks", "set", 9, nlohmann::json{{"number", 1}}), "FactionWriteFailed");
    refuses(factionArgs("ranks", "remove", 9, nullptr), "FactionWriteFailed");
    refuses(factionArgs("relations", "remove", 9, nullptr), "FactionWriteFailed");

    const auto rawId = backend.editFactionEntries(factionArgs(
        "ranks", "add", nullptr, nlohmann::json{{"number", 3}, {"maleTitle", nlohmann::json{{"rawId", "3412"}}}}));
    REQUIRE_FALSE(rawId.has_value());
    CHECK(rawId.error().type == "InvalidFactionEntry");
    CHECK(rawId.error().message.find("rawId") != std::string::npos);

    CHECK_FALSE(entryEditPending(backend, 1));
    const bool unchanged = entryEditReadBytes(patchPath) == before;
    CHECK(unchanged);
}

TEST_CASE("Native typed entry edits refuse 64-bit unsigned values that wrap into signed fields", "[!shouldfail]") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditNpcPatch());
    const nlohmann::json wrapped(18446744073709551615ULL);

    SECTION("NPC inventory count") {
        fo4ide::plugin::RadiclePluginBackend backend;
        entryEditLoad(backend, basePath, patchPath);
        const auto added = backend.editNpcEntries(nlohmann::json::array({
            "Patch.esp", "000800:Patch.esp", "inventory", "add", nullptr,
            nlohmann::json{{"item", "000902:Base.esm"}, {"count", wrapped}}
        }));
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().type == "InvalidNpcEntry");
        CHECK_FALSE(entryEditPending(backend, 1));
    }

    SECTION("NPC faction rank") {
        fo4ide::plugin::RadiclePluginBackend backend;
        entryEditLoad(backend, basePath, patchPath);
        const auto added = backend.editNpcEntries(nlohmann::json::array({
            "Patch.esp", "000800:Patch.esp", "factions", "add", nullptr,
            nlohmann::json{{"faction", "000901:Base.esm"}, {"rank", wrapped}}
        }));
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().type == "InvalidNpcEntry");
        CHECK_FALSE(entryEditPending(backend, 1));
    }
}

TEST_CASE("Native NPC set and remove refuse an undecodable record with the undecodable reason") {
    const auto root = entryEditTempRoot();
    EntryEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    entryEditWriteBytes(basePath, entryEditBasePlugin());
    entryEditWriteBytes(patchPath, entryEditUndecodableNpcPatch());
    const auto before = entryEditReadBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    entryEditLoad(backend, basePath, patchPath);

    const auto set = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "set", 0,
        nlohmann::json{{"faction", "000900:Base.esm"}, {"rank", 1}}
    }));
    REQUIRE_FALSE(set.has_value());
    CHECK(set.error().message.find("undecodable") != std::string::npos);

    const auto removed = backend.editNpcEntries(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "factions", "remove", 0, nullptr
    }));
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().message.find("undecodable") != std::string::npos);

    CHECK_FALSE(entryEditPending(backend, 1));
    const bool unchanged = entryEditReadBytes(patchPath) == before;
    CHECK(unchanged);
}
