#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_cond_backend_" + id);
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

std::vector<std::byte> makeCtda(std::uint8_t flagsByte, std::uint32_t comparison,
                                std::uint16_t function, std::uint16_t unknown2,
                                std::int32_t param1, std::int32_t param2,
                                std::uint32_t runOn, std::uint32_t reference,
                                std::int32_t unknown3) {
    fo4::test::ByteWriter ctda;
    ctda.u8(flagsByte);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u32(comparison);
    ctda.u16(function);
    ctda.u16(unknown2);
    ctda.u32(static_cast<std::uint32_t>(param1));
    ctda.u32(static_cast<std::uint32_t>(param2));
    ctda.u32(runOn);
    ctda.u32(reference);
    ctda.u32(static_cast<std::uint32_t>(unknown3));
    return ctda.bytes;
}

std::vector<std::byte> makeZString(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size() + 1);
    for (const char ch : text) {
        out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    out.push_back(std::byte{0});
    return out;
}

std::vector<std::byte> makeConditionPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "WEAP", 0);
    const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", makeZString("CondWeapon"));
    fo4::test::ByteWriter citc;
    citc.u32(2);
    fo4::test::write_subrecord(out, "CITC", citc.bytes);
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x04, 0x00000800, 1, 0, 0x00000800, 0, 2, 0x00000800, -1));
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));
    fo4::test::write_subrecord(out, "CIS1", makeZString("SomeKeyword"));
    fo4::test::write_subrecord(out, "CIS2", makeZString("TargetName"));
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
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

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}  // namespace

TEST_CASE("condition backend: GetConditions serializes the CTDA list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "CondEdit.esp";
    writeBytes(pluginPath, makeConditionPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const auto conditions = parseStringResult(
        backend.invoke("backend.GetConditions",
                       nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp"})));
    REQUIRE(conditions.is_array());
    REQUIRE(conditions.size() == 2);

    CHECK(conditions[0].at("function") == "GetDistance");
    CHECK(conditions[0].at("operator") == "==");
    CHECK(conditions[0].at("compareGlobal") == "000800:CondEdit.esp");
    CHECK(conditions[0].at("param1") == "000800:CondEdit.esp");
    CHECK(conditions[0].at("runOn") == "Reference");
    CHECK(conditions[0].at("reference") == "000800:CondEdit.esp");
    CHECK_FALSE(conditions[0].contains("flags"));

    CHECK(conditions[1].at("function") == "GetAngle");
    CHECK(conditions[1].at("operator") == ">");
    CHECK(conditions[1].at("value") == 1.0f);
    CHECK(conditions[1].at("param1") == 1);
    CHECK(conditions[1].at("runOn") == "Subject");
    CHECK_FALSE(conditions[1].contains("compareGlobal"));
    CHECK_FALSE(conditions[1].contains("reference"));
    CHECK_FALSE(conditions[1].contains("flags"));
}
