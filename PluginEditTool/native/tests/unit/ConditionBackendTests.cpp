#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/ConditionTestSupport.hpp"
#include "plugin_bytes.hpp"

using namespace condition_test;

namespace {

// A WEAP with EDID + a condition list: CITC(2), GetDistance (UseGlobal,
// param1 = 02000812, runOn Reference, reference 01000800) and GetAngle
// (value 1.0, param1 = 1, runOn Subject) with CIS1/CIS2 strings.
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
    // GetDistance = 1, UseGlobal + EqualTo: flags byte 0x04 | (0 << 5).
    // All references are index-0 raws: with no masters they resolve to the
    // plugin itself, mirroring a no-master ESP's local address space.
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x04, 0x00000800, 1, 0, 0x00000800, 0, 2, 0x00000800, -1));
    // GetAngle = 8, GreaterThan (op 2 << 5), value 1.0, param1 = 1.
    fo4::test::write_subrecord(
        out, "CTDA", makeCtda(0x40, 0x3F800000u, 8, 0, 1, 0, 0, 0, -1));
    fo4::test::write_subrecord(out, "CIS1", makeZString("SomeKeyword"));
    fo4::test::write_subrecord(out, "CIS2", makeZString("TargetName"));
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
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

    // GetDistance: UseGlobal -> compareGlobal, param1 as FormID text.
    CHECK(conditions[0].at("function") == "GetDistance");
    CHECK(conditions[0].at("operator") == "==");
    CHECK(conditions[0].at("compareGlobal") == "000800:CondEdit.esp");
    CHECK(conditions[0].at("param1") == "000800:CondEdit.esp");
    CHECK(conditions[0].at("runOn") == "Reference");
    CHECK(conditions[0].at("reference") == "000800:CondEdit.esp");
    // UseGlobal is stripped from the exposed flags.
    CHECK_FALSE(conditions[0].contains("flags"));

    // GetAngle: float value, param1 = number slot.
    CHECK(conditions[1].at("function") == "GetAngle");
    CHECK(conditions[1].at("operator") == ">");
    CHECK(conditions[1].at("value") == 1.0f);
    CHECK(conditions[1].at("param1") == "000001:CondEdit.esp");
    CHECK(conditions[1].at("runOn") == "Subject");
    CHECK_FALSE(conditions[1].contains("compareGlobal"));
    CHECK_FALSE(conditions[1].contains("reference"));
    CHECK_FALSE(conditions[1].contains("flags"));
}

