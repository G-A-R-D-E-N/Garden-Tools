#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

TEST_CASE("Native exterior grid move reports its coordinate, keeps an edited property and survives save and reopen") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto before = parseStringResult(backend.getCellProperties(cellView));
    CHECK(before.at("gridX") == 0);
    CHECK(before.at("gridY") == 0);
    CHECK(before.at("descriptors").size() > 0);

    const auto tooFar = backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 200, 0}));
    REQUIRE_FALSE(tooFar.has_value());
    CHECK(tooFar.error().type == "CellRelocateInvalidCoordinate");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 0);

    const auto moved = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 3, 5})));
    CHECK(moved.at("gridX") == 3);
    CHECK(moved.at("changed") == true);
    CHECK(moved.at("formKey") == "000901:Base.esm");
    CHECK(moved.at("teleportsUpdated") == 0);
    const auto afterMove = parseStringResult(backend.getCellProperties(cellView));
    CHECK(afterMove.at("gridX") == 3);
    CHECK(afterMove.at("gridY") == 5);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    const auto edited = parseStringResult(backend.setCellProperties(nlohmann::json::array({
        "000901:Base.esm", "Patch.esp", nlohmann::json{{"WaterHeight", 7.5}}
    })));
    CHECK(edited.at("formKey") == "000901:Base.esm");
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight") == 7.5f);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    fo4ide::plugin::RadiclePluginBackend reopened;
    REQUIRE(reopened.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());
    const auto reloaded = parseStringResult(reopened.getCellProperties(cellView));
    CHECK(reloaded.at("gridX") == 3);
    CHECK(reloaded.at("gridY") == 5);
    CHECK(reloaded.at("values").at("WaterHeight") == 7.5f);
}

TEST_CASE("Native exterior grid move refuses a destination held by a master cell and leaves the master untouched") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());
    const auto readFile = [&](const char* name) {
        std::ifstream stream(root / name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    };
    const auto masterBefore = readFile("Base.esm");

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto occupied = backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 1, 0}));
    REQUIRE_FALSE(occupied.has_value());
    CHECK(occupied.error().type == "CellRelocateDestinationOccupied");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 0);

    const auto vacated = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000903:Base.esm", "Patch.esp", 7, 7})));
    CHECK(vacated.at("changed") == true);
    const auto intoVacated = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 1, 0})));
    CHECK(intoVacated.at("changed") == true);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 1);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readFile("Base.esm") == masterBefore);
}

namespace {

std::vector<std::byte> sourceWithExteriorCellPlugin(std::int32_t gridX, std::int32_t gridY) {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
    const auto worldspace = fo4::test::begin_record(out, "WRLD", 0, 0x00000800u);
    fo4::test::end_record(out, worldspace);
    const auto worldChildren = beginGroupRaw(out, 0x00000800u, fo4::GroupType::world_children);
    const auto block = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_block);
    const auto subblock = beginGroupRaw(out, 0u, fo4::GroupType::exterior_cell_subblock);
    writeExteriorCell(out, 0x01000A00u, gridX, gridY);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    endGroupRaw(out, worldChildren);
    fo4::test::end_group(out, wrld);
    return out.bytes;
}

}  // namespace

TEST_CASE("Native exterior grid move still sees a master occupant that a higher plugin cell used to hide, and the stale grid of a moved cell is free") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Source.esp", sourceWithExteriorCellPlugin(1, 0));
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto visitorOut = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000A00:Source.esp", "Patch.esp", 9, 9})));
    CHECK(visitorOut.at("changed") == true);

    const auto hidden = backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 1, 0}));
    REQUIRE_FALSE(hidden.has_value());
    CHECK(hidden.error().type == "CellRelocateDestinationOccupied");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 0);

    const auto intoStale = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000903:Base.esm", "Patch.esp", 7, 7})));
    CHECK(intoStale.at("changed") == true);
    const auto intoVacated = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 1, 0})));
    CHECK(intoVacated.at("changed") == true);
}

TEST_CASE("Native exterior grid move to the current coordinate reports no change and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto same = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 0, 0})));
    CHECK(same.at("changed") == false);
    CHECK(same.at("gridX") == 0);
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 0);
}

