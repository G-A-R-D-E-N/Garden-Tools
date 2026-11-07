#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "vegetation_record_fixture.hpp"

namespace {

namespace veg = fo4::test::vegetation;

std::vector<veg::Entry> localFixture() {
    auto entries = veg::fixture(false);
    for (auto& record : entries) {
        for (auto& [name, data] : record.subs) {
            if (record.id == veg::k_flor && name == "PFIG") data = veg::words({veg::k_misc});
            if (record.id == veg::k_flor && name == "SNAM") data = veg::words({veg::k_sndr});
            if (record.id == veg::k_ltex && name == "GNAM" && data == veg::words({veg::k_master_ingredient})) {
                data = veg::words({veg::k_gras_no_data});
            }
        }
    }
    return entries;
}

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fo4ide_vegetation_" + fo4ide_test::uniqueTempName());
    std::filesystem::path path = root / veg::k_plugin;
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit Fixture(std::vector<veg::Entry> entries = localFixture()) {
        std::filesystem::create_directories(root);
        fo4::write_plugin_file(veg::read(veg::build(entries, false)), path);
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({{
                {"name", veg::k_plugin}, {"path", path.string()}, {"source", "VegetationTest"}, {"resolved", true}
            }})}
        }).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    auto readGrid(std::string_view signature) {
        return backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({
            veg::k_plugin, signature, 200, 0}));
    }

    nlohmann::json grid(std::string_view signature) {
        const auto result = readGrid(signature);
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto edit(std::uint32_t id, std::string_view field, std::string_view value) {
        return backend.setFieldValue(nlohmann::json::array({
            veg::k_plugin, veg::key(id).to_string(), field, value}));
    }

    nlohmann::json describe(std::uint32_t id, std::string_view field) {
        const auto result = static_cast<fo4ide::plugin::IPluginBackend&>(backend).describeElement(
            nlohmann::json::array({veg::k_plugin, veg::key(id).to_string(), field}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }

    auto structural(std::uint32_t id, std::string_view field, bool add) {
        const auto arguments = nlohmann::json::array({veg::k_plugin, veg::key(id).to_string(), field});
        auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
        return add ? elements.addElement(arguments) : elements.removeElement(arguments);
    }

    void checkNotPending() {
        const auto result = backend.invokeBackend("GetActivePlugins", nlohmann::json::array());
        REQUIRE(result.has_value());
        CHECK(nlohmann::json::parse(result->get<std::string>())[0].at("Pending") == false);
    }

    void save() {
        REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({veg::k_plugin})).has_value());
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
        [&](const auto& candidate) { return candidate.at("formKey") == veg::key(id).to_string(); });
    if (row == rows.end()) { FAIL("Missing vegetation record " << id); }
    return {{"value", row->at("cells")[index]}, {"editable", row->at("editable")[index]}};
}

}

TEST_CASE("Vegetation semantic grids expose family values and the registered grass list", "[vegetation-semantic]") {
    Fixture fixture;
    const auto grass = fixture.grid("GRAS");
    CHECK(cell(grass, veg::k_gras, "GRAS\\Density").at("value") == "30");
    CHECK(cell(grass, veg::k_gras_no_data, "GRAS\\Density").at("editable") == false);
    const auto tree = fixture.grid("TREE");
    CHECK(cell(tree, veg::k_tree, "TREE\\TrunkFlexibility").at("value") == "1");
    CHECK(cell(tree, veg::k_tree, "TREE\\Summer").at("value") == "2");
    CHECK(cell(tree, veg::k_tree_bare, "TREE\\Summer").at("editable") == false);
    const auto flora = fixture.grid("FLOR");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Flags").at("value") == "0");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Winter").at("value") == "4");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Ingredient").at("value") == veg::key(veg::k_misc).to_string());
    CHECK(cell(flora, veg::k_flor, "FLOR\\HarvestSound").at("value") == veg::key(veg::k_sndr).to_string());
    const auto land = fixture.grid("LTEX");
    CHECK(cell(land, veg::k_ltex, "LTEX\\Grasses\\Count").at("value") == "2");
    CHECK(cell(land, veg::k_ltex, "LTEX\\Grasses[0]").at("value") == veg::key(veg::k_gras).to_string());
    CHECK(cell(land, veg::k_ltex, "LTEX\\Grasses[1]").at("value") == veg::key(veg::k_gras_no_data).to_string());
    CHECK(cell(land, veg::k_ltex_bare, "LTEX\\Grasses\\Count").at("value") == "0");
    CHECK(cell(land, veg::k_ltex_bare, "LTEX\\Grasses[0]").at("editable") == false);
    CHECK(std::none_of(land.at("columns").begin(), land.at("columns").end(), [](const auto& name) {
        return name.template get<std::string>() == "LTEX\\Grass";
    }));
}

