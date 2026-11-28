#include <cstdlib>
#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fo4ide/preview/SoftwareRaster.hpp>

using fo4ide::preview::Camera;
using fo4ide::preview::Rgba;
using fo4ide::preview::SoftwareRaster;
using fo4ide::preview::Vec3;

namespace {

constexpr Rgba kBackground{24, 26, 30, 255};
constexpr Rgba kRed{220, 30, 30, 255};
constexpr Rgba kBlue{30, 30, 220, 255};

Camera orthoCamera(Vec3 eye, Vec3 target, Vec3 up, double halfHeight = 1.0) {
    Camera camera;
    camera.eye = eye;
    camera.target = target;
    camera.up = up;
    camera.orthographic = true;
    camera.orthoHalfHeight = halfHeight;
    return camera;
}

bool sameColour(Rgba left, Rgba right) {
    return left.r == right.r && left.g == right.g && left.b == right.b;
}

void unshaded(SoftwareRaster& raster) {
    raster.setLight({0.0, 0.0, 1.0}, 1.0);
}

}  // namespace

TEST_CASE("Preview raster front view maps +X to the right and +Z up") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({0.0, -10.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));

    const auto origin = raster.project({0.0, 0.0, 0.0});
    CHECK(origin.visible);
    CHECK_THAT(origin.x, Catch::Matchers::WithinAbs(50.0, 1e-9));
    CHECK_THAT(origin.y, Catch::Matchers::WithinAbs(50.0, 1e-9));
    CHECK(raster.project({1.0, 0.0, 0.0}).x > 50.0);
    CHECK(raster.project({0.0, 0.0, 1.0}).y < 50.0);
}

TEST_CASE("Preview raster top view maps +X right and +Y up") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));

    CHECK(raster.project({1.0, 0.0, 0.0}).x > 50.0);
    CHECK(raster.project({0.0, 1.0, 0.0}).y < 50.0);
}

TEST_CASE("Preview raster right view from +X maps +Y to the right") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({10.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));

    CHECK(raster.project({0.0, 1.0, 0.0}).x > 50.0);
    CHECK(raster.project({0.0, 0.0, 1.0}).y < 50.0);
}

TEST_CASE("Preview raster perspective centres the target and hides points behind the eye") {
    SoftwareRaster raster(100, 100, kBackground);
    Camera camera;
    camera.eye = {0.0, -10.0, 0.0};
    camera.target = {0.0, 0.0, 0.0};
    raster.setCamera(camera);

    const auto centre = raster.project({0.0, 0.0, 0.0});
    CHECK(centre.visible);
    CHECK_THAT(centre.x, Catch::Matchers::WithinAbs(50.0, 1e-9));
    CHECK_THAT(centre.y, Catch::Matchers::WithinAbs(50.0, 1e-9));
    CHECK_FALSE(raster.project({0.0, -20.0, 0.0}).visible);
}

TEST_CASE("Preview raster fills a triangle with the expected footprint") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({0.0, -10.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));

    raster.fillTriangle({Vec3{-0.5, 0.0, -0.5}, Vec3{0.5, 0.0, -0.5}, Vec3{0.5, 0.0, 0.5}},
                        Rgba{200, 200, 200, 255});

    const auto drawn = raster.countDrawnPixels();
    CHECK(drawn > 1150);
    CHECK(drawn < 1350);
    CHECK_FALSE(sameColour(raster.pixel(70, 70), kBackground));
    CHECK(sameColour(raster.pixel(30, 30), kBackground));
}

TEST_CASE("Preview raster keeps the nearer triangle regardless of draw order") {
    const auto triangleAt = [](double y) {
        return std::array<Vec3, 3>{Vec3{-0.5, y, -0.5}, Vec3{0.5, y, -0.5}, Vec3{0.0, y, 0.5}};
    };
    const auto camera = orthoCamera({0.0, -10.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});

    SoftwareRaster farFirst(100, 100, kBackground);
    farFirst.setCamera(camera);
    unshaded(farFirst);
    farFirst.fillTriangle(triangleAt(1.0), kBlue);
    farFirst.fillTriangle(triangleAt(-1.0), kRed);
    CHECK(sameColour(farFirst.pixel(50, 50), kRed));

    SoftwareRaster nearFirst(100, 100, kBackground);
    nearFirst.setCamera(camera);
    unshaded(nearFirst);
    nearFirst.fillTriangle(triangleAt(-1.0), kRed);
    nearFirst.fillTriangle(triangleAt(1.0), kBlue);
    CHECK(sameColour(nearFirst.pixel(50, 50), kRed));
}

TEST_CASE("Preview raster clips triangles at the perspective near plane") {
    Camera camera;
    camera.eye = {0.0, -5.0, 0.0};
    camera.target = {0.0, 0.0, 0.0};
    camera.nearPlane = 1.0;

    SoftwareRaster partial(100, 100, kBackground);
    partial.setCamera(camera);
    partial.fillTriangle({Vec3{0.0, -10.0, 0.0}, Vec3{-1.0, 2.0, -1.0}, Vec3{1.0, 2.0, -1.0}},
                         Rgba{200, 200, 200, 255});
    CHECK(partial.countDrawnPixels() > 0);

    SoftwareRaster behind(100, 100, kBackground);
    behind.setCamera(camera);
    behind.fillTriangle({Vec3{0.0, -10.0, 0.0}, Vec3{-1.0, -12.0, -1.0}, Vec3{1.0, -12.0, -1.0}},
                        Rgba{200, 200, 200, 255});
    CHECK(behind.countDrawnPixels() == 0);
}

