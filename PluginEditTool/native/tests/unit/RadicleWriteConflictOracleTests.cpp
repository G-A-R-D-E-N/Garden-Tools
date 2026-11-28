#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/components.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/mo2/Mo2Profile.hpp>

#include "../helpers/PluginFixtures.hpp"

#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;
using fo4ide::plugin::RadiclePluginBackend;

std::vector<std::byte> ctda(std::uint8_t flagsByte, std::uint32_t comparison, std::uint16_t function,
                            std::int32_t param1, std::uint32_t runOn, std::uint32_t reference) {
    fo4::test::ByteWriter out;
    out.u8(flagsByte);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    out.u32(comparison);
    out.u16(function);
    out.u16(0);
    out.u32(static_cast<std::uint32_t>(param1));
    out.u32(0);
    out.u32(runOn);
    out.u32(reference);
    out.u32(0xFFFFFFFFu);
    return out.bytes;
}

std::vector<std::byte> cobjPlugin(const std::vector<std::vector<std::byte>>& conditions = {},
                                  std::uint32_t componentCount = 0) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0xF00);
    const auto cmpo = fo4::test::begin_group(out, "CMPO", 0);
    for (const std::uint32_t id : {0x801u, 0x802u}) {
        const auto record = fo4::test::begin_record(out, "CMPO", 0, id, 131);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, cmpo);
    const auto group = fo4::test::begin_group(out, "COBJ", 0);
    const auto record = fo4::test::begin_record(out, "COBJ", 0, 0xA00, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Recipe\0", 7});
    if (componentCount != 0) {
        fo4::test::ByteWriter cvpa;
        cvpa.u32(0x801);
        cvpa.u32(componentCount);
        fo4::test::write_subrecord(out, "CVPA", cvpa.bytes);
    }
    for (const auto& condition : conditions) fo4::test::write_subrecord(out, "CTDA", condition);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

const fo4::Record& requireRecord(const fo4::Plugin& plugin, std::string_view key) {
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    return *record;
}

std::vector<const fo4::Subrecord*> subrecordsOf(const fo4::Record& record, std::string_view signature) {
    std::vector<const fo4::Subrecord*> found;
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == wanted) found.push_back(&subrecord);
    }
    return found;
}

using Extra = std::function<void(fo4::test::ByteWriter&)>;

std::vector<std::byte> versionPlugin(bool override, std::string_view type, const std::string& edid,
                                     const Extra& extra = {}) {
    fo4::test::ByteWriter out;
    if (override) {
        writeHeader(out, 1.0f, 0, 0x801, {"Base.esp"});
    } else {
        writeHeader(out, 1.0f, 0, 0x801);
    }
    const auto group = fo4::test::begin_group(out, type, 0);
    const auto record = fo4::test::begin_record(out, type, 0, 0x800, 131);
    const auto text = edid + '\0';
    fo4::test::write_subrecord(out, "EDID", std::string_view{text});
    if (extra) extra(out);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

using Versions = std::vector<std::vector<std::byte>>;

void loadChain(RadiclePluginBackend& backend, const std::filesystem::path& root, const Versions& versions) {
    std::vector<std::pair<std::string, std::filesystem::path>> plugins;
    for (std::size_t index = 0; index < versions.size(); ++index) {
        const auto name = index == 0 ? std::string{"Base.esp"} : "Over" + std::to_string(index) + ".esp";
        writeBytes(root / name, versions[index]);
        plugins.emplace_back(name, root / name);
    }
    loadPlugins(backend, plugins);
}

nlohmann::json conflictMatrix(RadiclePluginBackend& backend) {
    return parseStringResult(backend.invoke(
        "backend.GetConflictMatrix", nlohmann::json::array({"000800:Base.esp"})));
}

nlohmann::json rowFor(const nlohmann::json& matrix, std::string_view field) {
    for (const auto& row : matrix.at("Rows")) {
        if (row.at("Field") == std::string(field)) return row;
    }
    FAIL("matrix row not found: " << field);
}

Extra floatValue(float value) {
    return [value](fo4::test::ByteWriter& out) {
        fo4::test::ByteWriter data;
        data.f32(value);
        fo4::test::write_subrecord(out, "FLTV", data.bytes);
    };
}

Extra rawSubrecords(std::string_view signature, std::vector<std::vector<std::byte>> payloads) {
    return [signature, payloads = std::move(payloads)](fo4::test::ByteWriter& out) {
        for (const auto& payload : payloads) fo4::test::write_subrecord(out, signature, payload);
    };
}

std::vector<std::byte> dwords(std::initializer_list<std::uint32_t> values) {
    fo4::test::ByteWriter out;
    for (const auto value : values) out.u32(value);
    return out.bytes;
}

void expectStatuses(const nlohmann::json& row, std::initializer_list<const char*> statuses) {
    REQUIRE(row.at("Statuses").size() == statuses.size());
    std::size_t index = 0;
    for (const auto* status : statuses) CHECK(row.at("Statuses")[index++] == status);
}

}  // namespace

