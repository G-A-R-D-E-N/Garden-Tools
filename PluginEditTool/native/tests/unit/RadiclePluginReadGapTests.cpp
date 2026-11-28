#include "../helpers/TempRoot.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4ide/settings/Settings.hpp>

#include "../../apps/fo4ide/McpCatalog.hpp"

#include "plugin_bytes.hpp"

namespace {

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
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void header(fo4::test::ByteWriter& out, std::uint32_t flags, std::string_view master) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (!master.empty()) {
        fo4::test::write_subrecord(out, "MAST", std::string(master) + '\0');
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
    out.u16(static_cast<std::uint16_t>(text.size()));
    out.raw(text);
}

std::vector<std::byte> baseMaster() {
    fo4::test::ByteWriter out;
    header(out, 0, {});
    const auto group = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWeapon\0", 11});
    fo4::test::write_subrecord(out, "FULL", std::string_view{"Plain Sword\0", 12});
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, group);
    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DupId\0", 6});
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, miscGroup);
    return out.bytes;
}

std::vector<std::byte> scriptedPatch() {
    fo4::test::ByteWriter vmad;
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(1);
    wstring(vmad, "MyScript");
    vmad.u8(0);
    vmad.u16(3);
    wstring(vmad, "Count");
    vmad.u8(3);
    vmad.u8(1);
    vmad.i32(7);
    wstring(vmad, "Label");
    vmad.u8(2);
    vmad.u8(1);
    wstring(vmad, "hi");
    wstring(vmad, "Target");
    vmad.u8(1);
    vmad.u8(1);
    vmad.u16(0);
    vmad.u16(0xFFFFu);
    vmad.u32(0x00000800u);

    fo4::test::ByteWriter out;
    header(out, 0, "Base.esm");
    const auto group = fo4::test::begin_group(out, "ACTI", 0);
    const auto acti = fo4::test::begin_record(out, "ACTI", 0, 0x01000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ScriptedActi\0", 13});
    fo4::test::write_subrecord(out, "FULL", std::string_view{"Fancy Door\0", 11});
    fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
    fo4::test::end_record(out, acti);
    const auto dup = fo4::test::begin_record(out, "ACTI", 0, 0x01000801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DupId\0", 6});
    fo4::test::end_record(out, dup);
    fo4::test::end_group(out, group);

    fo4::test::ByteWriter overrideVmad;
    overrideVmad.u16(6);
    overrideVmad.u16(2);
    overrideVmad.u16(1);
    wstring(overrideVmad, "PatchScript");
    overrideVmad.u8(0);
    overrideVmad.u16(1);
    wstring(overrideVmad, "Level");
    overrideVmad.u8(3);
    overrideVmad.u8(1);
    overrideVmad.i32(3);
    const auto weaponGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PatchedWeapon\0", 14});
    fo4::test::write_subrecord(out, "VMAD", overrideVmad.bytes);
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, weaponGroup);
    return out.bytes;
}

std::vector<std::byte> localizedPlugin() {
    fo4::test::ByteWriter out;
    header(out, fo4::RecordFlags::localized, {});
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"LocalizedMisc\0", 14});
    fo4::test::ByteWriter id;
    id.u32(100);
    fo4::test::write_subrecord(out, "FULL", id.bytes);
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    for (const auto ch : text) bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    return bytes;
}

nlohmann::json entry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "ReadGap"}, {"resolved", true}};
}

nlohmann::json parse(const fo4ide::Result<nlohmann::json>& result) {
    if (!result.has_value()) INFO(result.error().type + ": " + result.error().message);
    REQUIRE(result.has_value());
    return nlohmann::json::parse(result->get<std::string>());
}

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("fo4ide_read_gap_" + fo4ide_test::uniqueTempName());
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;

    Fixture() {
        writeBytes(root / "Base.esm", baseMaster());
        writeBytes(root / "Patch.esp", scriptedPatch());
        writeBytes(root / "Loc.esp", localizedPlugin());
        std::filesystem::create_directories(root / "Strings");
        fo4::localization::write_string_table_file(
            {fo4::localization::StringTableKind::strings, {{100, rawBytes("Shiny Localized Thing")}}},
            root / "Strings" / "Loc_en.STRINGS");
        fo4::localization::write_string_table_file(
            {fo4::localization::StringTableKind::dlstrings, {{500, rawBytes("Desc")}}},
            root / "Strings" / "Loc_en.DLSTRINGS");
        fo4::localization::write_string_table_file(
            {fo4::localization::StringTableKind::ilstrings, {{900, rawBytes("Dialog")}}},
            root / "Strings" / "Loc_en.ILSTRINGS");
        const nlohmann::json environment{{"plugins", nlohmann::json::array({
            entry("Base.esm", root / "Base.esm"), entry("Patch.esp", root / "Patch.esp"),
            entry("Loc.esp", root / "Loc.esp")})}};
        REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    }
};

}  // namespace