TEST_CASE("condition backend: GetConditions resolves by editor ID and reports failures") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "CondEdit.esp";
    writeBytes(pluginPath, makeConditionPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
                .has_value());

    // Editor-ID resolution.
    const auto byEditorId = parseStringResult(
        backend.invoke("backend.GetConditions",
                       nlohmann::json::array({"CondEdit.esp", "CondWeapon"})));
    REQUIRE(byEditorId.is_array());
    CHECK(byEditorId.size() == 2);

    // Unknown record.
    const auto missing = backend.invoke("backend.GetConditions",
                                        nlohmann::json::array({"CondEdit.esp", "000999:CondEdit.esp"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "RecordNotFound");

    // Unknown plugin.
    const auto badPlugin = backend.invoke("backend.GetConditions",
                                          nlohmann::json::array({"Nope.esp", "000800:CondEdit.esp"}));
    REQUIRE_FALSE(badPlugin.has_value());
    CHECK(badPlugin.error().type == "PluginNotLoaded");
}

TEST_CASE("condition backend: GetConditionFunctions and GetConditionRunOnTypes serve the tables") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "CondEdit.esp";
    writeBytes(pluginPath, makeConditionPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const auto functions = parseStringResult(
        backend.invoke("backend.GetConditionFunctions", nlohmann::json::array()));
    REQUIRE(functions.is_object());
    CHECK(functions.contains("GetDistance"));
    CHECK(functions.contains("GetAngle"));
    CHECK(functions.size() == 220);  // the full ConditionFunctions table

    const auto runOns = parseStringResult(
        backend.invoke("backend.GetConditionRunOnTypes", nlohmann::json::array()));
    REQUIRE(runOns.is_array());
    REQUIRE(runOns.size() == 11);
    CHECK(runOns[0] == "Subject");
    CHECK(runOns[10] == "MyKiller");
}

TEST_CASE("condition backend: SetConditions replaces the list and round-trips") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "CondEdit.esp";
    writeBytes(pluginPath, makeConditionPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
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
            {"compareGlobal", "000800:CondEdit.esp"},
            {"param1", "000800:CondEdit.esp"},
            {"runOn", "Reference"},
            {"reference", "000801:CondEdit.esp"},
            {"flags", "OR"},
        },
    });

    const auto message = backend.invoke("backend.SetConditions",
                                        nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp",
                                                               replacement.dump()}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>().find("Set 2 condition(s)") == 0);
    CHECK(message->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(message->get<std::string>().find("SavePlugin") != std::string::npos);

    const auto conditions = parseStringResult(
        backend.invoke("backend.GetConditions",
                       nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp"})));
    REQUIRE(conditions.is_array());
    REQUIRE(conditions.size() == 2);

    CHECK(conditions[0].at("function") == "GetStage");
    CHECK(conditions[0].at("operator") == ">=");
    CHECK(conditions[0].at("value") == 5.0f);
    CHECK(conditions[0].at("param1") == "000002:CondEdit.esp");
    CHECK(conditions[0].at("runOn") == "Subject");

    CHECK(conditions[1].at("function") == "GetIsID");
    CHECK(conditions[1].at("operator") == "==");
    CHECK(conditions[1].at("compareGlobal") == "000800:CondEdit.esp");
    CHECK(conditions[1].at("param1") == "000800:CondEdit.esp");
    CHECK(conditions[1].at("runOn") == "Reference");
    CHECK(conditions[1].at("reference") == "000801:CondEdit.esp");
    CHECK(conditions[1].at("flags") == "OR");

    auto beforeSave = fo4ide::plugin::RadiclePluginBackend{};
    REQUIRE(beforeSave.invoke("environment.LoadMo2",
                              nlohmann::json{{"plugins", nlohmann::json::array(
                                                             {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
                .has_value());
    const auto beforeSaveConditions = parseStringResult(
        beforeSave.invoke("backend.GetConditions",
                          nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp"})));
    REQUIRE(beforeSaveConditions.size() == 2);
    CHECK(beforeSaveConditions[0].at("function") == "GetDistance");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"CondEdit.esp"})).has_value());

    auto onDisk = fo4ide::plugin::RadiclePluginBackend{};
    REQUIRE(onDisk.invoke("environment.LoadMo2",
                          nlohmann::json{{"plugins", nlohmann::json::array(
                                                         {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
                .has_value());
    const auto reread = parseStringResult(
        onDisk.invoke("backend.GetConditions",
                      nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp"})));
    REQUIRE(reread.size() == 2);
    CHECK(reread[0].at("function") == "GetStage");
}

TEST_CASE("condition backend: SetConditions refuses a list with a bad entry and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "CondEdit.esp";
    writeBytes(pluginPath, makeConditionPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array(
                                                          {pluginEntry("CondEdit.esp", pluginPath, "Override")})}})
                .has_value());

    const nlohmann::json withFailure = nlohmann::json::array({
        {{"function", "NotARealFunction"}, {"value", 1}},
        {{"function", "GetScale"}, {"value", 2}},
    });
    const auto refused = backend.invoke("backend.SetConditions",
                                        nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp",
                                                               withFailure.dump()}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ConditionEntriesRefused");
    CHECK(refused.error().message ==
          "Refused: 1 of 2 condition entries failed: #1: unknown function 'NotARealFunction'.");
    CHECK_FALSE(parseStringResult(backend.invoke("backend.GetActivePlugins", nlohmann::json::array()))
                    .at(0).at("Pending").get<bool>());
    const auto kept = parseStringResult(backend.invoke(
        "backend.GetConditions", nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp"})));
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].at("function") == "GetDistance");

    const auto bad = backend.invoke("backend.SetConditions",
                                    nlohmann::json::array({"CondEdit.esp", "000800:CondEdit.esp",
                                                           "{not json"}));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().type == "InvalidConditionsJson");

    const auto noCond = backend.invoke("backend.SetConditions",
                                       nlohmann::json::array({"CondEdit.esp", "000999:CondEdit.esp",
                                                              "[]"}));
    REQUIRE_FALSE(noCond.has_value());
    CHECK(noCond.error().type == "RecordNotFound");
}
