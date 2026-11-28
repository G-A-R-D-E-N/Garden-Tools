#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "climate_record_fixture.hpp"

namespace {

namespace sky = fo4::test::sky;

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fo4ide_climate_" + fo4ide_test::uniqueTempName());
    std::filesystem::path path = root / sky::k_plugin;
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit Fixture(std::vector<sky::Entry> entries = sky::fixture(false)) {
        std::filesystem::create_directories(root);
        fo4::write_plugin_file(sky::read(sky::build(entries, false)), path);
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({{
                {"name", sky::k_plugin}, {"path", path.string()}, {"source", "ClimateTest"}, {"resolved", true}
            }})}
        }).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    nlohmann::json grid(std::string_view signature) {
        const auto result = backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({
            sky::k_plugin, signature, 200, 0}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto edit(std::uint32_t id, std::string_view field, std::string_view value) {
        return backend.setFieldValue(nlohmann::json::array({
            sky::k_plugin, sky::key(id).to_string(), field, value}));
    }

    nlohmann::json describe(std::uint32_t id, std::string_view field) {
        const auto result = static_cast<fo4ide::plugin::IPluginBackend&>(backend).describeElement(
            nlohmann::json::array({sky::k_plugin, sky::key(id).to_string(), field}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto structural(std::uint32_t id, std::string_view field, bool add) {
        const auto arguments = nlohmann::json::array({sky::k_plugin, sky::key(id).to_string(), field});
        auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
        return add ? elements.addElement(arguments) : elements.removeElement(arguments);
    }

    void checkNotPending() {
        const auto result = backend.invokeBackend("GetActivePlugins", nlohmann::json::array());
        REQUIRE(result.has_value());
        CHECK(nlohmann::json::parse(result->get<std::string>())[0].at("Pending") == false);
    }

    void save() {
        REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({sky::k_plugin})).has_value());
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
        [&](const auto& candidate) { return candidate.at("formKey") == sky::key(id).to_string(); });
    if (row == rows.end()) { FAIL("Missing climate/weather record " << id); }
    return {{"value", row->at("cells")[index]}, {"editable", row->at("editable")[index]}};
}

}

TEST_CASE("Climate semantic rows retain weather-table indices and values", "[climate-semantic]") {
    Fixture fixture;
    const auto grid = fixture.grid("CLMT");
    CHECK(cell(grid, sky::k_clmt, "CLMT\\SunriseBegin").at("value") == "30");
    CHECK(cell(grid, sky::k_clmt, "CLMT\\SunTexture").at("value") == "sun.dds");
    CHECK(cell(grid, sky::k_clmt, "CLMT\\WeatherChance\\Count").at("value") == "2");
    CHECK(cell(grid, sky::k_clmt, "CLMT\\WeatherChance[1]").at("value") == "40");
    CHECK(cell(grid, sky::k_clmt, "CLMT\\Weather[1]").at("value") == sky::key(sky::k_wthr_old).to_string());
    CHECK(cell(grid, sky::k_clmt, "CLMT\\WeatherGlobal[0]").at("value") == fo4::FormKey{}.to_string());
    CHECK(cell(grid, sky::k_clmt, "CLMT\\WeatherGlobal[1]").at("value") == sky::key(sky::k_glob).to_string());
    CHECK(cell(grid, sky::k_clmt_bare, "CLMT\\WeatherChance\\Count").at("value") == "0");
    CHECK(cell(grid, sky::k_clmt_bare, "CLMT\\WeatherChance[1]").at("editable") == false);
}

TEST_CASE("Weather semantic rows keep legacy optional slots without compacting", "[climate-semantic]") {
    Fixture fixture;
    const auto grid = fixture.grid("WTHR");
    CHECK(cell(grid, sky::k_wthr, "WTHR\\CloudSpeedY[31]").at("value") == "127");
    CHECK(cell(grid, sky::k_wthr, "WTHR\\CloudAlphaLateSunset[31]").at("editable") == true);
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\CloudAlphaLateSunset[31]").at("editable") == false);
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\AmbientFresnelPower\\Count").at("value") == "4");
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\AmbientFresnelPower[0]").at("editable") == false);
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\AmbientFresnelPower[1]").at("editable") == false);
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\AmbientFresnelPower[2]").at("editable") == true);
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\AmbientSpecularRed[0]").at("editable") == false);
    CHECK(cell(grid, sky::k_wthr_old, "WTHR\\AmbientSpecularRed[1]").at("editable") == true);
    CHECK(cell(grid, sky::k_wthr, "WTHR\\SkyStatic[1]").at("value") == fo4::FormKey{}.to_string());
    CHECK(cell(grid, sky::k_wthr, "WTHR\\Sound[0]").at("value") == sky::key(sky::k_sndr).to_string());
}

TEST_CASE("Climate and weather semantic edits persist only requested payloads", "[climate-semantic]") {
    Fixture fixture;
    const auto before = fo4::read_plugin_file(fixture.path);
    REQUIRE(fixture.edit(sky::k_clmt, "CLMT\\WeatherChance[1]", "-5").has_value());
    REQUIRE(fixture.edit(sky::k_clmt, "CLMT\\Weather[1]", sky::key(sky::k_wthr).to_string()).has_value());
    REQUIRE(fixture.edit(sky::k_clmt, "CLMT\\SunTexture", "sky\\new.dds").has_value());
    REQUIRE(fixture.edit(sky::k_wthr, "WTHR\\CloudSpeedY[31]", "216").has_value());
    REQUIRE(fixture.edit(sky::k_wthr, "WTHR\\CloudTexture0", "sky\\b.dds").has_value());
    REQUIRE(fixture.edit(sky::k_wthr, "WTHR\\CloudColorEarlySunsetGreen[2]", "9").has_value());
    REQUIRE(fixture.edit(sky::k_wthr, "WTHR\\SoundType[0]", "3").has_value());
    REQUIRE(fixture.edit(sky::k_wthr_old, "WTHR\\AmbientFresnelPower[2]", "2").has_value());
    fixture.save();
    const auto after = fo4::read_plugin_file(fixture.path);
    CHECK(sky::payloads(after, sky::key(sky::k_clmt), "WLST").at(0) ==
          sky::words({sky::k_wthr, 60, 0, sky::k_wthr, 0xFFFFFFFBu, sky::k_glob}));
    CHECK(sky::payloads(after, sky::key(sky::k_clmt), "FNAM").at(0) == sky::zstr("sky\\new.dds"));
    auto speed = sky::filled(32, 127);
    speed[31] = std::byte{216};
    CHECK(sky::payloads(after, sky::key(sky::k_wthr), "RNAM").at(0) == speed);
    CHECK(sky::payloads(after, sky::key(sky::k_wthr), "00TX").at(0) == sky::zstr("sky\\b.dds"));
    auto colors = sky::filled(1024, 0);
    colors[2 * 32 + 6 * 4 + 1] = std::byte{9};
    CHECK(sky::payloads(after, sky::key(sky::k_wthr), "PNAM").at(0) == colors);
    CHECK(sky::payloads(after, sky::key(sky::k_wthr), "SNAM").at(0) == sky::words({sky::k_sndr, 3}));
    const auto oldAmbient = sky::payloads(after, sky::key(sky::k_wthr_old), "DALC");
    CHECK(oldAmbient.at(0) == sky::filled(24, 0));
    CHECK(oldAmbient.at(1) == sky::filled(28, 0));
    auto editedAmbient = sky::filled(28, 0);
    const auto power = sky::f32s({2});
    editedAmbient.insert(editedAmbient.end(), power.begin(), power.end());
    CHECK(oldAmbient.at(2) == editedAmbient);
    CHECK(oldAmbient.at(3) == sky::filled(32, 0));
    const auto actual = sky::snapshot(after);
    for (const auto& [key, bytes] : sky::snapshot(before)) {
        if (key != sky::key(sky::k_clmt) && key != sky::key(sky::k_wthr) && key != sky::key(sky::k_wthr_old)) {
            CHECK(actual.at(key) == bytes);
        }
    }
    for (const auto id : {sky::k_clmt, sky::k_wthr, sky::k_wthr_old}) {
        CHECK(sky::order(after, id) == sky::order(before, id));
        for (const auto& sub : fo4::find_record(before, sky::key(id))->subrecords) {
            const auto tag = sub.signature.to_string();
            const bool edited = (id == sky::k_clmt && (tag == "WLST" || tag == "FNAM")) ||
                (id == sky::k_wthr && (tag == "RNAM" || tag == "00TX" || tag == "PNAM" || tag == "SNAM")) ||
                (id == sky::k_wthr_old && tag == "DALC");
            if (!edited) CHECK(sky::payloads(after, sky::key(id), tag.c_str()) == sky::payloads(before, sky::key(id), tag.c_str()));
        }
    }
}

TEST_CASE("Climate and weather refusals and unchanged edits stage nothing", "[climate-semantic]") {
    Fixture fixture;
    SECTION("Unchanged table entry") { CHECK(fixture.edit(sky::k_clmt, "CLMT\\WeatherChance[1]", "40").has_value()); }
    SECTION("Missing table entry") { CHECK_FALSE(fixture.edit(sky::k_clmt_bare, "CLMT\\WeatherChance[0]", "1").has_value()); }
    SECTION("Wrong target type") { CHECK_FALSE(fixture.edit(sky::k_clmt, "CLMT\\Weather[0]", sky::key(sky::k_misc).to_string()).has_value()); }
    SECTION("Legacy absent float") { CHECK_FALSE(fixture.edit(sky::k_wthr_old, "WTHR\\AmbientFresnelPower[1]", "2").has_value()); }
    SECTION("Legacy absent colors") { CHECK_FALSE(fixture.edit(sky::k_wthr_old, "WTHR\\CloudColorDayRed[0]", "1").has_value()); }
    SECTION("Invalid sound type") { CHECK_FALSE(fixture.edit(sky::k_wthr, "WTHR\\SoundType[0]", "4").has_value()); }
    SECTION("Missing array index") { CHECK_FALSE(fixture.edit(sky::k_wthr, "WTHR\\CloudSpeedY[32]", "1").has_value()); }
    SECTION("Read-only count") { CHECK_FALSE(fixture.edit(sky::k_clmt, "CLMT\\WeatherChance\\Count", "1").has_value()); }
    fixture.checkNotPending();
}

TEST_CASE("Climate and weather structural permissions use native defaults", "[climate-semantic]") {
    Fixture fixture;
    CHECK(fixture.describe(sky::k_wthr, "Authoring\\DATA[0]").at("canRemove") == false);
    CHECK(fixture.describe(sky::k_wthr, "Authoring\\DALC[0]").at("canRemove") == false);
    CHECK(fixture.describe(sky::k_wthr_old, "Authoring\\NAM4").at("canAdd") == false);
    CHECK(fixture.describe(sky::k_wthr, "Authoring\\GNAM").at("canCreate") == false);
    CHECK_FALSE(fixture.structural(sky::k_wthr, "Authoring\\DATA[0]", false).has_value());
    CHECK_FALSE(fixture.structural(sky::k_wthr_old, "Authoring\\NAM4", true).has_value());
    fixture.checkNotPending();
    CHECK(fixture.describe(sky::k_clmt_bare, "Authoring\\FNAM").at("canAdd") == true);
    REQUIRE(fixture.structural(sky::k_clmt_bare, "Authoring\\FNAM", true).has_value());
    REQUIRE(fixture.edit(sky::k_clmt_bare, "CLMT\\SunTexture", "new.dds").has_value());
    fixture.save();
    CHECK(sky::payloads(fo4::read_plugin_file(fixture.path), sky::key(sky::k_clmt_bare), "FNAM").at(0) == sky::zstr("new.dds"));
}
