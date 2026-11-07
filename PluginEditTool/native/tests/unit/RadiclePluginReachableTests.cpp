#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::vector<std::byte> gamePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(4);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const std::pair<const char*, std::uint32_t> records[] = {
        {"KYWD", 0x800}, {"GMST", 0x801}, {"KYWD", 0x802}, {"GLOB", 0x803}};
    for (const auto& [signature, id] : records) {
        const auto group = fo4::test::begin_group(out, signature, 0);
        const auto record = fo4::test::begin_record(out, signature, 0, id);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    }
    return out.bytes;
}

}  // namespace

TEST_CASE("Native FindUnreachable lists records no root reaches") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fo4ide_reachable_" + fo4ide_test::uniqueTempName());
    std::filesystem::create_directories(root);
    const auto bytes = gamePlugin();
    std::ofstream(root / "Game.esm", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Game.esm"}, {"path", (root / "Game.esm").string()}, {"source", "test"},
         {"resolved", true}}})}}).has_value());

    const auto result = backend.findUnreachable(nlohmann::json::array({"Game.esm", 1}));
    REQUIRE(result.has_value());
    const auto report = nlohmann::json::parse(result->get<std::string>());
    CHECK(report.at("Total") == 3);
    CHECK(report.at("BySignature") == nlohmann::json{{"GLOB", 1}, {"KYWD", 2}});
    const auto& rows = report.at("Records");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].at("FormKey") == "000800:Game.esm");
    CHECK(rows[0].at("Signature") == "KYWD");

    const auto missing = backend.findUnreachable(nlohmann::json::array({"Missing.esp"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PluginNotLoaded");
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