// Detects: SearchRecords must match a record by display name, inline or localized, and return the name, not only FormKey, EditorID and signature.
TEST_CASE("Native SearchRecords matches the display name and returns it") {
    Fixture fixture;

    const auto inlineHit = parse(fixture.backend.invoke("backend.SearchRecords", nlohmann::json::array({"plain sword", ""})));
    REQUIRE(inlineHit.size() == 1);
    CHECK(inlineHit[0].at("FormKey") == "000800:Base.esm");
    CHECK(inlineHit[0].at("Name") == "Plain Sword");

    const auto localizedHit = parse(fixture.backend.invoke("backend.SearchRecords", nlohmann::json::array({"SHINY LOCALIZED", ""})));
    REQUIRE(localizedHit.size() == 1);
    CHECK(localizedHit[0].at("FormKey") == "000800:Loc.esp");
    CHECK(localizedHit[0].at("Name") == "Shiny Localized Thing");

    const auto byEditorId = parse(fixture.backend.invoke("backend.SearchRecords", nlohmann::json::array({"ScriptedActi", ""})));
    REQUIRE(byEditorId.size() == 1);
    CHECK(byEditorId[0].at("Name") == "Fancy Door");
}

// Detects: ResolveEditorId must map an exact EditorID, ignoring case, to its FormKey with the winning version's EditorID and plugin, prefer the last-loaded plugin for a duplicate EditorID, resolve only the winning version's EditorID, and refuse an unknown EditorID or a FormKey that no loaded plugin holds.
TEST_CASE("Native ResolveEditorId resolves an EditorID or FormKey across the load order") {
    Fixture fixture;
    const auto resolve = [&](std::string id) { return fixture.backend.invoke("backend.ResolveEditorId", nlohmann::json::array({id})); };

    const auto actor = parse(resolve("scriptedACTI"));
    CHECK(actor.at("FormKey") == "000800:Patch.esp");
    CHECK(actor.at("EditorID") == "ScriptedActi");
    CHECK(actor.at("Type") == "ACTI");
    CHECK(actor.at("Plugin") == "Patch.esp");

    for (const auto* id : {"PatchedWeapon", "000800:Base.esm"}) {
        INFO(id);
        const auto weapon = parse(resolve(id));
        CHECK(weapon.at("FormKey") == "000800:Base.esm");
        CHECK(weapon.at("EditorID") == "PatchedWeapon");
        CHECK(weapon.at("Type") == "WEAP");
        CHECK(weapon.at("Plugin") == "Patch.esp");
    }
    CHECK(resolve("BaseWeapon").error().type == "EditorIdNotFound");

    const auto duplicate = parse(resolve("dupid"));
    CHECK(duplicate.at("FormKey") == "000801:Patch.esp");
    CHECK(duplicate.at("Type") == "ACTI");

    CHECK(resolve("Base").error().type == "EditorIdNotFound");
    CHECK(resolve("000999:Base.esm").error().type == "RecordNotFound");
    CHECK(resolve("").error().type == "EditorIdMissing");
}

std::vector<std::byte> dupPlugin(std::string_view master, std::uint32_t formId, std::string_view editor, std::uint32_t second = 0, std::string_view secondEditor = {}) {
    fo4::test::ByteWriter out;
    header(out, 0, master);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto first = fo4::test::begin_record(out, "MISC", 0, formId);
    fo4::test::write_subrecord(out, "EDID", std::string(editor) + '\0');
    fo4::test::end_record(out, first);
    if (second != 0) {
        const auto other = fo4::test::begin_record(out, "MISC", 0, second);
        fo4::test::write_subrecord(out, "EDID", std::string(secondEditor) + '\0');
        fo4::test::end_record(out, other);
    }
    fo4::test::end_group(out, group);
    return out.bytes;
}

