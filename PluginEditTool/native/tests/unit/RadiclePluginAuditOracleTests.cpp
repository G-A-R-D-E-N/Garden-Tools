#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/cell.hpp>
#include <fo4/forms/placed_reference.hpp>
#include <fo4/schema/form_links.hpp>

#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;

std::vector<std::byte> colorPlugin(std::string_view editorId) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x801u);
    const auto group = fo4::test::begin_group(out, "CLFM", 0);
    writeNamedRecord(out, "CLFM", 0x800u, std::string(editorId));
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> raceBytes(std::initializer_list<std::string_view> masters, std::uint32_t rawId,
                                 std::string_view editorId, std::uint32_t linkRaw) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x801u, masters);
    const auto group = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, rawId, 131);
    const auto name = std::string(editorId) + '\0';
    fo4::test::write_subrecord(out, "EDID", std::string_view{name});
    fo4::test::ByteWriter link;
    link.u32(linkRaw);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, group);
    return out.bytes;
}

bool pending(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& name) {
    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    for (const auto& row : active) {
        if (row.at("Name") == name) return row.at("Pending").get<bool>();
    }
    FAIL("plugin not listed: " + name);
}

bool hasLink(const fo4::Plugin& plugin, const std::string& record, const std::string& target) {
    const auto links = fo4::collect_form_links(plugin);
    return std::any_of(links.begin(), links.end(), [&](const auto& link) {
        return link.record == fo4::FormKey::parse(record) && link.key == fo4::FormKey::parse(target);
    });
}

struct RemapRoot {
    std::filesystem::path root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::path base = root / "Base.esm";
    std::filesystem::path other = root / "Other.esm";
    std::filesystem::path mod = root / "Mod.esp";
    std::filesystem::path patch = root / "Patch.esp";
    std::filesystem::path later = root / "Later.esp";

    RemapRoot() {
        writeBytes(base, colorPlugin("FromColor"));
        writeBytes(other, colorPlugin("ToColor"));
        writeBytes(mod, raceBytes({"Base.esm"}, 0x01000800u, "UsesFrom", 0x00000800u));
        writeBytes(patch, raceBytes({"Base.esm", "Mod.esp"}, 0x01000800u, "EditedByPatch", 0x00000800u));
        writeBytes(later, raceBytes({"Base.esm", "Mod.esp"}, 0x01000800u, "EditedByLater", 0x00000800u));
    }
};

}

TEST_CASE("Oracle change referencing records keeps an existing target edit and repoints its link") {
    RemapRoot files;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Base.esm", files.base}, {"Other.esm", files.other},
                          {"Mod.esp", files.mod}, {"Patch.esp", files.patch}});

    const auto applied = textResult(backend.changeReferencingRecords(
        nlohmann::json::array({"000800:Base.esm", "000800:Other.esm", "Patch.esp", true, 300})));
    CHECK(applied.find("Repointed 1 of 1") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(files.patch);
    const auto* race = fo4::find_record(disk, fo4::FormKey::parse("000800:Mod.esp"));
    REQUIRE(race != nullptr);
    CHECK(editorIdOf(*race) == "EditedByPatch");
    CHECK(hasLink(disk, "000800:Mod.esp", "000800:Other.esm"));
    CHECK_FALSE(hasLink(disk, "000800:Mod.esp", "000800:Base.esm"));
}

TEST_CASE("Oracle change referencing records refuses before changing anything when a later plugin holds the winner") {
    RemapRoot files;
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Base.esm", files.base}, {"Other.esm", files.other}, {"Mod.esp", files.mod},
                          {"Patch.esp", files.patch}, {"Later.esp", files.later}});

    const auto refused = backend.changeReferencingRecords(
        nlohmann::json::array({"000800:Base.esm", "000800:Other.esm", "Patch.esp", true, 300}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PatchLoadOrderTooEarly");
    CHECK(refused.error().message.find("Later.esp") != std::string::npos);
    CHECK_FALSE(pending(backend, "Patch.esp"));
}

TEST_CASE("Oracle create seq file for a light plugin encodes the saved master count") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "LightSeq.esl";
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, fo4::RecordFlags::small_file, 0x802u, {"Full.esm", "Other.esm", "Light.esl"});
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x03000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"StartOnExistingSave\0", 20});
    fo4::test::ByteWriter data;
    data.u16(0x0001u);
    data.u8(50);
    data.u8(0);
    data.f32(0.0f);
    data.i32(0);
    fo4::test::write_subrecord(out, "DNAM", data.bytes);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    writeBytes(path, out.bytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"LightSeq.esl", path}});
    REQUIRE(backend.createSeqFile(nlohmann::json::array({"LightSeq.esl", ""})).has_value());

    const auto seq = readFile(root / "Seq" / "LightSeq.seq");
    REQUIRE(seq.size() == 4);
    std::uint32_t value = 0;
    std::memcpy(&value, seq.data(), sizeof(value));
    CHECK(value == 0x03000801u);
}

