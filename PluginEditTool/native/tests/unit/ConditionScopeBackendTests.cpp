#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/ConditionTestSupport.hpp"
#include "plugin_bytes.hpp"

using namespace condition_test;

namespace {

// A SPEL with two effects: Effects[0] has one CTDA, Effects[1] has two.
std::vector<std::byte> makeSpellWithEffects() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "SPEL", 0);
    const auto record = fo4::test::begin_record(out, "SPEL", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", makeZString("CondSpell"));
    fo4::test::ByteWriter spit;
    for (int i = 0; i < 15; ++i) spit.u32(0);
    fo4::test::write_subrecord(out, "SPIT", spit.bytes);

    fo4::test::ByteWriter efid0;
    efid0.u32(0x00000801);
    fo4::test::write_subrecord(out, "EFID", efid0.bytes);
    fo4::test::ByteWriter efit0;
    efit0.f32(1.0f);
    efit0.u32(0);
    efit0.u32(0);
    fo4::test::write_subrecord(out, "EFIT", efit0.bytes);
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));

    fo4::test::ByteWriter efid1;
    efid1.u32(0x00000802);
    fo4::test::write_subrecord(out, "EFID", efid1.bytes);
    fo4::test::ByteWriter efit1;
    efit1.f32(1.0f);
    efit1.u32(0);
    efit1.u32(0);
    fo4::test::write_subrecord(out, "EFIT", efit1.bytes);
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x04, 0x00000800, 1, 0, 0x00000800, 0, 2, 0x00000800, -1));

    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeSpellNullBaseEffect() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    fo4::test::ByteWriter efit;
    efit.f32(1.0f);
    efit.u32(0);
    efit.u32(0);

    const auto group = fo4::test::begin_group(out, "SPEL", 0);
    const auto record = fo4::test::begin_record(out, "SPEL", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", makeZString("CondSpell"));
    fo4::test::ByteWriter spit;
    for (int i = 0; i < 15; ++i) spit.u32(0);
    fo4::test::write_subrecord(out, "SPIT", spit.bytes);

    fo4::test::ByteWriter efid0;
    efid0.u32(0x00000801);
    fo4::test::write_subrecord(out, "EFID", efid0.bytes);
    fo4::test::write_subrecord(out, "EFIT", efit.bytes);
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));

    fo4::test::write_subrecord(out, "EFIT", efit.bytes);
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x04, 0x00000800, 1, 0, 0x00000800, 0, 2, 0x00000800, -1));

    fo4::test::ByteWriter efid2;
    efid2.u32(0x00000803);
    fo4::test::write_subrecord(out, "EFID", efid2.bytes);
    fo4::test::write_subrecord(out, "EFIT", efit.bytes);
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));

    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

}  // namespace

TEST_CASE("condition backend: GetConditionsAt reads a nested effect scope") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "SpellEdit.esp";
    writeBytes(pluginPath, makeSpellWithEffects());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("SpellEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const auto first = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[0].Conditions"})));
    REQUIRE(first.is_array());
    CHECK(first.size() == 1);
    CHECK(first[0].at("function") == "GetAngle");

    const auto second = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[1].Conditions"})));
    REQUIRE(second.is_array());
    CHECK(second.size() == 2);

    const auto oob = backend.invoke("backend.GetConditionsAtPath",
                                    nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                           "Effects[2].Conditions"}));
    REQUIRE_FALSE(oob.has_value());
    CHECK(oob.error().type == "ConditionScopeOutOfRange");

    const auto unsupported = backend.invoke("backend.GetConditionsAtPath",
                                            nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                                   "Perks[0].Conditions"}));
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().type == "ConditionPathUnsupported");
}

