#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("fo4ide_w0_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = root / "W0.esp";
    fo4ide::plugin::RadiclePluginBackend backend;

    explicit Fixture(bool malformed = false, std::string_view signature = "RACE",
                     bool withMovementNames = false) {
        fo4::test::ByteWriter out;
        const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
        fo4::test::ByteWriter hedr;
        hedr.f32(1.0f);
        hedr.u32(1);
        hedr.u32(0x801);
        fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
        fo4::test::end_record(out, tes4);
        const auto group = fo4::test::begin_group(out, signature, 0);
        const auto record = fo4::test::begin_record(out, signature, 0, 0x800);
        if (signature == "RACE") {
            fo4::test::write_subrecord(out, "NAME", std::string_view{"first\0", 6});
            fo4::test::write_subrecord(out, "NAME", malformed
                ? std::string_view{"bad", 3} : std::string_view{"second\0", 7});
            if (malformed) {
                fo4::test::write_subrecord(out, "NAME", std::string_view{"third\0", 6});
            }
            if (withMovementNames) {
                fo4::test::write_subrecord(out, "MTNM", std::string_view{"WALK"});
                fo4::test::write_subrecord(out, "MTNM", std::string_view{"RUN1"});
                fo4::test::write_subrecord(out, "MTNM", std::string_view{"SWIM"});
            }
        }
        if (signature == "PROJ") {
            fo4::test::ByteWriter shortData;
            shortData.u32(0);
            fo4::test::write_subrecord(out, "DNAM", shortData.bytes);
        }
        fo4::test::write_subrecord(out, "ZZZZ", std::string_view{"opaque", 6});
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
        std::filesystem::create_directories(root);
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(out.bytes.data()),
                     static_cast<std::streamsize>(out.bytes.size()));
        stream.close();
        REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{
            {"plugins", nlohmann::json::array({{
                {"name", "W0.esp"}, {"path", path.string()},
                {"source", "W0Test"}, {"resolved", true}
            }})}
        }).has_value());
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    nlohmann::json grid() {
        const auto result = backend.invokeBackend("GetRecordsGrid",
            nlohmann::json::array({"W0.esp", "RACE", 200, 0}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    }
};

std::size_t column(const nlohmann::json& grid, std::string_view path) {
    const auto& columns = grid.at("columns");
    const auto found = std::find_if(columns.begin(), columns.end(),
        [&](const auto& name) { return name.template get<std::string>() == path; });
    if (found == columns.end()) { FAIL("Missing semantic field: " << path); }
    return static_cast<std::size_t>(found - columns.begin());
}

}

TEST_CASE("W0 repeated strings have stable grid addresses", "[w0]") {
    Fixture fixture;
    const auto grid = fixture.grid();
    const auto first = column(grid, "RACE\\BipedObjectNames[0]");
    const auto second = column(grid, "RACE\\BipedObjectNames[1]");
    CHECK(grid.at("rows")[0].at("cells")[first] == "first");
    CHECK(grid.at("rows")[0].at("cells")[second] == "second");
    CHECK(grid.at("rows")[0].at("editable")[second] == true);
}

TEST_CASE("W0 indexed string edits stage and preserve unrelated payloads", "[w0]") {
    Fixture fixture;
    const auto before = fo4::read_plugin_file(fixture.path);
    const auto changed = fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "RACE\\BipedObjectNames[1]", "replacement"
    }));
    REQUIRE(changed.has_value());
    const auto grid = fixture.grid();
    CHECK(grid.at("rows")[0].at("cells")[column(grid, "RACE\\BipedObjectNames[1]")] == "replacement");
    REQUIRE(fixture.backend.invokeBackend("SavePlugin",
        nlohmann::json::array({"W0.esp"})).has_value());
    const auto after = fo4::read_plugin_file(fixture.path);
    const auto key = fo4::FormKey::parse("000800:W0.esp");
    const auto* original = fo4::find_record(before, key);
    const auto* rewritten = fo4::find_record(after, key);
    REQUIRE(original != nullptr);
    REQUIRE(rewritten != nullptr);
    REQUIRE(rewritten->subrecords.size() == original->subrecords.size());
    CHECK(rewritten->subrecords[0].data == original->subrecords[0].data);
    CHECK(rewritten->subrecords[2].data == original->subrecords[2].data);
    CHECK(rewritten->subrecords[1].data != original->subrecords[1].data);
}

