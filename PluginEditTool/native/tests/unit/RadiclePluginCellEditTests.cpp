#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
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

const fo4::Subrecord* findSubrecord(const fo4::Record& record, std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& sub : record.subrecords) {
        if (sub.signature == wanted) return &sub;
    }
    return nullptr;
}

std::optional<std::vector<std::byte>> savedPatchXesp(fo4ide::plugin::RadiclePluginBackend& backend,
                                                    const std::filesystem::path& root) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(root / "Patch.esp");
    const auto* record = fo4::find_record(reloaded, fo4::FormKey::parse("000801:Base.esm"));
    REQUIRE(record != nullptr);
    const auto* xesp = findSubrecord(*record, "XESP");
    if (xesp == nullptr) return std::nullopt;
    return xesp->data;
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

TEST_CASE("Native cell editor id null clears the id and undo restores it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto editorIdOf = [&] {
        return parseStringResult(backend.getPlacedReferences(cellView)).at("references")[0].at("editorId");
    };
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"editorId", "RenamedRef"}}
    })).has_value());
    REQUIRE(editorIdOf() == "RenamedRef");

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"editorId", nullptr}}
    })).has_value());
    CHECK(editorIdOf() == "");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(editorIdOf() == "RenamedRef");
}

TEST_CASE("Native cell enable parent set and clear are separate undo steps") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"enableParent", {{"parent", "000810:Base.esm"}, {"flags", 1}}}}
    })).has_value());
    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"enableParent", nullptr}}
    })).has_value());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 2);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("redoCount") == 1);
    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell enable parent writes XESP to the saved patch and a null clear removes it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"enableParent", {{"parent", "000810:Base.esm"}, {"flags", 1}}}}
    })).has_value());
    const auto set = savedPatchXesp(backend, root);
    REQUIRE(set.has_value());
    REQUIRE(set->size() == 8);
    std::uint32_t parent = 0;
    std::memcpy(&parent, set->data(), 4);
    CHECK((parent & 0x00FFFFFFu) == 0x810u);
    CHECK(std::to_integer<int>((*set)[4]) == 1);

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"enableParent", nullptr}}
    })).has_value());
    CHECK_FALSE(savedPatchXesp(backend, root).has_value());
}

TEST_CASE("Native cell enable parent without flags saves flags 0") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    REQUIRE(backend.editPlacedReference(nlohmann::json::array({
        "000801:Base.esm", "Patch.esp", nlohmann::json{{"enableParent", {{"parent", "000810:Base.esm"}}}}
    })).has_value());
    const auto saved = savedPatchXesp(backend, root);
    REQUIRE(saved.has_value());
    REQUIRE(saved->size() == 8);
    CHECK(std::to_integer<int>((*saved)[4]) == 0);
}

TEST_CASE("Native cell enable parent refuses malformed or non-master parents without staging") {
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
    const auto refusal = [&](nlohmann::json enableParent) {
        const auto result = backend.editPlacedReference(nlohmann::json::array({
            "000801:Base.esm", "Patch.esp", nlohmann::json{{"enableParent", std::move(enableParent)}}
        }));
        return result.has_value() ? std::string{} : result.error().type;
    };

    CHECK(refusal(nlohmann::json::object()) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"flags", 1}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"parent", "000810"}}) == "InvalidFormKey");
    CHECK(refusal({{"parent", "000810:Base.esm"}, {"flags", 256}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"parent", "000810:Base.esm"}, {"flags", -1}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"parent", "000810:Base.esm"}, {"flags", 1.5}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"parent", "000810:Base.esm"}, {"flags", true}}) == "PlacedEditArgumentsMissing");
    CHECK(refusal({{"parent", "000800:Source.esp"}}) == "PlacedEditInvalid");

    CHECK(parseStringResult(backend.getPlacedReferences(cellView)) == before);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}
