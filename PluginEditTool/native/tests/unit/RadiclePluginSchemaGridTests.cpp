#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../../src/plugin/RadiclePluginFields.hpp"
#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_schema_grid_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> makeRefrPlugin(bool includeScalars,
                                      bool duplicateXhlt = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "REFR", 0);
    const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SchemaRef\0", 10});
    if (includeScalars) {
        fo4::test::ByteWriter health;
        health.u32(50);
        fo4::test::write_subrecord(out, "XHLT", health.bytes);
        if (duplicateXhlt) {
            fo4::test::write_subrecord(out, "XHLT", health.bytes);
        }

        fo4::test::ByteWriter scale;
        scale.f32(1.25f);
        fo4::test::write_subrecord(out, "XSCL", scale.bytes);
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeFactPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "FACT", 0);
    const auto record = fo4::test::begin_record(out, "FACT", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SchemaFaction\0", 14});
    fo4::test::ByteWriter flags;
    flags.u32(0x3);
    fo4::test::write_subrecord(out, "DATA", flags.bytes);
    fo4::test::ByteWriter crime;
    crime.u8(1);
    crime.u8(0);
    for (std::uint8_t index = 2; index < 20; ++index) crime.u8(0);
    fo4::test::write_subrecord(out, "CRVA", crime.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> word(std::uint32_t value) {
    fo4::test::ByteWriter out;
    out.u32(value);
    return out.bytes;
}

std::vector<std::byte> makeNpcLinkPlugin(bool paddedPackage = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "NPC_", 0);
    const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SchemaNpc\0", 10});
    fo4::test::write_subrecord(out, "PKID", word(0x00000900));
    fo4::test::write_subrecord(out, "PKID",
                               paddedPackage ? std::vector<std::byte>(8) : word(0x00000901));
    fo4::test::write_subrecord(out, "PKID", word(0x00000902));
    fo4::test::write_subrecord(out, "PNAM", word(0x00000910));
    fo4::test::write_subrecord(out, "PNAM", word(0x00000911));
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(const std::filesystem::path& path) {
    return {
        {"name", "Schema.esp"},
        {"path", path.string()},
        {"source", "SchemaTest"},
        {"resolved", true}
    };
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({pluginEntry(path)})}})
        .has_value());
}

}

TEST_CASE("Spreadsheet scalar columns come from generated record descriptors") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeRefrPlugin(true));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "REFR", 200, 0})));
    const auto fields =
        fo4::schema::scalar_fields(fo4::Signature::from_string("REFR"));
    const auto links =
        fo4::schema::formlink_fields(fo4::Signature::from_string("REFR"));

    std::vector<std::string> expected{"EDID"};
    for (const auto& field : fields) {
        const auto& address = field.scalar.address;
        expected.push_back(std::string(field.path) +
                           (address.repeated || address.stride != 0 ? "\\Count" : ""));
    }
    for (const auto& link : links) {
        expected.push_back(std::string(link.path) +
                           (link.address.repeated || link.address.stride != 0 ? "\\Count" : ""));
    }
    for (const auto& list : fo4::schema::k_generated_formlink_lists) {
        if (list.record != fo4::Signature::from_string("REFR")) continue;
        expected.push_back(std::string(list.path) + "\\Count");
    }
    const auto& columns = grid.at("columns");
    REQUIRE(expected.size() <= columns.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(columns[index].get<std::string>() == expected[index]);
    }
    for (std::size_t index = expected.size(); index < columns.size(); ++index) {
        CHECK(columns[index].get<std::string>().rfind("Authoring\\", 0) == 0);
    }

    REQUIRE(grid.at("rows").size() == 1);
    const auto& row = grid.at("rows")[0];
    CHECK(row.at("cells")[0] == "SchemaRef");
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto subrecord = fields[index].scalar.subrecord;
        const auto& address = fields[index].scalar.address;
        if (subrecord == fo4::Signature::from_string("XHLT")) {
            CHECK(row.at("cells")[index + 1] == "50");
            CHECK(row.at("editable")[index + 1] == true);
        } else if (subrecord == fo4::Signature::from_string("XSCL")) {
            CHECK(row.at("cells")[index + 1] == "1.25");
            CHECK(row.at("editable")[index + 1] == true);
        } else if (address.repeated || address.stride != 0) {
            CHECK(row.at("cells")[index + 1] == "0");
            CHECK(row.at("editable")[index + 1] == false);
        } else {
            CHECK(row.at("cells")[index + 1] == "");
            CHECK(row.at("editable")[index + 1] == false);
        }
    }
}

