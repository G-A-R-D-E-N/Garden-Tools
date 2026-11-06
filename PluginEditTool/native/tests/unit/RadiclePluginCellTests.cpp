#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/placed_reference.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"
#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

TEST_CASE("Native cell transform creates a nested override before moving the reference") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto sourcePath = root / "Source.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(basePath, basePlugin());
    writeBytes(sourcePath, sourcePlugin());
    writeBytes(patchPath, patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", basePath),
            pluginEntry("Source.esp", sourcePath),
            pluginEntry("Patch.esp", patchPath)
        })}
    };
    auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    CHECK(loaded->at("loadedPlugins") == 3);

    const auto beforeMove = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    REQUIRE(beforeMove.at("references").size() == 1);
    const auto& renderRef = beforeMove.at("references")[0];
    CHECK(renderRef.at("baseType") == "StaticCollection");
    REQUIRE(renderRef.at("scolParts").is_array());
    REQUIRE(renderRef.at("scolParts").size() == 2);
    CHECK(renderRef.at("scolParts")[0].at("modelPath") == "Meshes\\PartA.nif");
    CHECK(renderRef.at("scolParts")[0].at("placements")[0].at("x") == 1.0f);
    CHECK(renderRef.at("scolParts")[0].at("placements")[0].at("scale") == 7.0f);
    CHECK(renderRef.at("scolParts")[1].at("modelPath") == "Meshes\\PartB.nif");
    CHECK(renderRef.at("scolParts")[1].at("placements")[0].at("x") == 10.0f);

    const auto moved = backend.setPlacedReferenceTransform(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp",
        100.0, 200.0, 300.0, 1.5, 2.5, 3.5
    }));
    REQUIRE(moved.has_value());
    REQUIRE(moved->is_string());
    CHECK(moved->get<std::string>().find("Created the nested override from Source.esp") !=
          std::string::npos);

    const auto pendingView = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    REQUIRE(pendingView.at("references").size() == 1);
    CHECK(pendingView.at("references")[0].at("position").at("x") == 100.0f);

    const auto beforeSave = fo4::read_plugin_file(patchPath);
    REQUIRE(beforeSave.header.masters.size() == 1);
    CHECK(beforeSave.header.masters[0].key.filename() == "Other.esm");
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Base.esm")) == nullptr);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000801:Base.esm")) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reread = fo4::read_plugin_file(patchPath);
    REQUIRE(reread.header.masters.size() == 2);
    CHECK(reread.header.masters[0].key.filename() == "Other.esm");
    CHECK(reread.header.masters[1].key.filename() == "Base.esm");

    const auto cellKey = fo4::FormKey::parse("000800:Base.esm");
    const auto referenceKey = fo4::FormKey::parse("000801:Base.esm");
    REQUIRE(fo4::find_record(reread, cellKey) != nullptr);
    REQUIRE(fo4::find_record(reread, referenceKey) != nullptr);

    const auto refs = fo4::forms::placed_references_for_cell(reread, cellKey);
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].record->form_key == referenceKey);
    CHECK(refs[0].transform.x == 100.0f);
    CHECK(refs[0].transform.y == 200.0f);
    CHECK(refs[0].transform.z == 300.0f);
    CHECK(refs[0].transform.rx == 1.5f);
    CHECK(refs[0].transform.ry == 2.5f);
    CHECK(refs[0].transform.rz == 3.5f);

    const auto view = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    REQUIRE(view.at("references").size() == 1);
    CHECK(view.at("references")[0].at("formKey") == "000801:Base.esm");
    CHECK(view.at("references")[0].at("position").at("x") == 100.0f);
}