TEST_CASE("Oracle set conditions writes the MyKiller run-on byte at CTDA offset 20") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, cobjPlugin());

    RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto message = backend.invoke("backend.SetConditions", nlohmann::json::array({
        "Patch.esp", "000A00:Patch.esp",
        R"([{"function":"GetEventData","operator":"==","value":1,"runOn":"MyKiller"}])"}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>().find("Set 1 condition(s)") == 0);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    const auto conditions = subrecordsOf(requireRecord(disk, "000A00:Patch.esp"), "CTDA");
    REQUIRE(conditions.size() == 1);
    const auto& payload = conditions[0]->data;
    REQUIRE(payload.size() == 32);
    CHECK(static_cast<unsigned>(payload[20]) == 10u);
    CHECK(static_cast<unsigned>(payload[21]) == 0u);
    CHECK(static_cast<unsigned>(payload[22]) == 0u);
    CHECK(static_cast<unsigned>(payload[23]) == 0u);

    RadiclePluginBackend reload;
    loadPatch(reload, path);
    const auto reread = parseStringResult(reload.invoke(
        "backend.GetConditions", nlohmann::json::array({"Patch.esp", "000A00:Patch.esp"})));
    REQUIRE(reread.size() == 1);
    CHECK(reread[0].at("function") == "GetEventData");
    CHECK(reread[0].at("runOn") == "MyKiller");
}

TEST_CASE("Oracle set conditions encodes a light master parameter with the final saved master ordinal") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Full.esm", miscPlugin(1.0f, 0xF00, false, {0x800}, 0x1));
    writeBytes(root / "Light.esl", miscPlugin(1.0f, 0xF00, false, {0x812}, 0x200));
    fo4::test::ByteWriter patch;
    writeHeader(patch, 1.0f, 0, 0xF00, {"Full.esm"});
    const auto group = fo4::test::begin_group(patch, "COBJ", 0);
    writeNamedRecord(patch, "COBJ", 0x01000A00u, "Recipe");
    fo4::test::end_group(patch, group);
    writeBytes(root / "Patch.esp", patch.bytes);

    RadiclePluginBackend backend;
    loadPlugins(backend, {{"Full.esm", root / "Full.esm"},
                          {"Light.esl", root / "Light.esl"},
                          {"Patch.esp", root / "Patch.esp"}});
    REQUIRE(backend.invoke("backend.SetConditions", nlohmann::json::array({
        "Patch.esp", "000A00:Patch.esp",
        R"([{"function":"GetBaseValue","param1":"000812:Light.esl","operator":">=","value":10,"runOn":"Subject"}])"}))
                .has_value());
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(root / "Patch.esp");
    REQUIRE(disk.header.masters.size() == 2u);
    CHECK(disk.header.masters[0].key == fo4::PluginKey::from_filename("Full.esm"));
    CHECK(disk.header.masters[1].key == fo4::PluginKey::from_filename("Light.esl"));
    const auto conditions = subrecordsOf(requireRecord(disk, "000A00:Patch.esp"), "CTDA");
    REQUIRE(conditions.size() == 1);
    const auto& payload = conditions[0]->data;
    REQUIRE(payload.size() == 32);
    std::uint32_t param1 = 0;
    std::memcpy(&param1, payload.data() + 12, sizeof(param1));
    CHECK(param1 == 0x01000812u);
}