TEST_CASE("W0 unavailable string indices stage nothing", "[w0]") {
    Fixture fixture;
    const auto result = fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "RACE\\BipedObjectNames[2]", "refused"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ScalarFieldMissing");
    const auto plugins = fixture.backend.invokeBackend("GetActivePlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    CHECK(nlohmann::json::parse(plugins->get<std::string>())[0].at("Pending") == false);
}

TEST_CASE("W0 semantic matrix keeps raw unknown subrecords available", "[w0]") {
    Fixture fixture;
    const auto result = fixture.backend.invokeBackend("GetConflictMatrix",
        nlohmann::json::array({"000800:W0.esp"}));
    REQUIRE(result.has_value());
    const auto matrix = nlohmann::json::parse(result->get<std::string>());
    bool semantic = false;
    bool raw = false;
    for (const auto& row : matrix.at("Rows")) {
        if (row.at("Field") == "RACE\\BipedObjectNames[1]") {
            semantic = true;
            CHECK(row.at("Values")[0] == "second");
            CHECK(row.at("Editable")[0] == true);
        }
        if (row.at("Field") == "Subrecords\\ZZZZ[0]") {
            raw = true;
            CHECK(row.at("Group") == "Raw");
            CHECK(row.at("Editable")[0] == false);
        }
    }
    CHECK(semantic);
    CHECK(raw);
}

TEST_CASE("W0 opaque occurrence does not compact later indices", "[w0]") {
    Fixture fixture{true};
    const auto grid = fixture.grid();
    const auto bad = column(grid, "RACE\\BipedObjectNames[1]");
    const auto third = column(grid, "RACE\\BipedObjectNames[2]");
    CHECK(grid.at("rows")[0].at("cells")[bad] == "<opaque>");
    CHECK(grid.at("rows")[0].at("editable")[bad] == false);
    CHECK(grid.at("rows")[0].at("cells")[third] == "third");
}

TEST_CASE("W0 strings use UTF-8 transport and reject unrepresentable text", "[w0]") {
    Fixture fixture;
    const std::string euro = "\xe2\x82\xac";
    REQUIRE(fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "RACE\\BipedObjectNames[1]", euro
    })).has_value());
    const auto grid = fixture.grid();
    CHECK(grid.at("rows")[0].at("cells")[column(grid, "RACE\\BipedObjectNames[1]")] == euro);
    const auto refused = fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "RACE\\BipedObjectNames[1]", "\xf0\x9f\x98\x80"
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ScalarFieldWriteFailed");
    REQUIRE(fixture.backend.invokeBackend("SavePlugin", nlohmann::json::array({"W0.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(fixture.path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:W0.esp"));
    REQUIRE(record != nullptr);
    CHECK(record->subrecords[1].data == std::vector<std::byte>{std::byte{0x80}, std::byte{0}});
}

TEST_CASE("W0 EDID is stored as Windows-1252 and refuses unrepresentable text", "[w0]") {
    Fixture fixture;
    REQUIRE(fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "EDID", "Caf\xc3\xa9"
    })).has_value());
    CHECK(fixture.grid().at("rows")[0].at("cells")[0] == "Caf\xc3\xa9");
    const auto refused = fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "EDID", "\xe6\x97\xa5"
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "FieldWriteFailed");
    REQUIRE(fixture.backend.invokeBackend("SavePlugin", nlohmann::json::array({"W0.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(fixture.path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:W0.esp"));
    REQUIRE(record != nullptr);
    CHECK(record->subrecords[0].data == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}});
}

