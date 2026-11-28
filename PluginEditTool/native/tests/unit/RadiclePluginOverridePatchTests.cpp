#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_override_patch_" + id);
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

void header(fo4::test::ByteWriter& out, std::uint32_t flags, std::string_view master = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (!master.empty()) {
        std::vector<std::byte> name;
        for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

void writeEdid(fo4::test::ByteWriter& out, std::string_view text) {
    std::vector<std::byte> data;
    for (const char ch : text) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    data.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "EDID", std::span<const std::byte>(data));
}

// A plugin owning MISC 000800 in its own name (used as the origin master).
std::vector<std::byte> miscMaster(std::string_view editorId) {
    fo4::test::ByteWriter out;
    header(out, fo4::RecordFlags::master);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
    writeEdid(out, editorId);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

// A plugin overriding Base.esm's MISC 000800.
std::vector<std::byte> miscOverride(std::string_view editorId) {
    fo4::test::ByteWriter out;
    header(out, 0, "Base.esm");
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
    writeEdid(out, editorId);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> emptyPlugin() {
    fo4::test::ByteWriter out;
    header(out, 0);
    return out.bytes;
}

void writeFvpaSteel(fo4::test::ByteWriter& out) {
    fo4::test::ByteWriter fvpa;
    fvpa.u32(0x00000F00);
    fvpa.u32(1);
    fo4::test::write_subrecord(out, "FVPA", fvpa.bytes);
}

// Base.esm master: CMPO 000F00 "SteelComp", CMPO 000F01 "OtherComp", and COBJ
// 000800 whose FVPA uses only 000F00.
std::vector<std::byte> cobjMaster() {
    fo4::test::ByteWriter out;
    header(out, fo4::RecordFlags::master);
    const auto cmpoGroup = fo4::test::begin_group(out, "CMPO", 0);
    const auto steel = fo4::test::begin_record(out, "CMPO", 0, 0x00000F00);
    writeEdid(out, "SteelComp");
    fo4::test::end_record(out, steel);
    const auto other = fo4::test::begin_record(out, "CMPO", 0, 0x00000F01);
    writeEdid(out, "OtherComp");
    fo4::test::end_record(out, other);
    fo4::test::end_group(out, cmpoGroup);

    const auto cobjGroup = fo4::test::begin_group(out, "COBJ", 0);
    const auto cobj = fo4::test::begin_record(out, "COBJ", 0, 0x00000800);
    writeEdid(out, "RecipeBase");
    writeFvpaSteel(out);
    fo4::test::end_record(out, cobj);
    fo4::test::end_group(out, cobjGroup);
    return out.bytes;
}

// A plugin overriding Base.esm's COBJ 000800, keeping the 000F00 FVPA component.
std::vector<std::byte> cobjOverride(std::string_view editorId) {
    fo4::test::ByteWriter out;
    header(out, 0, "Base.esm");
    const auto group = fo4::test::begin_group(out, "COBJ", 0);
    const auto cobj = fo4::test::begin_record(out, "COBJ", 0, 0x00000800);
    writeEdid(out, editorId);
    writeFvpaSteel(out);
    fo4::test::end_record(out, cobj);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> nestedRefrPlugin(std::uint32_t flags, std::string_view master,
                                        std::string_view editorId) {
    fo4::test::ByteWriter out;
    header(out, flags, master);
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto children = fo4::test::begin_group(out, "CELL", 6);
    const auto temporary = fo4::test::begin_group(out, "CELL", 9);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    writeEdid(out, editorId);
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, temporary);
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, top);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path, std::string source) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", std::move(source)}, {"resolved", true}};
}

std::string readEdid(const fo4::Record& record) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature != fo4::sig_edid || sub.data.empty()) continue;
        std::string out;
        for (const auto byte : sub.data) {
            const auto ch = static_cast<char>(std::to_integer<std::uint8_t>(byte));
            if (ch == '\0') break;
            out.push_back(ch);
        }
        return out;
    }
    return {};
}

// Loads Base.esm (origin) + ModA/ModB (overrides) + an empty editable Patch.
struct OverrideFixture {
    std::filesystem::path root{tempRoot()};
    Cleanup cleanup{root};
    std::filesystem::path patchPath{root / "mods" / "Patch" / "Patch.esp"};
    fo4ide::plugin::RadiclePluginBackend backend;