TEST_CASE("Native cell terrain write accepts heights as a JSON string and refuses unparseable text") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    auto raised = parseStringResult(backend.getCellLand(cellView)).at("heights").get<std::vector<float>>();
    raised[34] = 88.0f;

    const auto written = backend.setCellLand(nlohmann::json::array(
        {"000901:Base.esm", "Patch.esp", nlohmann::json(raised).dump()}));
    if (!written.has_value()) INFO(written.error().type + ": " + written.error().message);
    REQUIRE(written.has_value());
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 1);

    const auto garbled = backend.setCellLand(nlohmann::json::array({"000901:Base.esm", "Patch.esp", "[1, 2,"}));
    REQUIRE_FALSE(garbled.has_value());
    CHECK(garbled.error().type == "CellLandInvalid");
    const auto notAnArray = backend.setCellLand(nlohmann::json::array({"000901:Base.esm", "Patch.esp", "{}"}));
    REQUIRE_FALSE(notAnArray.has_value());
    CHECK(notAnArray.error().type == "CellLandArgumentsMissing");
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 1);
}

TEST_CASE("Native exterior grid move refuses an interior cell without writing an override") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto interior = backend.moveCellToGrid(nlohmann::json::array({"000800:Base.esm", "Patch.esp", 3, 5}));
    REQUIRE_FALSE(interior.has_value());
    CHECK(interior.error().type == "CellRelocateNotExterior");
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 0);
    CHECK(parseStringResult(backend.getCellProperties(nlohmann::json::array({"000800:Base.esm"})))
              .at("gridX").is_null());
}

TEST_CASE("Native cell terrain reads the winning heightmap and a written edit survives save and reopen") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto before = parseStringResult(backend.getCellLand(cellView));
    CHECK(before.at("cellFormKey") == "000901:Base.esm");
    CHECK(before.at("landFormKey") == "000980:Base.esm");
    CHECK(before.at("width") == 33);
    CHECK(before.at("height") == 33);
    CHECK(before.at("offset") == 10.0f);
    REQUIRE(before.at("heights").size() == 1089);
    CHECK(before.at("heights").at(0) == 80.0f);
    CHECK(before.at("heights").at(34) == 80.0f);

    auto raised = before.at("heights").get<std::vector<float>>();
    raised[34] = 88.0f;
    const auto writeResult =
        backend.setCellLand(nlohmann::json::array({"000901:Base.esm", "Patch.esp", raised}));
    if (!writeResult.has_value()) {
        INFO(writeResult.error().type + ": " + writeResult.error().message);
        REQUIRE(writeResult.has_value());
    }
    const auto written = nlohmann::json::parse(writeResult->get<std::string>());
    CHECK(written.at("formKey") == "000901:Base.esm");
    CHECK(written.at("copiedFrom") == "Base.esm");
    const auto after = parseStringResult(backend.getCellLand(cellView));
    CHECK(after.at("heights").at(34) == 88.0f);
    CHECK(after.at("heights").at(0) == 80.0f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    fo4ide::plugin::RadiclePluginBackend reopened;
    REQUIRE(reopened.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());
    const auto reloaded = parseStringResult(reopened.getCellLand(cellView));
    CHECK(reloaded.at("offset") == 10.0f);
    CHECK(reloaded.at("heights").at(34) == 88.0f);
    CHECK(reloaded.at("heights").at(0) == 80.0f);
}