TEST_CASE("W0 typed EYES creation round-trips a Windows-1252 EDID and refuses unrepresentable text", "[w0]") {
    Fixture fixture;
    const auto payload = [](std::string_view editor) {
        return nlohmann::json{{"editorId", std::string(editor)}, {"fullText", "Eyes"}, {"icon", "eyes.dds"}};
    };
    const auto created = fixture.backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "W0.esp", "EYES", payload("Caf\xc3\xa9")
    }));
    REQUIRE(created.has_value());
    const auto formKey = nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    const auto grid = nlohmann::json::parse(fixture.backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"W0.esp", "EYES", 20, 0}))->get<std::string>());
    CHECK(grid.at("rows")[0].at("cells")[0] == "Caf\xc3\xa9");
    const auto refused = fixture.backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "W0.esp", "EYES", payload("\xe6\x97\xa5")
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "EyesCreateFailed");
    const auto afterRefusal = nlohmann::json::parse(fixture.backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"W0.esp", "EYES", 20, 0}))->get<std::string>());
    CHECK(afterRefusal.at("rows").size() == 1);
    REQUIRE(fixture.backend.invokeBackend("SavePlugin", nlohmann::json::array({"W0.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(fixture.path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto edid = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& sub) { return sub.signature.to_string() == "EDID"; });
    REQUIRE(edid != record->subrecords.end());
    CHECK(edid->data == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}});
}

TEST_CASE("W0 malformed FormLink storage stays distinguishable from absence", "[w0]") {
    Fixture fixture{false, "PROJ"};
    const auto grid = fixture.backend.invokeBackend("GetRecordsGrid",
        nlohmann::json::array({"W0.esp", "PROJ", 200, 0}));
    REQUIRE_FALSE(grid.has_value());
    CHECK(grid.error().type == "FormLinkReadFailed");
}

TEST_CASE("W0 absent fields do not manufacture semantic conflicts", "[w0]") {
    Fixture fixture{false, "REFR"};
    const auto result = fixture.backend.invokeBackend("GetConflictMatrix",
        nlohmann::json::array({"000800:W0.esp"}));
    REQUIRE(result.has_value());
    const auto matrix = nlohmann::json::parse(result->get<std::string>());
    bool found = false;
    for (const auto& row : matrix.at("Rows")) {
        if (row.at("Field") == "REFR\\HealthPercent") {
            found = true;
            CHECK(row.at("Differs") == false);
            CHECK(row.at("Statuses")[0] == "notdefined");
            CHECK(row.at("Editable")[0] == false);
        }
    }
    CHECK(found);
}

TEST_CASE("W0 override comparison aligns a missing repeated occurrence", "[w0]") {
    Fixture fixture;
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"W0.esp\0", 7});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "RACE", 0);
    const auto record = fo4::test::begin_record(out, "RACE", 0, 0x800);
    fo4::test::write_subrecord(out, "NAME", std::string_view{"changed\0", 8});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    const auto overridePath = fixture.root / "Override.esp";
    std::ofstream stream(overridePath, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
    stream.close();
    REQUIRE(fixture.backend.invoke("environment.LoadMo2", nlohmann::json{
        {"plugins", nlohmann::json::array({
            {{"name", "W0.esp"}, {"path", fixture.path.string()}, {"source", "W0Test"}, {"resolved", true}},
            {{"name", "Override.esp"}, {"path", overridePath.string()}, {"source", "W0Test"}, {"resolved", true}}
        })}
    }).has_value());
    const auto result = fixture.backend.invokeBackend("GetConflictMatrix",
        nlohmann::json::array({"000800:W0.esp"}));
    REQUIRE(result.has_value());
    const auto matrix = nlohmann::json::parse(result->get<std::string>());
    bool found = false;
    for (const auto& row : matrix.at("Rows")) {
        if (row.at("Field") == "RACE\\BipedObjectNames[1]") {
            found = true;
            CHECK(row.at("Values") == nlohmann::json::array({"second", ""}));
            CHECK(row.at("Editable") == nlohmann::json::array({true, false}));
            CHECK(row.at("Statuses") == nlohmann::json::array({"only", "notdefined"}));
        }
    }
    CHECK(found);
}

TEST_CASE("W0 fixed-width movement names write through the string setter", "[w0]") {
    Fixture fixture{false, "RACE", true};
    const auto before = fixture.grid();
    const auto second = column(before, "RACE\\MovementTypeNames[1]");
    CHECK(before.at("rows")[0].at("cells")[second] == "RUN1");
    CHECK(before.at("rows")[0].at("editable")[second] == true);

    REQUIRE(fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "RACE\\MovementTypeNames[1]", "FLY1"
    })).has_value());

    for (const std::string width : {"FLY", "FLY11"}) {
        const auto refused = fixture.backend.setFieldValue(nlohmann::json::array({
            "W0.esp", "000800:W0.esp", "RACE\\MovementTypeNames[1]", width
        }));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "ScalarFieldWriteFailed");
    }

    const auto after = fixture.grid();
    CHECK(after.at("rows")[0].at("cells")[column(after, "RACE\\MovementTypeNames[0]")] == "WALK");
    CHECK(after.at("rows")[0].at("cells")[column(after, "RACE\\MovementTypeNames[1]")] == "FLY1");
    CHECK(after.at("rows")[0].at("cells")[column(after, "RACE\\MovementTypeNames[2]")] == "SWIM");

    REQUIRE(fixture.backend.invokeBackend("SavePlugin",
        nlohmann::json::array({"W0.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(fixture.path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:W0.esp"));
    REQUIRE(record != nullptr);
    std::vector<std::string> stored;
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature.to_string() == "MTNM") {
            stored.emplace_back(reinterpret_cast<const char*>(subrecord.data.data()),
                                subrecord.data.size());
        }
    }
    CHECK(stored == std::vector<std::string>{"WALK", "FLY1", "SWIM"});
}

TEST_CASE("W0 fixed-width movement names round-trip Windows-1252 text", "[w0]") {
    Fixture fixture{false, "RACE", true};
    const std::string cafe = "Caf\xc3\xa9";
    REQUIRE(fixture.backend.setFieldValue(nlohmann::json::array({
        "W0.esp", "000800:W0.esp", "RACE\\MovementTypeNames[1]", cafe
    })).has_value());

    const auto grid = fixture.grid();
    CHECK(grid.at("rows")[0].at("cells")[column(grid, "RACE\\MovementTypeNames[1]")] == cafe);

    REQUIRE(fixture.backend.invokeBackend("SavePlugin",
        nlohmann::json::array({"W0.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(fixture.path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:W0.esp"));
    REQUIRE(record != nullptr);
    CHECK(record->subrecords[3].data == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}});
}