// Detects: an EditorID that only a losing record version carries must not resolve to that record, and a later duplicate that lost its EditorID must fall through to the live record.
TEST_CASE("Native ResolveEditorId accepts only the winning version's EditorID") {
    const auto root = std::filesystem::temp_directory_path() / ("fo4ide_read_gap_win_" + fo4ide_test::uniqueTempName());
    Cleanup cleanup{root};
    writeBytes(root / "Dup1.esm", dupPlugin({}, 0x00000800u, "Dup", 0x00000801u, "Orig"));
    writeBytes(root / "Dup2.esp", dupPlugin("Dup1.esm", 0x00000801u, "Dup"));
    writeBytes(root / "Dup3.esp", dupPlugin("Dup1.esm", 0x00000801u, "Other"));
    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{{"plugins", nlohmann::json::array({
        entry("Dup1.esm", root / "Dup1.esm"), entry("Dup2.esp", root / "Dup2.esp"), entry("Dup3.esp", root / "Dup3.esp")})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    const auto resolve = [&](std::string id) { return backend.invoke("backend.ResolveEditorId", nlohmann::json::array({id})); };

    const auto live = parse(resolve("Dup"));
    CHECK(live.at("FormKey") == "000800:Dup1.esm");
    CHECK(live.at("Plugin") == "Dup1.esm");

    const auto winner = parse(resolve("Other"));
    CHECK(winner.at("FormKey") == "000801:Dup1.esm");
    CHECK(winner.at("Plugin") == "Dup3.esp");
    CHECK(resolve("Orig").error().type == "EditorIdNotFound");
}

// Detects: GetScripts must read the VMAD of the winning record version, so an override in a later plugin replaces the master's scripts, and must report a record without an adapter as having none.
TEST_CASE("Native GetScripts reads back scripts and property values") {
    Fixture fixture;
    const auto scriptsOf = [&](std::string id) { return fixture.backend.invoke("backend.GetScripts", nlohmann::json::array({id})); };

    const auto scripted = parse(scriptsOf("ScriptedActi"));
    CHECK(scripted.at("FormKey") == "000800:Patch.esp");
    CHECK(scripted.at("HasAdapter") == true);
    REQUIRE(scripted.at("Scripts").size() == 1);
    const auto& script = scripted.at("Scripts")[0];
    CHECK(script.at("Name") == "MyScript");
    REQUIRE(script.at("Properties").size() == 3);
    CHECK(script.at("Properties")[0].at("Name") == "Count");
    CHECK(script.at("Properties")[0].at("Type") == "Int");
    CHECK(script.at("Properties")[0].at("Value") == 7);
    CHECK(script.at("Properties")[1].at("Value") == "hi");
    CHECK(script.at("Properties")[2].at("Type") == "Object");
    CHECK(script.at("Properties")[2].at("Value") == "000800:Base.esm");

    CHECK(parse(scriptsOf("000800:Patch.esp")).at("Scripts").size() == 1);

    for (const auto* id : {"PatchedWeapon", "000800:Base.esm"}) {
        INFO(id);
        const auto winner = parse(scriptsOf(id));
        CHECK(winner.at("Plugin") == "Patch.esp");
        CHECK(winner.at("EditorID") == "PatchedWeapon");
        REQUIRE(winner.at("Scripts").size() == 1);
        CHECK(winner.at("Scripts")[0].at("Name") == "PatchScript");
        CHECK(winner.at("Scripts")[0].at("Properties")[0].at("Value") == 3);
    }

    const auto bare = parse(scriptsOf("LocalizedMisc"));
    CHECK(bare.at("HasAdapter") == false);
    CHECK(bare.at("Scripts").empty());

    CHECK(scriptsOf("BaseWeapon").error().type == "EditorIdNotFound");
    CHECK(scriptsOf("NoSuchRecord").error().type == "EditorIdNotFound");
    CHECK(scriptsOf("000999:Base.esm").error().type == "RecordNotFound");
}

namespace {
std::atomic<int> stringTableReads{0};
void countStringTableRead() { ++stringTableReads; }
struct ResetHook { ~ResetHook() { fo4ide::plugin::setLegacyStringResolverHook(nullptr); } };
}

// Detects: a localized plugin's string tables must be read once and reused across searches, and must be re-read when a string file changes.
TEST_CASE("Native SearchRecords caches localized names until the string file changes") {
    Fixture fixture;
    std::filesystem::remove(fixture.root / "Strings" / "Loc_en.DLSTRINGS");
    std::filesystem::remove(fixture.root / "Strings" / "Loc_en.ILSTRINGS");
    ResetHook reset;
    stringTableReads = 0;
    fo4ide::plugin::setLegacyStringResolverHook(&countStringTableRead);
    const auto search = [&](std::string query) {
        return parse(fixture.backend.invoke("backend.SearchRecords", nlohmann::json::array({query, ""})));
    };

    CHECK(search("shiny").size() == 1);
    CHECK(stringTableReads == 1);
    CHECK(search("LocalizedMisc").size() == 1);
    CHECK(search("ScriptedActi").size() == 1);
    CHECK(stringTableReads == 1);

    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{100, rawBytes("Other Thing")}}},
        fixture.root / "Strings" / "Loc_en.STRINGS");
    CHECK(search("shiny").empty());
    const auto renamed = search("other thing");
    REQUIRE(renamed.size() == 1);
    CHECK(renamed[0].at("Name") == "Other Thing");
    CHECK(stringTableReads == 2);
}