TEST_CASE("Native cell takes each placed base's model from its type-aware primary model") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    writeBytes(basePath, typedCellPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", basePath)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto view = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    const auto& refs = view.at("references");
    REQUIRE(refs.size() == 8);
    CHECK(refs[0].at("baseType") == "Static");
    CHECK(refs[0].at("modelPath") == "Meshes\\StatFixture.nif");
    CHECK(refs[1].at("baseType") == "MovableStatic");
    CHECK(refs[1].at("modelPath") == "Meshes\\MsttFixture.nif");
    CHECK(refs[2].at("baseType") == "Furniture");
    CHECK(refs[2].at("modelPath") == "Meshes\\FurnFixture.nif");
    CHECK(refs[3].at("baseType") == "Activator");
    CHECK(refs[3].at("modelPath") == "Meshes\\ActiFixture.nif");
    CHECK(refs[4].at("baseType") == "Container");
    CHECK(refs[4].at("modelPath") == "");
    CHECK(refs[5].at("baseType") == "StaticCollection");
    CHECK(refs[5].at("modelPath") == "");
    CHECK(refs[6].at("modelPath") == "Armor\\ArmoFixture.nif");
    CHECK(refs[7].at("recordType") == "PlacedTrap");
}

TEST_CASE("Native cell reports decal width and height") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto decalPath = root / "Decal.esp";
    writeBytes(decalPath, decalCellPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Decal.esp", decalPath)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto view = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Decal.esp"})));
    const auto& refs = view.at("references");
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].at("decalDiffuse") == "Textures\\Decal.dds");
    CHECK(refs[0].at("decalWidth") == 128.0f);
    CHECK(refs[0].at("decalHeight") == 64.0f);
}

TEST_CASE("Native cell hides a reference moved to another cell") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Moved.esp", movedPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", root / "Base.esm"),
            pluginEntry("Source.esp", root / "Source.esp"),
            pluginEntry("Moved.esp", root / "Moved.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto oldCell = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000800:Base.esm"})));
    CHECK(oldCell.at("references").empty());

    const auto newCell = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000900:Moved.esp"})));
    REQUIRE(newCell.at("references").size() == 1);
    CHECK(newCell.at("references")[0].at("formKey") == "000801:Base.esm");
}

TEST_CASE("Native cell environment cache is dropped after a staged edit") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", root / "Base.esm"),
            pluginEntry("Source.esp", root / "Source.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(before.at("references").size() == 1);
    CHECK(before.at("references")[0].at("position").at("x") == 10.0f);

    REQUIRE(backend.setPlacedReferenceTransform(nlohmann::json::array({
        "000801:Base.esm", "Source.esp", 100.0, 200.0, 300.0, 1.5, 2.5, 3.5
    })).has_value());
    const auto afterFirst = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(afterFirst.at("references").size() == 1);
    CHECK(afterFirst.at("references")[0].at("position").at("x") == 100.0f);

    REQUIRE(backend.setPlacedReferenceTransform(nlohmann::json::array({
        "000801:Base.esm", "Source.esp", 500.0, 200.0, 300.0, 1.5, 2.5, 3.5
    })).has_value());
    const auto afterSecond = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(afterSecond.at("references").size() == 1);
    CHECK(afterSecond.at("references")[0].at("position").at("x") == 500.0f);
}

TEST_CASE("Native cell reads stay current after openPlugin adds a plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Moved.esp", movedPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", root / "Base.esm"),
            pluginEntry("Source.esp", root / "Source.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto oldCellView = nlohmann::json::array({"000800:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(oldCellView));
    REQUIRE(before.at("references").size() == 1);

    REQUIRE(backend.invoke(
        "backend.OpenPlugin", nlohmann::json::array({(root / "Moved.esp").string()})).has_value());

    const auto oldCell = parseStringResult(backend.getPlacedReferences(oldCellView));
    CHECK(oldCell.at("references").empty());
    const auto newCell = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000900:Moved.esp"})));
    REQUIRE(newCell.at("references").size() == 1);
    CHECK(newCell.at("references")[0].at("formKey") == "000801:Base.esm");
}

