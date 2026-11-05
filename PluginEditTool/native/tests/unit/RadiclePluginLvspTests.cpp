#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_lvsp_" + id);
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

std::vector<std::byte> lvlo(std::uint16_t level, std::uint32_t formId,
                            std::uint16_t count, std::uint8_t chanceNone) {
    fo4::test::ByteWriter out;
    out.u16(level);
    out.u16(0);
    out.u32(formId);
    out.u16(count);
    out.u8(chanceNone);
    out.u8(0);
    return out.bytes;
}

std::vector<std::byte> makeMasterPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 1u, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto spellGroup = fo4::test::begin_group(out, "SPEL", 0);
    const auto spell = fo4::test::begin_record(out, "SPEL", 0, 0x00000123);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterSpell\0", 12});
    fo4::test::end_record(out, spell);
    fo4::test::end_group(out, spellGroup);

    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000456);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"NotSpell\0", 9});
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, miscGroup);
    return out.bytes;
}

std::vector<std::byte> makePatchPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esp\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVSP", 0);
    const auto record = fo4::test::begin_record(out, "LVSP", 0, 0x01000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PatchList\0", 10});
    const std::array<std::byte, 1> lvld{std::byte{10}};
    const std::array<std::byte, 1> lvlm{std::byte{0}};
    const std::array<std::byte, 1> lvlf{std::byte{1}};
    const std::array<std::byte, 1> llct{std::byte{2}};
    fo4::test::write_subrecord(out, "LVLD", lvld);
    fo4::test::write_subrecord(out, "LVLM", lvlm);
    fo4::test::write_subrecord(out, "LVLF", lvlf);
    fo4::test::write_subrecord(out, "LLCT", llct);
    fo4::test::write_subrecord(out, "LVLO", lvlo(5, 0x00000123, 1, 0));
    fo4::test::write_subrecord(out, "LVLO", lvlo(15, 0x01000810, 2, 5));
    fo4::test::end_record(out, record);

    const auto nested = fo4::test::begin_record(out, "LVSP", 0, 0x01000810);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"NestedList\0", 11});
    const std::array<std::byte, 1> zero{std::byte{0}};
    fo4::test::write_subrecord(out, "LVLD", zero);
    fo4::test::write_subrecord(out, "LVLM", zero);
    fo4::test::write_subrecord(out, "LVLF", zero);
    fo4::test::write_subrecord(out, "LLCT", zero);
    fo4::test::end_record(out, nested);
    fo4::test::end_group(out, group);
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

fo4::forms::LeveledSpell readList(const std::filesystem::path& path) {
    const auto plugin = fo4::read_plugin_file(path);
    return fo4::forms::decode_leveled_spell(
        plugin, fo4::FormKey::parse("000800:Patch.esp"));
}

}

TEST_CASE("Native SetField edits LVSP scalar settings transactionally") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);

    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\ChanceNone", "42"
    })).has_value());
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Flags", "7"
    })).has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto list = readList(patchPath);
    CHECK(list.chance_none == 42u);
    CHECK(list.max_count == 0u);
    CHECK(list.flags == 7u);
    REQUIRE(list.entries.size() == 2u);
}

TEST_CASE("Native SetField edits LVSP entry level count chance and spell") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);

    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Entries[0]\\Level", "25"
    })).has_value());
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Entries[1]\\Count", "3"
    })).has_value());
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Entries[1]\\ChanceNone", "8"
    })).has_value());
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Entries[1]\\Spell", "000810:Patch.esp"
    })).has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto list = readList(patchPath);
    REQUIRE(list.entries.size() == 2u);
    CHECK(list.entries[0].level == 15u);
    CHECK(list.entries[0].count == 2u);
    CHECK(list.entries[0].chance_none == 5u);
    CHECK(list.entries[0].spell == fo4::FormKey::parse("000810:Patch.esp"));
    CHECK(list.entries[1].level == 25u);
    CHECK(list.entries[1].count == 3u);
    CHECK(list.entries[1].chance_none == 8u);
    CHECK(list.entries[1].spell == fo4::FormKey::parse("000810:Patch.esp"));
}

TEST_CASE("Native LVSP Spell edit refuses non-spell targets without touching disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(patchPath));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);

    const auto result = backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Entries[0]\\Spell", "000456:Master.esp"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidLeveledSpellReference");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patchPath)) == before);
}

TEST_CASE("Native LVSP edits validate numeric widths and defined flag bits") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);

    const auto chance = backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\ChanceNone", "256"
    }));
    REQUIRE_FALSE(chance.has_value());
    CHECK(chance.error().type == "InvalidFieldValue");

    const auto flags = backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Flags", "8"
    }));
    REQUIRE_FALSE(flags.has_value());
    CHECK(flags.error().type == "InvalidFieldValue");

    const auto level = backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "LVSP\\Entries[0]\\Level", "65536"
    }));
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().type == "InvalidFieldValue");
}