    OverrideFixture() {
        const auto basePath = root / "mods" / "Base" / "Base.esm";
        const auto modAPath = root / "mods" / "ModA" / "ModA.esp";
        const auto modBPath = root / "mods" / "ModB" / "ModB.esp";
        writeBytes(basePath, miscMaster("WidgetBase"));
        writeBytes(modAPath, miscOverride("WidgetA"));
        writeBytes(modBPath, miscOverride("WidgetB"));
        writeBytes(patchPath, emptyPlugin());
        REQUIRE(backend.invoke(
            "environment.LoadMo2",
            nlohmann::json{{"plugins", nlohmann::json::array({
                pluginEntry("Base.esm", basePath, "Base"),
                pluginEntry("ModA.esp", modAPath, "ModA"),
                pluginEntry("ModB.esp", modBPath, "ModB"),
                pluginEntry("Patch.esp", patchPath, "Patch")
            })}}).has_value());
    }
};

// Base.esm COBJ 000800 (FVPA uses CMPO 000F00), overridden by ModA then ModB.
struct ComponentOverrideFixture {
    std::filesystem::path root{tempRoot()};
    Cleanup cleanup{root};
    std::filesystem::path patchPath{root / "mods" / "Patch" / "Patch.esp"};
    fo4ide::plugin::RadiclePluginBackend backend;

    ComponentOverrideFixture() {
        const auto basePath = root / "mods" / "Base" / "Base.esm";
        const auto modAPath = root / "mods" / "ModA" / "ModA.esp";
        const auto modBPath = root / "mods" / "ModB" / "ModB.esp";
        writeBytes(basePath, cobjMaster());
        writeBytes(modAPath, cobjOverride("RecipeA"));
        writeBytes(modBPath, cobjOverride("RecipeB"));
        writeBytes(patchPath, emptyPlugin());
        REQUIRE(backend.invoke(
            "environment.LoadMo2",
            nlohmann::json{{"plugins", nlohmann::json::array({
                pluginEntry("Base.esm", basePath, "Base"),
                pluginEntry("ModA.esp", modAPath, "ModA"),
                pluginEntry("ModB.esp", modBPath, "ModB"),
                pluginEntry("Patch.esp", patchPath, "Patch")
            })}}).has_value());
    }
};

}  // namespace

TEST_CASE("CreateMergedPatch dry-run lists the contested winners") {
    OverrideFixture fixture;
    const auto result = fixture.backend.createMergedPatch(
        nlohmann::json::array({"ModA.esp,ModB.esp", "Patch.esp", false}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto text = result->get<std::string>();
    CHECK(text.find("DRY RUN") != std::string::npos);
    CHECK(text.find("ModB.esp: 1 record(s) win") != std::string::npos);
}

TEST_CASE("CreateMergedPatch forwards the winning version into the patch") {
    OverrideFixture fixture;
    const auto result = fixture.backend.createMergedPatch(
        nlohmann::json::array({"ModA.esp,ModB.esp", "Patch.esp", true}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().starts_with("Merged patch Patch.esp: forwarded 1 of 1"));
    CHECK(result->get<std::string>().find("SavePlugin to persist") != std::string::npos);
    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(fixture.patchPath);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Base.esm")) == nullptr);

    REQUIRE(fixture.backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(fixture.patchPath);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Base.esm"));
    REQUIRE(record != nullptr);
    CHECK(readEdid(*record) == "WidgetB");
}

TEST_CASE("CreateMergedPatch refuses to overwrite an existing patch override") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "mods" / "Base" / "Base.esm";
    const auto modAPath = root / "mods" / "ModA" / "ModA.esp";
    const auto modBPath = root / "mods" / "ModB" / "ModB.esp";
    const auto patchPath = root / "mods" / "Patch" / "Patch.esp";
    writeBytes(basePath, miscMaster("WidgetBase"));
    writeBytes(modAPath, miscOverride("WidgetA"));
    writeBytes(modBPath, miscOverride("WidgetB"));
    writeBytes(patchPath, miscOverride("WidgetPatch"));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath, "Base"),
            pluginEntry("ModA.esp", modAPath, "ModA"),
            pluginEntry("ModB.esp", modBPath, "ModB"),
            pluginEntry("Patch.esp", patchPath, "Patch")
        })}}).has_value());

    const auto result = backend.createMergedPatch(
        nlohmann::json::array({"ModA.esp,ModB.esp", "Patch.esp", true}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("forwarded 0 of 1") != std::string::npos);
    CHECK(text.find("already present in the patch") != std::string::npos);

    const auto disk = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Base.esm"));
    REQUIRE(record != nullptr);
    CHECK(readEdid(*record) == "WidgetPatch");  // preserved, not clobbered with WidgetB
}

