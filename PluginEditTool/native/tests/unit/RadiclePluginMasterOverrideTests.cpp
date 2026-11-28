#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_master_override_" + id);
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

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::error_code sizeEc;
    const auto size = std::filesystem::file_size(path, sizeEc);
    REQUIRE_FALSE(sizeEc);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void header(fo4::test::ByteWriter& out,
            std::uint32_t flags,
            std::string_view master = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (!master.empty()) {
        std::vector<std::byte> name;
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

std::vector<std::byte> dialSource() {
    fo4::test::ByteWriter out;
    header(out, 0, "Base.esm");
    const auto group = fo4::test::begin_group(out, "DIAL", 0);
    const auto record = fo4::test::begin_record(out, "DIAL", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TopicOverride\0", 14});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> nestedRefrSource() {
    fo4::test::ByteWriter out;
    header(out, 0, "Base.esm");
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto children = fo4::test::begin_group(out, "CELL", 6);
    const auto temporary = fo4::test::begin_group(out, "CELL", 9);
    const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"NestedRef\0", 10});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, temporary);
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> target(std::uint32_t flags) {
    fo4::test::ByteWriter out;
    header(out, flags);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name,
                           const std::filesystem::path& path,
                           std::string source) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", std::move(source)},
        {"resolved", true}
    };
}

}

TEST_CASE("Master target copies top-level override and rebuilds ONAM") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "mods" / "Source" / "Source.esp";
    const auto targetPath = root / "mods" / "Patch" / "Patch.esm";
    writeBytes(sourcePath, dialSource());
    writeBytes(targetPath, target(fo4::RecordFlags::master));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            pluginEntry("Source.esp", sourcePath, "Source"),
            pluginEntry("Patch.esm", targetPath, "Patch")
        })}}).has_value());

    const auto copied = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp", "000800:Base.esm", "Patch.esm", false
        }));
    REQUIRE(copied.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esm"})).has_value());
    auto disk = fo4::read_plugin_file(targetPath);
    REQUIRE(disk.header.tes4_flags.is_master());
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Base.esm");
    REQUIRE(disk.header.overrides.size() == 1u);
    CHECK(disk.header.overrides[0] == fo4::RawFormId{0x00000800u});
    CHECK(fo4::find_record(
              disk, fo4::FormKey::parse("000800:Base.esm")) != nullptr);

    const auto replaced = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp", "000800:Base.esm", "Patch.esm", true
        }));
    REQUIRE(replaced.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esm"})).has_value());
    disk = fo4::read_plugin_file(targetPath);
    REQUIRE(disk.header.overrides.size() == 1u);
    CHECK(disk.header.overrides[0] == fo4::RawFormId{0x00000800u});
}

TEST_CASE("Missing nested override fails with containment error and leaves target intact") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "mods" / "Source" / "Source.esp";
    const auto targetPath = root / "mods" / "Patch" / "Patch.esp";
    writeBytes(sourcePath, nestedRefrSource());
    const auto targetBytes = target(0);
    writeBytes(targetPath, targetBytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            pluginEntry("Source.esp", sourcePath, "Source"),
            pluginEntry("Patch.esp", targetPath, "Patch")
        })}}).has_value());

    const auto copied = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp", "000800:Base.esm", "Patch.esp", false
        }));
    REQUIRE_FALSE(copied.has_value());
    CHECK(copied.error().type == "RecordContainmentUnsupported");
    CHECK(readBytes(targetPath) == targetBytes);
}
