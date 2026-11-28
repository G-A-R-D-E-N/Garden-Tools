#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

namespace {

std::vector<std::byte> assetPlugin() {
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
    const auto dupOne = fo4::test::begin_record(out, "STAT", 0, 0x00000812u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DupA\0", 5});
    fo4::test::end_record(out, dupOne);
    const auto dupTwo = fo4::test::begin_record(out, "STAT", 0, 0x00000813u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DupA\0", 5});
    fo4::test::end_record(out, dupTwo);
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

    writeInteriorCell(out, 0x00000800u, 0x00000801u, 0x00000810u, "AssetCell");
    return out.bytes;
}

std::vector<std::byte> patchOverBase() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    return out.bytes;
}

nlohmann::json vec(double x, double y, double z) {
    return {{"x", x}, {"y", y}, {"z", z}};
}

struct AssetSetup {
    std::filesystem::path root;
    Cleanup cleanup;
    fo4ide::plugin::RadiclePluginBackend backend;

    AssetSetup() : root(tempRoot()), cleanup{root} {
        writeBytes(root / "Base.esm", assetPlugin());
        writeBytes(root / "Patch.esp", patchOverBase());
        REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());
    }
};

const nlohmann::json kAssetCell = nlohmann::json::array({"000800:Base.esm"});
const nlohmann::json kAssetPatch = nlohmann::json::array({"Patch.esp"});

nlohmann::json placeArgs(const nlohmann::json& asset, const nlohmann::json& tail) {
    auto args = nlohmann::json::array({"Patch.esp", "000800:Base.esm", asset});
    for (const auto& value : tail) args.push_back(value);
    return args;
}

std::set<double> xsOf(const nlohmann::json& references) {
    std::set<double> xs;
    for (const auto& reference : references) xs.insert(reference.at("position").at("x").get<double>());
    return xs;
}

}  // namespace

TEST_CASE("AssetCatalog backend lists categories and plugin counts without deleted assets") {
    AssetSetup setup;
    const auto categories = parseStringResult(setup.backend.assetOperation(
        "BrowseCategories", nlohmann::json::array({nlohmann::json()})));
    CHECK(categories.at("total") == 5);
    CHECK(categories.at("groups").is_array());
    CHECK(categories.at("plugins").is_array());
}

TEST_CASE("AssetCatalog backend searches assets and pages with a next offset") {
    AssetSetup setup;
    const auto first = parseStringResult(setup.backend.assetOperation(
        "SearchAssets", nlohmann::json::array({"", "", "", "", "", "", nlohmann::json(), "", 2, 0})));
    CHECK(first.at("total") == 5);
    CHECK(first.at("items").size() == 2);
    CHECK(first.at("nextOffset") == 2);

    const auto last = parseStringResult(setup.backend.assetOperation(
        "SearchAssets", nlohmann::json::array({"", "", "", "", "", "", nlohmann::json(), "", 2, 4})));
    CHECK(last.at("items").size() == 1);
    CHECK(last.at("nextOffset").is_null());

    const auto byText = parseStringResult(setup.backend.assetOperation(
        "SearchAssets", nlohmann::json::array({"gun", "", "", "", "", "", nlohmann::json(), "", 10, 0})));
    REQUIRE(byText.at("items").size() == 1);
    CHECK(byText.at("items").at(0).at("editorId") == "GunA");
}

