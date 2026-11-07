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

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_editor_id_text_" + id);
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

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Existing\0", 9});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeConditionedPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "WEAP", 0);
    const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Caf\xE9\0", 5});
    fo4::test::ByteWriter ctda;
    ctda.u8(0x40);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u8(0);
    ctda.u32(0x3F800000u);
    ctda.u16(8);
    ctda.u16(0);
    ctda.u32(1);
    ctda.u32(0);
    ctda.u32(0);
    ctda.u32(0);
    ctda.u32(0xFFFFFFFFu);
    fo4::test::write_subrecord(out, "CTDA", ctda.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeLvlo(fo4::test::ByteWriter& out, std::uint32_t target) {
    fo4::test::ByteWriter lvlo;
    lvlo.u16(1);
    lvlo.u16(0);
    lvlo.u32(target);
    lvlo.u16(1);
    lvlo.u8(100);
    lvlo.u8(0);
    fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
}

std::vector<std::byte> makeCircularLeveledPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x802u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVLI", 0);
    const auto first = fo4::test::begin_record(out, "LVLI", 0, 0x800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Caf\xE9\0", 5});
    writeLvlo(out, 0x801u);
    fo4::test::end_record(out, first);

    const auto second = fo4::test::begin_record(out, "LVLI", 0, 0x801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ListB\0", 6});
    writeLvlo(out, 0x800u);
    fo4::test::end_record(out, second);
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

nlohmann::json eyesPayload() {
    return {
        {"editorId", "CreatedEyes"},
        {"fullText", "Created Eyes"},
        {"icon", "Textures\\Eyes\\Created.dds"},
        {"dataFlags", 5},
        {"recordFlags", 0x00000404u}
    };
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Native EditorId reads decode Windows-1252 for listings and lookups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "mods" / "MyMod" / "Patch.esp";
    const auto conditionsPath = root / "mods" / "MyMod" / "Conditions.esp";
    writeBytes(pluginPath, makePlugin());
    writeBytes(conditionsPath, makeConditionedPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", pluginPath, "MyMod"),
            pluginEntry("Conditions.esp", conditionsPath, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    auto payload = eyesPayload();
    payload["editorId"] = "Caf\xC3\xA9";
    const auto created = parseStringResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EYES", payload})));
    REQUIRE(created.at("formKey") == "000900:Patch.esp");

    SECTION("listing returns the decoded EditorId") {
        const auto listed = parseStringResult(backend.invokeBackend(
            "SearchRecords", nlohmann::json::array({"", "EYES"})));
        REQUIRE(listed.is_array());
        REQUIRE(listed.size() == 1);
        CHECK(listed[0].at("EditorID") == "Caf\xC3\xA9");
    }

    SECTION("lookup by the decoded EditorId finds the record") {
        const auto conditions = parseStringResult(backend.invokeBackend(
            "GetConditions", nlohmann::json::array({"Conditions.esp", "Caf\xC3\xA9"})));
        REQUIRE(conditions.is_array());
        REQUIRE(conditions.size() == 1);
        CHECK(conditions[0].at("function") == "GetAngle");
    }
}

TEST_CASE("Native leveled-list cycle report decodes a Windows-1252 EditorID") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "mods" / "MyMod" / "Cycle.esp";
    writeBytes(path, makeCircularLeveledPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
        {"plugins", nlohmann::json::array({pluginEntry("Cycle.esp", path, "MyMod")})}
    }).has_value());
    const auto report = backend.checkCircularLeveledLists(nlohmann::json::array({"Cycle.esp", 200}));
    REQUIRE(report.has_value());
    REQUIRE(report->is_string());
    CHECK(report->get<std::string>().find(
        "Caf\xC3\xA9 [000800:Cycle.esp] -> ListB [000801:Cycle.esp]") != std::string::npos);
}