TEST_CASE("CreateMergedPatch requires at least two source plugins") {
    OverrideFixture fixture;
    const auto result = fixture.backend.createMergedPatch(
        nlohmann::json::array({"ModA.esp", "Patch.esp", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "MergedPatchNeedsSources");
}

TEST_CASE("RevertOverrides dry-run reports the winner and its prior version") {
    OverrideFixture fixture;
    const auto result = fixture.backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "", false, 50}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("DRY RUN -- 1 record(s) where ModB.esp is the winner") != std::string::npos);
    CHECK(text.find("restore ModA.esp's version") != std::string::npos);
}

TEST_CASE("RevertOverrides restores the prior version into the patch") {
    OverrideFixture fixture;
    const auto result = fixture.backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "", true, 50}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with(
        "Reverted 1 record(s) that ModB.esp was overriding, into Patch.esp."));
    CHECK(result->get<std::string>().find("SavePlugin to persist") != std::string::npos);
    CHECK(result->get<std::string>().find("Saved Patch.esp") == std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(fixture.patchPath);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Base.esm")) == nullptr);

    REQUIRE(fixture.backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(fixture.patchPath);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Base.esm"));
    REQUIRE(record != nullptr);
    CHECK(readEdid(*record) == "WidgetA");
}

TEST_CASE("RevertOverrides preserves an existing patch override instead of replacing it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "mods" / "Base" / "Base.esm";
    const auto modAPath = root / "mods" / "ModA" / "ModA.esp";
    const auto modBPath = root / "mods" / "ModB" / "ModB.esp";
    const auto patchPath = root / "mods" / "Patch" / "Patch.esp";
    writeBytes(basePath, miscMaster("WidgetBase"));
    writeBytes(modAPath, miscOverride("WidgetA"));
    writeBytes(modBPath, miscOverride("WidgetB"));
    writeBytes(patchPath, miscOverride("WidgetPatch"));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath, "Base"),
            pluginEntry("Patch.esp", patchPath, "Patch"),
            pluginEntry("ModA.esp", modAPath, "ModA"),
            pluginEntry("ModB.esp", modBPath, "ModB")
        })}}).has_value());

    const auto result = backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "", true, 50}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("Reverted 1 record(s)"));

    const auto disk = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Base.esm"));
    REQUIRE(record != nullptr);
    CHECK(readEdid(*record) == "WidgetPatch");  // existing override kept, not clobbered with WidgetA
}

TEST_CASE("RevertOverrides counts an un-copyable revert as skipped, not lost") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "mods" / "Base" / "Base.esm";
    const auto modAPath = root / "mods" / "ModA" / "ModA.esp";
    const auto modBPath = root / "mods" / "ModB" / "ModB.esp";
    const auto patchPath = root / "mods" / "Patch" / "Patch.esp";
    writeBytes(basePath, nestedRefrPlugin(fo4::RecordFlags::master, {}, "NestedBase"));
    writeBytes(modAPath, nestedRefrPlugin(0, "Base.esm", "NestedA"));
    writeBytes(modBPath, nestedRefrPlugin(0, "Base.esm", "NestedB"));
    writeBytes(patchPath, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath, "Base"),
            pluginEntry("ModA.esp", modAPath, "ModA"),
            pluginEntry("ModB.esp", modBPath, "ModB"),
            pluginEntry("Patch.esp", patchPath, "Patch")
        })}}).has_value());

    const auto result = backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "", true, 50}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.starts_with("Reverted 0 record(s)"));
    CHECK(text.find("1 skipped") != std::string::npos);
}

TEST_CASE("RevertOverrides reports nothing to do for a non-overriding plugin") {
    OverrideFixture fixture;
    const auto result = fixture.backend.revertOverrides(
        nlohmann::json::array({"Base.esm", "Patch.esp", "", "", false, 50}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("carries no override records") != std::string::npos);
}

TEST_CASE("RevertOverrides component filter keeps a record that uses the component") {
    ComponentOverrideFixture fixture;
    const auto result = fixture.backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "000F00:Base.esm", false, 50}));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("DRY RUN -- 1 record(s)") != std::string::npos);
    CHECK(text.find("filtered to ones using 000F00:Base.esm") != std::string::npos);
}

TEST_CASE("RevertOverrides component filter skips a record that lacks the component") {
    ComponentOverrideFixture fixture;
    const auto result = fixture.backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "OtherComp", false, 50}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("DRY RUN -- 0 record(s)") != std::string::npos);
}

TEST_CASE("RevertOverrides component filter rejects an unresolvable component") {
    ComponentOverrideFixture fixture;
    const auto result = fixture.backend.revertOverrides(
        nlohmann::json::array({"ModB.esp", "Patch.esp", "", "NoSuchThing", false, 50}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ComponentNotResolved");
}
