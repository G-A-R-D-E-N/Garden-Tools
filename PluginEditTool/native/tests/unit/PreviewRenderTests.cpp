#include <cmath>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/preview/PreviewRender.hpp>
#include <fo4ide/preview/PreviewVolumes.hpp>

using fo4ide::preview::Bounds;
using fo4ide::preview::CellData;
using fo4ide::preview::GeometryMap;
using fo4ide::preview::RenderOptions;
using fo4ide::preview::SoftwareRaster;
using nlohmann::json;

namespace {

constexpr double kHalfPi = 1.57079632679489661923;

json quadGeometry(double z = 0.0) {
    json shape = json::object();
    shape["verts"] = json::array({-1.0, -1.0, z, 1.0, -1.0, z, 1.0, 1.0, z, -1.0, 1.0, z});
    shape["tris"] = json::array({0, 1, 2, 0, 2, 3});
    json model = json::object();
    model["shapes"] = json::array({shape});
    return model;
}

json reference(std::string formKey, std::string baseType, std::string modelPath, double x, double y, double z) {
    json ref = json::object();
    ref["formKey"] = std::move(formKey);
    ref["baseType"] = std::move(baseType);
    ref["modelPath"] = std::move(modelPath);
    ref["isMarker"] = false;
    ref["position"] = {{"x", x}, {"y", y}, {"z", z}};
    ref["rotation"] = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
    ref["scale"] = 1.0;
    return ref;
}

json cellPayload(json references) {
    json root = json::object();
    root["cellFormKey"] = "00ABCD:Test.esp";
    root["references"] = std::move(references);
    return root;
}

json geometryPayload(const json& models) {
    json root = json::object();
    root["count"] = models.size();
    root["geometry"] = models;
    return root;
}

CellData parseCell(const json& root) {
    auto parsed = fo4ide::preview::parseCellReferences(root);
    REQUIRE(parsed.has_value());
    return *parsed;
}

GeometryMap parseGeo(const json& root) {
    auto parsed = fo4ide::preview::parseGeometry(root);
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("Preview render parses references and rejects a payload without them") {
    CHECK_FALSE(fo4ide::preview::parseCellReferences(json::object()).has_value());

    json ref = reference("000001:Test.esp", "STAT", "quad.nif", 10.0, 20.0, 30.0);
    ref["scale"] = 0.0;
    const CellData cell = parseCell(cellPayload(json::array({ref})));
    REQUIRE(cell.references.size() == 1);
    CHECK(cell.references[0].formKey == "000001:Test.esp");
    CHECK(cell.references[0].position.y == 20.0);
    CHECK_THAT(cell.references[0].pose.m[0][0], Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("Preview render applies Euler XYZ rotation and scale") {
    const auto rotatedZ = fo4ide::preview::apply(
        fo4ide::preview::poseAffine({0.0, 0.0, 0.0}, {0.0, 0.0, kHalfPi}, 2.0), {1.0, 0.0, 0.0});
    CHECK_THAT(rotatedZ.x, Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(rotatedZ.y, Catch::Matchers::WithinAbs(2.0, 1e-9));

    const auto rotatedX = fo4ide::preview::apply(
        fo4ide::preview::poseAffine({0.0, 0.0, 0.0}, {kHalfPi, 0.0, 0.0}, 2.0), {0.0, 1.0, 0.0});
    CHECK_THAT(rotatedX.z, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(rotatedX.y, Catch::Matchers::WithinAbs(0.0, 1e-9));
}

TEST_CASE("Preview render composes SCOL placements under the reference pose") {
    json ref = reference("000002:Test.esp", "SCOL", "scol.nif", 10.0, 0.0, 0.0);
    json part = json::object();
    part["modelPath"] = "part.nif";
    json placement = json::object();
    placement["x"] = 0.0;
    placement["y"] = 0.0;
    placement["z"] = 5.0;
    placement["rx"] = 0.0;
    placement["ry"] = 0.0;
    placement["rz"] = 0.0;
    placement["scale"] = 1.0;
    part["placements"] = json::array({placement});
    ref["scolParts"] = json::array({part});

    const CellData cell = parseCell(cellPayload(json::array({ref})));
    const GeometryMap geometry = parseGeo(geometryPayload({{"part.nif", quadGeometry()}}));
    const auto scan = fo4ide::preview::scanCell(cell, geometry, {});
    CHECK(scan.stats.triangles == 2);
    CHECK_THAT(scan.bounds.min.x, Catch::Matchers::WithinAbs(9.0, 1e-9));
    CHECK_THAT(scan.bounds.max.x, Catch::Matchers::WithinAbs(11.0, 1e-9));
    CHECK_THAT(scan.bounds.min.z, Catch::Matchers::WithinAbs(5.0, 1e-9));
    CHECK_THAT(scan.bounds.max.z, Catch::Matchers::WithinAbs(5.0, 1e-9));
}

TEST_CASE("Preview render places a billboard shape through its column-major world matrix") {
    json shape = quadGeometry()["shapes"][0];
    shape["verts"] = json::array({1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0});
    shape["tris"] = json::array({0, 1, 2});
    shape["billboard"] = true;
    shape["world"] = json::array({0.0, 2.0, 0.0, 0.0, -2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 5.0, 0.0, 0.0, 1.0});
    json model = json::object();
    model["shapes"] = json::array({shape});

    const CellData cell = parseCell(cellPayload(json::array({reference("000003:Test.esp", "STAT", "shape.nif", 0.0, 0.0, 0.0)})));
    const auto scan = fo4ide::preview::scanCell(cell, parseGeo(geometryPayload({{"shape.nif", model}})), {});
    REQUIRE(scan.stats.triangles == 1);
    CHECK_THAT(scan.bounds.min.x, Catch::Matchers::WithinAbs(5.0, 1e-9));
    CHECK_THAT(scan.bounds.max.x, Catch::Matchers::WithinAbs(5.0, 1e-9));
    CHECK_THAT(scan.bounds.min.y, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(scan.bounds.max.y, Catch::Matchers::WithinAbs(2.0, 1e-9));
}

TEST_CASE("Preview render orthographic presets place +X right and +Z up") {
    Bounds box;
    box.include(fo4ide::preview::Vec3{-10.0, -10.0, -10.0});
    box.include(fo4ide::preview::Vec3{10.0, 10.0, 10.0});
    const int width = 200;
    const int height = 200;

    fo4ide::preview::CameraRequest front;
    front.preset = "front";
    SoftwareRaster frontRaster(width, height, fo4ide::preview::backgroundColour());
    frontRaster.setCamera(fo4ide::preview::frameCamera(front, box, width, height));
    CHECK(frontRaster.project({5.0, 0.0, 5.0}).x > width / 2.0);
    CHECK(frontRaster.project({-5.0, 0.0, 5.0}).y < height / 2.0);

    fo4ide::preview::CameraRequest right;
    right.preset = "right";
    SoftwareRaster rightRaster(width, height, fo4ide::preview::backgroundColour());
    rightRaster.setCamera(fo4ide::preview::frameCamera(right, box, width, height));
    CHECK(rightRaster.project({0.0, 5.0, 0.0}).x > width / 2.0);
    CHECK(rightRaster.project({0.0, 0.0, 5.0}).y < height / 2.0);

    fo4ide::preview::CameraRequest top;
    top.preset = "top";
    SoftwareRaster topRaster(width, height, fo4ide::preview::backgroundColour());
    topRaster.setCamera(fo4ide::preview::frameCamera(top, box, width, height));
    CHECK(topRaster.project({5.0, 0.0, 0.0}).x > width / 2.0);
    CHECK(topRaster.project({0.0, 5.0, 0.0}).y < height / 2.0);
}

TEST_CASE("Preview render iso camera keeps every bounds corner on screen") {
    Bounds box;
    box.include(fo4ide::preview::Vec3{-300.0, -200.0, 0.0});
    box.include(fo4ide::preview::Vec3{300.0, 200.0, 120.0});
    const int width = 320;
    const int height = 240;

    fo4ide::preview::CameraRequest iso;
    const auto camera = fo4ide::preview::frameCamera(iso, box, width, height);
    CHECK_FALSE(camera.orthographic);
    SoftwareRaster raster(width, height, fo4ide::preview::backgroundColour());
    raster.setCamera(camera);
    for (const double x : {box.min.x, box.max.x}) {
        for (const double y : {box.min.y, box.max.y}) {
            for (const double z : {box.min.z, box.max.z}) {
                const auto point = raster.project({x, y, z});
                CHECK(point.visible);
                CHECK(point.x >= 0.0);
                CHECK(point.x <= width);
                CHECK(point.y >= 0.0);
                CHECK(point.y <= height);
            }
        }
    }
}

TEST_CASE("Preview render draws geometry and reports drawn and skipped references") {
    json marker = reference("000009:Test.esp", "MARKER", "marker.nif", 0.0, 0.0, 40.0);
    marker["isMarker"] = true;
    const CellData cell = parseCell(cellPayload(json::array({
        reference("000001:Test.esp", "STAT", "quad.nif", 0.0, 0.0, 0.0),
        reference("000002:Test.esp", "DOOR", "missing.nif", 50.0, 0.0, 0.0),
        marker,
    })));
    const GeometryMap geometry = parseGeo(geometryPayload({{"quad.nif", quadGeometry()}}));

    RenderOptions options;
    options.width = 160;
    options.height = 120;
    options.highlightKeys = {"000001:test.esp", "000777:Test.esp"};
    const auto image = fo4ide::preview::renderCell(cell, geometry, options);
    REQUIRE(image.has_value());
    CHECK(image->rgba.size() == static_cast<std::size_t>(160 * 120 * 4));
    CHECK(image->report["drawn"] == 2);
    CHECK(image->report["markers"] == 1);
    CHECK(image->report["skipped"]["missingGeometry"] == 1);
    CHECK(image->report["skipped"]["overBudget"] == 0);
    CHECK(image->report["highlights"][0]["found"] == true);
    CHECK(image->report["highlights"][0]["onScreen"] == true);
    CHECK(image->report["highlights"][1]["found"] == false);
    CHECK(image->report["legend"].size() >= 10);

    const auto png = fo4ide::preview::encodePngBase64(*image);
    REQUIRE(png.has_value());
    CHECK(png->rfind("iVBORw0KGgo", 0) == 0);
}

TEST_CASE("Preview render budget reports references beyond max_references") {
    const CellData cell = parseCell(cellPayload(json::array({
        reference("000001:Test.esp", "STAT", "quad.nif", 0.0, 0.0, 0.0),
        reference("000002:Test.esp", "STAT", "quad.nif", 4.0, 0.0, 0.0),
        reference("000003:Test.esp", "STAT", "quad.nif", 8.0, 0.0, 0.0),
    })));
    const GeometryMap geometry = parseGeo(geometryPayload({{"quad.nif", quadGeometry()}}));
    const auto scan = fo4ide::preview::scanCell(cell, geometry, fo4ide::preview::WalkBudget{1, 0});
    CHECK(scan.stats.walked == 1);
    CHECK(scan.stats.drawn == 1);
    CHECK(scan.stats.overBudget == 2);
}

TEST_CASE("Preview render draws a single model in the default iso view") {
    json shape = json::object();
    shape["verts"] = json::array({-5.0, 0.0, 0.0, 5.0, 0.0, 0.0, -5.0, 0.0, 10.0});
    shape["tris"] = json::array({0, 1, 2});
    json model = json::object();
    model["shapes"] = json::array({shape});
    const auto mesh = fo4ide::preview::parseModelMesh(model);

    RenderOptions options;
    options.width = 128;
    options.height = 128;
    const auto image = fo4ide::preview::renderModel(mesh, options);
    REQUIRE(image.has_value());
    CHECK(image->report["triangles"] == 1);
    CHECK(image->report["pixelsDrawn"].get<std::size_t>() > 0);
    CHECK(image->report["camera"]["preset"] == "iso");
}

TEST_CASE("Preview render model rejects an error entry and an empty model") {
    json errorEntry = json::object();
    errorEntry["error"] = "Not found (loose or in any indexed archive).";
    const auto errorMesh = fo4ide::preview::parseModelMesh(errorEntry);
    CHECK_FALSE(errorMesh.error.empty());
    CHECK_FALSE(fo4ide::preview::renderModel(errorMesh, RenderOptions{}).has_value());
    CHECK_FALSE(fo4ide::preview::renderModel(fo4ide::preview::ModelMesh{}, RenderOptions{}).has_value());
}

TEST_CASE("Preview render layout summary counts types, occupancy and horizontal surfaces") {
    const CellData cell = parseCell(cellPayload(json::array({
        reference("000001:Test.esp", "STAT", "quad.nif", 0.0, 0.0, 0.0),
        reference("000002:Test.esp", "STAT", "quad.nif", 100.0, 100.0, 0.0),
        reference("000003:Test.esp", "DOOR", "", -50.0, 20.0, 0.0),
    })));
    const GeometryMap geometry = parseGeo(geometryPayload({{"quad.nif", quadGeometry()}}));
    const json summary = fo4ide::preview::layoutSummary(cell, geometry);

    CHECK(summary["references"]["total"] == 3);
    CHECK(summary["references"]["missingGeometry"] == 1);
    CHECK(summary["counts"]["byType"]["STAT"] == 2);
    CHECK(summary["counts"]["byType"]["DOOR"] == 1);

    std::size_t occupied = 0;
    for (const auto& row : summary["occupancy"]["counts"]) {
        for (const auto& count : row) occupied += count.get<std::size_t>();
    }
    CHECK(occupied == 3);
    CHECK(summary["occupancy"]["counts"].size() == 16);
    CHECK(summary["largeStructures"].size() == 2);
    CHECK(summary["largeStructures"][0]["formKey"] == "000001:Test.esp");
    CHECK(summary["heights"]["floors"][0]["z"] == 0.0);
    CHECK(summary["heights"]["ceilings"].empty());
}

TEST_CASE("Preview render parses camera and render arguments") {
    const auto top = fo4ide::preview::parseRenderOptions(json{{"width", 100}, {"camera", "top"}}, 800, 600);
    REQUIRE(top.has_value());
    CHECK(top->width == 100);
    CHECK(top->height == 600);
    CHECK(top->camera.preset == "top");

    const auto custom = fo4ide::preview::parseRenderOptions(
        json{{"camera", {{"eye", json::array({0.0, -100.0, 50.0})},
                         {"target", json::array({0.0, 0.0, 0.0})},
                         {"fov", 60.0}}},
             {"highlight_keys", json::array({"000001:Test.esp"})}},
        800, 600);
    REQUIRE(custom.has_value());
    CHECK(custom->camera.preset == "custom");
    REQUIRE(custom->camera.fovDegrees.has_value());
    CHECK(*custom->camera.fovDegrees == 60.0);
    CHECK(custom->highlightKeys.size() == 1);

    CHECK_FALSE(fo4ide::preview::parseRenderOptions(json{{"width", 10}}, 800, 600).has_value());
    CHECK_FALSE(fo4ide::preview::parseRenderOptions(json{{"camera", "bogus"}}, 800, 600).has_value());
    CHECK_FALSE(fo4ide::preview::parseRenderOptions(
                    json{{"camera", {{"eye", json::array({1.0, 1.0, 1.0})}, {"target", json::array({1.0, 1.0, 1.0})}}}},
                    800, 600)
                    .has_value());
    CHECK_FALSE(fo4ide::preview::parseRenderOptions(json{{"camera", {{"fov", 5.0}}}}, 800, 600).has_value());
}

namespace {

json primitiveReference(std::string formKey, std::string baseEditorId, double x, json bounds, int type = 1) {
    json ref = reference(std::move(formKey), "STAT", "", x, 0.0, 0.0);
    ref["baseEditorId"] = std::move(baseEditorId);
    ref["primitive"] = {{"type", type}, {"bounds", std::move(bounds)}};
    return ref;
}

std::size_t countPixels(const fo4ide::preview::RenderedImage& image, fo4ide::preview::Rgba colour) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        if (image.rgba[i] == colour.r && image.rgba[i + 1] == colour.g && image.rgba[i + 2] == colour.b) ++count;
    }
    return count;
}

constexpr fo4ide::preview::Rgba kTriggerOrange{0xFF, 0x99, 0x00, 255};
constexpr fo4ide::preview::Rgba kRoomCyan{0x00, 0xFF, 0xFF, 255};

}  // namespace

// Constraint, detects: trigger or room primitive volumes missing from the render, mis-sized, or ignoring the layer switches.
TEST_CASE("Preview render draws trigger and room primitive volumes and honours the layers") {
    const CellData cell = parseCell(cellPayload(json::array({
        primitiveReference("000010:Test.esp", "TriggerBox", 0.0, {{"x", 32.0}, {"y", 16.0}, {"z", 8.0}}),
        primitiveReference("000011:Test.esp", "RoomMarker", 300.0, {{"x", 100.0}, {"y", 100.0}, {"z", 50.0}}),
    })));
    const GeometryMap geometry;

    const auto scan = fo4ide::preview::scanCell(cell, geometry, {});
    CHECK_THAT(scan.bounds.min.x, Catch::Matchers::WithinAbs(-32.0, 1e-9));
    CHECK_THAT(scan.bounds.max.x, Catch::Matchers::WithinAbs(400.0, 1e-9));
    CHECK_THAT(scan.bounds.max.z, Catch::Matchers::WithinAbs(50.0, 1e-9));

    RenderOptions options;
    options.width = 320;
    options.height = 240;
    const auto all = fo4ide::preview::renderCell(cell, geometry, options);
    REQUIRE(all.has_value());
    CHECK(all->report["volumes"]["trigger"] == 1);
    CHECK(all->report["volumes"]["room"] == 1);
    CHECK(countPixels(*all, kTriggerOrange) > 0);
    CHECK(countPixels(*all, kRoomCyan) > 0);

    options.layers.trigger = false;
    const auto noTrigger = fo4ide::preview::renderCell(cell, geometry, options);
    REQUIRE(noTrigger.has_value());
    CHECK(noTrigger->report["volumes"]["trigger"] == 0);
    CHECK(noTrigger->report["volumes"]["room"] == 1);
    CHECK(countPixels(*noTrigger, kTriggerOrange) == 0);
    CHECK(countPixels(*noTrigger, kRoomCyan) > 0);
}

// Constraint, detects: a room placeholder with no primitive and no model collapsing to a point or the wrong default edge instead of the 256-unit box.
TEST_CASE("Preview render sizes a primitive-less room volume from the default edge") {
    json room = reference("000012:Test.esp", "STAT", "Meshes\\RoomMarker.nif", 0.0, 0.0, 0.0);
    const CellData cell = parseCell(cellPayload(json::array({room})));
    const auto scan = fo4ide::preview::scanCell(cell, GeometryMap{}, {});
    CHECK_THAT(scan.bounds.min.x, Catch::Matchers::WithinAbs(-128.0, 1e-9));
    CHECK_THAT(scan.bounds.max.z, Catch::Matchers::WithinAbs(128.0, 1e-9));
}

// Constraint, detects: the marker or effect layer switch leaving unresolved-marker boxes, effect models or their framing in the image.
TEST_CASE("Preview render marker and effect layers hide unresolved markers and effect models") {
    const CellData markers = parseCell(cellPayload(json::array({
        reference("000001:Test.esp", "STAT", "quad.nif", 0.0, 0.0, 0.0),
        reference("000013:Test.esp", "SOUN", "", 80.0, 0.0, 0.0)})));
    const GeometryMap quad = parseGeo(geometryPayload({{"quad.nif", quadGeometry()}}));
    RenderOptions options;
    options.width = 160;
    options.height = 120;
    const auto shown = fo4ide::preview::renderCell(markers, quad, options);
    REQUIRE(shown.has_value());
    CHECK(shown->report["unresolvedMarkers"] == 1);
    options.layers.marker = false;
    const auto hidden = fo4ide::preview::renderCell(markers, quad, options);
    REQUIRE(hidden.has_value());
    CHECK(hidden->report["unresolvedMarkers"] == 0);
    CHECK(countPixels(*shown, fo4ide::preview::markerColour()) > 0);
    CHECK(countPixels(*hidden, fo4ide::preview::markerColour()) == 0);

    fo4ide::preview::LayerSet markerOff;
    markerOff.marker = false;
    CHECK(fo4ide::preview::scanCell(markers, quad, {}).bounds.max.x >= 80.0);
    CHECK(fo4ide::preview::scanCell(markers, quad, {}, markerOff).bounds.max.x < 10.0);

    const CellData effect = parseCell(cellPayload(json::array({
        reference("000014:Test.esp", "STAT", "Effects\\mist.nif", 0.0, 0.0, 0.0)})));
    const GeometryMap geometry = parseGeo(geometryPayload({{"Effects\\mist.nif", quadGeometry()}}));
    RenderOptions fx;
    fx.width = 160;
    fx.height = 120;
    const auto visibleFx = fo4ide::preview::renderCell(effect, geometry, fx);
    fx.layers.effect = false;
    const auto hiddenFx = fo4ide::preview::renderCell(effect, geometry, fx);
    REQUIRE(visibleFx.has_value());
    REQUIRE(hiddenFx.has_value());
    CHECK(hiddenFx->report["pixelsDrawn"].get<std::size_t>() < visibleFx->report["pixelsDrawn"].get<std::size_t>());
}

// Constraint, detects: the layers argument defaulting off, accepting unknown names or non-boolean values instead of failing loudly.
TEST_CASE("Preview render parses the layers argument") {
    const auto none = fo4ide::preview::parseRenderOptions(json::object(), 800, 600);
    REQUIRE(none.has_value());
    CHECK(none->layers.room);
    CHECK(none->layers.trigger);
    CHECK(none->layers.marker);
    CHECK(none->layers.effect);

    const auto some = fo4ide::preview::parseRenderOptions(json{{"layers", {{"trigger", false}, {"room", true}}}}, 800, 600);
    REQUIRE(some.has_value());
    CHECK_FALSE(some->layers.trigger);
    CHECK(some->layers.room);
    CHECK(some->layers.marker);

    CHECK_FALSE(fo4ide::preview::parseRenderOptions(json{{"layers", {{"bogus", true}}}}, 800, 600).has_value());
    CHECK_FALSE(fo4ide::preview::parseRenderOptions(json{{"layers", {{"room", "no"}}}}, 800, 600).has_value());
    CHECK_FALSE(fo4ide::preview::parseRenderOptions(json{{"layers", "room"}}, 800, 600).has_value());
}

namespace {

using fo4ide::preview::Rgba;
using fo4ide::preview::Vec3;
using fo4ide::preview::Volume;
using fo4ide::preview::VolumeShape;

Volume primitiveVolume(int type, json bounds) {
    const CellData cell = parseCell(cellPayload(json::array({
        primitiveReference("000020:Test.esp", "TriggerBox", 0.0, std::move(bounds), type)})));
    const auto infos = fo4ide::preview::classifyReferences(cell, GeometryMap{});
    REQUIRE(infos.size() == 1);
    REQUIRE(infos[0].volume.has_value());
    return *infos[0].volume;
}

SoftwareRaster drawnVolume(const Volume& volume, const char* preset) {
    Bounds bounds;
    for (const Vec3& corner : volume.corners) bounds.include(corner);
    fo4ide::preview::CameraRequest request;
    request.preset = preset;
    SoftwareRaster raster(240, 240, fo4ide::preview::backgroundColour());
    raster.setCamera(fo4ide::preview::frameCamera(request, bounds, 240, 240));
    fo4ide::preview::drawVolume(raster, volume);
    return raster;
}

bool paintedNear(const SoftwareRaster& raster, Vec3 point, Rgba colour) {
    const auto screen = raster.project(point);
    const int cx = static_cast<int>(std::floor(screen.x));
    const int cy = static_cast<int>(std::floor(screen.y));
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Rgba pixel = raster.pixel(cx + dx, cy + dy);
            if (pixel.r == colour.r && pixel.g == colour.g && pixel.b == colour.b) return true;
        }
    }
    return false;
}

}  // namespace

// Constraint, detects: a sphere primitive drawn as its bounding box so the corners light up instead of three axis rings.
TEST_CASE("Preview render draws a sphere primitive as three rings, not a box") {
    const Volume volume = primitiveVolume(2, {{"x", 50.0}, {"y", 20.0}, {"z", 10.0}});
    CHECK(volume.shape == VolumeShape::sphere);
    CHECK(volume.half.y == 50.0);
    const SoftwareRaster front = drawnVolume(volume, "front");
    CHECK(paintedNear(front, {50.0, 0.0, 0.0}, kTriggerOrange));
    CHECK(paintedNear(front, {0.0, 0.0, 50.0}, kTriggerOrange));
    CHECK_FALSE(paintedNear(front, {50.0, -50.0, 50.0}, kTriggerOrange));
}

// Constraint, detects: an ellipsoid primitive drawn as a box or as a sphere that ignores its three semi-axes.
TEST_CASE("Preview render draws an ellipsoid primitive with its own semi-axes") {
    const Volume volume = primitiveVolume(5, {{"x", 60.0}, {"y", 20.0}, {"z", 40.0}});
    CHECK(volume.shape == VolumeShape::ellipsoid);
    const SoftwareRaster front = drawnVolume(volume, "front");
    CHECK(paintedNear(front, {60.0, 0.0, 0.0}, kTriggerOrange));
    CHECK(paintedNear(front, {0.0, 0.0, 40.0}, kTriggerOrange));
    CHECK_FALSE(paintedNear(front, {60.0, -20.0, 40.0}, kTriggerOrange));
    CHECK_FALSE(paintedNear(front, {30.0, 0.0, 40.0}, kTriggerOrange));
}

// Constraint, detects: a plane primitive (bounds z of 0) falling back to the 64-unit default box or drawing a solid box instead of a flat rectangle.
TEST_CASE("Preview render draws a plane primitive as a flat rectangle") {
    const Volume volume = primitiveVolume(3, {{"x", 50.0}, {"y", 30.0}, {"z", 0.0}});
    CHECK(volume.shape == VolumeShape::plane);
    CHECK(volume.corners[0].z == volume.corners[4].z);
    CHECK(volume.corners[7].x == 50.0);
    const SoftwareRaster top = drawnVolume(volume, "top");
    CHECK(paintedNear(top, {50.0, 30.0, 0.0}, kTriggerOrange));
    CHECK(paintedNear(top, {50.0, 0.0, 0.0}, kTriggerOrange));
    CHECK(paintedNear(top, {0.0, -30.0, 0.0}, kTriggerOrange));
    CHECK_FALSE(paintedNear(top, {0.0, 0.0, 0.0}, kTriggerOrange));
}

// Constraint, detects: a line primitive drawn as a box so the off-diagonal corners light up instead of a single segment.
TEST_CASE("Preview render draws a line primitive as one segment") {
    const Volume volume = primitiveVolume(4, {{"x", 40.0}, {"y", 0.0}, {"z", 30.0}});
    CHECK(volume.shape == VolumeShape::line);
    const SoftwareRaster front = drawnVolume(volume, "front");
    CHECK(paintedNear(front, {20.0, 0.0, 15.0}, kTriggerOrange));
    CHECK(paintedNear(front, {-20.0, 0.0, -15.0}, kTriggerOrange));
    CHECK_FALSE(paintedNear(front, {40.0, 0.0, -30.0}, kTriggerOrange));
    CHECK_FALSE(paintedNear(front, {-40.0, 0.0, 30.0}, kTriggerOrange));
}

// Constraint, detects: a box primitive losing its edges when shapes were added, or a sphere with unusable bounds producing no volume instead of the default box.
TEST_CASE("Preview render keeps box primitives as boxes and falls back to the default box for unusable bounds") {
    const Volume box = primitiveVolume(1, {{"x", 32.0}, {"y", 16.0}, {"z", 8.0}});
    CHECK(box.shape == VolumeShape::box);
    const SoftwareRaster front = drawnVolume(box, "front");
    CHECK(paintedNear(front, {32.0, 0.0, 8.0}, kTriggerOrange));
    CHECK(paintedNear(front, {-32.0, 0.0, -8.0}, kTriggerOrange));

    const Volume empty = primitiveVolume(2, {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}});
    CHECK(empty.shape == VolumeShape::box);
    CHECK(empty.half.x == 32.0);
}

// Constraint, detects: a disabled room layer still stretching the render_cell_preview frame, and layout summary bounds inflated by 256-unit placeholder boxes.
TEST_CASE("Preview render scan honours layers and layout bounds ignore placeholder volumes") {
    const CellData cell = parseCell(cellPayload(json::array({
        primitiveReference("000010:Test.esp", "TriggerBox", 0.0, {{"x", 32.0}, {"y", 16.0}, {"z", 8.0}}),
        primitiveReference("000011:Test.esp", "RoomMarker", 300.0, {{"x", 100.0}, {"y", 100.0}, {"z", 50.0}}),
    })));
    fo4ide::preview::LayerSet noRoom;
    noRoom.room = false;
    const auto scan = fo4ide::preview::scanCell(cell, GeometryMap{}, {}, noRoom);
    CHECK_THAT(scan.bounds.max.x, Catch::Matchers::WithinAbs(32.0, 1e-9));

    const CellData layout = parseCell(cellPayload(json::array({
        reference("000001:Test.esp", "STAT", "quad.nif", 0.0, 0.0, 0.0),
        reference("000012:Test.esp", "STAT", "Meshes\\RoomMarker.nif", 1000.0, 0.0, 0.0)})));
    const json summary = fo4ide::preview::layoutSummary(layout, parseGeo(geometryPayload({{"quad.nif", quadGeometry()}})));
    CHECK_THAT(summary["bounds"]["max"][0].get<double>(), Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK_THAT(summary["bounds"]["min"][0].get<double>(), Catch::Matchers::WithinAbs(-1.0, 1e-9));
}
