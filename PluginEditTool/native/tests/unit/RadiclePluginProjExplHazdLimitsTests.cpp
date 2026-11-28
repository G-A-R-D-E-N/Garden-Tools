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
           ("fo4ide_proj_expl_hazd_" + id);
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

std::vector<std::byte> pluginWithRecord(std::string_view signature,
                                        std::uint32_t recordFormId = 0x00000800,
                                        std::uint32_t nextFormId = 0x801,
                                        std::uint32_t tes4Flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, recordFormId);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingTarget\0", 15});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ProjExplHazd"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
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

}

TEST_CASE("Native CreateRecord rejects payload scalars outside the generated schema") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("EXPL"));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto soundLevel = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "TooLoud"}, {"soundLevel", 5}}
        }));
    REQUIRE_FALSE(soundLevel.has_value());
    CHECK(soundLevel.error().type == "InvalidRecordCreationPayload");
    CHECK(soundLevel.error().message.find("PROJ\\SoundLevel") != std::string::npos);

    const auto stagger = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EXPL",
            nlohmann::json{{"editorId", "TooStaggered"}, {"staggerMagnitude", 5}}
        }));
    REQUIRE_FALSE(stagger.has_value());
    CHECK(stagger.error().type == "InvalidRecordCreationPayload");
    CHECK(stagger.error().message.find("EXPL\\StaggerMagnitude") != std::string::npos);

    const auto count = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EXPL",
            nlohmann::json{{"editorId", "TooMany"}, {"projectileCount", 65536}}
        }));
    REQUIRE_FALSE(count.has_value());
    CHECK(count.error().type == "InvalidRecordCreationPayload");
    CHECK(count.error().message.find("EXPL\\ProjectileCount") != std::string::npos);

    const auto speed = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "TooFast"}, {"speed", 1e39}}
        }));
    REQUIRE_FALSE(speed.has_value());
    CHECK(speed.error().type == "InvalidRecordCreationPayload");
    CHECK(speed.error().message.find("DNAM storage") != std::string::npos);
    CHECK(readBytes(path) == before);

    writeBytes(path, pluginWithRecord("SPEL"));
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ProjExplHazd"},
            {"resolved", true}
        }})}}).has_value());
    const auto limit = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "HAZD",
            nlohmann::json{
                {"editorId", "TooManyHazards"},
                {"model", "Effects\\FXFireMedSmokey01.nif"},
                {"limit", 65536}
            }
        }));
    REQUIRE_FALSE(limit.has_value());
    CHECK(limit.error().type == "InvalidRecordCreationPayload");
    CHECK(limit.error().message.find("HAZD\\Limit") != std::string::npos);
    CHECK(readBytes(path) == pluginWithRecord("SPEL"));
}