TEST_CASE("Missing generated scalar subrecords remain blank and read-only") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeRefrPlugin(false));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "REFR", 200, 0})));
    const auto fields =
        fo4::schema::scalar_fields(fo4::Signature::from_string("REFR"));
    REQUIRE(grid.at("rows").size() == 1);
    const auto& row = grid.at("rows")[0];
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto& address = fields[index].scalar.address;
        const bool counted = address.repeated || address.stride != 0;
        CHECK(row.at("cells")[index + 1] == (counted ? "0" : ""));
        CHECK(row.at("editable")[index + 1] == false);
    }
}

TEST_CASE("Duplicate generated scalar subrecords fail closed in Spreadsheet") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeRefrPlugin(true, true));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "REFR", 200, 0}));
    REQUIRE_FALSE(grid.has_value());
    CHECK(grid.error().type == "ScalarReadFailed");
}

TEST_CASE("Schema enum and flag names reach the grid and record matrix") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeFactPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "FACT", 200, 0})));
    const auto& presentations = grid.at("presentations");
    REQUIRE(presentations.at("FACT\\CrimeArrest").at("kind") == "enum");
    REQUIRE(presentations.at("FACT\\CrimeArrest").at("names").size() == 2);
    CHECK(presentations.at("FACT\\CrimeArrest").at("names")[1] == nlohmann::json{{"value", 1}, {"name", "True"}});
    REQUIRE(presentations.at("FACT\\Flags").at("kind") == "flags");
    REQUIRE(presentations.at("FACT\\Flags").at("names").size() == 17);
    CHECK(presentations.at("FACT\\Flags").at("names")[0] == nlohmann::json{{"bit", 0}, {"name", "Hidden From NPC"}});
    CHECK(presentations.at("FACT\\Flags").at("names")[1] == nlohmann::json{{"bit", 1}, {"name", "Special Combat"}});
    CHECK(presentations.at("FACT\\Flags").at("names")[16] == nlohmann::json{{"bit", 16}, {"name", "Ignore Crimes: Werewolf (unused)"}});
    CHECK_FALSE(presentations.contains("FACT\\CrimeMurder"));

    const auto npcFlags =
        fo4ide::plugin::scalarPresentationJson(fo4::Signature('N', 'P', 'C', '_'), "NPC_\\ConfigurationFlags");
    REQUIRE(npcFlags.at("kind") == "flags");
    CHECK(npcFlags.at("names").back() == nlohmann::json{{"bit", 31}, {"name", "Invulnerable"}});

    const auto columns = grid.at("columns");
    const auto& cells = grid.at("rows")[0].at("cells");
    const auto columnIndex = [&](std::string_view column) {
        return static_cast<std::size_t>(std::distance(
            columns.begin(), std::find(columns.begin(), columns.end(), std::string(column))));
    };
    CHECK(cells[columnIndex("FACT\\Flags")] == "3");
    CHECK(cells[columnIndex("FACT\\CrimeArrest")] == "1");

    const auto matrix = parseResult(backend.invokeBackend(
        "GetConflictMatrix", nlohmann::json::array({"000800:Schema.esp"})));
    const auto& rows = matrix.at("Rows");
    const auto row = [&](std::string_view field) {
        return *std::find_if(rows.begin(), rows.end(), [&](const auto& candidate) {
            return candidate.at("Field").template get<std::string>() == field;
        });
    };
    CHECK(row("FACT\\Flags").at("Presentation").at("kind") == "flags");
    CHECK(row("FACT\\CrimeArrest").at("Presentation").at("names")[1].at("name") == "True");
    CHECK_FALSE(row("FACT\\CrimeMurder").contains("Presentation"));
}

TEST_CASE("NPC_ package and head part lists reach the grid as indexed columns") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeNpcLinkPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = parseResult(backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "NPC_", 200, 0})));
    const auto& columns = grid.at("columns");
    const auto cell = [&](std::string_view column) {
        const auto found = std::find(columns.begin(), columns.end(), std::string(column));
        REQUIRE(found != columns.end());
        return grid.at("rows")[0].at("cells")[
            static_cast<std::size_t>(std::distance(columns.begin(), found))].template get<std::string>();
    };
    CHECK(cell("NPC_\\Packages\\Count") == "3");
    CHECK(cell("NPC_\\Packages[0]") == "000900:Schema.esp");
    CHECK(cell("NPC_\\Packages[2]") == "000902:Schema.esp");
    CHECK(cell("NPC_\\HeadParts\\Count") == "2");
    CHECK(cell("NPC_\\HeadParts[1]") == "000911:Schema.esp");
}

TEST_CASE("Malformed NPC_ package payload fails the page instead of blanking the list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Schema.esp";
    writeBytes(path, makeNpcLinkPlugin(true));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto grid = backend.invokeBackend(
        "GetRecordsGrid",
        nlohmann::json::array({"Schema.esp", "NPC_", 200, 0}));
    REQUIRE_FALSE(grid.has_value());
    CHECK(grid.error().type == "FormLinkReadFailed");
}