TEST_CASE("Oracle set components and set conditions round trip through the saved binary") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, cobjPlugin());

    RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto components = backend.invokeBackend("SetComponents", nlohmann::json::array({
        "Patch.esp", "000A00:Patch.esp",
        R"([{"component":"000801:Patch.esp","count":1},{"component":"000802:Patch.esp","count":3}])"}));
    REQUIRE(components.has_value());
    CHECK(components->get<std::string>().find("Set 2 component(s)") == 0);
    const auto conditions = backend.invoke("backend.SetConditions", nlohmann::json::array({
        "Patch.esp", "000A00:Patch.esp",
        R"([{"function":"GetGlobalValue","param1":"000801:Patch.esp","operator":"==","value":0,"runOn":"Subject"},
            {"function":"GetBaseValue","param1":"000802:Patch.esp","operator":">=","value":25,"runOn":"Reference","reference":"000801:Patch.esp"}])"}));
    REQUIRE(conditions.has_value());
    CHECK(conditions->get<std::string>().find("Set 2 condition(s)") == 0);
    savePatch(backend);

    const auto disk = fo4::read_plugin_file(path);
    const auto entries = fo4::forms::decode_components(disk, fo4::FormKey::parse("000A00:Patch.esp"));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].component == fo4::FormKey::parse("000801:Patch.esp"));
    CHECK(entries[0].count == 1u);
    CHECK(entries[1].component == fo4::FormKey::parse("000802:Patch.esp"));
    CHECK(entries[1].count == 3u);

    RadiclePluginBackend reload;
    loadPatch(reload, path);
    const auto reread = parseStringResult(reload.invoke(
        "backend.GetConditions", nlohmann::json::array({"Patch.esp", "000A00:Patch.esp"})));
    REQUIRE(reread.size() == 2);
    CHECK(reread[0].at("function") == "GetGlobalValue");
    CHECK(reread[0].at("operator") == "==");
    CHECK(reread[0].at("value") == 0.0f);
    CHECK(reread[1].at("function") == "GetBaseValue");
    CHECK(reread[1].at("operator") == ">=");
    CHECK(reread[1].at("value") == 25.0f);
    CHECK(reread[1].at("runOn") == "Reference");
}

TEST_CASE("Oracle set field refuses Components index paths and leaves the record unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, cobjPlugin({}, 1));

    RadiclePluginBackend backend;
    loadPatch(backend, path);
    for (const auto* field : {"Components[0].Count", "Components[0].Component", "Components[5].Count"}) {
        INFO(field);
        const auto result = backend.invoke("backend.SetField", nlohmann::json::array({
            "Patch.esp", "000A00:Patch.esp", field, "000802:Patch.esp"}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "FieldWriteUnsupported");
    }
    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK(active.at(0).at("Pending") == false);

    const auto replaced = backend.invokeBackend("SetComponents", nlohmann::json::array({
        "Patch.esp", "000A00:Patch.esp", R"([{"component":"000802:Patch.esp","count":9}])"}));
    REQUIRE(replaced.has_value());
    savePatch(backend);
    const auto entries = fo4::forms::decode_components(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000A00:Patch.esp"));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].component == fo4::FormKey::parse("000802:Patch.esp"));
    CHECK(entries[0].count == 9u);
}

TEST_CASE("Oracle GetConditions names the fields and GetRecordTree lists components as raw rows") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, cobjPlugin({ctda(0x04, 0, 1, 0x801, 0, 0)}, 3));

    RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto conditions = textResult(backend.invoke(
        "backend.GetConditions", nlohmann::json::array({"Patch.esp", "000A00:Patch.esp"})));
    CHECK(conditions.find("GetDistance") != std::string::npos);
    CHECK(conditions.find("ParameterOneNumber") == std::string::npos);
    CHECK(conditions.find("RunOnType") == std::string::npos);

    fo4ide::app::PluginBackendTarget target(backend, "backend");
    const auto nodes = parseStringResult(target.invoke(
        "GetRecordTree", nlohmann::json::array({"Patch.esp", "Recipe"})));
    std::set<std::string> keys;
    for (const auto& node : nodes) keys.insert(node.at("key").get<std::string>());
    CHECK(keys.contains("Subrecords\\CVPA[0]"));
    CHECK(keys.contains("Subrecords\\CTDA[0]"));
}