// Detects: a non-ASCII file name in a plugin or Data Strings directory must not make SearchRecords throw, and the localized names must still resolve and stay cached.
TEST_CASE("Native SearchRecords scans string directories holding non-ASCII file names") {
    Fixture fixture;
    std::filesystem::remove(fixture.root / "Strings" / "Loc_en.DLSTRINGS");
    std::filesystem::remove(fixture.root / "Strings" / "Loc_en.ILSTRINGS");
    const std::filesystem::path stray(u8"Привет_en.STRINGS");
    writeBytes(fixture.root / "Strings" / stray, rawBytes("x"));
    writeBytes(fixture.root / "Data" / "Strings" / stray, rawBytes("x"));
    fixture.backend.setDataDirectory(fixture.root / "Data");
    ResetHook reset;
    stringTableReads = 0;
    fo4ide::plugin::setLegacyStringResolverHook(&countStringTableRead);

    for (int round = 0; round < 2; ++round) {
        const auto hit = parse(fixture.backend.invoke("backend.SearchRecords", nlohmann::json::array({"shiny localized", ""})));
        REQUIRE(hit.size() == 1);
        CHECK(hit[0].at("Name") == "Shiny Localized Thing");
    }
    CHECK(stringTableReads == 1);
}

// Detects: reopening a plugin or changing the Data directory must drop its cached localized names even when no loose string file stamp moved.
TEST_CASE("Native SearchRecords re-reads string tables after a plugin reopen or a Data directory change") {
    Fixture fixture;
    std::filesystem::remove(fixture.root / "Strings" / "Loc_en.DLSTRINGS");
    std::filesystem::remove(fixture.root / "Strings" / "Loc_en.ILSTRINGS");
    ResetHook reset;
    stringTableReads = 0;
    fo4ide::plugin::setLegacyStringResolverHook(&countStringTableRead);
    const auto search = [&] {
        return parse(fixture.backend.invoke("backend.SearchRecords", nlohmann::json::array({"shiny", ""}))).size();
    };

    CHECK(search() == 1);
    CHECK(search() == 1);
    CHECK(stringTableReads == 1);

    REQUIRE(fixture.backend.invoke("backend.OpenPlugin", nlohmann::json::array({(fixture.root / "Loc.esp").string()})).has_value());
    CHECK(search() == 1);
    CHECK(stringTableReads == 2);
    CHECK(search() == 1);
    CHECK(stringTableReads == 2);

    std::filesystem::create_directories(fixture.root / "EmptyData");
    fixture.backend.setDataDirectory(fixture.root / "EmptyData");
    CHECK(search() == 1);
    CHECK(stringTableReads == 3);
}

// Detects: resolve_editorid and get_scripts must be reachable through the real catalog and backend target, not only as backend ops.
TEST_CASE("MCP resolve_editorid and get_scripts run through the real catalog") {
    Fixture fixture;
    fo4ide::settings::Settings settings;
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::rpc::Dispatcher rpc;
    REQUIRE(rpc.registerTarget("backend", std::make_shared<fo4ide::app::PluginBackendTarget>(fixture.backend, "backend")).has_value());
    fo4ide::mcp::Registry registry;
    REQUIRE(fo4ide::app::registerCatalogTools(registry, rpc, {}, shared, [] { return std::vector<std::filesystem::path>{}; }));

    const auto resolved = registry.call("resolve_editorid", {{"id", "scriptedacti"}});
    REQUIRE_FALSE(resolved.isError);
    CHECK(nlohmann::json::parse(resolved.text).at("FormKey") == "000800:Patch.esp");

    const auto scripts = registry.call("get_scripts", {{"id", "ScriptedActi"}});
    REQUIRE_FALSE(scripts.isError);
    CHECK(nlohmann::json::parse(scripts.text).at("Scripts")[0].at("Name") == "MyScript");

    CHECK(registry.call("resolve_editorid", {{"id", "000999:Base.esm"}}).isError);
}
