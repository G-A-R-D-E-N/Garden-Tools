#include <cstddef>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

namespace {

nlohmann::json offsetX(double x) {
    return {{"x", x}, {"y", 0.0}, {"z", 0.0}, {"rx", 0.0}, {"ry", 0.0}, {"rz", 0.0}};
}

std::size_t countAtX(const nlohmann::json& cell, float x) {
    std::size_t count = 0;
    for (const auto& reference : cell.at("references")) {
        if (reference.at("position").at("x") == x) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("Native cell duplicate copies a reference with its offset and undo and redo follow it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto duplicated = parseStringResult(backend.duplicatePlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", offsetX(100.0)
    })));
    REQUIRE(duplicated.contains("newFormKey"));
    const auto afterDuplicate = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(afterDuplicate.at("references").size() == 2);
    CHECK(countAtX(afterDuplicate, 10.0f) == 1);
    CHECK(countAtX(afterDuplicate, 110.0f) == 1);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 1);

    REQUIRE(backend.redoCellEdit(patchView).has_value());
    CHECK(countAtX(parseStringResult(backend.getPlacedReferences(cellView)), 110.0f) == 1);
}

TEST_CASE("Native cell duplicate refuses an unknown reference and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto refused = backend.duplicatePlacedReference(nlohmann::json::array({
        "000899:Base.esm", "Patch.esp"
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PlacedReferenceNotFound");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell delete of a master reference is marked deleted and undo brings it back") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto deleted = parseStringResult(backend.deletePlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp"
    })));
    CHECK(deleted.at("status") == "markedDeleted");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 0);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    const auto restored = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(restored.at("references").size() == 1);
    CHECK(countAtX(restored, 10.0f) == 1);
}

TEST_CASE("Native cell delete of a patch-owned duplicate removes it outright") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto duplicated = parseStringResult(backend.duplicatePlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", offsetX(100.0)
    })));
    const auto newKey = duplicated.at("newFormKey").get<std::string>();
    const auto deleted = parseStringResult(backend.deletePlacedReference(nlohmann::json::array({
        newKey, "Patch.esp"
    })));
    CHECK(deleted.at("status") == "deleted");
    const auto afterDelete = parseStringResult(backend.getPlacedReferences(cellView));
    CHECK(afterDelete.at("references").size() == 1);
    CHECK(countAtX(afterDelete, 110.0f) == 0);
}

TEST_CASE("Native cell properties read reports the winning cell's values and the editable descriptors") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto properties = parseStringResult(backend.getCellProperties(nlohmann::json::array({"000800:Base.esm"})));
    CHECK(properties.at("values").at("Flags") == 1);
    CHECK(properties.at("values").at("WaterHeight").is_null());
    CHECK(properties.at("values").at("Owner").is_null());
    REQUIRE(properties.at("descriptors").is_array());
    CHECK(properties.at("descriptors").size() > 0);

    const auto unknown = backend.getCellProperties(nlohmann::json::array({"000999:Base.esm"}));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "CellNotFound");
}

TEST_CASE("Native cell properties edit sets the water height in the patch and undo removes it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto edited = parseStringResult(backend.setCellProperties(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", nlohmann::json{{"WaterHeight", 12.5}}
    })));
    CHECK(edited.at("formKey") == "000800:Base.esm");
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight") == 12.5f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight").is_null());
}

TEST_CASE("Native cell properties edit refuses an unknown field or a non-number and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto unknownField = backend.setCellProperties(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", nlohmann::json{{"NotAField", 1}}
    }));
    REQUIRE_FALSE(unknownField.has_value());
    CHECK(unknownField.error().type == "CellPropertyInvalid");

    const auto notNumber = backend.setCellProperties(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", nlohmann::json{{"WaterHeight", "high"}}
    }));
    REQUIRE_FALSE(notNumber.has_value());
    CHECK(notNumber.error().type == "CellPropertyInvalid");

    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
    CHECK(parseStringResult(backend.getCellProperties(cellView)).at("values").at("WaterHeight").is_null());
}

TEST_CASE("Native cell properties edit refuses a link to a record that does not exist") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto refused = backend.setCellProperties(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", nlohmann::json{{"Location", "000999:Base.esm"}}
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "CellTargetMissing");
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"})))
              .at("undoCount") == 0);
}
