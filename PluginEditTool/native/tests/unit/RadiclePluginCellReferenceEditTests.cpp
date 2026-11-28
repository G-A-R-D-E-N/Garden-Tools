#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

namespace {

std::vector<std::byte> lockBasePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    const auto statTop = fo4::test::begin_group(out, "STAT", 0);
    const auto stat = fo4::test::begin_record(out, "STAT", 0, 0x00000810u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PartA\0", 6});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\PartA.nif\0", 17});
    fo4::test::end_record(out, stat);
    fo4::test::end_group(out, statTop);
    const auto keyTop = fo4::test::begin_group(out, "KEYM", 0);
    const auto key = fo4::test::begin_record(out, "KEYM", 0, 0x00000820u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestKey\0", 8});
    fo4::test::end_record(out, key);
    fo4::test::end_group(out, keyTop);
    const auto npcTop = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x00000830u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestNpc\0", 8});
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, npcTop);
    const auto factTop = fo4::test::begin_group(out, "FACT", 0);
    const auto fact = fo4::test::begin_record(out, "FACT", 0, 0x00000840u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestFaction\0", 12});
    fo4::test::end_record(out, fact);
    fo4::test::end_group(out, factTop);
    const auto keywordTop = fo4::test::begin_group(out, "KYWD", 0);
    const auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000850u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestKeyword\0", 12});
    fo4::test::end_record(out, keyword);
    fo4::test::end_group(out, keywordTop);
    const auto doorTop = fo4::test::begin_group(out, "DOOR", 0);
    const auto door = fo4::test::begin_record(out, "DOOR", 0, 0x00000860u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TestDoor\0", 9});
    fo4::test::end_record(out, door);
    fo4::test::end_group(out, doorTop);
    return out.bytes;
}

std::vector<std::byte> flagSourcePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    writeInteriorCell(out, 0x00000800u, 0x00000801u, 0x00000810u);
    return out.bytes;
}

void writeFixture(const std::filesystem::path& root) {
    writeBytes(root / "Base.esm", lockBasePlugin());
    writeBytes(root / "Source.esp", flagSourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());
}

nlohmann::json referenceDetails(fo4ide::plugin::RadiclePluginBackend& backend) {
    return parseStringResult(backend.getReferenceDetails(nlohmann::json::array({"000801:Base.esm"})));
}

std::string editRefusalType(fo4ide::plugin::RadiclePluginBackend& backend, const nlohmann::json& changes) {
    const auto result = backend.editPlacedReference(nlohmann::json::array({"000801:Base.esm", "Patch.esp", changes}));
    return result.has_value() ? std::string{} : result.error().type;
}

nlohmann::json optionFor(const nlohmann::json& details, std::uint32_t bit) {
    for (const auto& option : details.at("flags").at("options")) {
        if (option.at("bit") == bit) return option;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Native placed reference flags set and clear Initially Disabled and undo restores them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"flags", 0x800}}
    })).has_value());
    CHECK(referenceDetails(backend).at("flags").at("raw") == 0x800);
    CHECK(referenceDetails(backend).at("flags").at("initiallyDisabled") == true);
    CHECK(optionFor(referenceDetails(backend), 11).at("editable") == true);
    CHECK(optionFor(referenceDetails(backend), 10).at("editable") == false);

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"flags", 0}}
    })).has_value());
    CHECK(referenceDetails(backend).at("flags").at("raw") == 0);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("flags").at("raw") == 0x800);
}

TEST_CASE("Native placed reference flags refuse unknown, persistent, deleted and compressed bits without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(editRefusalType(backend, {{"flags", 0x4}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"flags", 0x400}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"flags", 0x20}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"flags", 0x40000}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"flags", 0xFFFFFFFFu}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"flags", 4294967296.0}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"flags", -1}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"flags", 1.5}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"flags", "800"}}) == "PlacedEditArgumentsMissing");

    CHECK(referenceDetails(backend).at("flags").at("raw") == 0);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native placed reference lock set and clear write and remove the XLOC and undo restores it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(referenceDetails(backend).at("lock").is_null());

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"lock", {{"level", 75}, {"key", "000820:Base.esm"}, {"flags", 4}}}}
    })).has_value());
    const auto locked = referenceDetails(backend).at("lock");
    CHECK(locked.at("level") == 75);
    CHECK(locked.at("levelName") == "Expert");
    CHECK(locked.at("key").at("formKey") == "000820:Base.esm");
    CHECK(locked.at("flags") == 4);

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"lock", nullptr}}
    })).has_value());
    CHECK(referenceDetails(backend).at("lock").is_null());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("lock").at("level") == 75);
}