TEST_CASE("condition backend: SetConditionsAt replaces one effect scope and round-trips") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "SpellEdit.esp";
    writeBytes(pluginPath, makeSpellWithEffects());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("SpellEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const nlohmann::json replacement = nlohmann::json::array({
        {
            {"function", "GetStage"},
            {"operator", ">="},
            {"value", 5},
            {"param1", 2},
            {"runOn", "Subject"},
        },
        {
            {"function", "GetIsID"},
            {"operator", "="},
            {"param1", "000800:SpellEdit.esp"},
            {"runOn", "Target"},
        },
    });

    const auto message = backend.invoke("backend.SetConditionsAtPath",
                                        nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                               "Effects[0].Conditions", replacement.dump()}));
    REQUIRE(message.has_value());
    const auto text = message->get<std::string>();
    CHECK(text.find("Set 2 condition(s) at Effects[0].Conditions on 000800:SpellEdit.esp in SpellEdit.esp.") == 0);
    CHECK(text.find("SavePlugin to persist.") != std::string::npos);

    const auto first = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[0].Conditions"})));
    REQUIRE(first.size() == 2);
    CHECK(first[0].at("function") == "GetStage");
    CHECK(first[1].at("function") == "GetIsID");

    const auto second = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[1].Conditions"})));
    REQUIRE(second.size() == 2);

    auto beforeSave = fo4ide::plugin::RadiclePluginBackend{};
    REQUIRE(beforeSave.invoke("environment.LoadMo2",
                              nlohmann::json{{"plugins", nlohmann::json::array(
                                                             {pluginEntry("SpellEdit.esp", pluginPath, "Override")})}})
                .has_value());
    const auto beforeSaveFirst = parseStringResult(
        beforeSave.invoke("backend.GetConditionsAtPath",
                          nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                 "Effects[0].Conditions"})));
    REQUIRE(beforeSaveFirst.size() == 1);
    CHECK(beforeSaveFirst[0].at("function") == "GetAngle");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"SpellEdit.esp"})).has_value());
    auto onDisk = fo4ide::plugin::RadiclePluginBackend{};
    REQUIRE(onDisk.invoke("environment.LoadMo2",
                          nlohmann::json{{"plugins", nlohmann::json::array(
                                                         {pluginEntry("SpellEdit.esp", pluginPath, "Override")})}})
                .has_value());
    const auto persisted = parseStringResult(
        onDisk.invoke("backend.GetConditionsAtPath",
                      nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                             "Effects[0].Conditions"})));
    REQUIRE(persisted.size() == 2);
    CHECK(persisted[0].at("function") == "GetStage");
}

TEST_CASE("condition backend: SetConditionsAt clears one effect scope without touching siblings") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "SpellEdit.esp";
    writeBytes(pluginPath, makeSpellWithEffects());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("SpellEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const auto message = backend.invoke("backend.SetConditionsAtPath",
                                        nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                               "Effects[1].Conditions", "[]"}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>().find("Set 0 condition(s) at Effects[1].Conditions") == 0);

    const auto first = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[0].Conditions"})));
    REQUIRE(first.size() == 1);

    const auto second = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[1].Conditions"})));
    REQUIRE(second.is_array());
    CHECK(second.empty());

    const auto oob = backend.invoke("backend.SetConditionsAtPath",
                                    nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                           "Effects[9].Conditions", "[]"}));
    REQUIRE_FALSE(oob.has_value());
    CHECK(oob.error().type == "ConditionScopeOutOfRange");
}

