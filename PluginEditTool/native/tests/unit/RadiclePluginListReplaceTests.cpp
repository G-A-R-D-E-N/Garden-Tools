#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_list_replace_" + id);
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

std::vector<std::byte> makeZString(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size() + 1);
    for (const char ch : text) {
        out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    out.push_back(std::byte{0});
    return out;
}

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

void appendAngleConditions(fo4::test::ByteWriter& out) {
    for (const std::uint32_t valueBits : {0x3F800000u, 0x40000000u, 0x40400000u}) {
        fo4::test::write_subrecord(out, "CTDA", makeCtda(0x40, valueBits, 8, 0, 1, 0, 0, 0, -1));
    }
}

std::vector<std::byte> makeWeaponWithThreeConditions() {
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
    fo4::test::write_subrecord(out, "EDID", makeZString("ListWeapon"));
    fo4::test::ByteWriter citc;
    citc.u32(3);
    fo4::test::write_subrecord(out, "CITC", citc.bytes);
    appendAngleConditions(out);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeSpellWithThreeEffectConditions() {
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
    fo4::test::write_subrecord(out, "EDID", makeZString("ListSpell"));
    fo4::test::ByteWriter spit;
    for (int i = 0; i < 15; ++i) spit.u32(0);
    fo4::test::write_subrecord(out, "SPIT", spit.bytes);
    fo4::test::ByteWriter efid;
    efid.u32(0x00000801);
    fo4::test::write_subrecord(out, "EFID", efid.bytes);
    fo4::test::ByteWriter efit;
    efit.f32(1.0f);
    efit.u32(0);
    efit.u32(0);
    fo4::test::write_subrecord(out, "EFIT", efit.bytes);
    appendAngleConditions(out);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeMiscWithComponents() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(7);
    hedr.u32(0xF00);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto cmpoGroup = fo4::test::begin_group(out, "CMPO", 0);
    for (const std::uint32_t id : {0x00000801u, 0x00000802u}) {
        const auto cmpo = fo4::test::begin_record(out, "CMPO", 0, id, 131);
        fo4::test::end_record(out, cmpo);
    }
    fo4::test::end_group(out, cmpoGroup);

    const auto weapGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weap = fo4::test::begin_record(out, "WEAP", 0, 0x00000B00, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Gun"));
    fo4::test::end_record(out, weap);
    fo4::test::end_group(out, weapGroup);

    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000900, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view("Scrap"));
    fo4::test::ByteWriter data;
    data.u32(10);
    data.f32(1.0f);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::ByteWriter cvpa;
    cvpa.u32(0x00000801);
    cvpa.u32(1);
    cvpa.u32(0x00000802);
    cvpa.u32(2);
    fo4::test::write_subrecord(out, "CVPA", cvpa.bytes);
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, miscGroup);
    return out.bytes;
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend,
               const std::filesystem::path& path) {
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "test"},
            {"resolved", true}
        }})}});
    REQUIRE(loaded.has_value());
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

bool isPending(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    return active.at(0).at("Pending").get<bool>();
}