TEST_CASE("Oracle matrix classifies a three way disagreement as master lose win") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    loadChain(backend, root, {versionPlugin(false, "MISC", "Alpha"), versionPlugin(true, "MISC", "Bravo"),
                              versionPlugin(true, "MISC", "Charlie")});

    const auto matrix = conflictMatrix(backend);
    CHECK(matrix.at("Level") == "conflict");
    const auto row = rowFor(matrix, "Subrecords\\EDID[0]");
    expectStatuses(row, {"master", "lose", "win"});
    CHECK(row.at("Differs") == true);
    CHECK(row.at("Severity") == "conflict");
}

TEST_CASE("Oracle matrix marks a column agreeing with the winner as override and still reports a conflict") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    loadChain(backend, root, {versionPlugin(false, "MISC", "Alpha"), versionPlugin(true, "MISC", "Bravo"),
                              versionPlugin(true, "MISC", "Bravo")});

    const auto matrix = conflictMatrix(backend);
    const auto row = rowFor(matrix, "Subrecords\\EDID[0]");
    expectStatuses(row, {"master", "override", "win"});
    CHECK(row.at("Severity") == "conflict");
    CHECK(matrix.at("Level") == "conflict");
}

TEST_CASE("Oracle identical overrides are listed as conflicts while the matrix reports no difference") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    loadChain(backend, root, {versionPlugin(false, "MISC", "Same"), versionPlugin(true, "MISC", "Same"),
                              versionPlugin(true, "MISC", "Same")});

    const auto listed = parseStringResult(backend.invoke("backend.GetConflicts", nlohmann::json::array()));
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].at("Plugins").size() == 3);
    CHECK(listed[0].at("Winner") == "Over2.esp");

    const auto matrix = conflictMatrix(backend);
    CHECK(matrix.at("Level") == "override");
    const auto row = rowFor(matrix, "Subrecords\\EDID[0]");
    expectStatuses(row, {"master", "identical", "identical"});
    CHECK(row.at("Differs") == false);
    CHECK(row.at("Severity") == "none");
}

TEST_CASE("Oracle matrix counts an empty editor id as a present value") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    loadChain(backend, root, {versionPlugin(false, "MISC", ""), versionPlugin(true, "MISC", "")});

    const auto row = rowFor(conflictMatrix(backend), "Subrecords\\EDID[0]");
    expectStatuses(row, {"master", "identical"});
    CHECK(row.at("Differs") == false);
    CHECK(row.at("Severity") == "none");
}

