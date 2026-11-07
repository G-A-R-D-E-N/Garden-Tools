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

#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_quest_authoring_" + id);
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

void writeU32(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

void writeIndx(fo4::test::ByteWriter& out,
               std::uint16_t index,
               std::uint8_t flags,
               std::uint8_t unknown) {
    fo4::test::ByteWriter data;
    data.u16(index);
    data.u8(flags);
    data.u8(unknown);
    fo4::test::write_subrecord(out, "INDX", data.bytes);
}

template <typename Body>
std::vector<std::byte> questPluginWith(std::uint32_t tes4Flags, Body&& body) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
    body(out);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "QuestTest"},
            {"resolved", true}
        }})}}).has_value());
}

std::vector<std::byte> makeCtdaPayload(std::uint16_t function,
                                       std::int32_t param1,
                                       float value,
                                       std::uint8_t flagsByte = 0,
                                       std::uint16_t unknown2 = 0,
                                       std::int32_t unknown3 = -1,
                                       std::uint32_t runOn = 0,
                                       std::uint32_t reference = 0) {
    fo4::test::ByteWriter ctda;
    ctda.u8(flagsByte);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    ctda.u32(bits);
    ctda.u16(function);
    ctda.u16(unknown2);
    ctda.u32(static_cast<std::uint32_t>(param1));
    ctda.u32(0);
    ctda.u32(runOn);
    ctda.u32(reference);
    ctda.u32(static_cast<std::uint32_t>(unknown3));
    return ctda.bytes;
}

nlohmann::json setAliasesArgs(const nlohmann::json& aliases) {
    return nlohmann::json::array({"Patch.esp", "000800:Patch.esp", aliases});
}

std::vector<std::byte> makeEventDataPayload() {
    fo4::test::ByteWriter ctda;
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u32(0);
    ctda.u16(576);
    ctda.u16(0);
    ctda.u32(0);
    ctda.u32(0);
    ctda.u32(0);
    ctda.u32(0);
    ctda.u32(static_cast<std::uint32_t>(-1));
    return ctda.bytes;
}

}

TEST_CASE("Native SetQuestStages refuses existing EventData CTDA without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(0, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "CTDA", makeEventDataPayload());
        writeU32(out, "ANAM", 0);
    }));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 20}, {"logEntry", "replacement"}}})
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
    CHECK(result.error().message.find("GetEventData") != std::string::npos);
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native SetQuestAliases refuses malformed and EventData existing alias CTDA") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";

    auto tryRefuse = [&](std::vector<std::byte> ctda, const char* needle) {
        writeBytes(path, questPluginWith(0, [&](fo4::test::ByteWriter& out) {
            writeU32(out, "ANAM", 2);
            writeU32(out, "ALST", 1);
            fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
            writeU32(out, "FNAM", 0);
            fo4::test::write_subrecord(out, "CTDA", ctda);
            fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
        }));
        const auto before = readBytes(path);
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto result = backend.invokeBackend(
            "SetQuestAliases",
            setAliasesArgs(nlohmann::json::array({
                {{"kind", "reference"}, {"id", 2}, {"name", "NewRef"}, {"flags", 0}}
            })));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "QuestAuthoringCoverageIncomplete");
        CHECK(result.error().message.find(needle) != std::string::npos);
        CHECK(readBytes(path) == before);
    };

    tryRefuse(std::vector<std::byte>(33, std::byte{0}), "CTDA/CIS");
    tryRefuse(makeEventDataPayload(), "GetEventData");
    tryRefuse(makeCtdaPayload(8, 1, 1.0f, static_cast<std::uint8_t>(6u << 5)), "operator");
    tryRefuse(makeCtdaPayload(8, 1, 1.0f, 0, 1), "unknown fields");
}
