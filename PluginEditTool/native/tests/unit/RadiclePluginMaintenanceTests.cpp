#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_maintenance_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void writeHeader(fo4::test::ByteWriter& out,
                 std::uint32_t nextFormId,
                 std::initializer_list<std::string_view> masters = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
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

std::vector<std::byte> deletedPluginBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(
        out, "STAT", fo4::RecordFlags::deleted, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DeletedStatic\0", 14});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> basePluginBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(out, "STAT", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SharedStatic\0", 13});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\Shared.nif\0", 18});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> identicalOverrideBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x800, {"Base.esm"});
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(out, "STAT", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SharedStatic\0", 13});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\Shared.nif\0", 18});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "MaintenanceTest"},
        {"resolved", true}
    };
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const nlohmann::json& plugins) {
    const auto result = backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", plugins}});
    REQUIRE(result.has_value());
}

}  // namespace

TEST_CASE("Native CleanPlugin undeletes and initially disables records on disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, deletedPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({pluginEntry("Patch.esp", path)}));

    const auto result = backend.invokeLegacyBackend(
        "CleanPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("undeleted + disabled 1 record") != std::string::npos);

    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    CHECK_FALSE(record->header.flags.is_deleted());
    CHECK((record->header.flags.value() & 0x00000800u) != 0u);
}

TEST_CASE("Native RemoveIdenticalToMaster previews and removes top-level ITM overrides") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(basePath, basePluginBytes());
    writeBytes(patchPath, identicalOverrideBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", basePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto preview = backend.invokeLegacyBackend(
        "RemoveIdenticalToMaster",
        nlohmann::json::array({"Patch.esp", false, 2000}));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN: 1 identical-to-master record"));
    CHECK(preview->get<std::string>().find("SharedStatic") != std::string::npos);

    const auto applied = backend.invokeLegacyBackend(
        "RemoveIdenticalToMaster",
        nlohmann::json::array({"Patch.esp", true, 2000}));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Removed 1 identical-to-master record") != std::string::npos);

    const auto patch = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(
        patch, fo4::FormKey::parse("000800:Base.esm")) == nullptr);

    const auto base = fo4::read_plugin_file(basePath);
    CHECK(fo4::find_record(
        base, fo4::FormKey::parse("000800:Base.esm")) != nullptr);
}
