#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"
#include <catch2/generators/catch_generators.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>
#include "plugin_bytes.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <vector>

namespace {

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

}

TEST_CASE("quest stage and alias replacements carry opaque condition padding", "[quest-opaque-condition]") {
    const bool alias = GENERATE(false, true);
    const auto scenario = GENERATE(0, 1, 2, 3, 4, 5);
    const auto root = std::filesystem::temp_directory_path() /
        ("quest_opaque_" + fo4ide_test::uniqueTempName());
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";
    const std::array<std::byte, 3> padding{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}};
    const auto makeCondition = [](std::uint32_t comparison, std::array<std::byte, 3> bytes) {
        fo4::CtdaCondition condition;
        condition.function = 8;
        condition.param1 = 1;
        condition.comparison_raw = comparison;
        condition.unknown3 = -1;
        condition.padding = bytes;
        return condition;
    };
    const auto condition = makeCondition(0x3f800000u, padding);
    const std::array<std::byte, 3> otherPadding{std::byte{0x9a}, std::byte{0xbc}, std::byte{0xde}};
    const auto second = makeCondition(0x40000000u, otherPadding);
    const auto existing = scenario >= 3 ? std::vector<fo4::CtdaCondition>{condition, second}
                                        : std::vector<fo4::CtdaCondition>{condition};
    fo4::test::ByteWriter out;
    const auto header = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f); hedr.u32(1); hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, header);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto record = fo4::test::begin_record(out, "QUST", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", std::string_view("PrefixQuest\0", 12));
    if (!alias) {
        fo4::test::ByteWriter indx;
        indx.u16(10); indx.u8(0); indx.u8(0);
        fo4::test::write_subrecord(out, "INDX", indx.bytes);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        for (const auto& existingCondition : existing) {
            fo4::test::write_subrecord(out, "CTDA", fo4::serialize_ctda_condition(existingCondition));
        }
        fo4::test::write_subrecord(out, "CNAM", std::string_view("New\0", 4));
    }
    const auto u32 = [&](const char* signature, std::uint32_t value) {
        fo4::test::ByteWriter payload;
        payload.u32(value);
        fo4::test::write_subrecord(out, signature, payload.bytes);
    };
    u32("ANAM", 2); u32("ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view("Existing\0", 9));
    u32("FNAM", 0);
    if (alias) {
        for (const auto& existingCondition : existing) {
            fo4::test::write_subrecord(out, "CTDA", fo4::serialize_ctda_condition(existingCondition));
        }
    }
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
        REQUIRE(stream.good());
    }
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "Override"}, {"resolved", true}}
    })}}).has_value());
    const auto visible = [](double value) {
        return nlohmann::json{{"function", "GetAngle"}, {"param1", 1}, {"value", value}};
    };
    auto conditions = nlohmann::json::array({visible(1.0)});
    auto expected = std::vector<fo4::CtdaCondition>{condition};
    if (scenario == 1) {
        conditions = nlohmann::json::array({visible(2.0)});
        expected = {makeCondition(0x40000000u, padding)};
    } else if (scenario == 2) {
        conditions.push_back(visible(3.0));
        expected.push_back(makeCondition(0x40400000u, {}));
    } else if (scenario == 3) {
        conditions = nlohmann::json::array({visible(3.0), visible(1.0), visible(2.0)});
        expected = {makeCondition(0x40400000u, {}), condition, second};
    } else if (scenario == 4) {
        conditions = nlohmann::json::array({visible(2.0)});
        expected = {second};
    } else if (scenario == 5) {
        conditions = nlohmann::json::array({visible(1.0), visible(4.0)});
        expected = {condition, makeCondition(0x40800000u, otherPadding)};
    }
    const auto rows = alias ? nlohmann::json::array({
        {{"kind", "reference"}, {"id", 1}, {"name", "Existing"}, {"flags", 0}, {"conditions", conditions}}
    }) : nlohmann::json::array({{{"index", 10}, {"logEntries", nlohmann::json::array({
        {{"logText", "New"}, {"conditions", conditions}}
    })}}});
    const auto result = backend.invokeBackend(alias ? "SetQuestAliases" : "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", rows}));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto* savedRecord = fo4::find_record(saved, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(savedRecord != nullptr);
    std::vector<std::vector<std::byte>> payloads;
    for (const auto& subrecord : savedRecord->subrecords) {
        if (subrecord.signature == fo4::sig_ctda) payloads.push_back(subrecord.data);
    }
    REQUIRE(payloads.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(payloads[i] == fo4::serialize_ctda_condition(expected[i]));
    }
}

TEST_CASE("quest log entry CIS1 is stored as Windows-1252 and reads back as UTF-8", "[quest-opaque-condition]") {
    const auto root = std::filesystem::temp_directory_path() /
        ("quest_cis_" + fo4ide_test::uniqueTempName());
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";
    fo4::test::ByteWriter out;
    const auto header = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f); hedr.u32(1); hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, header);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto record = fo4::test::begin_record(out, "QUST", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", std::string_view("PrefixQuest\0", 12));
    const auto u32 = [&](const char* signature, std::uint32_t value) {
        fo4::test::ByteWriter payload;
        payload.u32(value);
        fo4::test::write_subrecord(out, signature, payload.bytes);
    };
    u32("ANAM", 2); u32("ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view("Existing\0", 9));
    u32("FNAM", 0);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
        REQUIRE(stream.good());
    }
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "Override"}, {"resolved", true}}
    })}}).has_value());
    const auto conditions = nlohmann::json::array({
        {{"function", "GetGraphVariableInt"}, {"operator", "=="}, {"cis1", "caf\xC3\xA9"}, {"value", 1.0}}
    });
    const auto rows = nlohmann::json::array({{{"index", 10}, {"logEntries", nlohmann::json::array({
        {{"logText", "New"}, {"conditions", conditions}}
    })}}});
    const auto result = backend.invokeBackend("SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", rows}));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto* savedRecord = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(savedRecord != nullptr);
    std::string storedCis1;
    for (const auto& subrecord : savedRecord->subrecords) {
        if (subrecord.signature.to_string() == "CIS1") {
            storedCis1.assign(reinterpret_cast<const char*>(subrecord.data.data()), subrecord.data.size());
        }
    }
    CHECK(storedCis1 == std::string("caf\xE9\0", 5));

    const auto decoded = fo4::forms::decode_quest_stages(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    std::vector<fo4::CtdaCondition> stored;
    std::size_t consumed = 0;
    REQUIRE(fo4::forms::lossless_quest_log_entry_conditions(
        decoded.stages[0].log_entries[0].members, stored, consumed));
    REQUIRE(stored.size() == 1u);
    REQUIRE(stored[0].parameter1_string.has_value());
    CHECK(*stored[0].parameter1_string == "caf\xE9");
    const auto described = fo4ide::plugin::describeCondition(stored[0], disk);
    CHECK_NOTHROW(described.dump());
    CHECK(described.at("cis1") == "caf\xC3\xA9");
}