TEST_CASE("Vegetation semantic edits save exact values and preserve unrelated bytes", "[vegetation-semantic]") {
    Fixture fixture;
    const auto before = fo4::read_plugin_file(fixture.path);
    REQUIRE(fixture.edit(veg::k_gras, "GRAS\\Density", "55").has_value());
    REQUIRE(fixture.edit(veg::k_tree, "TREE\\LeafFrequency", "2.5").has_value());
    REQUIRE(fixture.edit(veg::k_tree, "TREE\\Summer", "7").has_value());
    REQUIRE(fixture.edit(veg::k_flor, "FLOR\\Flags", "257").has_value());
    REQUIRE(fixture.edit(veg::k_flor, "FLOR\\Ingredient", "NULL").has_value());
    REQUIRE(fixture.edit(veg::k_ltex, "LTEX\\Grasses[1]", veg::key(veg::k_gras).to_string()).has_value());
    fixture.save();
    const auto after = fo4::read_plugin_file(fixture.path);
    auto grass = veg::gras_data();
    grass[0] = std::byte{55};
    CHECK(veg::payloads(after, veg::key(veg::k_gras), "DATA").at(0) == grass);
    auto wind = veg::tree_cnam();
    fo4::test::ByteWriter value;
    value.f32(2.5f);
    std::copy(value.bytes.begin(), value.bytes.end(), wind.end() - 4);
    CHECK(veg::payloads(after, veg::key(veg::k_tree), "CNAM").at(0) == wind);
    CHECK(veg::payloads(after, veg::key(veg::k_tree), "PFPC").at(0) ==
          veg::Bytes{std::byte{1}, std::byte{7}, std::byte{3}, std::byte{4}});
    CHECK(veg::payloads(after, veg::key(veg::k_flor), "FNAM").at(0) == veg::Bytes{std::byte{1}, std::byte{1}});
    CHECK(veg::payloads(after, veg::key(veg::k_flor), "PFIG").at(0) == veg::words({0}));
    CHECK(veg::payloads(after, veg::key(veg::k_ltex), "GNAM") ==
          std::vector<veg::Bytes>{veg::words({veg::k_gras}), veg::words({veg::k_gras})});
    for (const auto id : {veg::k_gras, veg::k_gras_no_data, veg::k_tree, veg::k_tree_bare,
                         veg::k_flor, veg::k_ltex, veg::k_ltex_bare, veg::k_sndr, veg::k_misc, veg::k_stat}) {
        CHECK(veg::order(after, id) == veg::order(before, id));
        for (const auto& sub : fo4::find_record(before, veg::key(id))->subrecords) {
            const auto tag = sub.signature.to_string();
            const bool edited = (id == veg::k_gras && tag == "DATA") ||
                (id == veg::k_tree && (tag == "CNAM" || tag == "PFPC")) ||
                (id == veg::k_flor && (tag == "FNAM" || tag == "PFIG")) ||
                (id == veg::k_ltex && tag == "GNAM");
            if (!edited) CHECK(veg::payloads(after, veg::key(id), tag.c_str()) == veg::payloads(before, veg::key(id), tag.c_str()));
        }
    }
}

TEST_CASE("Vegetation semantic refusals and unchanged edits stage nothing", "[vegetation-semantic]") {
    Fixture fixture;
    SECTION("Unchanged scalar") { CHECK(fixture.edit(veg::k_gras, "GRAS\\Density", "30").has_value()); }
    SECTION("Unchanged indexed link") { CHECK(fixture.edit(veg::k_ltex, "LTEX\\Grasses[1]", veg::key(veg::k_gras_no_data).to_string()).has_value()); }
    SECTION("Missing data") { CHECK_FALSE(fixture.edit(veg::k_gras_no_data, "GRAS\\Density", "1").has_value()); }
    SECTION("Missing list index") { CHECK_FALSE(fixture.edit(veg::k_ltex, "LTEX\\Grasses[2]", veg::key(veg::k_gras).to_string()).has_value()); }
    SECTION("Null grass") { CHECK_FALSE(fixture.edit(veg::k_ltex, "LTEX\\Grasses[0]", "NULL").has_value()); }
    SECTION("Wrong grass target") { CHECK_FALSE(fixture.edit(veg::k_ltex, "LTEX\\Grasses[0]", veg::key(veg::k_stat).to_string()).has_value()); }
    SECTION("Nonnullable flora sound") { CHECK_FALSE(fixture.edit(veg::k_flor, "FLOR\\HarvestSound", "NULL").has_value()); }
    SECTION("Out of range season") { CHECK_FALSE(fixture.edit(veg::k_tree, "TREE\\Summer", "256").has_value()); }
    SECTION("Read-only count") { CHECK_FALSE(fixture.edit(veg::k_ltex, "LTEX\\Grasses\\Count", "1").has_value()); }
    fixture.checkNotPending();
}

