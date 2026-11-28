#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"
#include <catch2/generators/catch_generators.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/ctda.hpp>
#include "plugin_bytes.hpp"
#include <chrono>
#include <fstream>

namespace {

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("condition_preservation_" + fo4ide_test::uniqueTempName());
    std::filesystem::path path = root / "Patch.esp";
    std::vector<fo4::CtdaCondition> conditions;
    fo4ide::plugin::RadiclePluginBackend backend;
    bool scoped;

    explicit Fixture(bool scopedValue, int malformed = 0) : scoped(scopedValue) {
        fo4::CtdaCondition first;
        first.function = 8;
        first.comparison_raw = 0x3f800000;
        first.padding = {std::byte{0xac}, std::byte{0x93}, std::byte{0x13}};
        first.unknown2 = 0x1234;
        first.unknown3 = 123;
        first.parameter1_string = "first";
        first.parameter2_string = "second";
        auto second = first;
        second.padding[0] = std::byte{0x55};
        second.parameter1_string = "other";
        conditions = {first, second};
        fo4::test::ByteWriter out;
        const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
        fo4::test::ByteWriter hedr;
        hedr.f32(1.0f); hedr.u32(1); hedr.u32(0x800);
        fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
        fo4::test::end_record(out, tes4);
        const auto signature = scoped ? "SPEL" : "WEAP";
        const auto group = fo4::test::begin_group(out, signature, 0);
        const auto record = fo4::test::begin_record(out, signature, 0, 0x800);
        if (scoped) {
            fo4::test::ByteWriter efid, efit;
            efid.u32(0x801);
            efit.f32(1.0f); efit.u32(0); efit.u32(0);
            fo4::test::write_subrecord(out, "EFID", efid.bytes);
            fo4::test::write_subrecord(out, "EFIT", efit.bytes);
        }
        for (const auto& member : fo4::serialize_ctda_run(conditions)) {
            auto data = member.data;
            if (member.signature == fo4::sig_ctda) {
                if (malformed == 1) data.push_back(std::byte{0x55});
                if (malformed == 2) data.resize(4);
            }
            fo4::test::write_subrecord(out, member.signature.to_string(), data);
        }
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
        std::filesystem::create_directories(root);
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            REQUIRE(stream.good());
            stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
            REQUIRE(stream.good());
        }
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
            {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "Override"}, {"resolved", true}}})}}).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    nlohmann::json arguments() const {
        auto args = nlohmann::json::array({"Patch.esp", "000800:Patch.esp"});
        if (scoped) args.push_back("Effects[0].Conditions");
        return args;
    }

    nlohmann::json read() {
        const auto result = backend.invoke(scoped ? "backend.GetConditionsAtPath" : "backend.GetConditions", arguments());
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    fo4ide::Result<nlohmann::json> set(const nlohmann::json& rows) {
        auto args = arguments();
        args.push_back(rows.dump());
        return backend.invoke(scoped ? "backend.SetConditionsAtPath" : "backend.SetConditions", args);
    }

    void checkRaw(const std::vector<fo4::CtdaCondition>& expected) {
        const auto plugin = fo4::read_plugin_file(path);
        const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Patch.esp"));
        REQUIRE(record != nullptr);
        std::vector<fo4::Subrecord> actual;
        for (const auto& member : record->subrecords) {
            if (member.signature == fo4::sig_ctda || member.signature == fo4::Signature::from_string("CIS1") ||
                member.signature == fo4::Signature::from_string("CIS2")) actual.push_back(member);
        }
        const auto wanted = fo4::serialize_ctda_run(expected);
        REQUIRE(actual.size() == wanted.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i].signature == wanted[i].signature);
            CHECK(actual[i].data == wanted[i].data);
        }
    }
};

}

TEST_CASE("condition preservation refuses non-round-tripping payloads on both routes", "[condition-preservation]") {
    Fixture fixture(GENERATE(false, true), GENERATE(1, 2));
    const auto read = fixture.backend.invoke(
        fixture.scoped ? "backend.GetConditionsAtPath" : "backend.GetConditions", fixture.arguments());
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().type == "ConditionPayloadUnsupported");
    const auto result = fixture.set(nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ConditionPayloadUnsupported");
    const auto active = fixture.backend.invoke("backend.GetActivePlugins", nlohmann::json::array());
    REQUIRE(active.has_value());
    CHECK_FALSE(nlohmann::json::parse(active->get<std::string>()).at(0).at("Pending").get<bool>());
}

TEST_CASE("condition preservation survives whole and scoped save edit and reorder", "[condition-preservation]") {
    Fixture fixture(GENERATE(false, true));
    auto rows = fixture.read();
    const auto operation = GENERATE(0, 1, 2);
    if (operation == 1) {
        rows[0]["value"] = 2.0;
        fixture.conditions[0].comparison_raw = 0x40000000;
    }
    if (operation == 2) {
        std::swap(rows[0], rows[1]);
        std::swap(fixture.conditions[0], fixture.conditions[1]);
    }
    REQUIRE(fixture.set(rows).has_value());
    REQUIRE(fixture.backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fixture.checkRaw(fixture.conditions);
}

TEST_CASE("condition preservation refuses an invalid row atomically on both writer routes", "[condition-preservation]") {
    Fixture fixture(GENERATE(false, true));
    auto rows = fixture.read();
    rows[0]["value"] = 2.0;
    const auto invalid = GENERATE(0, 1, 2, 3);
    if (invalid == 0) rows[1]["_native"] = "bad";
    if (invalid == 1) rows.push_back({{"function", "NotAFunction"}});
    if (invalid == 2) rows[1]["_native"]["ctda"][1] = 111;
    if (invalid == 3) rows[1] = rows[0];
    const auto result = fixture.set(rows);
    CHECK_FALSE(result.has_value());
    const auto active = fixture.backend.invoke("backend.GetActivePlugins", nlohmann::json::array());
    REQUIRE(active.has_value());
    const auto plugins = nlohmann::json::parse(active->get<std::string>());
    CHECK_FALSE(plugins.at(0).at("Pending").get<bool>());
    fixture.checkRaw(fixture.conditions);
}

TEST_CASE("condition backend: SetConditions refuses invalid entries and hard failures") {
    Fixture fixture(false);
    const auto before = fixture.read();
    const auto invalid = fixture.set(nlohmann::json::array({
        {{"function", "NotARealFunction"}, {"value", 1}},
        {{"function", "GetScale"}, {"value", 2}}}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidConditionsJson");
    CHECK(invalid.error().message.find("#1: unknown function 'NotARealFunction'") != std::string::npos);
    auto args = fixture.arguments();
    args.push_back("{not json");
    const auto bad = fixture.backend.invoke("backend.SetConditions", args);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().type == "InvalidConditionsJson");
    const auto missing = fixture.backend.invoke("backend.SetConditions",
        nlohmann::json::array({"Patch.esp", "000999:Patch.esp", "[]"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "RecordNotFound");
    CHECK(fixture.read() == before);
    fixture.checkRaw(fixture.conditions);
}