TEST_CASE("Native placed reference lock refuses unnamed levels and non-KEYM keys without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(editRefusalType(backend, {{"lock", {{"level", 2}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"lock", {{"level", 75}, {"key", "000810:Base.esm"}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"lock", {{"level", 75}, {"key", "000899:Base.esm"}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"lock", {{"level", 256}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"lock", {{"key", "000820:Base.esm"}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"lock", {{"level", 75}, {"key", 820}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"lock", "Expert"}}) == "PlacedEditArgumentsMissing");

    CHECK(referenceDetails(backend).at("lock").is_null());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native placed reference flags and lock apply as one undo step on an overridden master reference") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto edited = parseStringResult(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp",
        nlohmann::json{{"flags", 0x800}, {"lock", {{"level", 50}, {"key", "000820:Base.esm"}, {"flags", 0}}}}
    })));
    CHECK_FALSE(edited.at("copiedFrom").get<std::string>().empty());

    const auto details = referenceDetails(backend);
    CHECK(details.at("winningPlugin") == "Patch.esp");
    CHECK(details.at("flags").at("raw") == 0x800);
    CHECK(details.at("lock").at("levelName") == "Advanced");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    const auto reverted = referenceDetails(backend);
    CHECK(reverted.at("flags").at("raw") == 0);
    CHECK(reverted.at("lock").is_null());
}

TEST_CASE("Native placed reference ownership sets an NPC owner, clears it and undo restores it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(referenceDetails(backend).at("ownership").is_null());

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"ownership", {{"owner", "000830:Base.esm"}, {"noCrime", true}}}}
    })).has_value());
    const auto owned = referenceDetails(backend);
    CHECK(owned.at("ownership").at("owner").at("formKey") == "000830:Base.esm");
    CHECK(owned.at("ownership").at("noCrime") == true);
    CHECK(owned.at("factionRank").is_null());

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"ownership", nullptr}}
    })).has_value());
    CHECK(referenceDetails(backend).at("ownership").is_null());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("ownership").at("owner").at("formKey") == "000830:Base.esm");
}