TEST_CASE("Oracle copy as override many reports a repeated form key and still applies the rest") {
    for (const bool overwrite : {false, true}) {
        INFO("overwrite " << overwrite);
        const auto root = tempRoot();
        Cleanup cleanup{root};
        const auto source = root / "Source.esp";
        const auto patch = root / "Patch.esp";
        writeBytes(source, miscPlugin(1.0f, 0x801u, false, {0x800u}));
        fo4::test::ByteWriter empty;
        writeHeader(empty, 1.0f, 0, 0x800u);
        writeBytes(patch, empty.bytes);
        fo4ide::plugin::RadiclePluginBackend backend;
        loadPlugins(backend, {{"Source.esp", source}, {"Patch.esp", patch}});

        const nlohmann::json item{{"formKey", "000800:Source.esp"}, {"source", "Source.esp"}};
        const auto items = nlohmann::json::array({item, item});
        const auto result = parseStringResult(backend.invokeBackend(
            "CopyAsOverrideMany", nlohmann::json::array({items.dump(), "Patch.esp", overwrite})));
        INFO(result.dump());
        CHECK(result.at("total") == 2);
        CHECK(result.at("ok") == 1);
        CHECK(result.at("failed") == 1);
        REQUIRE(result.at("failures").size() == 1);
        CHECK(result.at("failures")[0].at("reason") == "duplicate FormKey in batch");
        CHECK(result.at("failures")[0].at("formKey") == "000800:Source.esp");
        CHECK(pending(backend, "Patch.esp"));
    }
}

TEST_CASE("Oracle create plugin refuses a name that is already open by case or by a missing extension") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(root).has_value());
    REQUIRE(backend.createPlugin(nlohmann::json::array({"CaseTwin.esp"})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"CaseTwin.esp"})).has_value());
    const auto before = readFile(root / "CaseTwin.esp");

    for (const char* name : {"CASETWIN.ESP", "CaseTwin"}) {
        INFO(name);
        const auto again = backend.createPlugin(nlohmann::json::array({name}));
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().type == "PluginAlreadyLoaded");
    }
    CHECK(readFile(root / "CaseTwin.esp") == before);
}

TEST_CASE("Oracle create plugin refuses the name of an opened plugin and keeps its file and records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "DataLoss.esp";
    writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    const auto before = readFile(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(root).has_value());
    loadPlugins(backend, {{"DataLoss.esp", path}});

    const auto again = backend.createPlugin(nlohmann::json::array({"DataLoss.esp"}));
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().type == "PluginAlreadyLoaded");
    CHECK(readFile(path) == before);
    CHECK_FALSE(pending(backend, "DataLoss.esp"));
}

