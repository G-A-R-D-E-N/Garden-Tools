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

// 32-byte function-condition CTDA payload.
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