TEST_CASE("Native cell reads stay current after openPlugin reloads a changed file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", root / "Base.esm"),
            pluginEntry("Source.esp", root / "Source.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto oldCellView = nlohmann::json::array({"000800:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(oldCellView));
    REQUIRE(before.at("references").size() == 1);

    writeBytes(root / "Source.esp", movedPlugin());
    REQUIRE(backend.invoke(
        "backend.OpenPlugin", nlohmann::json::array({(root / "Source.esp").string()})).has_value());

    const auto oldAfter = parseStringResult(backend.getPlacedReferences(oldCellView));
    CHECK((oldAfter.contains("error") || oldAfter.at("references").empty()));
    const auto newCell = parseStringResult(
        backend.getPlacedReferences(nlohmann::json::array({"000900:Source.esp"})));
    REQUIRE(newCell.at("references").size() == 1);
    CHECK(newCell.at("references")[0].at("formKey") == "000801:Base.esm");
    CHECK(newCell.at("references")[0].at("position").at("x") == 10.0f);
}

TEST_CASE("Native cell reads the disk value again after discardPlugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", root / "Base.esm"),
            pluginEntry("Source.esp", root / "Source.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(before.at("references").size() == 1);

    REQUIRE(backend.setPlacedReferenceTransform(nlohmann::json::array({
        "000801:Base.esm", "Source.esp", 100.0, 200.0, 300.0, 1.5, 2.5, 3.5
    })).has_value());
    const auto pending = parseStringResult(backend.getPlacedReferences(cellView));
    CHECK(pending.at("references")[0].at("position").at("x") == 100.0f);

    REQUIRE(backend.invoke(
        "backend.DiscardPlugin", nlohmann::json::array({"Source.esp"})).has_value());
    const auto discarded = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(discarded.at("references").size() == 1);
    CHECK(discarded.at("references")[0].at("position").at("x") == 10.0f);
}

TEST_CASE("Native cell edit records an undo step and undo and redo restore the patch") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    })).has_value());
    const auto edited = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(edited.at("references").size() == 1);
    CHECK(edited.at("references")[0].at("position").at("x") == 100.0f);
    const auto afterEdit = parseStringResult(backend.getCellEditHistory(patchView));
    CHECK(afterEdit.at("undoCount") == 1);
    CHECK(afterEdit.at("redoCount") == 0);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    const auto undone = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(undone.at("references").size() == 1);
    CHECK(undone.at("references")[0].at("position").at("x") == 10.0f);
    const auto afterUndo = parseStringResult(backend.getCellEditHistory(patchView));
    CHECK(afterUndo.at("undoCount") == 0);
    CHECK(afterUndo.at("redoCount") == 1);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reread = fo4::read_plugin_file(root / "Patch.esp");
    CHECK(fo4::find_record(reread, fo4::FormKey::parse("000801:Base.esm")) == nullptr);
    CHECK(reread.header.masters.size() == 1);

    REQUIRE(backend.redoCellEdit(patchView).has_value());
    const auto redone = parseStringResult(backend.getPlacedReferences(cellView));
    CHECK(redone.at("references")[0].at("position").at("x") == 100.0f);
    const auto afterRedo = parseStringResult(backend.getCellEditHistory(patchView));
    CHECK(afterRedo.at("undoCount") == 1);
    CHECK(afterRedo.at("redoCount") == 0);
}

TEST_CASE("Native cell edit is refused on a protected plugin and leaves it unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    auto protectedPatch = pluginEntry("Patch.esp", root / "Patch.esp");
    protectedPatch["source"] = "data";
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esm", root / "Base.esm"),
            pluginEntry("Source.esp", root / "Source.esp"),
            protectedPatch
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(cellView));
    const auto refused = backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PluginReadOnly");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)) == before);
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"})))
              .at("undoCount") == 0);
}

TEST_CASE("Native cell edit refuses an unknown reference and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(cellView));
    const auto missing = backend.editPlacedReference(nlohmann::json::array({
        "000999:Base.esm", "Patch.esp", transformChange()
    }));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PlacedReferenceNotFound");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)) == before);
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"})))
              .at("undoCount") == 0);
}