TEST_CASE("Oracle create placed object round trips all three rotation axes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Placed.esp";
    fo4::test::ByteWriter empty;
    writeHeader(empty, 1.0f, 0, 0x800u);
    writeBytes(path, empty.bytes);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Placed.esp", path}});

    const auto cell = parseStringResult(backend.createInteriorCell(
        nlohmann::json::array({"Placed.esp", "RotationCell"})))["formKey"].get<std::string>();
    const auto base = parseStringResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Placed.esp", "EYES",
            nlohmann::json{{"editorId", "RotationBase"}, {"fullText", "Base"}, {"icon", "Textures/Eyes/base.dds"}}})))
        ["formKey"].get<std::string>();
    const auto ref = parseStringResult(backend.createPlacedObject(nlohmann::json::array({
        "Placed.esp", cell, base, 10.0, 20.0, 30.0, 0.25, -0.5, 1.75})))["formKey"].get<std::string>();
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Placed.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto refs = fo4::forms::placed_references_for_cell(disk, fo4::FormKey::parse(cell));
    REQUIRE(refs.size() == 1u);
    CHECK(refs[0].record->form_key == fo4::FormKey::parse(ref));
    CHECK(refs[0].transform.x == 10.0f);
    CHECK(refs[0].transform.y == 20.0f);
    CHECK(refs[0].transform.z == 30.0f);
    CHECK(refs[0].transform.rx == 0.25f);
    CHECK(refs[0].transform.ry == -0.5f);
    CHECK(refs[0].transform.rz == 1.75f);
}

TEST_CASE("Oracle load reports a plugin whose header is valid but whose body is corrupt") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto good = root / "Good.esp";
    const auto corrupt = root / "Corrupt.esp";
    writeBytes(good, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    auto bytes = miscPlugin(1.0f, 0x801u, false, {0x800u});
    for (std::size_t i = 60; i < bytes.size(); ++i) bytes[i] = std::byte{0xFF};
    writeBytes(corrupt, bytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json entries = nlohmann::json::array({pluginEntry("Good.esp", good), pluginEntry("Corrupt.esp", corrupt)});
    const auto loaded = backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", entries}});
    REQUIRE(loaded.has_value());
    CHECK(loaded->at("loadedPlugins") == 1);
    CHECK(loaded->at("failedPlugins") == 1);
    REQUIRE(loaded->at("errors").size() == 1);
    CHECK(loaded->at("errors")[0].at("plugin") == "Corrupt.esp");
    CHECK_FALSE(loaded->at("errors")[0].at("error").get<std::string>().empty());

    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    REQUIRE(active.size() == 1);
    CHECK(active[0].at("Name") == "Good.esp");
}

TEST_CASE("Oracle protected plugin names are refused for create and loaded read only") {
    struct Row { const char* name; bool isProtected; };
    const Row rows[] = {
        {"Fallout4.esm", true}, {"fallout4.ESM", true}, {"DLCRobot.esm", true}, {"dlccoast.esm", true},
        {"DLCWorkshop01.esm", true}, {"DLCworkshop02.esm", true}, {"DLCWorkshop03.esm", true},
        {"DLCNukaWorld.esm", true}, {"DLCUltraHighResolution.esm", true},
        {"ccLight.esp", true}, {"CCSomething.ESL", true},
        {"Fallout4Patch.esp", false}, {"NotFallout4.esm", false}, {"Accessory.esp", false}, {"MyDLCRobot.esm", false},
    };
    for (const auto& row : rows) {
        INFO(row.name);
        const auto root = tempRoot();
        Cleanup cleanup{root};
        const auto created = root / "created";
        fo4ide::plugin::RadiclePluginBackend maker;
        REQUIRE(maker.setTrustedNewPluginRoot(created).has_value());
        const auto made = maker.createPlugin(nlohmann::json::array({row.name}));
        if (row.isProtected) {
            REQUIRE_FALSE(made.has_value());
            CHECK(made.error().type == "PluginProtected");
        } else {
            CHECK(made.has_value());
        }

        const auto path = root / row.name;
        writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
        fo4ide::plugin::RadiclePluginBackend backend;
        loadPlugins(backend, {{row.name, path}});
        const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
        REQUIRE(active.size() == 1);
        CHECK(active[0].at("Editable").get<bool>() == !row.isProtected);
    }

    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend maker;
    REQUIRE(maker.setTrustedNewPluginRoot(root).has_value());
    const auto empty = maker.createPlugin(nlohmann::json::array({""}));
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().type != "PluginProtected");
}