TEST_CASE("Native placed reference ownership sets a faction owner with rank and refuses rank on other owners") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"ownership", {{"owner", "000840:Base.esm"}, {"rank", 2}}}}
    })).has_value());
    const auto details = referenceDetails(backend);
    CHECK(details.at("ownership").at("owner").at("formKey") == "000840:Base.esm");
    CHECK(details.at("factionRank") == 2);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    CHECK(editRefusalType(backend, {{"ownership", {{"owner", "000830:Base.esm"}, {"rank", 1}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"ownership", {{"owner", "000810:Base.esm"}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"ownership", {{"owner", "000820:Base.esm"}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"ownership", {{"owner", "000899:Base.esm"}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"ownership", {{"rank", 1}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"ownership", {{"owner", "000840:Base.esm"}, {"rank", 1.5}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"ownership", {{"owner", "000840:Base.esm"}, {"noCrime", "yes"}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"ownership", "000840:Base.esm"}}) == "PlacedEditArgumentsMissing");

    CHECK(referenceDetails(backend).at("factionRank") == 2);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);
}
TEST_CASE("Native placed reference primitive bounds and colour edit in place and refuse the shape type") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", primitiveCellPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(referenceDetails(backend).at("primitive").at("bounds").at("x") == 512.0);

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp",
        nlohmann::json{{"primitive", {{"bounds", {{"x", 10.0}, {"y", 20.0}, {"z", 30.0}}}, {"color", {1.0, 0.0, 0.0, 1.0}}}}}
    })).has_value());
    const auto edited = referenceDetails(backend).at("primitive");
    CHECK(edited.at("bounds").at("x") == 10.0);
    CHECK(edited.at("bounds").at("y") == 20.0);
    CHECK(edited.at("bounds").at("z") == 30.0);
    CHECK(edited.at("color")[0] == 1.0);
    CHECK(edited.at("color")[1] == 0.0);
    CHECK(edited.at("color")[3] == 1.0);
    CHECK(edited.at("type") == 1);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    CHECK(editRefusalType(backend, {{"primitive", {{"type", 2}}}}) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, {{"primitive", {{"bounds", {{"x", -1.0}, {"y", 1.0}, {"z", 1.0}}}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"primitive", {{"color", {1.0, 0.0, 0.0}}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"primitive", {{"bounds", {{"x", 1.0}}}}}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"primitive", nlohmann::json::object()}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"primitive", nullptr}}) == "PlacedEditArgumentsMissing");

    const auto absent = backend.editPlacedReference(nlohmann::json::array({
        "000802:Base.esm", "Patch.esp", nlohmann::json{{"primitive", {{"color", {1.0, 0.0, 0.0, 1.0}}}}}
    }));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().type == "PlacedReferenceFieldAbsent");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("primitive").at("bounds").at("x") == 512.0);
}
TEST_CASE("Native placed reference open by default and ignored by sandbox toggle their markers with undo") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(referenceDetails(backend).at("openByDefault") == false);
    CHECK(referenceDetails(backend).at("ignoredBySandbox") == false);

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"openByDefault", true}, {"ignoredBySandbox", true}}
    })).has_value());
    CHECK(referenceDetails(backend).at("openByDefault") == true);
    CHECK(referenceDetails(backend).at("ignoredBySandbox") == true);

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"openByDefault", false}}
    })).has_value());
    CHECK(referenceDetails(backend).at("openByDefault") == false);
    CHECK(referenceDetails(backend).at("ignoredBySandbox") == true);
    CHECK(editRefusalType(backend, {{"openByDefault", "yes"}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"ignoredBySandbox", 1}}) == "PlacedEditArgumentsMissing");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("openByDefault") == true);
    CHECK(referenceDetails(backend).at("ignoredBySandbox") == true);
}
TEST_CASE("Native placed reference linked references replace the XLKR list and refuse wrong targets") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp",
        nlohmann::json{{"linkedReferences", nlohmann::json::array({
            {{"keyword", "000850:Base.esm"}, {"reference", "000801:Base.esm"}},
            {{"keyword", nullptr}, {"reference", "000801:Base.esm"}},
        })}}
    })).has_value());
    const auto linked = referenceDetails(backend).at("linkedReferences");
    REQUIRE(linked.size() == 2);
    CHECK(linked[0].at("keyword").at("formKey") == "000850:Base.esm");
    CHECK(linked[0].at("reference").at("formKey") == "000801:Base.esm");
    CHECK(linked[1].at("keyword").is_null());

    const auto refused = [&](nlohmann::json entry) {
        return editRefusalType(backend, {{"linkedReferences", nlohmann::json::array({entry})}});
    };
    CHECK(refused({{"keyword", "000820:Base.esm"}, {"reference", "000801:Base.esm"}}) == "PlacedEditInvalid");
    CHECK(refused({{"keyword", "000899:Base.esm"}, {"reference", "000801:Base.esm"}}) == "PlacedEditInvalid");
    CHECK(refused({{"keyword", nullptr}, {"reference", "000810:Base.esm"}}) == "PlacedEditInvalid");
    CHECK(refused({{"keyword", nullptr}, {"reference", "000899:Base.esm"}}) == "PlacedEditInvalid");
    CHECK(refused({{"keyword", nullptr}}) == "PlacedEditArgumentsMissing");
    CHECK(editRefusalType(backend, {{"linkedReferences", "000801:Base.esm"}}) == "PlacedEditArgumentsMissing");

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"linkedReferences", nlohmann::json::array()}}
    })).has_value());
    CHECK(referenceDetails(backend).at("linkedReferences").empty());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("linkedReferences").size() == 2);
}
TEST_CASE("Native placed reference comments set and clear the MNAM and undo restores it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    CHECK(referenceDetails(backend).at("comments").is_null());
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"comments", "Checked"}}
    })).has_value());
    CHECK(referenceDetails(backend).at("comments") == "Checked");

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"comments", nullptr}}
    })).has_value());
    CHECK(referenceDetails(backend).at("comments").is_null());
    CHECK(editRefusalType(backend, {{"comments", 5}}) == "PlacedEditArgumentsMissing");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(referenceDetails(backend).at("comments") == "Checked");
}

TEST_CASE("Native placed reference teleport refuses non-door targets and a reference without XTEL") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeFixture(root);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const nlohmann::json destination{{"x", 1.0}, {"y", 2.0}, {"z", 3.0}};
    const nlohmann::json rotation{{"x", 0.0}, {"y", 0.0}, {"z", 90.0}};
    const auto teleport = [&](const std::string& door) {
        return nlohmann::json{{"teleport", {{"door", door}, {"position", destination}, {"rotation", rotation}}}};
    };
    CHECK(editRefusalType(backend, teleport("000810:Base.esm")) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, teleport("000820:Base.esm")) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, teleport("000899:Base.esm")) == "PlacedEditInvalid");
    CHECK(editRefusalType(backend, teleport("000860:Base.esm")) == "PlacedReferenceFieldAbsent");
    CHECK(editRefusalType(backend, {{"teleport", {{"door", "000860:Base.esm"}, {"position", destination}}}}) == "PlacedEditArgumentsMissing");
    // Detects: a number too large for a float being written to the record as infinity instead of refused.
    CHECK(editRefusalType(backend, {{"teleport", {{"door", "000860:Base.esm"}, {"position", {{"x", 1e300}, {"y", 2.0}, {"z", 3.0}}}, {"rotation", rotation}}}}) == "PlacedEditArgumentsMissing");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}