TEST_CASE("Native cell undo refuses to revert a change staged after the edit") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    })).has_value());
    REQUIRE(backend.createInteriorCell(nlohmann::json::array({"Patch.esp", "Kitchen"})).has_value());

    const auto undo = backend.undoCellEdit(patchView);
    REQUIRE_FALSE(undo.has_value());
    CHECK(undo.error().type == "NothingToUndo");
    const auto kept = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(kept.at("references").size() == 1);
    CHECK(kept.at("references")[0].at("position").at("x") == 100.0f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell edit applies scale and editor id as one undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"scale", 2.5}, {"editorId", "RenamedRef"}}
    })).has_value());
    const auto edited = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(edited.at("references").size() == 1);
    CHECK(edited.at("references")[0].at("scale") == 2.5f);
    CHECK(edited.at("references")[0].at("editorId") == "RenamedRef");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    const auto undone = parseStringResult(backend.getPlacedReferences(cellView));
    REQUIRE(undone.at("references").size() == 1);
    CHECK(undone.at("references")[0].at("scale") == 1.0f);
    CHECK(undone.at("references")[0].at("editorId") != "RenamedRef");
}

TEST_CASE("Native cell exterior move into an unloaded cell is refused and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", exteriorCellPlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root, false)).has_value());

    const auto cellView = nlohmann::json::array({"000901:Base.esm"});
    const auto before = parseStringResult(backend.getPlacedReferences(cellView));
    auto farMove = transformChange();
    farMove["transform"]["x"] = 5000.0;
    const auto refused = backend.editPlacedReference(
        nlohmann::json::array({"000902:Base.esm", "Patch.esp", farMove}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PlacedReferenceTargetCellMissing");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)) == before);
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"Patch.esp"})))
              .at("undoCount") == 0);
}

TEST_CASE("Native cell new edit clears the redo step so a later redo cannot overwrite it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    })).has_value());
    REQUIRE(backend.undoCellEdit(patchView).has_value());

    auto newer = transformChange();
    newer["transform"]["x"] = 300.0;
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", newer
    })).has_value());
    const auto counts = parseStringResult(backend.getCellEditHistory(patchView));
    CHECK(counts.at("undoCount") == 1);
    CHECK(counts.at("redoCount") == 0);

    const auto redo = backend.redoCellEdit(patchView);
    REQUIRE_FALSE(redo.has_value());
    CHECK(redo.error().type == "NothingToRedo");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references")[0]
              .at("position").at("x") == 300.0f);
}

TEST_CASE("Native cell edit history is cleared by openPlugin, discardPlugin and a reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());
    writeBytes(root / "Moved.esp", movedPlugin());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto stageEdit = [&](fo4ide::plugin::RadiclePluginBackend& backend) {
        REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());
        REQUIRE(backend.editPlacedReference(nlohmann::json::array({
            "000801:Base.esm", "Patch.esp", transformChange()
        })).has_value());
        REQUIRE(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);
    };
    const auto checkCleared = [&](fo4ide::plugin::RadiclePluginBackend& backend) {
        CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
        const auto undo = backend.undoCellEdit(patchView);
        REQUIRE_FALSE(undo.has_value());
        CHECK(undo.error().type == "NothingToUndo");
    };

    SECTION("openPlugin of another file") {
        fo4ide::plugin::RadiclePluginBackend backend;
        stageEdit(backend);
        REQUIRE(backend.invoke("backend.OpenPlugin",
                               nlohmann::json::array({(root / "Moved.esp").string()})).has_value());
        checkCleared(backend);
    }

    SECTION("discardPlugin") {
        fo4ide::plugin::RadiclePluginBackend backend;
        stageEdit(backend);
        REQUIRE(backend.invoke("backend.DiscardPlugin",
                               nlohmann::json::array({"Patch.esp"})).has_value());
        checkCleared(backend);
    }

    SECTION("LoadMo2 reload that discards pending edits") {
        fo4ide::plugin::RadiclePluginBackend backend;
        stageEdit(backend);
        auto reload = patchEnvironment(root);
        reload["discardPending"] = true;
        REQUIRE(backend.invoke("environment.LoadMo2", reload).has_value());
        checkCleared(backend);
    }
}