TEST_CASE("Oracle matrix compares decoded float values at full precision") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend same;
    loadChain(same, root / "same", {versionPlugin(false, "GLOB", "Global", floatValue(1.0f)),
                                    versionPlugin(true, "GLOB", "Global", floatValue(1.0f))});
    const auto tie = rowFor(conflictMatrix(same), R"(GLOB\Value)");
    expectStatuses(tie, {"master", "identical"});
    CHECK(tie.at("Severity") == "none");
    CHECK(tie.at("Values")[0] == "1");

    RadiclePluginBackend different;
    loadChain(different, root / "different", {versionPlugin(false, "GLOB", "Global", floatValue(1.0f)),
                                              versionPlugin(true, "GLOB", "Global", floatValue(1.5f))});
    const auto changed = rowFor(conflictMatrix(different), R"(GLOB\Value)");
    expectStatuses(changed, {"master", "win"});
    CHECK(changed.at("Severity") == "conflict");
    CHECK(changed.at("Values")[1] == "1.5");

    RadiclePluginBackend adjacent;
    loadChain(adjacent, root / "adjacent", {versionPlugin(false, "GLOB", "Global", floatValue(1.0f)),
                                            versionPlugin(true, "GLOB", "Global", floatValue(1.0000001f))});
    const auto neighbour = rowFor(conflictMatrix(adjacent), R"(GLOB\Value)");
    expectStatuses(neighbour, {"master", "win"});
    CHECK(neighbour.at("Values")[1] == "1.00000012");
}

TEST_CASE("Oracle matrix reports a reordered repeated subrecord as a conflict") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    loadChain(backend, root, {
        versionPlugin(false, "MISC", "Tagged", rawSubrecords("KWDA", {dwords({0x800u}), dwords({0x801u})})),
        versionPlugin(true, "MISC", "Tagged", rawSubrecords("KWDA", {dwords({0x801u}), dwords({0x800u})}))});

    const auto matrix = conflictMatrix(backend);
    CHECK(matrix.at("Level") == "conflict");
    CHECK(rowFor(matrix, "Subrecords\\KWDA[0]").at("Differs") == true);
    CHECK(rowFor(matrix, "Subrecords\\KWDA[1]").at("Differs") == true);
}

TEST_CASE("Oracle matrix pins binary severity when the winner holds a broken link") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    loadChain(backend, root, {
        versionPlugin(false, "MISC", "Linked", rawSubrecords("NAME", {dwords({0x800u})})),
        versionPlugin(true, "MISC", "Linked", rawSubrecords("NAME", {dwords({0x000999u})}))});

    const auto matrix = conflictMatrix(backend);
    const auto row = rowFor(matrix, "Subrecords\\NAME[0]");
    expectStatuses(row, {"master", "win"});
    CHECK(row.at("Severity") == "conflict");
    for (const auto& item : matrix.at("Rows")) {
        const auto severity = item.at("Severity").get<std::string>();
        CHECK((severity == "none" || severity == "conflict"));
    }
}

TEST_CASE("Oracle matrix drills into condition and component subrecords one row per ordinal") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    RadiclePluginBackend backend;
    const auto cobj = [](std::uint32_t count, std::int32_t secondParam) {
        return [count, secondParam](fo4::test::ByteWriter& out) {
            fo4::test::ByteWriter cvpa;
            cvpa.u32(0x800);
            cvpa.u32(count);
            fo4::test::write_subrecord(out, "CVPA", cvpa.bytes);
            fo4::test::write_subrecord(out, "CTDA", ctda(0x04, 0, 1, 0x800, 0, 0));
            fo4::test::write_subrecord(out, "CTDA", ctda(0x04, 0, 1, secondParam, 0, 0));
        };
    };
    loadChain(backend, root, {versionPlugin(false, "COBJ", "Recipe", cobj(1, 0x800)),
                              versionPlugin(true, "COBJ", "Recipe", cobj(3, 0x801))});

    const auto matrix = conflictMatrix(backend);
    CHECK(rowFor(matrix, "Subrecords").at("HasChildren") == true);
    CHECK(rowFor(matrix, "Subrecords\\CVPA[0]").at("Differs") == true);
    CHECK(rowFor(matrix, "Subrecords\\CTDA[0]").at("Differs") == false);
    const auto second = rowFor(matrix, "Subrecords\\CTDA[1]");
    CHECK(second.at("Differs") == true);
    CHECK(second.at("Statuses")[1] == "win");
    CHECK(second.at("Values")[0].get<std::string>().find("32 B: ") == 0);
}