TEST_CASE("Native cell terrain reports each refusal with its own error and stages nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto flat = std::vector<float>(1089, 80.0f);
    const auto write = [&](const char* cell, const std::vector<float>& heights) {
        return backend.setCellLand(nlohmann::json::array({cell, "Patch.esp", heights}));
    };
    const auto read = [&](const char* cell) { return backend.getCellLand(nlohmann::json::array({cell})); };

    const auto missing = write("000999:Base.esm", flat);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "CellNotFound");
    const auto notACell = write("000800:Base.esm", flat);
    REQUIRE_FALSE(notACell.has_value());
    CHECK(notACell.error().type == "CellLandNotACell");
    const auto deleted = write("000904:Base.esm", flat);
    REQUIRE_FALSE(deleted.has_value());
    CHECK(deleted.error().type == "CellDeleted");
    const auto noLand = write("000903:Base.esm", flat);
    REQUIRE_FALSE(noLand.has_value());
    CHECK(noLand.error().type == "CellLandMissing");
    const auto malformed = write("000905:Base.esm", flat);
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().type == "CellLandMalformed");
    const auto shortHeights = write("000901:Base.esm", std::vector<float>(64, 80.0f));
    REQUIRE_FALSE(shortHeights.has_value());
    CHECK(shortHeights.error().type == "CellLandInvalid");

    auto steep = flat;
    steep[100] = 80.0f + 8.0f * 200.0f;
    const auto unencodable = write("000901:Base.esm", steep);
    REQUIRE_FALSE(unencodable.has_value());
    CHECK(unencodable.error().type == "CellLandInvalid");
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 0);

    const auto readMissing = read("000999:Base.esm");
    REQUIRE_FALSE(readMissing.has_value());
    CHECK(readMissing.error().type == "CellNotFound");
    const auto readNotACell = read("000800:Base.esm");
    REQUIRE_FALSE(readNotACell.has_value());
    CHECK(readNotACell.error().type == "CellLandNotACell");
    const auto readDeleted = read("000904:Base.esm");
    REQUIRE_FALSE(readDeleted.has_value());
    CHECK(readDeleted.error().type == "CellDeleted");
    const auto readNoLand = read("000903:Base.esm");
    REQUIRE_FALSE(readNoLand.has_value());
    CHECK(readNoLand.error().type == "CellLandMissing");
    const auto readMalformed = read("000905:Base.esm");
    REQUIRE_FALSE(readMalformed.has_value());
    CHECK(readMalformed.error().type == "CellLandMalformed");
}

TEST_CASE("Native world properties read the winning worldspace and an edited flag and height survive save and reopen") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto readWorld = [](fo4ide::plugin::RadiclePluginBackend& from, const std::string& key) {
        const auto result = from.getWorldProperties(nlohmann::json::array({key}));
        if (!result.has_value()) INFO(result.error().type + ": " + result.error().message);
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    };

    const auto before = readWorld(backend, "000800:Base.esm");
    CHECK(before.at("formKey") == "000800:Base.esm");
    CHECK(before.at("descriptors").size() >= 10);
    CHECK(before.at("values").at("Flags").is_null());
    CHECK(before.at("values").at("Parent").is_null());

    const auto written = parseStringResult(backend.setWorldProperties(nlohmann::json::array(
        {"000800:Base.esm", "Patch.esp",
         nlohmann::json{{"Flags", 3}, {"DefaultLandHeight", 1.5}, {"DefaultWaterHeight", -2.5}}})));
    CHECK(written.at("formKey") == "000800:Base.esm");
    CHECK(written.at("copiedFrom") == "Base.esm");

    const auto after = readWorld(backend, "000800:Base.esm");
    CHECK(after.at("values").at("Flags") == 3);
    CHECK(after.at("values").at("DefaultLandHeight") == 1.5f);
    CHECK(after.at("values").at("DefaultWaterHeight") == -2.5f);
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 1);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    fo4ide::plugin::RadiclePluginBackend reopened;
    REQUIRE(reopened.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());
    const auto reloaded = readWorld(reopened, "000800:Base.esm");
    CHECK(reloaded.at("values").at("Flags") == 3);
    CHECK(reloaded.at("values").at("DefaultLandHeight") == 1.5f);
}

