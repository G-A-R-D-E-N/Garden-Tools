#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/app/LayoutTarget.hpp>
#include <fo4ide/app/PreviewTarget.hpp>
#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/TempRoot.hpp"

using nlohmann::json;

namespace {

constexpr const char* kVaultDoor = "Meshes\\Interiors\\Vault\\Doors\\VltGearDoor\\VltGearDoor01.nif";

std::optional<std::filesystem::path> dataDirFromEnv() {
    const char* value = std::getenv("FO4_DATA_DIR");
    if (value == nullptr || *value == '\0') return std::nullopt;
    const std::filesystem::path path(value);
    if (!std::filesystem::is_regular_file(path / "Fallout4.esm")) return std::nullopt;
    return path;
}

json parsed(const fo4ide::Result<json>& result) {
    REQUIRE(result.has_value());
    return result->is_string() ? json::parse(result->get<std::string>()) : *result;
}

json parsedOrNull(const fo4ide::Result<json>& result) {
    if (!result) return nullptr;
    return result->is_string() ? json::parse(result->get<std::string>(), nullptr, false) : *result;
}

// Requires: a vanilla Fallout4.esm loaded through the real Radicle backend, with archives indexed under the same data folder.
struct Viewer {
    fo4ide_test::TempRoot temp{"fo4ide_cell_viewer_real"};
    fo4ide::plugin::RadiclePluginBackend backend;
    fo4ide::assets::NifService nif;
    fo4ide::assets::TextureResolver textures{temp.path() / "cache"};
    fo4ide::app::CellTarget cells{backend, nif, textures};
    fo4ide::app::PreviewTarget preview{cells};
    fo4ide::app::LayoutTarget layout{backend, fo4ide::app::nifGeometryProvider(nif, textures)};
    std::string cellId;
    std::string loadError;
    json cell;

    explicit Viewer(const std::filesystem::path& data) {
        backend.setDataDirectory(data);
        textures.setSessionRoots({data});
        const json environment{{"plugins", json::array({json{{"name", "Fallout4.esm"},
                                                              {"path", (data / "Fallout4.esm").string()},
                                                              {"source", "test"},
                                                              {"resolved", true}}})}};
        const auto loaded = backend.invoke("environment.LoadMo2", environment);
        if (!loaded) {
            loadError = "environment.LoadMo2 failed: " + loaded.error().message;
            return;
        }
        pickInteriorCell();
    }

    void pickInteriorCell() {
        constexpr std::size_t kMaxProbes = 150;
        std::size_t probes = 0;
        for (const char* query : {"Vault", ""}) {
            const auto hits = parsedOrNull(backend.invokeBackend("SearchRecords", json::array({query, "CELL"})));
            if (!hits.is_array()) { loadError = "SearchRecords CELL returned no array"; return; }
            for (const auto& hit : hits) {
                if (probes++ >= kMaxProbes) return;
                const auto key = hit.is_object() ? hit.value("FormKey", std::string{}) : std::string{};
                if (key.empty()) continue;
                const auto read = parsedOrNull(backend.getPlacedReferences(json::array({key})));
                if (!read.is_object() || !read.value("interior", false)) continue;
                const auto withModel = read.value("withModelCount", std::size_t{0});
                if (withModel < 20 || withModel > 400) continue;
                cellId = key;
                cell = read;
                return;
            }
        }
    }
};

Viewer* sharedViewer() {
    static const auto data = dataDirFromEnv();
    if (!data) return nullptr;
    static Viewer viewer(*data);
    return &viewer;
}

json options(json fields) {
    return json::array({std::move(fields)});
}

}  // namespace

// Constraint, detects: render_model_preview producing a blank or near-empty image for a real vanilla mesh because geometry, material or camera framing broke on real data.
TEST_CASE("CellViewerReal.PreviewRendersVanillaVaultDoorIntoCoveredPng", "[real-data][slow]") {
    auto* viewer = sharedViewer();
    if (viewer == nullptr) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to a folder holding Fallout4.esm");
    if (!viewer->loadError.empty()) FAIL(viewer->loadError);

    constexpr int side = 256;
    const auto reply = viewer->preview.invoke("RenderModel", options({{"model_path", kVaultDoor}, {"size", side}}));
    REQUIRE(reply.has_value());
    CHECK(reply->at("image").at("mimeType") == "image/png");
    CHECK(reply->at("image").at("base64").get<std::string>().rfind("iVBORw0KGgo", 0) == 0);

    const auto& report = reply->at("report");
    CHECK(report.at("modelPath") == kVaultDoor);
    CHECK(report.at("shapes").get<std::size_t>() >= 1);
    CHECK(report.at("triangles").get<std::size_t>() >= 12);
    const double coverage = report.at("pixelsDrawn").get<double>() / (static_cast<double>(side) * side);
    CHECK(coverage > 0.02);
    CHECK(coverage <= 1.0);
}

