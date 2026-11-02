#pragma once

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

namespace perk_fixture {

inline std::filesystem::path tempRoot() {
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

inline void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

inline void writeNamedRecord(fo4::test::ByteWriter& out, std::string_view type, std::uint32_t recordId,
                      std::string_view editorId) {
    const auto group = fo4::test::begin_group(out, type, 0);
    const auto record = fo4::test::begin_record(out, type, 0, recordId);
    fo4::test::write_subrecord(out, "EDID", editorId);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
}

inline std::vector<std::byte> makeMasterPlugin() {
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

inline std::vector<std::byte> makePatchPlugin(std::uint32_t flags = 0) {
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

inline nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "Test Mod"},
        {"resolved", true}
    };
}

inline void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
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

inline nlohmann::json perkPayload(nlohmann::json effects) {
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

inline nlohmann::json questEffect() {
    return {{"type", "quest_stage"}, {"rank", 1}, {"priority", 2},
            {"quest", "000100:Master.esp"}, {"questStage", 10}};
}

inline nlohmann::json abilityEffect() {
    return {{"type", "ability"}, {"ability", "000123:Master.esp"}};
}

inline std::string createPerk(fo4ide::plugin::RadiclePluginBackend& backend, const nlohmann::json& payload) {
    const auto created = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", payload}));
    REQUIRE(created.has_value());
    return nlohmann::json::parse(created->get<std::string>())["formKey"].get<std::string>();
}

inline nlohmann::json readEffects(fo4ide::plugin::RadiclePluginBackend& backend,
                           const std::string& formKey) {
    const auto got = backend.getPerkEffects(nlohmann::json::array({"Patch.esp", formKey}));
    REQUIRE(got.has_value());
    return got->at("effects");
}

inline nlohmann::json entryPoint(int function, nlohmann::json parameters) {
    return {{"type", "entry_point"}, {"entryPoint", 1}, {"function", function}, {"parameters", std::move(parameters)}};
}

inline std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

inline void writeStringTable(const std::filesystem::path& path,
                      fo4::localization::StringTableKind kind,
                      std::uint32_t id,
                      std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    fo4::localization::write_string_table_file({kind, {{id, rawBytes(text)}}}, path);
}
}  // namespace perk_fixture