TEST_CASE("Vegetation semantic reads refuse malformed grass link payloads", "[vegetation-semantic]") {
    auto entries = localFixture();
    for (auto& record : entries) {
        if (record.id != veg::k_ltex) continue;
        for (auto& [name, data] : record.subs) {
            if (name == "GNAM") data.push_back(std::byte{0});
        }
    }
    Fixture fixture{entries};
    const auto result = fixture.readGrid("LTEX");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkReadFailed");
    fixture.checkNotPending();
}

TEST_CASE("Vegetation semantic grids list a flora with a wrong-size ingredient as unavailable", "[vegetation-semantic]") {
    auto entries = localFixture();
    for (auto& record : entries) {
        for (auto& [name, data] : record.subs) {
            if (record.id == veg::k_flor && name == "PFIG") data = veg::words({veg::k_misc, veg::k_misc});
        }
    }
    Fixture fixture{entries};
    const auto flora = fixture.grid("FLOR");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Ingredient").at("value") == "<opaque>");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Ingredient").at("editable") == false);
    CHECK(cell(flora, veg::k_flor, "FLOR\\Winter").at("value") == "4");
    CHECK(cell(flora, veg::k_flor, "FLOR\\HarvestSound").at("value") == veg::key(veg::k_sndr).to_string());
    CHECK_FALSE(fixture.edit(veg::k_flor, "FLOR\\Ingredient", "NULL").has_value());
    fixture.checkNotPending();
}

TEST_CASE("Vegetation semantic grids list wrong-size flora and tree payloads as unavailable", "[vegetation-semantic]") {
    auto entries = localFixture();
    for (auto& record : entries) {
        for (auto& [name, data] : record.subs) {
            if (record.id == veg::k_flor && name == "PFPC") data = veg::Bytes{std::byte{1}, std::byte{2}, std::byte{3}};
            if (record.id == veg::k_flor && name == "FNAM") data = veg::filled(1, 0);
            if (record.id == veg::k_tree && name == "PFPC") data = veg::Bytes{std::byte{1}, std::byte{2}, std::byte{3}};
            if (record.id == veg::k_tree && name == "CNAM") data = veg::filled(47, 0);
        }
    }
    Fixture fixture{entries};
    const auto flora = fixture.grid("FLOR");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Winter").at("value") == "<opaque>");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Winter").at("editable") == false);
    CHECK(cell(flora, veg::k_flor, "FLOR\\Flags").at("value") == "<opaque>");
    CHECK(cell(flora, veg::k_flor, "FLOR\\Ingredient").at("value") == veg::key(veg::k_misc).to_string());
    CHECK(cell(flora, veg::k_flor, "FLOR\\HarvestSound").at("value") == veg::key(veg::k_sndr).to_string());
    const auto tree = fixture.grid("TREE");
    CHECK(cell(tree, veg::k_tree, "TREE\\Summer").at("value") == "<opaque>");
    CHECK(cell(tree, veg::k_tree, "TREE\\TrunkFlexibility").at("value") == "<opaque>");
    CHECK(cell(tree, veg::k_tree, "TREE\\TrunkFlexibility").at("editable") == false);
    CHECK(cell(tree, veg::k_tree_bare, "TREE\\Summer").at("editable") == false);
    CHECK_FALSE(fixture.edit(veg::k_flor, "FLOR\\Winter", "1").has_value());
    CHECK_FALSE(fixture.edit(veg::k_flor, "FLOR\\Flags", "1").has_value());
    CHECK_FALSE(fixture.edit(veg::k_tree, "TREE\\Summer", "1").has_value());
    fixture.checkNotPending();
}

TEST_CASE("Vegetation structural permissions use native coupling and list removal", "[vegetation-semantic]") {
    Fixture fixture;
    CHECK(fixture.describe(veg::k_flor, "Authoring\\PFPC[0]").at("canRemove") == false);
    CHECK_FALSE(fixture.structural(veg::k_flor, "Authoring\\PFPC[0]", false).has_value());
    fixture.checkNotPending();
    CHECK(fixture.describe(veg::k_tree_bare, "Authoring\\PFIG").at("canAdd") == true);
    REQUIRE(fixture.structural(veg::k_tree_bare, "Authoring\\PFIG", true).has_value());
    REQUIRE(fixture.edit(veg::k_tree_bare, "TREE\\Ingredient", veg::key(veg::k_stat).to_string()).has_value());
    CHECK(fixture.describe(veg::k_ltex, "LTEX\\Grasses[1]").at("canRemove") == true);
    REQUIRE(fixture.structural(veg::k_ltex, "LTEX\\Grasses[1]", false).has_value());
    fixture.save();
    const auto after = fo4::read_plugin_file(fixture.path);
    CHECK(veg::payloads(after, veg::key(veg::k_tree_bare), "PFIG").at(0) == veg::words({veg::k_stat}));
    CHECK(veg::payloads(after, veg::key(veg::k_ltex), "GNAM") == std::vector<veg::Bytes>{veg::words({veg::k_gras})});
}