namespace {

void writeText(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream << text;
}

void mo2Instance(const std::filesystem::path& root, const std::string& pluginsText,
                 const std::vector<std::pair<std::string, std::uint32_t>>& dataFiles) {
    const auto game = root / "Game";
    writeText(root / "ModOrganizer.ini", "selected_profile=Test\ngamePath=" + game.string() + "\n");
    writeText(root / "profiles" / "Test" / "plugins.txt", pluginsText);
    for (const auto& [name, flags] : dataFiles) {
        writeText(game / "Data" / name, plugin_fixtures_test::minimalPlugin(flags, name));
    }
}

}  // namespace

TEST_CASE("Oracle MO2 profile moves a later Fallout4.esm entry to the front without duplicating it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    mo2Instance(root, "*DLCCoast.esm\n*Fallout4.esm\n*SomeMod.esp\n",
                {{"Fallout4.esm", 1}, {"DLCCoast.esm", 1}, {"SomeMod.esp", 0}});

    const auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm", "DLCCoast.esm", "SomeMod.esp"};
    CHECK(snapshot->loadOrder == expected);
}

TEST_CASE("Oracle MO2 profile collapses duplicate and miscased Fallout4.esm entries to one canonical entry") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    mo2Instance(root, "*FALLOUT4.ESM\n*SomeMod.esp\n*fallout4.esm\n",
                {{"Fallout4.esm", 1}, {"SomeMod.esp", 0}});

    const auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm", "SomeMod.esp"};
    CHECK(snapshot->loadOrder == expected);
}

TEST_CASE("Oracle MO2 profile adds Fallout4.esm to an empty load order and reports it missing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    mo2Instance(root, "", {});

    const auto snapshot = fo4ide::mo2::readProfile(root);
    REQUIRE(snapshot.has_value());
    const std::vector<std::string> expected{"Fallout4.esm"};
    CHECK(snapshot->loadOrder == expected);
    CHECK(snapshot->missingPlugins == expected);
    REQUIRE(snapshot->plugins.size() == 1);
    CHECK(snapshot->plugins[0].resolved == false);
}

TEST_CASE("Oracle form key labels render the editor id with the key and fall back to the bare key") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, versionPlugin(false, "MISC", "NamedThing"));
    RadiclePluginBackend backend;
    loadPatch(backend, path);

    fo4ide::app::PluginBackendTarget target(backend, "backend");
    const auto labels = parseStringResult(target.invoke(
        "ResolveFormKeyLabels", nlohmann::json::array({"000800:Patch.esp, 000999:Patch.esp"})));
    CHECK(labels.at("000800:Patch.esp") == "NamedThing [000800:Patch.esp]");
    CHECK(labels.at("000999:Patch.esp") == "000999:Patch.esp");
}

TEST_CASE("Oracle weapon object templates with a malformed block refuse the add and nothing is staged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, versionPlugin(false, "WEAP", "Gun", [](fo4::test::ByteWriter& out) {
        fo4::test::ByteWriter obte;
        obte.u32(2);
        fo4::test::write_subrecord(out, "OBTE", obte.bytes);
        fo4::test::write_subrecord(out, "OBTS", std::vector<std::byte>(18, std::byte{0}));
        fo4::test::write_subrecord(out, "STOP", std::vector<std::byte>{});
    }));
    RadiclePluginBackend backend;
    loadPatch(backend, path);
    auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
    const auto args = nlohmann::json::array({"Patch.esp", "000800:Patch.esp", R"(Authoring\OBTE)"});

    const auto described = parseStringResult(elements.describeElement(args));
    CHECK(described.contains("error"));

    const auto added = elements.addElement(args);
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().type == "ElementTemplateBlockMalformed");

    const auto active = parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK(active.at(0).at("Pending") == false);
}