TEST_CASE("Oracle records of type pages with limit and offset and an unknown signature is empty") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Paged.esp";
    writeBytes(path, miscPlugin(1.0f, 0x806u, false, {0x800u, 0x801u, 0x802u, 0x803u, 0x804u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Paged.esp", path}});

    const auto rows = [&](const char* signature, int limit, int offset) {
        return parseStringResult(backend.invokeBackend(
            "GetRecordsOfType", nlohmann::json::array({signature, "", limit, offset})));
    };
    const auto all = rows("MISC", 100, 0);
    REQUIRE(all.size() == 5);
    const auto firstPage = rows("MISC", 2, 0);
    const auto secondPage = rows("MISC", 2, 2);
    const auto lastPage = rows("MISC", 2, 4);
    REQUIRE(firstPage.size() == 2);
    REQUIRE(secondPage.size() == 2);
    REQUIRE(lastPage.size() == 1);
    CHECK(firstPage[0] == all[0]);
    CHECK(firstPage[1] == all[1]);
    CHECK(secondPage[0] == all[2]);
    CHECK(secondPage[1] == all[3]);
    CHECK(lastPage[0] == all[4]);
    CHECK(rows("MISC", 2, 9).empty());
    CHECK(rows("NOSU", 10, 0).empty());
}

TEST_CASE("Oracle compaction writes a HEDR next id that no record uses") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    std::vector<std::uint32_t> ids;
    for (std::uint32_t id = 0x801u; id <= 0xFFFu; ++id) ids.push_back(id);
    ids.push_back(0x5000u);
    writeBytes(path, miscPlugin(1.0f, 0x5001u, false, ids));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    REQUIRE(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})).has_value());
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    REQUIRE(recordAt(disk, 0x800u) != nullptr);
    CHECK(recordAt(disk, disk.header.next_form_id) == nullptr);
}

TEST_CASE("Oracle list item links to two masters save the masters in load order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto one = root / "BaseOne.esm";
    const auto two = root / "BaseTwo.esm";
    const auto patch = root / "Patch.esp";
    writeBytes(one, colorPlugin("One"));
    writeBytes(two, colorPlugin("Two"));
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0x801u);
    const auto group = fo4::test::begin_group(out, "FLST", 0);
    writeNamedRecord(out, "FLST", 0x800u, "List");
    fo4::test::end_group(out, group);
    writeBytes(patch, out.bytes);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"BaseOne.esm", one}, {"BaseTwo.esm", two}, {"Patch.esp", patch}});

    for (const char* link : {"000800:BaseTwo.esm", "000800:BaseOne.esm"}) {
        const auto added = backend.invokeBackend(
            "AddListItem", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Items", link}));
        INFO(link);
        const std::string shown = added.has_value() ? added->dump() : added.error().type + ": " + added.error().message;
        INFO(shown);
        REQUIRE(added.has_value());
    }
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(patch);
    REQUIRE(disk.header.masters.size() == 2);
    CHECK(disk.header.masters[0].key.filename() == "BaseOne.esm");
    CHECK(disk.header.masters[1].key.filename() == "BaseTwo.esm");

    const auto* flst = recordAt(disk, 0x800u);
    REQUIRE(flst != nullptr);
    std::vector<std::uint32_t> raws;
    for (const auto& subrecord : flst->subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("LNAM")) continue;
        REQUIRE(subrecord.data.size() == 4u);
        std::uint32_t raw = 0;
        std::memcpy(&raw, subrecord.data.data(), sizeof raw);
        raws.push_back(raw);
    }
    REQUIRE(raws.size() == 2u);
    CHECK(raws[0] == 0x01000800u);
    CHECK(raws[1] == 0x00000800u);
}

TEST_CASE("Oracle renumber of a placed reference in a cell is refused and leaves the reference in place") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Placed.esp";
    fo4::test::ByteWriter empty;
    writeHeader(empty, 1.0f, 0, 0x800u);
    writeBytes(path, empty.bytes);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Placed.esp", path}});
    const auto cell = parseStringResult(backend.createInteriorCell(
        nlohmann::json::array({"Placed.esp", "HoldingCell"})))["formKey"].get<std::string>();
    const auto base = parseStringResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Placed.esp", "EYES",
            nlohmann::json{{"editorId", "Marker"}, {"fullText", "Base"}, {"icon", "Textures/Eyes/base.dds"}}})))
        ["formKey"].get<std::string>();
    const auto ref = parseStringResult(backend.createPlacedObject(nlohmann::json::array({
        "Placed.esp", cell, base, 1.0, 2.0, 3.0})))["formKey"].get<std::string>();

    const auto renumbered = backend.invokeBackend(
        "RenumberRecord", nlohmann::json::array({"Placed.esp", ref, "000900:Placed.esp"}));
    REQUIRE_FALSE(renumbered.has_value());
    CHECK(renumbered.error().type == "RecordRenumberFailed");
    CHECK(renumbered.error().message.find("nested record renumbering") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Placed.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto refs = fo4::forms::placed_references_for_cell(disk, fo4::FormKey::parse(cell));
    REQUIRE(refs.size() == 1u);
    CHECK(refs[0].record->form_key == fo4::FormKey::parse(ref));
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000900:Placed.esp")) == nullptr);
}

