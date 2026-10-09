#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_esl_" + std::to_string(id));
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

std::vector<std::byte> makePlugin(const std::vector<std::uint32_t>& objectIds,
                                  bool hasMaster = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(static_cast<std::uint32_t>(objectIds.size()));
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (hasMaster) {
        fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    for (std::size_t index = 0; index < objectIds.size(); ++index) {
        const auto record = fo4::test::begin_record(
            out, "MISC", 0, objectIds[index] | (hasMaster ? 0x01000000u : 0u));
        const auto editor = "Record" + std::to_string(index);
        std::vector<std::byte> editorBytes;
        editorBytes.reserve(editor.size() + 1);
        for (const char ch : editor) {
            editorBytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        editorBytes.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "EDID", editorBytes);
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, group);
    return out.bytes;
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& path) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "Test Mod"},
            {"resolved", true}
        }})}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 1);
}

std::string eligibility(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result = backend.invokeBackend(
        "CheckEslEligibility", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

}

TEST_CASE("Native ESL eligibility reports already-compatible plugins") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000800, 0x00000FFF}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("already ESL-compatible") != std::string::npos);
    CHECK(report.find("2 native record") != std::string::npos);
}

TEST_CASE("Native ESL eligibility reports compactable out-of-range records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000800, 0x00001000, 0x00002000}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("can fit the ESL compacted range") != std::string::npos);
    CHECK(report.find("remapping 2 of 3") != std::string::npos);
    CHECK(report.find("read-only") != std::string::npos);
}

TEST_CASE("Native ESL eligibility refuses plugins larger than the compacted range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    std::vector<std::uint32_t> ids;
    ids.reserve(2049);
    for (std::uint32_t i = 0; i < 2049; ++i) {
        ids.push_back(0x00001000u + i);
    }
    writeBytes(path, makePlugin(ids));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("CANNOT fit") != std::string::npos);
    CHECK(report.find("2049 native record") != std::string::npos);
    CHECK(report.find("2048-slot") != std::string::npos);
}

TEST_CASE("Native ESL compaction uses the target light range boundaries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x000u, 0x001u, 0x7FFu, 0x800u, 0xFFFu, 0x1000u}, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 2 record") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK_FALSE(disk.header.tes4_flags.is_small_file());
    for (const auto local : {0x001u, 0x002u, 0x003u, 0x7FFu, 0x800u, 0xFFFu}) {
        CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{local}}) != nullptr);
    }
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0u}}) == nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0x1000u}}) == nullptr);
}

TEST_CASE("Native ESL compaction can fill all 4095 target slots") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    std::vector<std::uint32_t> ids;
    for (std::uint32_t local = 1; local < 0xFFFu; ++local) ids.push_back(local);
    ids.push_back(0x1000u);
    writeBytes(path, makePlugin(ids, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    CHECK(eligibility(backend).find("can fit") != std::string::npos);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("remapped 1 record") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0xFFFu}}) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey{disk.key, fo4::LocalFormId{0x1000u}}) == nullptr);
}

TEST_CASE("Native ESL compaction refuses 4096 own records without staging changes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    std::vector<std::uint32_t> ids;
    for (std::uint32_t local = 1; local <= 0x1000u; ++local) ids.push_back(local);
    const auto original = makePlugin(ids, true);
    writeBytes(path, original);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);
    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "EslCapacityExceeded");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> actual{std::istreambuf_iterator<char>{stream}, {}};
    REQUIRE(actual.size() == original.size());
    CHECK(std::memcmp(actual.data(), original.data(), original.size()) == 0);
}

TEST_CASE("Native ESL eligibility counts masterless IDs below the full floor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin({0x00000700, 0x00000800}));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto report = eligibility(backend);
    CHECK(report.find("remapping 1 of 2") != std::string::npos);
    CHECK(report.find("0x800-0xFFF") != std::string::npos);
}
