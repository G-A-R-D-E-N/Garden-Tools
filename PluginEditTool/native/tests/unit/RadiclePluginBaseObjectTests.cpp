#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

namespace {

std::vector<std::byte> placeablePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

    const auto statTop = fo4::test::begin_group(out, "STAT", 0);
    const auto partA = fo4::test::begin_record(out, "STAT", 0, 0x00000810u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PartA\0", 6});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\PartA.nif\0", 17});
    fo4::test::end_record(out, partA);
    const auto deadA = fo4::test::begin_record(out, "STAT", 0x20, 0x00000850u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DeadA\0", 6});
    fo4::test::end_record(out, deadA);
    fo4::test::end_group(out, statTop);

    const auto weaponTop = fo4::test::begin_group(out, "WEAP", 0);
    const auto gun = fo4::test::begin_record(out, "WEAP", 0, 0x00000840u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"GunA\0", 5});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Weapons\\GunA.nif\0", 17});
    fo4::test::end_record(out, gun);
    fo4::test::end_group(out, weaponTop);

    const auto npcTop = fo4::test::begin_group(out, "NPC_", 0);
    const auto actor = fo4::test::begin_record(out, "NPC_", 0, 0x00000830u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ActorA\0", 7});
    fo4::test::end_record(out, actor);
    fo4::test::end_group(out, npcTop);

    const auto componentTop = fo4::test::begin_group(out, "CMPO", 0);
    const auto component = fo4::test::begin_record(out, "CMPO", 0, 0x00000820u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"CompA\0", 6});
    fo4::test::end_record(out, component);
    fo4::test::end_group(out, componentTop);

    writeInteriorCell(out, 0x00000800u, 0x00000801u, 0x00000810u);
    return out.bytes;
}

// The patch must list Base.esm as a master so placed references may name Base.esm bases.
std::vector<std::byte> patchOverBase() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    return out.bytes;
}

std::vector<std::byte> propsPlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {});
    const auto top = fo4::test::begin_group(out, "STAT", 0);
    const auto prop = fo4::test::begin_record(out, "STAT", 0, 0x00000810u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PropA\0", 6});
    fo4::test::end_record(out, prop);
    fo4::test::end_group(out, top);
    return out.bytes;
}

nlohmann::json transformAt(double x) {
    return {{"x", x}, {"y", 0.0}, {"z", 0.0}, {"rx", 0.0}, {"ry", 0.0}, {"rz", 0.0}};
}

nlohmann::json batchOf(std::size_t count, const std::string& base) {
    nlohmann::json items = nlohmann::json::array();
    for (std::size_t index = 0; index < count; ++index) {
        items.push_back({{"baseFormKey", base}, {"transform", transformAt(10.0 * static_cast<double>(index + 1))}});
    }
    return items;
}

struct Setup {
    std::filesystem::path root;
    Cleanup cleanup;
    fo4ide::plugin::RadiclePluginBackend backend;

    Setup() : root(tempRoot()), cleanup{root} {
        writeBytes(root / "Base.esm", placeablePlugin());
        writeBytes(root / "Patch.esp", patchOverBase());
        REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());
    }
};

const nlohmann::json kCell = nlohmann::json::array({"000800:Base.esm"});
const nlohmann::json kPatch = nlohmann::json::array({"Patch.esp"});

}  // namespace

TEST_CASE("ListBaseObjects lists placeable winners and leaves out non-placeable types") {
    Setup setup;
    const auto listed = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({nlohmann::json::object()})));
    CHECK(listed.at("total") == 3);
    CHECK(listed.at("typeCounts") == nlohmann::json({{"NPC_", 1}, {"STAT", 1}, {"WEAP", 1}}));
    CHECK_FALSE(listed.at("typeCounts").contains("CMPO"));
    CHECK(listed.at("items").at(0).at("hexId") == "00000830");
}