TEST_CASE("AssetCatalog backend refuses unknown search options with a typed error") {
    AssetSetup setup;
    const auto refused = setup.backend.assetOperation(
        "SearchAssets", nlohmann::json::array({"", "", "", "", "", "", nlohmann::json(), "bogus"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "AssetArgumentInvalid");

    const auto unknown = setup.backend.assetOperation("NoSuchMethod", nlohmann::json::array());
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "AssetMethodUnknown");
}

TEST_CASE("AssetCatalog backend returns asset details by form key and by unique editor ID") {
    AssetSetup setup;
    const auto byKey = parseStringResult(setup.backend.assetOperation(
        "GetAssetDetails", nlohmann::json::array({"000810:Base.esm", nlohmann::json()})));
    CHECK(byKey.at("editorId") == "PartA");
    CHECK(byKey.at("formKey") == "000810:Base.esm");

    const auto byEditorId = parseStringResult(setup.backend.assetOperation(
        "GetAssetDetails", nlohmann::json::array({nlohmann::json(), "GunA"})));
    CHECK(byEditorId.at("formKey") == "000840:Base.esm");
}

TEST_CASE("PlaceAsset places one asset at a position as one undo step") {
    AssetSetup setup;
    const auto placed = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", placeArgs("000810:Base.esm", nlohmann::json::array({vec(5.0, 0.0, 0.0)}))));
    REQUIRE(placed.at("created").size() == 1);
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("references").at(0).at("baseFormKey") == "000810:Base.esm");
    CHECK(placed.at("references").at(0).at("position").at("x") == 5.0);
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kAssetCell)).at("references").size() == 2);
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 1);

    REQUIRE(setup.backend.undoCellEdit(kAssetPatch).has_value());
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kAssetCell)).at("references").size() == 1);
}

TEST_CASE("PlaceAsset resolves a unique editor ID and refuses an ambiguous one with candidates") {
    AssetSetup setup;
    const auto placed = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", placeArgs("PartA", nlohmann::json::array({vec(1.0, 0.0, 0.0)}))));
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("references").at(0).at("baseFormKey") == "000810:Base.esm");

    const auto ambiguous = setup.backend.assetOperation(
        "PlaceAsset", placeArgs("DupA", nlohmann::json::array({vec(1.0, 0.0, 0.0)})));
    REQUIRE_FALSE(ambiguous.has_value());
    CHECK(ambiguous.error().type == "AssetAmbiguous");
    CHECK(ambiguous.error().message.find("000812") != std::string::npos);
    CHECK(ambiguous.error().message.find("000813") != std::string::npos);
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 1);
}

TEST_CASE("PlaceAsset refuses non-placeable and unknown assets without an undo step") {
    AssetSetup setup;
    const auto component = setup.backend.assetOperation(
        "PlaceAsset", placeArgs("000820:Base.esm", nlohmann::json::array({vec(1.0, 0.0, 0.0)})));
    REQUIRE_FALSE(component.has_value());
    CHECK(component.error().type == "AssetNotPlaceable");

    const auto unknown = setup.backend.assetOperation(
        "PlaceAsset", placeArgs("NoSuchAsset", nlohmann::json::array({vec(1.0, 0.0, 0.0)})));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "AssetNotFound");
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 0);
}

TEST_CASE("PlaceAsset count and spacing lay out copies and snap them to the grid") {
    AssetSetup setup;
    const auto row = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", placeArgs("GunA", nlohmann::json::array({
            vec(10.0, 0.0, 0.0), nlohmann::json(), nlohmann::json(), nlohmann::json(), nlohmann::json(),
            3, vec(2.0, 0.0, 0.0)}))));
    CHECK(row.at("count") == 3);
    CHECK(xsOf(row.at("references")) == std::set<double>{10.0, 12.0, 14.0});
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 1);

    REQUIRE(setup.backend.undoCellEdit(kAssetPatch).has_value());
    const auto snapped = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", placeArgs("GunA", nlohmann::json::array({
            vec(20.4, 0.0, 0.0), nlohmann::json(), nlohmann::json(), nlohmann::json(), nlohmann::json(),
            2, vec(1.0, 0.0, 0.0), 1.0}))));
    CHECK(xsOf(snapped.at("references")) == std::set<double>{20.0, 21.0});
}

TEST_CASE("PlaceAsset near_reference_key with offset places beside an existing reference") {
    AssetSetup setup;
    const auto placed = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", placeArgs("PartA", nlohmann::json::array({
            nlohmann::json(), "000801:Base.esm", vec(1.0, 0.0, 0.0)}))));
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("references").at(0).at("position").at("x") == 11.0);
}

