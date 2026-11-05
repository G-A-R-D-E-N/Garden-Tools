#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"
#include <catch2/generators/catch_generators.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4/schema/ctda.hpp>
#include "plugin_bytes.hpp"
#include <chrono>
#include <fstream>

namespace {

void writePlugin(const std::filesystem::path& path, bool second, bool ownTarget) {
    fo4::test::ByteWriter out;
    const auto header = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f); hedr.u32(1); hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (second) {
        const std::string master("A.esp\0", 6);
        fo4::test::write_subrecord(out, "MAST", std::string_view(master));
        fo4::test::ByteWriter data;
        data.u32(0); data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, header);
    const auto group = fo4::test::begin_group(out, "SPEL", 0);
    const auto record = fo4::test::begin_record(out, "SPEL", 0, second ? 0x01000800u : 0x800u);
    const std::string editorId = second ? std::string("OwnerSpell\0", 11) : std::string("OtherSpell\0", 11);
    fo4::test::write_subrecord(out, "EDID", std::string_view(editorId));
    fo4::test::ByteWriter efid, efit;
    efid.u32(second ? 0x01000800u : 0x800u);
    efit.f32(1.0f); efit.u32(0); efit.u32(0);
    fo4::test::write_subrecord(out, "EFID", efid.bytes);
    fo4::test::write_subrecord(out, "EFIT", efit.bytes);
    fo4::CtdaCondition condition;
    condition.function = 1;
    condition.flags_byte = fo4::k_ctda_flag_use_global;
    condition.comparison_raw = second && ownTarget ? 0x01000800u : 0x800u;
    condition.param1 = static_cast<std::int32_t>(condition.comparison_raw);
    condition.reference = condition.comparison_raw;
    for (const auto& member : fo4::serialize_ctda_run(std::span(&condition, 1))) {
        fo4::test::write_subrecord(out, member.signature.to_string(), member.data);
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    std::ofstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
    REQUIRE(stream.good());
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

}

TEST_CASE("condition reads retain the matched plugin owner with unnamed lookup", "[condition-owner]") {
    const auto root = std::filesystem::temp_directory_path() /
        ("condition_owner_" + fo4ide_test::uniqueTempName());
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const bool ownTarget = GENERATE(false, true);
    writePlugin(root / "A.esp", false, false);
    writePlugin(root / "B.esp", true, ownTarget);
    fo4ide::plugin::RadiclePluginBackend backend;
    auto plugins = nlohmann::json::array();
    for (const auto* name : {"A.esp", "B.esp"}) {
        plugins.push_back({{"name", name}, {"path", (root / name).string()},
                           {"source", "Override"}, {"resolved", true}});
    }
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", plugins}}).has_value());
    const auto read = [&](const char* method, nlohmann::json args) {
        const auto result = backend.invoke(method, args);
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    };
    const auto* id = GENERATE("000800:B.esp", "OwnerSpell");
    CHECK(read("backend.GetConditions", nlohmann::json::array({"B.esp", id})).empty());
    CHECK(read("backend.GetConditions", nlohmann::json::array({"", id})).empty());
    const auto named = read("backend.GetConditionsAtPath", nlohmann::json::array({"B.esp", id, "Effects[0].Conditions"}));
    const auto unnamed = read("backend.GetConditionsAtPath", nlohmann::json::array({"", id, "Effects[0].Conditions"}));
    REQUIRE(named.size() == 1);
    const auto expected = ownTarget ? "000800:B.esp" : "000800:A.esp";
    for (const auto* field : {"param1", "reference", "compareGlobal"}) {
        CHECK(named[0][field] == expected);
        CHECK(unnamed[0][field] == expected);
    }
    CHECK(unnamed == named);
}