TEST_CASE("Oracle deep copy uses the version of the plugin it is told to copy from") {
    for (const char* copyFrom : {"Base.esm", "Selected.esp"}) {
        INFO(copyFrom);
        const auto root = tempRoot();
        Cleanup cleanup{root};
        const auto base = root / "Base.esm";
        const auto selected = root / "Selected.esp";
        const auto patch = root / "Patch.esp";
        fo4::test::ByteWriter baseBytes;
        writeHeader(baseBytes, 1.0f, 0, 0x802u);
        const auto colors = fo4::test::begin_group(baseBytes, "CLFM", 0);
        writeNamedRecord(baseBytes, "CLFM", 0x800u, "Color");
        fo4::test::end_group(baseBytes, colors);
        const auto races = fo4::test::begin_group(baseBytes, "RACE", 0);
        const auto race = fo4::test::begin_record(baseBytes, "RACE", 0, 0x801u, 131);
        fo4::test::write_subrecord(baseBytes, "EDID", std::string_view{"BaseRace\0", 9});
        fo4::test::ByteWriter link;
        link.u32(0x800u);
        fo4::test::write_subrecord(baseBytes, "AHCM", link.bytes);
        fo4::test::end_record(baseBytes, race);
        fo4::test::end_group(baseBytes, races);
        writeBytes(base, baseBytes.bytes);
        writeBytes(selected, raceBytes({"Base.esm"}, 0x00000801u, "SelectedRace", 0x00000800u));
        fo4::test::ByteWriter empty;
        writeHeader(empty, 1.0f, 0, 0x800u);
        writeBytes(patch, empty.bytes);

        fo4ide::plugin::RadiclePluginBackend backend;
        loadPlugins(backend, {{"Base.esm", base}, {"Selected.esp", selected}, {"Patch.esp", patch}});
        const auto applied = textResult(backend.deepCopyAsOverride(
            nlohmann::json::array({copyFrom, "000801:Base.esm", "Patch.esp", true, false, 200})));
        INFO(applied);
        CHECK(applied.find("Deep-copied") != std::string::npos);
        savePatch(backend);

        const auto disk = fo4::read_plugin_file(patch);
        const auto* copied = fo4::find_record(disk, fo4::FormKey::parse("000801:Base.esm"));
        REQUIRE(copied != nullptr);
        CHECK(editorIdOf(*copied) == (std::string(copyFrom) == "Base.esm" ? "BaseRace" : "SelectedRace"));
    }
}

TEST_CASE("Oracle placed reference listing reports an unknown cell and a record that is not a cell as an error payload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Plain.esp";
    writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Plain.esp", path}});

    const auto unknown = backend.getPlacedReferences(nlohmann::json::array({"000999:Plain.esp"}));
    REQUIRE(unknown.has_value());
    CHECK(parseStringResult(unknown).at("error") == "Cell not found: 000999:Plain.esp");
    const auto notCell = backend.getPlacedReferences(nlohmann::json::array({"000800:Plain.esp"}));
    REQUIRE(notCell.has_value());
    CHECK(parseStringResult(notCell).at("error") == "Cell not found: 000800:Plain.esp");
}

TEST_CASE("Oracle a loaded plugin file is not held open and can be deleted") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Held.esp";
    writeBytes(path, miscPlugin(1.0f, 0x801u, false, {0x800u}));
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugins(backend, {{"Held.esp", path}});

    std::error_code ec;
    CHECK(std::filesystem::remove(path, ec));
    CHECK_FALSE(ec);
}