// Detects: place_asset reading the near reference through the cell's editor ID and then refusing the same cell_id as an invalid FormKey when it writes.
TEST_CASE("PlaceAsset takes the cell by editor ID as well as by form key") {
    AssetSetup setup;
    const auto placed = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", nlohmann::json::array({"Patch.esp", "AssetCell", "PartA", nlohmann::json(), "000801:Base.esm",
                                             vec(1.0, 0.0, 0.0)})));
    REQUIRE(placed.at("references").size() == 1);
    CHECK(placed.at("cellFormKey") == "000800:Base.esm");
    CHECK(placed.at("references").at(0).at("position").at("x") == 11.0);

    const auto unknown = setup.backend.assetOperation(
        "PlaceAsset", nlohmann::json::array({"Patch.esp", "NoSuchCell", "PartA", vec(1.0, 0.0, 0.0)}));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "PlacedCellNotFound");
}

TEST_CASE("PlaceAsset dry_run returns the plan and writes nothing") {
    AssetSetup setup;
    const auto plan = parseStringResult(setup.backend.assetOperation(
        "PlaceAsset", placeArgs("GunA", nlohmann::json::array({
            vec(10.0, 0.0, 0.0), nlohmann::json(), nlohmann::json(), nlohmann::json(), nlohmann::json(),
            2, vec(2.0, 0.0, 0.0), nlohmann::json(), nlohmann::json(), true}))));
    CHECK(plan.at("dryRun") == true);
    CHECK(plan.at("placements").size() == 2);
    CHECK(parseStringResult(setup.backend.getPlacedReferences(kAssetCell)).at("references").size() == 1);
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 0);
}

TEST_CASE("PlaceAsset refuses bad positions, counts and oversized batches") {
    AssetSetup setup;
    const auto both = setup.backend.assetOperation(
        "PlaceAsset", placeArgs("GunA", nlohmann::json::array({
            vec(1.0, 0.0, 0.0), "000801:Base.esm"})));
    REQUIRE_FALSE(both.has_value());
    CHECK(both.error().type == "AssetArgumentInvalid");

    const auto neither = setup.backend.assetOperation("PlaceAsset", placeArgs("GunA", nlohmann::json::array()));
    REQUIRE_FALSE(neither.has_value());
    CHECK(neither.error().type == "AssetArgumentInvalid");

    const auto tooMany = setup.backend.assetOperation(
        "PlaceAsset", placeArgs("GunA", nlohmann::json::array({
            vec(1.0, 0.0, 0.0), nlohmann::json(), nlohmann::json(), nlohmann::json(), nlohmann::json(), 201})));
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().type == "AssetPlacementTooMany");
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 0);
}

TEST_CASE("PlaceAsset with a missing near reference reports it and writes nothing") {
    AssetSetup setup;
    const auto missing = setup.backend.assetOperation(
        "PlaceAsset", placeArgs("GunA", nlohmann::json::array({
            nlohmann::json(), "000899:Base.esm"})));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "AssetNearReferenceMissing");
    CHECK(parseStringResult(setup.backend.getCellEditHistory(kAssetPatch)).at("undoCount") == 0);
}

TEST_CASE("AssetBrowseReal.VanillaCatalogBrowsesSearchesAndDetailsFallout4Esm") {
    const char* env = std::getenv("FO4_DATA_DIR");
    if (env == nullptr || *env == '\0') SKIP("FO4_DATA_DIR not set");
    const auto master = std::filesystem::path(env) / "Fallout4.esm";
    if (!std::filesystem::exists(master)) SKIP("Fallout4.esm not found under FO4_DATA_DIR");
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Patch.esp", patchOverBase());
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Fallout4.esm", master), pluginEntry("Patch.esp", root / "Patch.esp")})}}).has_value());

    const auto categories = parseStringResult(backend.assetOperation(
        "BrowseCategories", nlohmann::json::array({nlohmann::json()})));
    CHECK(categories.at("total").get<std::size_t>() > 30000);
    CHECK(categories.at("groups").is_array());

    const auto floors = parseStringResult(backend.assetOperation(
        "SearchAssets", nlohmann::json::array({"Floor", "Statics", "", "", "", "", nlohmann::json(), "", 5, 0})));
    REQUIRE(floors.at("items").size() == 5);
    CHECK(floors.at("total").get<std::size_t>() > 5);
    CHECK(floors.at("nextOffset") == 5);

    const auto details = parseStringResult(backend.assetOperation(
        "GetAssetDetails", nlohmann::json::array({"12DC30:Fallout4.esm", nlohmann::json()})));
    CHECK(details.at("editorId") == "AirportFloor01");
}