TEST_CASE("ListBaseObjects filters by type, text scope and match mode") {
    Setup setup;
    const auto byType = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"type", "STAT"}}})));
    REQUIRE(byType.at("items").size() == 1);
    CHECK(byType.at("items").at(0).at("editorId") == "PartA");
    CHECK(byType.at("items").at(0).at("hexId") == "00000810");

    const auto byEditorId = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"query", "gun"}}})));
    REQUIRE(byEditorId.at("total") == 1);
    CHECK(byEditorId.at("items").at(0).at("editorId") == "GunA");

    const auto byModel = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"query", "gunA.nif"}, {"scopes", nlohmann::json::array({"modelPath"})}}})));
    CHECK(byModel.at("total") == 1);

    const auto byHexId = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"query", "00000840"}, {"scopes", nlohmann::json::array({"hexId"})}}})));
    REQUIRE(byHexId.at("total") == 1);
    CHECK(byHexId.at("items").at(0).at("editorId") == "GunA");

    const auto exact = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"query", "gunA"}, {"match", "exact"}}})));
    CHECK(exact.at("total") == 1);

    const auto startsWith = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"query", "ACT"}, {"match", "startsWith"}}})));
    CHECK(startsWith.at("total") == 1);
}

TEST_CASE("ListBaseObjects sorts and pages server-side and caps the limit at 200") {
    Setup setup;
    const auto descending = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"sortBy", "editorId"}, {"sortDir", "desc"}}})));
    CHECK(descending.at("items").at(0).at("editorId") == "PartA");
    CHECK(descending.at("items").at(2).at("editorId") == "ActorA");

    const auto byHexId = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"sortBy", "hexId"}, {"sortDir", "desc"}}})));
    CHECK(byHexId.at("items").at(0).at("hexId") == "00000840");

    const auto page = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"sortBy", "editorId"}, {"limit", 1}, {"offset", 1}}})));
    CHECK(page.at("total") == 3);
    REQUIRE(page.at("items").size() == 1);
    CHECK(page.at("items").at(0).at("editorId") == "GunA");

    const auto capped = parseStringResult(setup.backend.listBaseObjects(nlohmann::json::array({
        {{"limit", 500}}})));
    CHECK(capped.at("limit") == 200);
}

TEST_CASE("ListBaseObjects refuses bad queries with a typed error") {
    Setup setup;
    const auto refusesType = setup.backend.listBaseObjects(nlohmann::json::array({{{"type", "CMPO"}}}));
    REQUIRE_FALSE(refusesType.has_value());
    CHECK(refusesType.error().type == "BaseObjectQueryInvalid");

    const auto refusesRegex = setup.backend.listBaseObjects(nlohmann::json::array({
        {{"match", "regex"}, {"query", "("}}}));
    REQUIRE_FALSE(refusesRegex.has_value());
    CHECK(refusesRegex.error().type == "BaseObjectQueryInvalid");

    const auto refusesSort = setup.backend.listBaseObjects(nlohmann::json::array({{{"sortBy", "bogus"}}}));
    CHECK_FALSE(refusesSort.has_value());
}

TEST_CASE("ListBaseObjects caps a regex pattern at 256 characters") {
    Setup setup;
    const auto refused = setup.backend.listBaseObjects(nlohmann::json::array({
        {{"match", "regex"}, {"query", std::string(257, 'a')}}}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "BaseObjectQueryInvalid");

    const auto accepted = setup.backend.listBaseObjects(nlohmann::json::array({
        {{"match", "regex"}, {"query", std::string(256, 'a')}}}));
    CHECK(accepted.has_value());
}

TEST_CASE("PlaceReference adds one reference with its base and transform and undo removes it") {
    Setup setup;
    const auto placed = parseStringResult(setup.backend.placeReference(nlohmann::json::array({
        "Patch.esp", "000800:Base.esm", "000810:Base.esm", transformAt(5.0), nlohmann::json::object()})));
    REQUIRE(placed.at("created").size() == 1);
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("references").at(0).at("recordType") == "PlacedObject");
    CHECK(placed.at("references").at(0).at("baseFormKey") == "000810:Base.esm");
    CHECK(placed.at("references").at(0).at("position").at("x") == 5.0);
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kCell)).at("references").size() == 2);
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kPatch)).at("undoCount") == 1);

    REQUIRE(setup.backend.undoCellEdit(kPatch).has_value());
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kCell)).at("references").size() == 1);
    REQUIRE(setup.backend.redoCellEdit(kPatch).has_value());
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kCell)).at("references").size() == 2);
}

