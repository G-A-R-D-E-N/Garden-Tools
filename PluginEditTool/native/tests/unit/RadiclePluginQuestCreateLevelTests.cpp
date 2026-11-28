#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeU32(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> questWithCreateLevel(std::uint32_t level) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
    writeU32(out, "ANAM", 2);
    writeU32(out, "ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
    writeU32(out, "FNAM", 0);
    writeU32(out, "ALCO", 0);
    writeU32(out, "ALCA", 0x80000000);
    writeU32(out, "ALCL", level);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::uint32_t createLevel(const std::filesystem::path& path) {
    const auto decoded = fo4::forms::decode_quest_aliases(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.aliases.size() == 1u);
    const auto* fill = std::get_if<fo4::forms::QuestCreateReferenceFill>(&decoded.aliases[0].fill);
    REQUIRE(fill != nullptr);
    return fill->level;
}

}

TEST_CASE("Native SetQuestAliases keeps and writes the None Create Reference Level") {
    const auto id = fo4ide_test::uniqueTempName();
    const auto root = std::filesystem::temp_directory_path() / ("fo4ide_quest_level_" + id);
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";
    const auto bytes = questWithCreateLevel(4);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));
    CHECK(createLevel(path) == 4u);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"}, {"path", path.string()},
            {"source", "QuestTest"}, {"resolved", true}
        }})}}).has_value());
    const auto written = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", nlohmann::json::array({
            {
                {"kind", "reference"},
                {"id", 1},
                {"name", "Create"},
                {"flags", 0},
                {"createReference", {{"alias", 0}, {"create", "At"}, {"level", "None"}}}
            }
        })}));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(createLevel(path) == 4u);
}
