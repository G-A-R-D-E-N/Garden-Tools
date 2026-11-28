#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path undecodableTempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_undecodable_maintenance_" + id);
}

struct UndecodableCleanup {
    std::filesystem::path root;
    ~UndecodableCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void undecodableWriteBytes(const std::filesystem::path& path,
                           const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> undecodableReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
    return bytes;
}

void undecodableHeader(fo4::test::ByteWriter& out, std::uint32_t nextFormId) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
}

void undecodableRawPayload(fo4::test::ByteWriter& out) {
    out.u32(32);
    out.raw(std::string_view{"not-zlib-data-at-all"});
}

std::vector<std::byte> undecodableRaceSourceBytes() {
    fo4::test::ByteWriter out;
    undecodableHeader(out, 0x802);
    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(
        out, "RACE", fo4::RecordFlags::compressed, 0x00000801u, 131);
    undecodableRawPayload(out);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

std::vector<std::byte> undecodableEmptyPatchBytes() {
    fo4::test::ByteWriter out;
    undecodableHeader(out, 0x800);
    return out.bytes;
}

std::vector<std::byte> undecodableDeletedStaticBytes() {
    fo4::test::ByteWriter out;
    undecodableHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(
        out, "STAT", fo4::RecordFlags::deleted | fo4::RecordFlags::compressed, 0x00000800u, 131);
    undecodableRawPayload(out);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json undecodablePluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "UndecodableMaintenanceTest"},
        {"resolved", true}
    };
}

void undecodableLoad(fo4ide::plugin::RadiclePluginBackend& backend,
                     const nlohmann::json& plugins) {
    const auto result = backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", plugins}});
    REQUIRE(result.has_value());
}

void undecodableExpectNothingPending(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto pending = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(pending.has_value());
    for (const auto& plugin : nlohmann::json::parse(pending->get<std::string>())) {
        CHECK(plugin.at("Pending") == false);
    }
}

}  // namespace

TEST_CASE("Native CopyAsNewRecord refuses an undecodable top-level source before staging") {
    const auto root = undecodableTempRoot();
    UndecodableCleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    undecodableWriteBytes(sourcePath, undecodableRaceSourceBytes());
    undecodableWriteBytes(patchPath, undecodableEmptyPatchBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    undecodableLoad(backend, nlohmann::json::array({
        undecodablePluginEntry("Base.esm", sourcePath),
        undecodablePluginEntry("Patch.esp", patchPath)
    }));
    const auto before = undecodableReadBytes(patchPath);

    const auto refused = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({"Base.esm", "000801:Base.esm", "Patch.esp", ""}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "CopyAsNewFailed");
    CHECK(refused.error().message.find("undecodable") != std::string::npos);
    undecodableExpectNothingPending(backend);
    CHECK(undecodableReadBytes(patchPath) == before);
}

TEST_CASE("Native CleanPlugin leaves an undecodable deleted record untouched") {
    const auto root = undecodableTempRoot();
    UndecodableCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    undecodableWriteBytes(path, undecodableDeletedStaticBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    undecodableLoad(backend, nlohmann::json::array({undecodablePluginEntry("Patch.esp", path)}));

    const auto result = backend.invokeLegacyBackend(
        "CleanPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>() ==
          "Cleaned Patch.esp: undeleted + disabled 0 record(s), skipped 1 undecodable.");
    undecodableExpectNothingPending(backend);
}

TEST_CASE("Native CleanUnusedMasters names the undecodable record that blocks it") {
    const auto root = undecodableTempRoot();
    UndecodableCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    undecodableWriteBytes(path, undecodableRaceSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    undecodableLoad(backend, nlohmann::json::array({undecodablePluginEntry("Patch.esp", path)}));
    const auto before = undecodableReadBytes(path);

    const auto refused = backend.cleanUnusedMasters(nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "MasterUsageIncomplete");
    CHECK(refused.error().message.find("RACE 000801:Patch.esp") != std::string::npos);
    CHECK(refused.error().message.find("VMAD") == std::string::npos);
    undecodableExpectNothingPending(backend);
    CHECK(undecodableReadBytes(path) == before);
}

TEST_CASE("Native DeleteRecord refuses while a loaded plugin has an undecodable record") {
    const auto root = undecodableTempRoot();
    UndecodableCleanup cleanup{root};
    const auto patchPath = root / "Patch.esp";
    const auto hiddenPath = root / "Hidden.esp";
    undecodableWriteBytes(patchPath, undecodableEmptyPatchBytes());
    undecodableWriteBytes(hiddenPath, undecodableRaceSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    undecodableLoad(backend, nlohmann::json::array({
        undecodablePluginEntry("Patch.esp", patchPath),
        undecodablePluginEntry("Hidden.esp", hiddenPath)
    }));
    const auto patchBefore = undecodableReadBytes(patchPath);
    const auto hiddenBefore = undecodableReadBytes(hiddenPath);

    const auto refused = backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ReferenceCoverageIncomplete");
    CHECK(refused.error().message.find("delete safety") != std::string::npos);
    CHECK(refused.error().message.find("Hidden.esp") != std::string::npos);
    undecodableExpectNothingPending(backend);
    CHECK(undecodableReadBytes(patchPath) == patchBefore);
    CHECK(undecodableReadBytes(hiddenPath) == hiddenBefore);
}

TEST_CASE("Native RenumberRecord refuses while a loaded plugin has an undecodable record") {
    const auto root = undecodableTempRoot();
    UndecodableCleanup cleanup{root};
    const auto patchPath = root / "Patch.esp";
    const auto hiddenPath = root / "Hidden.esp";
    undecodableWriteBytes(patchPath, undecodableEmptyPatchBytes());
    undecodableWriteBytes(hiddenPath, undecodableRaceSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    undecodableLoad(backend, nlohmann::json::array({
        undecodablePluginEntry("Patch.esp", patchPath),
        undecodablePluginEntry("Hidden.esp", hiddenPath)
    }));
    const auto patchBefore = undecodableReadBytes(patchPath);
    const auto hiddenBefore = undecodableReadBytes(hiddenPath);

    const auto refused = backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "000801:Patch.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ReferenceCoverageIncomplete");
    CHECK(refused.error().message.find("renumber safety") != std::string::npos);
    CHECK(refused.error().message.find("Hidden.esp") != std::string::npos);
    undecodableExpectNothingPending(backend);
    CHECK(undecodableReadBytes(patchPath) == patchBefore);
    CHECK(undecodableReadBytes(hiddenPath) == hiddenBefore);
}
