#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_preflight_" + id);
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

std::vector<std::byte> makePlugin(bool duplicateHealth,
                                  std::uint32_t health,
                                  float scale) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "REFR", 0);
    const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    fo4::test::ByteWriter healthBytes;
    healthBytes.u32(health);
    fo4::test::write_subrecord(out, "XHLT", healthBytes.bytes);
    if (duplicateHealth) {
        fo4::test::write_subrecord(out, "XHLT", healthBytes.bytes);
    }
    fo4::test::ByteWriter scaleBytes;
    scaleBytes.f32(scale);
    fo4::test::write_subrecord(out, "XSCL", scaleBytes.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "PreflightTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

nlohmann::json problems(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto result = backend.invokeBackend(
        "GetProblems", nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("GetProblems reports duplicate schema scalar storage as critical") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(true, 50, 1.0f));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto rows = problems(backend);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].at("Code") == "schema_preflight");
    CHECK(rows[0].at("Detail") == "duplicate_scalar_subrecord");
    CHECK(rows[0].at("Severity") == "critical");
    CHECK(rows[0].at("Description").get<std::string>().find("REFR\\HealthPercent") !=
          std::string::npos);
    CHECK(rows[0].at("Description").get<std::string>().find("duplicate") !=
          std::string::npos);
}

TEST_CASE("GetProblems reports readable out-of-range schema values as warnings") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin(false, 150, 12.0f));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto rows = problems(backend);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].at("Code") == "schema_preflight");
    CHECK(rows[1].at("Code") == "schema_preflight");
    CHECK(rows[0].at("Detail") == "scalar_value_out_of_range");
    CHECK(rows[1].at("Detail") == "scalar_value_out_of_range");
    CHECK(rows[0].at("Severity") == "warning");
    CHECK(rows[1].at("Severity") == "warning");
    const auto first = rows[0].at("Description").get<std::string>();
    const auto second = rows[1].at("Description").get<std::string>();
    CHECK((first.find("REFR\\HealthPercent") != std::string::npos ||
           second.find("REFR\\HealthPercent") != std::string::npos));
    CHECK((first.find("REFR\\Scale") != std::string::npos ||
           second.find("REFR\\Scale") != std::string::npos));
}