TEST_CASE("Native world properties report each refusal with its own error and stage nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto write = [&](const char* world, nlohmann::json changes) {
        return backend.setWorldProperties(nlohmann::json::array({world, "Patch.esp", std::move(changes)}));
    };

    const auto missing = write("000999:Base.esm", nlohmann::json{{"Flags", 1}});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "WorldNotFound");
    const auto notAWorld = write("000901:Base.esm", nlohmann::json{{"Flags", 1}});
    REQUIRE_FALSE(notAWorld.has_value());
    CHECK(notAWorld.error().type == "WorldNotAWorld");
    const auto targetMissing = write("000800:Base.esm", nlohmann::json{{"Climate", "000999:Base.esm"}});
    REQUIRE_FALSE(targetMissing.has_value());
    CHECK(targetMissing.error().type == "WorldTargetMissing");
    const auto notMaster = write("000800:Base.esm", nlohmann::json{{"Climate", "000900:Nowhere.esm"}});
    REQUIRE_FALSE(notMaster.has_value());
    CHECK(notMaster.error().type == "WorldTargetNotMaster");
    const auto wrongType = write("000800:Base.esm", nlohmann::json{{"Water", "000901:Base.esm"}});
    REQUIRE_FALSE(wrongType.has_value());
    CHECK(wrongType.error().type == "WorldTargetTypeNotAllowed");
    const auto badValue = write("000800:Base.esm", nlohmann::json{{"DefaultLandHeight", nullptr}});
    REQUIRE_FALSE(badValue.has_value());
    CHECK(badValue.error().type == "WorldPropertyInvalid");
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"}))).at("undoCount") == 0);

    const auto readMissing = backend.getWorldProperties(nlohmann::json::array({"000999:Base.esm"}));
    REQUIRE_FALSE(readMissing.has_value());
    CHECK(readMissing.error().type == "WorldNotFound");
    const auto readNotAWorld = backend.getWorldProperties(nlohmann::json::array({"000901:Base.esm"}));
    if (readNotAWorld.has_value()) INFO(readNotAWorld->get<std::string>());
    if (!readNotAWorld.has_value()) INFO(readNotAWorld.error().type);
    REQUIRE_FALSE(readNotAWorld.has_value());
    CHECK(readNotAWorld.error().type == "WorldNotAWorld");
}

