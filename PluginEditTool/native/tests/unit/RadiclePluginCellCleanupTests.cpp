#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

namespace {

void writeReference(fo4::test::ByteWriter& out, std::uint32_t referenceId, std::optional<std::uint32_t> enableParent) {
    const auto reference = fo4::test::begin_record(out, "REFR", 0, referenceId);
    fo4::test::ByteWriter name;
    name.u32(0x00000802u);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::ByteWriter transform;
    for (int index = 0; index < 6; ++index) {
        transform.f32(10.0f + static_cast<float>(index));
    }
    fo4::test::write_subrecord(out, "DATA", transform.bytes);
    if (enableParent) {
        fo4::test::ByteWriter parent;
        parent.u32(*enableParent);
        parent.u32(0);
        fo4::test::write_subrecord(out, "XESP", parent.bytes);
    }
    fo4::test::end_record(out, reference);
}

std::vector<std::byte> linkedDuplicatePlugin() {
    fo4::test::ByteWriter out;
    writeHeader(out, {"Base.esm"});
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_block);
    const auto subblock = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_subblock);
    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000800u);
    const std::vector<std::byte> cellData{std::byte{1}, std::byte{0}};
    fo4::test::write_subrecord(out, "DATA", cellData);
    fo4::test::end_record(out, cell);
    const auto children = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_children);
    const auto temporary = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);
    writeReference(out, 0x01000901u, 0x01000902u);
    writeReference(out, 0x01000902u, std::nullopt);
    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

}  // namespace

TEST_CASE("Native cell cleanup plan lists a duplicate and leaves the patch unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.duplicatePlacedReference(nlohmann::json::array({"000801:Base.esm", "Patch.esp"})).has_value());
    REQUIRE(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 2);

    const auto plan = parseStringResult(backend.planCellCleanup(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", "dedupe"
    })));
    REQUIRE(plan.at("entries").size() == 1);
    CHECK(plan.at("entries")[0].at("reason") == "duplicate");
    CHECK(plan.at("entries")[0].at("keptFormKey").is_string());
    CHECK(plan.at("counts").at("duplicate") == 1);
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 2);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);
}

TEST_CASE("Native cell cleanup apply removes the duplicate and undo restores it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(backend.duplicatePlacedReference(nlohmann::json::array({"000801:Base.esm", "Patch.esp"})).has_value());

    const auto applied = parseStringResult(backend.applyCellCleanup(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", "dedupe"
    })));
    CHECK(applied.at("removed") == 1);
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 1);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 1);

    REQUIRE(backend.undoCellEdit(patchView).has_value());
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 2);
}

TEST_CASE("Native cell cleanup refuses a duplicate another reference links to and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", linkedDuplicatePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    REQUIRE(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 3);

    const auto refused = backend.applyCellCleanup(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", "dedupe"
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PlacedReferenceReferenced");
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 3);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell cleanup reports an unknown cell as CellNotFound and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto unknown = nlohmann::json::array({"000999:Base.esm", "Patch.esp", "dedupe"});
    const auto plan = backend.planCellCleanup(unknown);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error().type == "CellNotFound");

    const auto applied = backend.applyCellCleanup(unknown);
    REQUIRE_FALSE(applied.has_value());
    CHECK(applied.error().type == "CellNotFound");
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}

TEST_CASE("Native cell cleanup with nothing to remove applies cleanly and records no undo step") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", basePlugin());
    writeBytes(root / "Source.esp", sourcePlugin());
    writeBytes(root / "Patch.esp", patchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", patchEnvironment(root)).has_value());

    const auto cellView = nlohmann::json::array({"000800:Base.esm"});
    const auto patchView = nlohmann::json::array({"Patch.esp"});
    const auto applied = parseStringResult(backend.applyCellCleanup(nlohmann::json::array({
        "000800:Base.esm", "Patch.esp", "dedupe"
    })));
    CHECK(applied.at("removed") == 0);
    CHECK(applied.at("entries").empty());
    CHECK(parseStringResult(backend.getPlacedReferences(cellView)).at("references").size() == 1);
    CHECK(parseStringResult(backend.getCellEditHistory(patchView)).at("undoCount") == 0);
}