const fo4::Subrecord* findSub(const fo4::Record& record, std::string_view signature) {
    const auto sig = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == sig) return &subrecord;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("list replace: SetConditions refuses a partly bad list and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makeWeaponWithThreeConditions();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const nlohmann::json withBadEntry = nlohmann::json::array({
        {{"function", "GetStage"}, {"operator", ">="}, {"value", 5}, {"param1", 2}, {"runOn", "Subject"}},
        {{"function", "NotARealFunction"}, {"value", 1}},
        {{"function", "GetScale"}, {"value", 2}},
    });
    const auto result = backend.invoke(
        "backend.SetConditions",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", withBadEntry.dump()}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidConditionsJson");
    CHECK(result.error().message ==
          "#2: unknown function 'NotARealFunction'");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
    const auto conditions = parseStringResult(backend.invoke(
        "backend.GetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    REQUIRE(conditions.size() == 3);
    CHECK(conditions[0].at("value") == 1.0f);
    CHECK(conditions[1].at("value") == 2.0f);
    CHECK(conditions[2].at("value") == 3.0f);
}

TEST_CASE("list replace: SetConditions with an explicit empty list clears the record") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeWeaponWithThreeConditions());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto message = backend.invoke(
        "backend.SetConditions",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "[]"}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>() ==
          "Set 0 condition(s) on 000800:Patch.esp in Patch.esp. SavePlugin to persist.");
    const auto conditions = parseStringResult(backend.invoke(
        "backend.GetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    CHECK(conditions.empty());
}

TEST_CASE("list replace: SetConditions refuses an empty string and keeps the list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeWeaponWithThreeConditions());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto result = backend.invoke(
        "backend.SetConditions",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", ""}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidConditionsJson");
    CHECK(result.error().message == "conditions JSON is empty; pass [] to clear");

    CHECK_FALSE(isPending(backend));
    const auto conditions = parseStringResult(backend.invoke(
        "backend.GetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    REQUIRE(conditions.size() == 3);
    CHECK(conditions[0].at("value") == 1.0f);
}

TEST_CASE("list replace: SetConditionsAtPath refuses an empty string and keeps the scope") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeSpellWithThreeEffectConditions());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto result = backend.invoke(
        "backend.SetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[0].Conditions", ""}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidConditionsJson");
    CHECK(result.error().message == "conditions JSON is empty; pass [] to clear");

    CHECK_FALSE(isPending(backend));
    const auto conditions = parseStringResult(backend.invoke(
        "backend.GetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[0].Conditions"})));
    REQUIRE(conditions.size() == 3);
    CHECK(conditions[0].at("value") == 1.0f);
}

TEST_CASE("list replace: SetConditionsAtPath refuses a partly bad list and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makeSpellWithThreeEffectConditions();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const nlohmann::json withBadEntry = nlohmann::json::array({
        {{"function", "GetStage"}, {"operator", ">="}, {"value", 5}, {"param1", 2}, {"runOn", "Subject"}},
        {{"function", "NotARealFunction"}, {"value", 1}},
        {{"function", "GetScale"}, {"value", 2}},
    });
    const auto result = backend.invoke(
        "backend.SetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[0].Conditions",
                               withBadEntry.dump()}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidConditionsJson");
    CHECK(result.error().message ==
          "#2: unknown function 'NotARealFunction'");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
    const auto conditions = parseStringResult(backend.invoke(
        "backend.GetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[0].Conditions"})));
    REQUIRE(conditions.size() == 3);
    CHECK(conditions[0].at("value") == 1.0f);
    CHECK(conditions[1].at("value") == 2.0f);
    CHECK(conditions[2].at("value") == 3.0f);
}

TEST_CASE("list replace: SetConditionsAtPath with an explicit empty list clears the scope") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeSpellWithThreeEffectConditions());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto message = backend.invoke(
        "backend.SetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[0].Conditions", "[]"}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>() ==
          "Set 0 condition(s) at Effects[0].Conditions on 000800:Patch.esp in Patch.esp. "
          "SavePlugin to persist.");
    const auto conditions = parseStringResult(backend.invoke(
        "backend.GetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Effects[0].Conditions"})));
    CHECK(conditions.empty());
}

TEST_CASE("list replace: SetComponents refuses a partly bad list and keeps the components") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makeMiscWithComponents();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto result = backend.invokeBackend(
        "SetComponents",
        nlohmann::json::array({
            "Patch.esp", "000900:Patch.esp",
            R"([{"component":"000801:Patch.esp","count":1},{"component":"000B00:Patch.esp","count":1},{"component":"000802:Patch.esp","count":-1}])"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ComponentEntriesRefused");
    CHECK(result.error().message ==
          "Refused: 2 of 3 component entries failed: #2: 000B00:Patch.esp: WEAP is not a CMPO "
          "component; #3: 000802:Patch.esp: count must be 1..4294967295.");

    CHECK_FALSE(isPending(backend));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("list replace: SetComponents with an explicit empty list clears the record") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeMiscWithComponents());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);

    const auto cleared = backend.invokeBackend(
        "SetComponents", nlohmann::json::array({"Patch.esp", "000900:Patch.esp", "[]"}));
    REQUIRE(cleared.has_value());
    CHECK(cleared->get<std::string>() ==
          "Set 0 component(s) on 000900:Patch.esp in Patch.esp. SavePlugin to persist.");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* misc = fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(misc != nullptr);
    CHECK(findSub(*misc, "CVPA") == nullptr);
}
