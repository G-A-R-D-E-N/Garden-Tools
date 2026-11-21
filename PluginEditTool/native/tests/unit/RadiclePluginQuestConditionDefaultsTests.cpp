#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/ctda.hpp>
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

std::vector<std::byte> bareQuestPlugin() {
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
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

}

TEST_CASE("Native SetQuestStages stores a quest condition without a value as comparison 0") {
    const auto id = fo4ide_test::uniqueTempName();
    const auto root = std::filesystem::temp_directory_path() /
                      ("fo4ide_quest_condition_default_" + id);
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";
    const auto bytes = bareQuestPlugin();
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"}, {"path", path.string()},
            {"source", "QuestTest"}, {"resolved", true}
        }})}}).has_value());
    const nlohmann::json stages = nlohmann::json::array({{
        {"index", 10},
        {"logEntries", nlohmann::json::array({{
            {"flags", 0},
            {"logText", "Start"},
            {"conditions", nlohmann::json::array({{
                {"function", "GetIsID"},
                {"param1", "000800:Patch.esp"}
            }})}
        }})}
    }});
    const auto written = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", stages}));
    REQUIRE(written.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto decoded = fo4::forms::decode_quest_stages(
        fo4::read_plugin_file(path), fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    std::vector<fo4::CtdaCondition> conditions;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        decoded.stages[0].log_entries[0].members, conditions, consumed));
    REQUIRE(conditions.size() == 1u);
    CHECK(conditions[0].comparison_float() == 0.0f);
}

TEST_CASE("Native SetQuestStages refuses a quest condition value outside float range") {
    const auto id = fo4ide_test::uniqueTempName();
    const auto root = std::filesystem::temp_directory_path() /
                      ("fo4ide_quest_condition_range_" + id);
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";
    const auto bytes = bareQuestPlugin();
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                static_cast<std::streamsize>(bytes.size()));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"}, {"path", path.string()},
            {"source", "QuestTest"}, {"resolved", true}
        }})}}).has_value());
    const nlohmann::json stages = nlohmann::json::array({{
        {"index", 10},
        {"logEntries", nlohmann::json::array({{
            {"flags", 0},
            {"logText", "Start"},
            {"conditions", nlohmann::json::array({{
                {"function", "GetIsID"},
                {"param1", "000800:Patch.esp"},
                {"value", 1e39}
            }})}
        }})}
    }});
    const auto written = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", stages}));
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().message.find("comparison value must be a finite number") != std::string::npos);
}
