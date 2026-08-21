#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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

std::vector<std::byte> makePlugin(const std::vector<std::uint32_t>& objectIds) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(static_cast<std::uint32_t>(objectIds.size()));
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    for (std::size_t index = 0; index < objectIds.size(); ++index) {
        const auto record = fo4::test::begin_record(out, "MISC", 0, objectIds[index]);
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
