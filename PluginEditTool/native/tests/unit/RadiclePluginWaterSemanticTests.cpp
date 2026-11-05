#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "water_record_fixture.hpp"

namespace {

namespace water = fo4::test::water;

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fo4ide_water_" + fo4ide_test::uniqueTempName());
    std::filesystem::path path = root / water::k_plugin;
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit Fixture(std::vector<water::Entry> entries = water::fixture(), std::uint32_t flags = 0) {
        std::filesystem::create_directories(root);
        fo4::write_plugin_file(water::read(water::build(entries, flags)), path);
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({{
                {"name", water::k_plugin}, {"path", path.string()}, {"source", "WaterTest"}, {"resolved", true}
            }})}
        }).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    nlohmann::json grid() {
        const auto result = backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({
            water::k_plugin, "WATR", 200, 0}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto edit(std::uint32_t id, std::string_view field, std::string_view value) {
        return backend.setFieldValue(nlohmann::json::array({
            water::k_plugin, water::key(id).to_string(), field, value}));
    }

    nlohmann::json describe(std::uint32_t id, std::string_view field) {
        const auto result = static_cast<fo4ide::plugin::IPluginBackend&>(backend).describeElement(
            nlohmann::json::array({water::k_plugin, water::key(id).to_string(), field}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto structural(std::uint32_t id, std::string_view field, bool add) {
        const auto arguments = nlohmann::json::array({water::k_plugin, water::key(id).to_string(), field});
        auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
        return add ? elements.addElement(arguments) : elements.removeElement(arguments);
    }

    void checkNotPending() {
        const auto result = backend.invokeBackend("GetActivePlugins", nlohmann::json::array());
        REQUIRE(result.has_value());
        CHECK(nlohmann::json::parse(result->get<std::string>())[0].at("Pending") == false);
    }

    void save() {
        REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({water::k_plugin})).has_value());
    }
};

nlohmann::json cell(const nlohmann::json& grid, std::uint32_t id, std::string_view field) {
    const auto& columns = grid.at("columns");
    const auto column = std::find_if(columns.begin(), columns.end(),
        [&](const auto& name) { return name.template get<std::string>() == field; });
    REQUIRE(column != columns.end());
    const auto index = static_cast<std::size_t>(column - columns.begin());
    const auto& rows = grid.at("rows");
    const auto row = std::find_if(rows.begin(), rows.end(),
        [&](const auto& candidate) { return candidate.at("formKey") == water::key(id).to_string(); });
    if (row == rows.end()) { FAIL("Missing water record " << id); }
    return {{"value", row->at("cells")[index]}, {"editable", row->at("editable")[index]}};
}

}

TEST_CASE("Water semantic optional fields require the complete native tail", "[water-semantic]") {
    for (const std::size_t size : {188u, 192u, 199u, 200u, 201u}) {
        auto entries = water::fixture();
        for (auto& [name, data] : entries[2].subs) {
            if (name == "DNAM") data = water::visual(size);
        }
        Fixture fixture{entries};
        const auto grid = fixture.grid();
        CAPTURE(size);
        CHECK(cell(grid, water::k_bare, "WATR\\DepthAmount").at("value") == "0.5");
        CHECK(cell(grid, water::k_bare, "WATR\\Layer3NoiseFalloff").at("editable") == true);
        for (const auto* field : {"WATR\\SiltAmount", "WATR\\LightColorRed", "WATR\\DarkColorBlue"}) {
            CHECK(cell(grid, water::k_bare, field).at("editable") == (size >= 200));
        }
        CHECK(cell(grid, water::k_bare, "WATR\\ScreenSpaceReflections").at("editable") == (size == 201));
        CHECK(cell(grid, water::k_full, "WATR\\Name").at("value") == "Lake water");
        CHECK(cell(grid, water::k_full, "WATR\\OpenSound").at("value") == water::key(water::k_sndr).to_string());
    }
}

TEST_CASE("Water semantic mutations save requested values and preserve other bytes", "[water-semantic]") {
    Fixture fixture;
    const auto before = fo4::read_plugin_file(fixture.path);
    REQUIRE(fixture.edit(water::k_full, "WATR\\DepthAmount", "4096").has_value());
    REQUIRE(fixture.edit(water::k_full, "WATR\\ShallowColorGreen", "200").has_value());
    REQUIRE(fixture.edit(water::k_full, "WATR\\ScreenSpaceReflections", "0").has_value());
    REQUIRE(fixture.edit(water::k_full, "WATR\\LinearVelocityZ", "-3.5").has_value());
    REQUIRE(fixture.edit(water::k_full, "WATR\\Layer1NoiseTexture", "water\\new.dds").has_value());
    REQUIRE(fixture.edit(water::k_full, "WATR\\Name", "Pond").has_value());
    REQUIRE(fixture.edit(water::k_full, "WATR\\OpenSound", "NULL").has_value());
    fixture.save();
    const auto after = fo4::read_plugin_file(fixture.path);
    auto visual = water::visual(201);
    water::put_f32(visual, 0, 4096);
    visual[5] = std::byte{200};
    visual[200] = std::byte{0};
    CHECK(water::payload(after, water::k_full, "DNAM") == visual);
    CHECK(water::payload(after, water::k_full, "NAM0") == water::f32s({1, 2, -3.5f}));
    CHECK(water::payload(after, water::k_full, "NAM2") == water::zstr("water\\new.dds"));
    CHECK(water::payload(after, water::k_full, "FULL") == water::zstr("Pond"));
    CHECK(water::payload(after, water::k_full, "SNAM") == water::words({0}));
    CHECK(water::order(after, water::k_full) == water::order(before, water::k_full));
    const auto actual = water::snapshot(after);
    for (const auto& [key, bytes] : water::snapshot(before)) {
        if (key != water::key(water::k_full)) CHECK(actual.at(key) == bytes);
    }
    for (const auto& sub : fo4::find_record(before, water::key(water::k_full))->subrecords) {
        const auto tag = sub.signature.to_string();
        if (tag != "DNAM" && tag != "NAM0" && tag != "NAM2" && tag != "FULL" && tag != "SNAM") {
            CHECK(water::payload(after, water::k_full, tag.c_str()) == water::payload(before, water::k_full, tag.c_str()));
        }
    }
}

TEST_CASE("Water semantic refusals and unchanged edits stage nothing", "[water-semantic]") {
    Fixture fixture;
    SECTION("Unchanged float") { CHECK(fixture.edit(water::k_full, "WATR\\DepthAmount", "0.5").has_value()); }
    SECTION("Incomplete optional tail") { CHECK_FALSE(fixture.edit(water::k_short, "WATR\\SiltAmount", "1").has_value()); }
    SECTION("Invalid color") { CHECK_FALSE(fixture.edit(water::k_full, "WATR\\ShallowColorRed", "256").has_value()); }
    SECTION("Invalid boolean") { CHECK_FALSE(fixture.edit(water::k_full, "WATR\\ScreenSpaceReflections", "2").has_value()); }
    SECTION("Missing texture") { CHECK_FALSE(fixture.edit(water::k_bare, "WATR\\Layer1NoiseTexture", "new.dds").has_value()); }
    SECTION("Wrong target type") { CHECK_FALSE(fixture.edit(water::k_full, "WATR\\OpenSound", water::key(water::k_misc).to_string()).has_value()); }
    fixture.checkNotPending();
}

TEST_CASE("Water text outside Windows-1252 is refused without staging", "[water-semantic]") {
    Fixture fixture;
    const auto before = fo4::read_plugin_file(fixture.path);
    const auto result = fixture.edit(water::k_full, "WATR\\Layer1NoiseTexture", "\xe4\xb8\xad.dds");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldWriteFailed");
    fixture.checkNotPending();
    REQUIRE(fixture.edit(water::k_full, "WATR\\DepthAmount", "4096").has_value());
    fixture.save();
    const auto after = fo4::read_plugin_file(fixture.path);
    CHECK(water::payload(after, water::k_full, "NAM2") == water::payload(before, water::k_full, "NAM2"));
    CHECK(water::payload(after, water::k_full, "NAM2") == water::zstr("water\\n1.dds"));
}

TEST_CASE("Water localized names stay opaque and refuse writes", "[water-semantic]") {
    auto entries = water::fixture();
    for (auto& [name, data] : entries[0].subs) {
        if (name == "FULL") data = water::words({123});
    }
    Fixture fixture{entries, water::k_tes4_localized};
    CHECK(cell(fixture.grid(), water::k_full, "WATR\\Name").at("editable") == false);
    CHECK_FALSE(fixture.edit(water::k_full, "WATR\\Name", "Pond").has_value());
    fixture.checkNotPending();
}

TEST_CASE("Water structural permissions use native required fields and defaults", "[water-semantic]") {
    Fixture fixture;
    CHECK(fixture.describe(water::k_bare, "Authoring\\DNAM[0]").at("canRemove") == false);
    CHECK(fixture.describe(water::k_bare, "Authoring\\FNAM[0]").at("canRemove") == false);
    CHECK_FALSE(fixture.structural(water::k_bare, "Authoring\\DNAM[0]", false).has_value());
    fixture.checkNotPending();
    CHECK(fixture.describe(water::k_bare, "Authoring\\NAM2").at("canAdd") == true);
    REQUIRE(fixture.structural(water::k_bare, "Authoring\\NAM2", true).has_value());
    REQUIRE(fixture.edit(water::k_bare, "WATR\\Layer1NoiseTexture", "new.dds").has_value());
    fixture.save();
    CHECK(water::payload(fo4::read_plugin_file(fixture.path), water::k_bare, "NAM2") == water::zstr("new.dds"));
}
