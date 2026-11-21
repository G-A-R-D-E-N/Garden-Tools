#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginAssetCatalog.hpp>

using namespace fo4ide::plugin::assets;

namespace {

AssetRow makeRow(std::string editorId, std::string signature, std::string modelPath,
                 std::string plugin = "Fallout4.esm", bool deleted = false, std::string name = {}) {
    AssetRow row;
    row.formKey = "000800:" + plugin + "/" + editorId;
    row.editorId = std::move(editorId);
    row.name = std::move(name);
    row.signature = std::move(signature);
    row.modelPath = std::move(modelPath);
    row.plugin = std::move(plugin);
    row.deleted = deleted;
    return row;
}

std::vector<AssetRow> sampleRows() {
    return {
        makeRow("FloorTileA", "STAT", "Meshes\\Interiors\\Vault\\Floor01.nif", "Fallout4.esm", false, "Floor Tile A"),
        makeRow("FloorTileB", "STAT", "Meshes\\Interiors\\Vault\\Floor02.nif", "Fallout4.esm", true, "Floor Tile B"),
        makeRow("WallA", "STAT", "Meshes\\Interiors\\Vault\\Wall.nif", "Fallout4.esm", false, "Wall"),
        makeRow("CrateA", "STAT", "Meshes\\Interiors\\Cellar\\Crate.nif", "Fallout4.esm", false, "Crate"),
        makeRow("BarrelA", "MSTT", "Meshes\\Props\\Barrel.nif", "Patch.esp", false, "Barrel"),
        makeRow("ChairA", "FURN", "", "Patch.esp", false, "Chair"),
        makeRow("DoorA", "DOOR", "Meshes\\Doors\\Door.nif", "Fallout4.esm", false, "Door"),
    };
}

std::size_t countOf(const std::vector<NamedCount>& counts, const std::string& name) {
    for (const auto& entry : counts) {
        if (entry.name == name) return entry.count;
    }
    return 0;
}

}  // namespace

TEST_CASE("AssetCatalog groups signatures into drawer order") {
    const auto& groups = assetGroups();
    REQUIRE(groups.size() == 16);
    CHECK(groups.front().name == "Statics");
    CHECK(groups[1].name == "Movable Statics");
    CHECK(groups[14].name == "Hazards");
    CHECK(groups.back().name == "Other");
    CHECK(groupOfSignature("SCOL") == "Statics");
    CHECK(groupOfSignature("PROJ") == "Weapons");
    CHECK(groupOfSignature("NPC_") == "NPCs");
    CHECK(groupOfSignature("XXXX") == "Other");
    CHECK(canonicalGroupName("movable statics") == std::optional<std::string>{"Movable Statics"});
    CHECK_FALSE(canonicalGroupName("Bogus").has_value());
}

TEST_CASE("AssetCatalog group counts follow drawer order and hide zero groups") {
    const AssetCatalog catalog(sampleRows());
    AssetFilter filter;
    const auto counts = catalog.groupCounts(filter);
    REQUIRE(counts.size() == 4);
    CHECK(counts[0].name == "Statics");
    CHECK(counts[0].count == 3);
    CHECK(counts[1].name == "Movable Statics");
    CHECK(counts[2].name == "Furniture");
    CHECK(counts[3].name == "Doors");
    CHECK(countOf(counts, "Hazards") == 0);
}

TEST_CASE("AssetCatalog group counts honour folder and hidden deleted filters") {
    const AssetCatalog catalog(sampleRows());
    AssetFilter filter;
    filter.folder = "Meshes\\Interiors\\Vault";
    CHECK(countOf(catalog.groupCounts(filter), "Statics") == 2);
    filter.folder = "Interiors";
    filter.hideDeleted = false;
    CHECK(countOf(catalog.groupCounts(filter), "Statics") == 4);
}

TEST_CASE("AssetCatalog model paths drop the meshes prefix and the file name") {
    CHECK(splitModelPath("meshes\\Interiors\\Vault\\Floor.nif") ==
          std::vector<std::string>{"Interiors", "Vault", "Floor.nif"});
    CHECK(modelFolderSegments("Meshes/Interiors/Vault/Floor.nif") ==
          std::vector<std::string>{"Interiors", "Vault"});
    CHECK(modelFolderSegments("Floor.nif").empty());
    CHECK(normalizeFolder("Meshes\\Interiors\\\\Vault\\") == "interiors\\vault");
    CHECK(normalizeFolder("").empty());
}

TEST_CASE("AssetCatalog folder browse returns child folders with counts and samples") {
    const AssetCatalog catalog(sampleRows());
    AssetFilter filter;

    const auto root = catalog.browseFolder(filter, "", 1, 0);
    CHECK(root.total == 6);
    CHECK(root.direct == 1);
    REQUIRE(root.folders.size() == 3);
    CHECK(root.folders[0].path == "Doors");
    CHECK(root.folders[0].count == 1);
    CHECK(root.folders[1].path == "Interiors");
    CHECK(root.folders[1].count == 3);
    CHECK(root.folders[2].path == "Props");
    CHECK(root.folders[2].count == 1);

    const auto interiors = catalog.browseFolder(filter, "Interiors", 2, 1);
    CHECK(interiors.total == 3);
    CHECK(interiors.direct == 0);
    REQUIRE(interiors.folders.size() == 2);
    CHECK(interiors.folders[0].path == "Interiors\\Cellar");
    CHECK(interiors.folders[0].count == 1);
    CHECK(interiors.folders[1].path == "Interiors\\Vault");
    CHECK(interiors.folders[1].count == 2);
    CHECK(interiors.folders[1].samples.size() == 1);

    const auto vault = catalog.browseFolder(filter, "meshes\\interiors\\vault", 1, 5);
    CHECK(vault.total == 2);
    CHECK(vault.direct == 2);
    CHECK(vault.folders.empty());
}