TEST_CASE("Preview raster draws a horizontal line across the centre row") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({0.0, -10.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));

    raster.drawLine({-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, kRed);
    CHECK(sameColour(raster.pixel(10, 50), kRed));
    CHECK(sameColour(raster.pixel(90, 50), kRed));
    CHECK(sameColour(raster.pixel(10, 10), kBackground));
}

// Detects: a wire line whose ends project far outside the image (a custom camera inside a cell) being stepped pixel by pixel along its whole projected length, which overflowed the step count and left the visible part undrawn.
TEST_CASE("Preview raster draws the visible part of a line whose ends are far off screen") {
    SoftwareRaster raster(64, 48, kBackground);
    Camera camera;
    camera.eye = {0.0, 0.0, 0.0};
    camera.target = {0.0, 1.0, 0.0};
    camera.up = {0.0, 0.0, 1.0};
    camera.nearPlane = 1.0;
    raster.setCamera(camera);

    raster.drawLine({-1.0e9, 2.0, 0.0}, {1.0e9, 2.0, 0.0}, kRed);
    for (int x = 0; x < 64; ++x) {
        INFO("column " << x);
        CHECK(sameColour(raster.pixel(x, 24), kRed));
    }
    CHECK(raster.countDrawnPixels() == 64);

    raster.drawLine({-1.0e9, 2.0, 500.0}, {1.0e9, 2.0, 500.0}, kBlue);
    CHECK(raster.countDrawnPixels() == 64);

    SoftwareRaster filled(64, 48, kBackground);
    filled.setCamera(camera);
    unshaded(filled);
    filled.fillTriangle({{{-1.0e12, 2.0, -1.0e12}, {1.0e12, 2.0, -1.0e12}, {0.0, 2.0, 1.0e12}}}, kBlue);
    CHECK(filled.countDrawnPixels() == 64 * 48);
    CHECK(sameColour(filled.pixel(0, 0), kBlue));
    CHECK(sameColour(filled.pixel(63, 47), kBlue));
}

TEST_CASE("Preview raster wire lines do not show through a nearer surface") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({0.0, -10.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
    unshaded(raster);

    raster.fillTriangle({Vec3{-0.5, -1.0, -0.5}, Vec3{0.5, -1.0, -0.5}, Vec3{0.0, -1.0, 0.5}}, kBlue);
    raster.drawLine({-1.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, kRed);
    CHECK(sameColour(raster.pixel(50, 50), kBlue));
}

TEST_CASE("Preview raster draws wire boxes and the ground grid") {
    SoftwareRaster raster(100, 100, kBackground);
    raster.setCamera(orthoCamera({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));

    raster.drawBox({-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5}, kRed);
    const auto boxPixels = raster.countDrawnPixels();
    CHECK(boxPixels > 100);

    fo4ide::preview::drawGroundGrid(raster, 0.0, 0.0, 2.0, 0.5, -0.5, fo4ide::preview::gridColour());
    CHECK(raster.countDrawnPixels() > boxPixels + 100);
}

TEST_CASE("Preview raster shades faces by their angle to the light") {
    const Rgba white{200, 200, 200, 255};

    SoftwareRaster top(100, 100, kBackground);
    top.setCamera(orthoCamera({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    top.fillTriangle({Vec3{-0.5, -0.5, 0.0}, Vec3{0.5, -0.5, 0.0}, Vec3{0.0, 0.5, 0.0}}, white);
    const auto lit = top.pixel(50, 58).r;

    SoftwareRaster side(100, 100, kBackground);
    side.setCamera(orthoCamera({10.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
    side.fillTriangle({Vec3{0.0, -0.5, -0.5}, Vec3{0.0, 0.5, -0.5}, Vec3{0.0, 0.0, 0.5}}, white);
    const auto grazing = side.pixel(50, 58).r;

    CHECK(lit > grazing);
    CHECK(static_cast<int>(lit) >= 170);
    CHECK(static_cast<int>(lit) <= 176);
    CHECK(static_cast<int>(grazing) >= 112);
    CHECK(static_cast<int>(grazing) <= 118);
}

TEST_CASE("Preview raster exports RGBA rows top to bottom") {
    SoftwareRaster raster(20, 10, kBackground);
    const auto bytes = raster.rgba();
    REQUIRE(bytes.size() == 20 * 10 * 4);
    CHECK(bytes[0] == kBackground.r);
    CHECK(bytes[3] == 255);
}

TEST_CASE("Preview record palette has a fallback and a legend") {
    CHECK_FALSE(sameColour(fo4ide::preview::colourForRecordType("STAT"),
                           fo4ide::preview::colourForRecordType("DOOR")));
    CHECK(sameColour(fo4ide::preview::colourForRecordType("UNKNOWN"),
                     fo4ide::preview::colourForRecordType("OTHER")));
    const auto legend = fo4ide::preview::recordTypeLegend();
    REQUIRE(legend.size() >= 10);
    CHECK(legend.front().label == "Static");
}
