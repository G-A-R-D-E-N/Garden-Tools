#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
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
    const auto scenario = GENERATE(0, 1, 2);
    const auto root = std::filesystem::temp_directory_path() /
        ("quest_opaque_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        fo4::test::write_subrecord(out, "CTDA", fo4::serialize_ctda_condition(condition));
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
    if (alias) fo4::test::write_subrecord(out, "CTDA", fo4::serialize_ctda_condition(condition));
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