TEST_CASE("AssetCatalog search filters by group, folder and plugin and pages") {
    const AssetCatalog catalog(sampleRows());

    AssetSearch query;
    query.filter.group = "statics";
    query.filter.folder = "Interiors\\Vault";
    auto page = catalog.search(query);
    CHECK(page.total == 2);
    REQUIRE(page.indices.size() == 2);
    CHECK(catalog.rows()[page.indices[0]].editorId == "FloorTileA");
    CHECK(catalog.rows()[page.indices[1]].editorId == "WallA");
    CHECK_FALSE(page.nextOffset.has_value());

    query.filter.plugin = "patch.esp";
    query.filter.folder.clear();
    page = catalog.search(query);
    CHECK(page.total == 0);

    AssetSearch byPlugin;
    byPlugin.filter.plugin = "Patch.esp";
    page = catalog.search(byPlugin);
    CHECK(page.total == 2);
}

TEST_CASE("AssetCatalog search matches text fields with the requested mode") {
    const AssetCatalog catalog(sampleRows());

    AssetSearch contains;
    contains.text = "floor";
    CHECK(catalog.search(contains).total == 1);
    contains.filter.hideDeleted = false;
    CHECK(catalog.search(contains).total == 2);

    AssetSearch exactName;
    exactName.text = "floor tile a";
    exactName.field = TextField::name;
    exactName.match = TextMatch::exact;
    exactName.filter.hideDeleted = false;
    const auto exact = catalog.search(exactName);
    REQUIRE(exact.total == 1);
    CHECK(catalog.rows()[exact.indices[0]].editorId == "FloorTileA");

    AssetSearch startsModel;
    startsModel.text = "meshes\\interiors\\cellar";
    startsModel.field = TextField::modelPath;
    startsModel.match = TextMatch::starts_with;
    CHECK(catalog.search(startsModel).total == 1);

    AssetSearch none;
    none.text = "nothing-like-this";
    CHECK(catalog.search(none).total == 0);
}

TEST_CASE("AssetCatalog search applies the hasModel filter") {
    const AssetCatalog catalog(sampleRows());
    AssetSearch withModel;
    withModel.hasModel = true;
    CHECK(catalog.search(withModel).total == 5);
    AssetSearch withoutModel;
    withoutModel.hasModel = false;
    const auto none = catalog.search(withoutModel);
    REQUIRE(none.total == 1);
    CHECK(catalog.rows()[none.indices[0]].editorId == "ChairA");
}

TEST_CASE("AssetCatalog search sorts descending and pages with a next offset") {
    std::vector<AssetRow> rows;
    for (int index = 1; index <= 5; ++index) {
        rows.push_back(makeRow("Item0" + std::to_string(index), "MISC", "Meshes\\Misc\\Item.nif"));
    }
    const AssetCatalog catalog(std::move(rows));

    AssetSearch first;
    first.descending = true;
    first.limit = 2;
    auto page = catalog.search(first);
    CHECK(page.total == 5);
    REQUIRE(page.indices.size() == 2);
    CHECK(catalog.rows()[page.indices[0]].editorId == "Item05");
    CHECK(catalog.rows()[page.indices[1]].editorId == "Item04");
    CHECK(page.nextOffset == std::optional<std::size_t>{2});

    AssetSearch last = first;
    last.offset = 4;
    page = catalog.search(last);
    CHECK(page.total == 5);
    REQUIRE(page.indices.size() == 1);
    CHECK(catalog.rows()[page.indices[0]].editorId == "Item01");
    CHECK_FALSE(page.nextOffset.has_value());

    AssetSearch past = first;
    past.offset = 99;
    page = catalog.search(past);
    CHECK(page.total == 5);
    CHECK(page.indices.empty());
    CHECK_FALSE(page.nextOffset.has_value());
}

TEST_CASE("AssetCatalog plugin counts and editor ID lookups") {
    const AssetCatalog catalog(sampleRows());
    AssetFilter filter;
    const auto plugins = catalog.pluginCounts(filter);
    REQUIRE(plugins.size() == 2);
    CHECK(plugins[0].name == "Fallout4.esm");
    CHECK(plugins[0].count == 4);
    CHECK(plugins[1].name == "Patch.esp");
    CHECK(plugins[1].count == 2);

    CHECK(catalog.editorIdMatches("wallA").size() == 1);
    CHECK(catalog.editorIdMatches("").empty());
    CHECK(catalog.editorIdMatches("Missing").empty());
}
