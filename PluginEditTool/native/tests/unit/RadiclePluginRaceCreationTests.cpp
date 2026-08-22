#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/race.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_race_creation_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> emptyPlugin(std::uint32_t flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

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
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "RaceTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("PluginEditTool creates minimal current-format RACE") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"editorId", "GardenRace"},
        {"fullName", "Garden Race"},
        {"description", "Native race"},
        {"maleHeight", 1.05},
        {"femaleHeight", 0.97},
        {"flags", 1},
        {"flags2", 2},
        {"size", 1},
        {"accelerationRate", 2.0},
        {"decelerationRate", 1.5},
        {"injuredHealthPct", 0.25}
    };
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "RACE", payload})));
    REQUIRE(created.at("formKey") == "000800:Patch.esp");

    const auto disk = fo4::read_plugin_file(path);
    const auto model = fo4::forms::decode_race(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(model.editor_id.has_value());
    CHECK(*model.editor_id == "GardenRace");
    REQUIRE(model.full_name.has_value());
    CHECK(*model.full_name == "Garden Race");
    CHECK(model.description == "Native race");
    CHECK(model.male_height == 1.05f);
    CHECK(model.female_height == 0.97f);
    CHECK(model.flags == 1u);
    CHECK(model.flags2 == 2u);
    CHECK(model.size == 1u);
    CHECK(model.acceleration_rate == 2.0f);
    CHECK(model.deceleration_rate == 1.5f);
    CHECK(model.injured_health_pct == 0.25f);
}

TEST_CASE("PluginEditTool rejects incomplete RACE payload before write") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto result = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "RACE",
            nlohmann::json{{"maleHeight", 1.0}, {"femaleHeight", 1.0}}
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidRecordCreationPayload");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool refuses RACE creation in localized plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(fo4::RecordFlags::localized));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"description", "needs strings table"},
        {"maleHeight", 1.0},
        {"femaleHeight", 1.0}
    };
    const auto result = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "RACE", payload}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "RaceCreateFailed");
    CHECK(readBytes(path) == before);
}