TEST_CASE("condition backend: effect boundaries handle a null-BaseEffect (no EFID) middle effect") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "SpellEdit.esp";
    writeBytes(pluginPath, makeSpellNullBaseEffect());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("SpellEdit.esp", pluginPath, "Override")})}})
                .has_value());

    for (int i = 0; i < 3; ++i) {
        const auto scope = "Effects[" + std::to_string(i) + "].Conditions";
        const auto conds = parseStringResult(
            backend.invoke("backend.GetConditionsAtPath",
                           nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp", scope})));
        REQUIRE(conds.is_array());
        CHECK(conds.size() == 1);
    }

    const auto oob = backend.invoke("backend.GetConditionsAtPath",
                                    nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                           "Effects[3].Conditions"}));
    REQUIRE_FALSE(oob.has_value());
    CHECK(oob.error().type == "ConditionScopeOutOfRange");

    const nlohmann::json replacement = nlohmann::json::array({
        {
            {"function", "GetStage"},
            {"operator", ">="},
            {"value", 5},
            {"param1", 2},
            {"runOn", "Subject"},
        },
    });
    const auto message = backend.invoke("backend.SetConditionsAtPath",
                                        nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                                               "Effects[1].Conditions", replacement.dump()}));
    REQUIRE(message.has_value());

    const auto middle = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[1].Conditions"})));
    REQUIRE(middle.size() == 1);
    CHECK(middle[0].at("function") == "GetStage");

    const auto firstScope = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[0].Conditions"})));
    CHECK(firstScope.size() == 1);
    CHECK(firstScope[0].at("function") == "GetAngle");

    const auto lastScope = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"SpellEdit.esp", "000800:SpellEdit.esp",
                                              "Effects[2].Conditions"})));
    CHECK(lastScope.size() == 1);
    CHECK(lastScope[0].at("function") == "GetAngle");
}

namespace {

void writeU32Subrecord(fo4::test::ByteWriter& out, std::string_view signature,
                       std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> makeQuestWithAliases() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto record = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", makeZString("CondQuest"));
    writeU32Subrecord(out, "ANAM", 3);

    writeU32Subrecord(out, "ALST", 0);
    fo4::test::write_subrecord(out, "ALID", makeZString("RefAlias"));
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));
    fo4::test::write_subrecord(out, "ALED", std::span<const std::byte>{});

    writeU32Subrecord(out, "ALCS", 1);

    writeU32Subrecord(out, "ALLS", 2);
    fo4::test::write_subrecord(out, "ALID", makeZString("LocAlias"));
    fo4::test::write_subrecord(out, "ALED", std::span<const std::byte>{});

    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

}  // namespace

TEST_CASE("condition backend: Get/SetConditionsAt address QUST aliases and reject collection aliases") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "QuestEdit.esp";
    writeBytes(pluginPath, makeQuestWithAliases());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("QuestEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const auto refAlias = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                              "Aliases[0].Conditions"})));
    REQUIRE(refAlias.is_array());
    REQUIRE(refAlias.size() == 1);
    CHECK(refAlias[0].at("function") == "GetAngle");

    const auto collection = backend.invoke("backend.GetConditionsAtPath",
                                           nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                                                  "Aliases[1].Conditions"}));
    REQUIRE_FALSE(collection.has_value());
    CHECK(collection.error().type == "ConditionScopeNotConditioned");

    const auto locAlias = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                              "Aliases[2].Conditions"})));
    REQUIRE(locAlias.is_array());
    CHECK(locAlias.empty());

    const auto oob = backend.invoke("backend.GetConditionsAtPath",
                                    nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                                           "Aliases[3].Conditions"}));
    REQUIRE_FALSE(oob.has_value());
    CHECK(oob.error().type == "ConditionScopeOutOfRange");

    const nlohmann::json replacement = nlohmann::json::array({
        {
            {"function", "GetStage"},
            {"operator", ">="},
            {"value", 5},
            {"param1", 2},
            {"runOn", "Subject"},
        },
    });
    const auto message = backend.invoke("backend.SetConditionsAtPath",
                                        nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                                               "Aliases[0].Conditions", replacement.dump()}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>().find("Set 1 condition(s) at Aliases[0].Conditions") == 0);

    const auto refAfter = parseStringResult(
        backend.invoke("backend.GetConditionsAtPath",
                       nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                              "Aliases[0].Conditions"})));
    REQUIRE(refAfter.size() == 1);
    CHECK(refAfter[0].at("function") == "GetStage");

    const auto rejected = backend.invoke("backend.SetConditionsAtPath",
                                         nlohmann::json::array({"QuestEdit.esp", "000800:QuestEdit.esp",
                                                                "Aliases[1].Conditions", replacement.dump()}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "ConditionScopeNotConditioned");
}