TEST_CASE("PlaceReferences places a batch as one undo step") {
    Setup setup;
    const auto placed = parseStringResult(setup.backend.placeReferences(nlohmann::json::array({
        "Patch.esp", "000800:Base.esm", batchOf(3, "000810:Base.esm").dump(), nlohmann::json::object()})));
    CHECK(placed.at("created").size() == 3);
    CHECK(placed.at("references").size() == 3);
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kPatch)).at("undoCount") == 1);

    REQUIRE(setup.backend.undoCellEdit(kPatch).has_value());
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kCell)).at("references").size() == 1);
    REQUIRE(setup.backend.redoCellEdit(kPatch).has_value());
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kCell)).at("references").size() == 4);
}

TEST_CASE("PlaceReference creates an ACHR for an NPC base") {
    Setup setup;
    const auto placed = parseStringResult(setup.backend.placeReference(nlohmann::json::array({
        "Patch.esp", "000800:Base.esm", "000830:Base.esm", transformAt(1.0), nlohmann::json::object()})));
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("references").at(0).at("recordType") == "PlacedNpc");
}

TEST_CASE("PlaceReference refuses unknown bases, non-placeable bases and unknown cells without an undo step") {
    Setup setup;
    const auto missing = setup.backend.placeReference(nlohmann::json::array({
        "Patch.esp", "000800:Base.esm", "000899:Base.esm", transformAt(1.0), nlohmann::json::object()}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PlacedBaseMissing");

    const auto component = setup.backend.placeReference(nlohmann::json::array({
        "Patch.esp", "000800:Base.esm", "000820:Base.esm", transformAt(1.0), nlohmann::json::object()}));
    REQUIRE_FALSE(component.has_value());
    CHECK(component.error().type == "PlacedBaseNotPlaceable");

    const auto noCell = setup.backend.placeReference(nlohmann::json::array({
        "Patch.esp", "000899:Base.esm", "000810:Base.esm", transformAt(1.0), nlohmann::json::object()}));
    REQUIRE_FALSE(noCell.has_value());
    CHECK(noCell.error().type == "PlacedCellNotFound");

    CHECK(parseStringResult(setup.backend.getCellEditHistory(kPatch)).at("undoCount") == 0);
}

TEST_CASE("PlaceReference refuses a deleted base without an undo step") {
    Setup setup;
    const auto deleted = setup.backend.placeReference(nlohmann::json::array({
        "Patch.esp", "000800:Base.esm", "000850:Base.esm", transformAt(1.0), nlohmann::json::object()}));
    REQUIRE_FALSE(deleted.has_value());
    CHECK(deleted.error().type == "PlacedBaseDeleted");
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kPatch)).at("undoCount") == 0);
}

TEST_CASE("PlaceReferences adds a base owner master that the patch lacks") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", placeablePlugin());
    writeBytes(root / "Props.esm", propsPlugin());
    fo4::test::ByteWriter loose;
    writeHeader(loose, {});
    writeBytes(root / "Loose.esp", loose.bytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto environment = nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esm", root / "Base.esm"),
        pluginEntry("Props.esm", root / "Props.esm"),
        pluginEntry("Loose.esp", root / "Loose.esp")})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto placed = parseStringResult(backend.placeReferences(nlohmann::json::array({
        "Loose.esp", "000800:Base.esm", batchOf(1, "000810:Props.esm").dump(), nlohmann::json::object()})));
    REQUIRE(placed.at("created").size() == 1);
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("references").at(0).at("baseFormKey") == "000810:Props.esm");
}