// Constraint, detects: a real cell whose references or models fail to flow from the Radicle backend into the cell render, leaving nothing drawn.
TEST_CASE("CellViewerReal.PreviewRendersVanillaInteriorCellWithDrawnReferences", "[real-data][slow]") {
    auto* viewer = sharedViewer();
    if (viewer == nullptr) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to a folder holding Fallout4.esm");
    if (!viewer->loadError.empty()) FAIL(viewer->loadError);
    if (viewer->cellId.empty()) SKIP("no interior cell with 20-400 modelled references found");

    const auto reply = viewer->preview.invoke("RenderCell", options({{"cell_id", viewer->cellId}, {"width", 320}, {"height", 240}}));
    REQUIRE(reply.has_value());
    CHECK(reply->at("image").at("base64").get<std::string>().rfind("iVBORw0KGgo", 0) == 0);
    const auto& report = reply->at("report");
    CHECK(report.at("cellFormKey") == viewer->cellId);
    CHECK(report.at("drawn").get<std::size_t>() > 0);
    CHECK(report.at("triangles").get<std::size_t>() > 0);
    CHECK(report.at("pixelsDrawn").get<double>() / (320.0 * 240.0) > 0.005);
}

// Constraint, detects: get_cell_layout_summary and check_overlaps returning empty or insane results for a real vanilla interior read through the real Radicle backend.
TEST_CASE("CellViewerReal.LayoutSummaryAndOverlapCheckCoverVanillaInteriorCell", "[real-data][slow]") {
    auto* viewer = sharedViewer();
    if (viewer == nullptr) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to a folder holding Fallout4.esm");
    if (!viewer->loadError.empty()) FAIL(viewer->loadError);
    if (viewer->cellId.empty()) SKIP("no interior cell with 20-400 modelled references found");

    const auto& references = viewer->cell.at("references");
    REQUIRE_FALSE(references.empty());
    std::set<std::string> formKeys;
    for (const auto& reference : references) formKeys.insert(reference.at("formKey").get<std::string>());

    const auto summary = parsed(viewer->preview.invoke("CellLayout", options({{"cell_id", viewer->cellId}})));
    CHECK(summary.at("references").at("total").get<std::size_t>() == references.size());
    CHECK(summary.at("references").at("withGeometry").get<std::size_t>() > 0);
    CHECK_FALSE(summary.at("counts").at("byType").empty());
    REQUIRE(summary.at("bounds").is_object());
    const auto& boundsMin = summary.at("bounds").at("min");
    const auto& boundsMax = summary.at("bounds").at("max");
    double diagonal = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double low = boundsMin.at(axis).get<double>();
        const double high = boundsMax.at(axis).get<double>();
        CHECK(std::isfinite(low));
        CHECK(std::isfinite(high));
        CHECK(high > low);
        diagonal += (high - low) * (high - low);
    }
    diagonal = std::sqrt(diagonal);
    CHECK(diagonal > 100.0);
    CHECK(diagonal < 200000.0);

    const auto overlaps = parsed(viewer->layout.invoke("CheckOverlaps", json::array({viewer->cellId})));
    CHECK(overlaps.at("cell_id") == viewer->cellId);
    CHECK(overlaps.at("checked").get<std::size_t>() > 0);
    CHECK(overlaps.at("checked").get<std::size_t>() + overlaps.at("skipped").size() == references.size());
    CHECK(overlaps.at("overlap_count").get<std::size_t>() == overlaps.at("overlaps").size());
    for (const auto& pair : overlaps.at("overlaps")) {
        CHECK(formKeys.contains(pair.at("a").get<std::string>()));
        CHECK(formKeys.contains(pair.at("b").get<std::string>()));
    }
}