TEST_CASE("Native exterior cell editor loop keeps terrain, cell and worldspace edits through save and reopen, and undo and redo follow them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellWithLandPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});

    const auto opened = parseStringResult(backend.getCellLand(cellView));
    CHECK(opened.at("width") == 33);
    CHECK(opened.at("height") == 33);
    CHECK(opened.at("offset") == 10.0f);
    CHECK(opened.at("landFormKey") == "000980:Base.esm");
    REQUIRE(opened.at("heights").size() == 1089);
    CHECK(opened.at("heights").at(0) == 80.0f);
    CHECK(opened.at("heights").at(34) == 80.0f);
    const auto properties = parseStringResult(backend.getCellProperties(cellView));
    CHECK(properties.at("descriptors").size() > 1);
    CHECK(properties.at("gridX") == 0);
    CHECK(properties.at("values").at("WaterHeight").is_null());
    CHECK(properties.at("values").at("Water").is_null());

    auto raised = opened.at("heights").get<std::vector<float>>();
    raised[34] = 88.0f;
    const auto terrainWrite =
        parseStringResult(backend.setCellLand(nlohmann::json::array({"000901:Base.esm", "Patch.esp", raised})));
    CHECK(terrainWrite.at("formKey") == "000901:Base.esm");
    CHECK(terrainWrite.at("copiedFrom") == "Base.esm");
    CHECK(parseStringResult(backend.setCellProperties(nlohmann::json::array({
        "000901:Base.esm", "Patch.esp", nlohmann::json{{"WaterHeight", 7.5}}
    }))).at("formKey") == "000901:Base.esm");
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight") == 7.5f);

    const auto worldView = nlohmann::json::array({"000800:Base.esm"});
    const auto worldBefore = parseStringResult(backend.getWorldProperties(worldView));
    CHECK(worldBefore.at("formKey") == "000800:Base.esm");
    CHECK(worldBefore.at("values").at("Flags").is_null());
    CHECK(worldBefore.at("values").at("DefaultLandHeight").is_null());
    CHECK(worldBefore.at("values").at("DefaultWaterHeight").is_null());
    const auto worldWrite = parseStringResult(backend.setWorldProperties(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp",
        nlohmann::json{{"Flags", 3}, {"DefaultLandHeight", 1.5}, {"DefaultWaterHeight", 450.0}}
    })));
    CHECK(worldWrite.at("formKey") == "000800:Base.esm");
    CHECK(worldWrite.at("copiedFrom") == "");
    const auto worldAfter = parseStringResult(backend.getWorldProperties(worldView));
    CHECK(worldAfter.at("values").at("Flags") == 3);
    CHECK(worldAfter.at("values").at("DefaultLandHeight") == 1.5f);
    CHECK(worldAfter.at("values").at("DefaultWaterHeight") == 450.0f);
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight") == 7.5f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 3);

    const auto steep = raised;
    auto unencodable = steep;
    unencodable[100] = 80.0f + 8.0f * 200.0f;
    const auto refusedTerrain =
        backend.setCellLand(nlohmann::json::array({"000901:Base.esm", "Patch.esp", unencodable}));
    REQUIRE_FALSE(refusedTerrain.has_value());
    CHECK(refusedTerrain.error().type == "CellLandInvalid");
    const auto refusedProperty = backend.setCellProperties(nlohmann::json::array({
        "000901:Base.esm", "Patch.esp", nlohmann::json{{"NoSuchField", 1}}
    }));
    REQUIRE_FALSE(refusedProperty.has_value());
    CHECK(refusedProperty.error().type == "CellPropertyInvalid");
    const auto refusedWorld = backend.setWorldProperties(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", nlohmann::json{{"DefaultLandHeight", nullptr}}
    }));
    REQUIRE_FALSE(refusedWorld.has_value());
    CHECK(refusedWorld.error().type == "WorldPropertyInvalid");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 3);
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    const auto worldUndone = parseStringResult(backend.getWorldProperties(worldView));
    CHECK(worldUndone.at("values").at("Flags").is_null());
    CHECK(worldUndone.at("values").at("DefaultLandHeight").is_null());
    CHECK(worldUndone.at("values").at("DefaultWaterHeight").is_null());
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight") == 7.5f);
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);
    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight").is_null());
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);
    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 80.0f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("redoCount") == 3);

    REQUIRE(backend.redoCellEdit(patchView).has_value());
    REQUIRE(backend.redoCellEdit(patchView).has_value());
    REQUIRE(backend.redoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellLand(cellView)).at("heights").at(34) == 88.0f);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight") == 7.5f);
    CHECK(parseStringResult(backend.getWorldProperties(worldView)).at("values").at("Flags") == 3);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    fo4ide::plugin::RadiclePluginBackend reopened;
    REQUIRE(reopened.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());
    const auto landAfter = parseStringResult(reopened.getCellLand(cellView));
    REQUIRE(landAfter.at("heights").size() == 1089);
    CHECK(landAfter.at("offset") == 10.0f);
    CHECK(landAfter.at("heights").at(0) == 80.0f);
    CHECK(landAfter.at("heights").at(34) == 88.0f);
    CHECK(landAfter.at("heights").at(1088) == 80.0f);
    const auto propertiesAfter = parseStringResult(reopened.getCellProperties(cellView));
    CHECK(propertiesAfter.at("values").at("WaterHeight") == 7.5f);
    CHECK(propertiesAfter.at("gridX") == 0);
    CHECK(propertiesAfter.at("descriptors").size() > 1);
    const auto worldReopened = parseStringResult(reopened.getWorldProperties(worldView));
    CHECK(worldReopened.at("values").at("Flags") == 3);
    CHECK(worldReopened.at("values").at("DefaultLandHeight") == 1.5f);
    CHECK(worldReopened.at("values").at("DefaultWaterHeight") == 450.0f);
    CHECK(worldReopened.at("descriptors").size() >= 10);
    CHECK(parseStringResult(reopened.getCellEditHistory(patchView)).at("undoCount") == 0);

    const auto savedPatch = [&] {
        REQUIRE(backend.invoke("backend.SavePlugin", patchView).has_value());
        std::ifstream stream(root / "Patch.esp", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    };
    const auto bytesBeforeMove = savedPatch();
    const auto farMove = parseStringResult(
        backend.moveCellToGrid(nlohmann::json::array({"000901:Base.esm", "Patch.esp", 100, -100})));
    CHECK(farMove.at("changed") == true);
    CHECK(farMove.at("teleportsUpdated") == 0);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 100);
    const auto bytesAfterMove = savedPatch();
    CHECK(bytesAfterMove != bytesBeforeMove);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 0);
    CHECK(savedPatch() == bytesBeforeMove);
    REQUIRE(backend.redoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("gridX") == 100);
    CHECK(savedPatch() == bytesAfterMove);
}