TEST_CASE("Native cell edit history of patch A survives an edit to patch B so undo in A still works") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());
    writeBytes(root / "Patch2.esp", patchPlugin());

    auto environment = patchEnvironment(root);
    environment["plugins"].push_back(pluginEntry("Patch2.esp", root / "Patch2.esp"));

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto otherView = nlohmann::json::array({"Patch2.esp"});

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    })).has_value());

    SECTION("a cell edit in patch B does not clear the undo step recorded for patch A") {
        auto newer = transformChange();
        newer["transform"]["x"] = 300.0;
        REQUIRE(backend.editPlacedReference(nlohmann::json::array({
            "000801:Base.esm", "Patch2.esp", newer
        })).has_value());
        CHECK(parseStringResult(backend.getCellEditHistory(otherView)).at("undoCount") == 1);
    }

    SECTION("a non-cell edit in patch B does not clear the undo step recorded for patch A") {
        REQUIRE(backend.createInteriorCell(nlohmann::json::array({"Patch2.esp", "Kitchen"})).has_value());
    }

    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);
    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("redoCount") == 1);
}

TEST_CASE("Native undo after discarding patch A returns NothingToUndo without touching A and keeps patch B's undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());
    writeBytes(root / "Patch2.esp", patchPlugin());

    auto environment = patchEnvironment(root);
    environment["plugins"].push_back(pluginEntry("Patch2.esp", root / "Patch2.esp"));

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto otherView = nlohmann::json::array({"Patch2.esp"});

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    })).has_value());
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch2.esp", transformChange()
    })).has_value());

    REQUIRE(backend.invoke("backend.DiscardPlugin", patchView).has_value());
    const auto afterDiscard = parseStringResult(backend.getPlacedReferences(cellView));

    const auto undo = backend.undoCellEdit(patchView);
    REQUIRE_FALSE(undo.has_value());
    CHECK(undo.error().type == "NothingToUndo");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)) == afterDiscard);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);

    CHECK(parseStringResult(backend.getCellEditHistory(otherView)).at("undoCount") == 1);
    REQUIRE(backend.undoCellEdit(otherView).has_value());
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references")[0]
              .at("position").at("x") == 10.0f);
}

TEST_CASE("Native cell edit rejects malformed arguments and a NUL editor id without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto before = parseStringResult(backend.getPlacedReferences(cellView));
    const auto refusal = [&](nlohmann::json changes) {
        const auto result = backend.editPlacedReference(
            nlohmann::json::array({"000801:Base.esm", "Patch.esp", std::move(changes)}));
        return result.has_value() ? std::string{} : result.error().type;
    };

    CHECK(refusal(nullptr) == "PlacedEditArgumentsMissing");
    CHECK(refusal(nlohmann::json::object()) == "PlacedEditArgumentsMissing");
    CHECK(refusal("bad") == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"scale", "big"}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"editorId", 7}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"transform", {{"x", 1.0}}}}) == "PlacedTransformArgumentsMissing");
    CHECK(refusal({{"editorId", std::string("Bad\0Id", 6)}}) == "PlacedEditInvalid");

    CHECK(parseStringResult(backend.getPlacedReferences(cellView)) == before);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell edit with a null scale clears the override and undo restores each step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto scaleOf = [&] {
        return parseStringResult(backend.getPlacedReferences(cellView)).at("references")[0].at("scale");
    };
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"scale", 2.5}}
    })).has_value());
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"scale", nullptr}}
    })).has_value());
    CHECK(scaleOf() == 1.0f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(scaleOf() == 2.5f);
    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(scaleOf() == 1.0f);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell edit history lookup ignores ASCII case and refuses an unloaded plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", transformChange()
    })).has_value());
    CHECK(parseStringResult(backend.getCellEditHistory(nlohmann::json::array({"patch.esp"})))
              .at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(nlohmann::json::array({"patch.esp"})).has_value());
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references")[0]
              .at("position").at("x") == 10.0f);

    const auto unloaded = backend.getCellEditHistory(nlohmann::json::array({"Missing.esp"}));
    REQUIRE_FALSE(unloaded.has_value());
    CHECK(unloaded.error().type == "PluginNotLoaded");
}
