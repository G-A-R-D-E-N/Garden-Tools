#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/nif/havok_names.hpp>
#include <fo4ide/assets/NifCollisionService.hpp>
#include <fo4ide/assets/NifService.hpp>

#include "../helpers/NifFixture.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_nif_collision_test_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void touch(const std::filesystem::path& path, std::string_view contents = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

constexpr const char* kTriangleObj = "v 0 0 0\nv 70 0 0\nv 0 70 0\nf 1 2 3\n";
constexpr const char* kTetraObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n";
constexpr double kBoxConvexRadius = 0.005;

nlohmann::json collisionInfo(const std::filesystem::path& nif) {
    auto result = fo4ide::assets::NifCollisionService{}.info(nif, {});
    REQUIRE(result.has_value());
    return nlohmann::json::parse(*result);
}

void importPlain(const std::filesystem::path& obj, const std::filesystem::path& nif,
                 fo4ide::assets::NifCollisionShape collision = fo4ide::assets::NifCollisionShape::none,
                 double massKg = 0.0) {
    fo4ide::assets::NifService service;
    fo4ide::assets::NifImportOptions options;
    options.collision = collision;
    options.massKg = massKg;
    REQUIRE(service.importObj(obj, nif, options).has_value());
}

}

TEST_CASE("NifService import writes a hull body with its mass") {
    // Regression: the hull choice and body mass were dropped, so the import wrote a static box instead.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, kTetraObj);
    const auto nif = root / "hull.nif";

    importPlain(obj, nif, fo4ide::assets::NifCollisionShape::hull, 12.0);

    const auto body = collisionInfo(nif).at("systems")[0].at("bodies")[0];
    CHECK(body.at("motion") == "dynamic");
    CHECK(std::abs(body.at("mass").get<double>() - 12.0) < 1e-3);
    CHECK(body.at("shape").at("vertices") == 4);
}

TEST_CASE("NifService import refuses a mass on a mesh collision and writes no NIF") {
    // Regression: a mesh with a mass was written as a dynamic body, which a compressed mesh cannot carry.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, kTetraObj);
    const auto nif = root / "mesh.nif";

    fo4ide::assets::NifService service;
    fo4ide::assets::NifImportOptions options;
    options.collision = fo4ide::assets::NifCollisionShape::mesh;
    options.massKg = 5.0;
    auto result = service.importObj(obj, nif, options);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "a mesh collision is static only; a body mass needs a box or hull");
    CHECK_FALSE(std::filesystem::exists(nif));
}

TEST_CASE("NifCollisionService info reads an imported box as a static body on layer 1") {
    // Regression: layer and motion were misread, so a static box came back as a dynamic body or lost its layer.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto nif = root / "box.nif";
    importPlain(obj, nif, fo4ide::assets::NifCollisionShape::box);

    const auto info = collisionInfo(nif);
    REQUIRE(info.at("systems").size() == 1);
    const auto body = info.at("systems")[0].at("bodies")[0];
    CHECK(body.at("motion") == "static");
    CHECK(body.at("layer") == 1);
    CHECK(body.at("shape").at("kind") == "convex");
    CHECK(info.at("findings").empty());
}

TEST_CASE("NifCollisionService set copies a donor body onto the named node") {
    // Regression: the donor body was attached to another node or dropped, so the target kept no collision.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    const auto donor = root / "donor.nif";
    importPlain(obj, target);
    importPlain(obj, donor, fo4ide::assets::NifCollisionShape::box);
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = target;
    request.node = "Scene Root";
    request.outputPath = out;
    request.fromDonor = true;
    request.donorPath = donor;
    auto result = fo4ide::assets::NifCollisionService{}.set(request);

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: collision set OK (in-process)\nSaved: " + out.string()));
    const auto info = collisionInfo(out);
    REQUIRE(info.at("systems").size() == 1);
    CHECK(info.at("systems")[0].at("node") == "Scene Root");
    CHECK(info.at("systems")[0].at("bodies").size() == 1);
}

TEST_CASE("NifCollisionService set generates a hull from the node's own shapes with its mass") {
    // Regression: generated collision lost the node's geometry or the mass, so the body came out empty or static.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, kTetraObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = target;
    request.node = "Scene Root";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::hull;
    request.massKg = 3.0;
    REQUIRE(fo4ide::assets::NifCollisionService{}.set(request).has_value());

    const auto body = collisionInfo(out).at("systems")[0].at("bodies")[0];
    CHECK(body.at("motion") == "dynamic");
    CHECK(std::abs(body.at("mass").get<double>() - 3.0) < 1e-3);
    CHECK(body.at("shape").at("vertices") == 4);
}

TEST_CASE("NifCollisionService set refuses to write over its source NIF") {
    // Regression: a Save As path equal to the source overwrote the original NIF with no backup.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto before = fileBytes(target);

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = target;
    request.node = "Scene Root";
    request.outputPath = target;
    request.shape = fo4ide::assets::NifCollisionShape::box;
    auto result = fo4ide::assets::NifCollisionService{}.set(request);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("source NIF") != std::string::npos);
    CHECK(fileBytes(target) == before);
    CHECK_FALSE(std::filesystem::exists(target.string() + ".bak"));
}

TEST_CASE("NifCollisionService keeps one backup of an existing output NIF") {
    // Regression: replacing an existing output NIF left no copy of the bytes it overwrote.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto out = root / "out.nif";
    touch(out, "old bytes");

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = target;
    request.node = "Scene Root";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::box;
    REQUIRE(fo4ide::assets::NifCollisionService{}.set(request).has_value());

    CHECK(fileBytes(root / "out.nif.bak") == "old bytes");
    CHECK(fileBytes(out) != "old bytes");
}

TEST_CASE("NifCollisionService remove clears the collision and leaves no lint finding") {
    // Regression: removing a body left its physics system or object behind, which lint reports as a finding.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto boxed = root / "boxed.nif";
    importPlain(obj, boxed, fo4ide::assets::NifCollisionShape::box);
    const auto out = root / "plain.nif";

    auto result = fo4ide::assets::NifCollisionService{}.remove(boxed, "Scene Root", out);

    REQUIRE(result.has_value());
    const auto info = collisionInfo(out);
    CHECK(info.at("systems").empty());
    CHECK(info.at("findings").empty());
}

TEST_CASE("NifCollisionService geometry places a body in metres under its node") {
    // Regression: the packfile's Havok metres were read as NIF units, so the overlay drew the body 70 times too small.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto nif = root / "box.nif";
    importPlain(obj, nif, fo4ide::assets::NifCollisionShape::box);

    auto result = fo4ide::assets::NifCollisionService{}.geometry(nif);

    REQUIRE(result.has_value());
    const auto geo = nlohmann::json::parse(*result);
    REQUIRE(geo.at("bodies").size() == 1);
    const auto& body = geo.at("bodies")[0];
    CHECK(body.at("transform").at("s") == 1.0);
    double maxX = 0.0;
    const auto& vertices = body.at("shapes")[0].at("vertices");
    for (std::size_t i = 0; i < vertices.size(); i += 3) maxX = std::max(maxX, vertices[i].get<double>());
    CHECK(std::abs(maxX - (70.0 / 69.99125 - kBoxConvexRadius)) < 1e-4);
    CHECK(body.at("shapes")[0].at("triangles").size() % 3 == 0);
}

TEST_CASE("NifCollisionService set refuses a negative mass instead of writing a static body") {
    // Regression: a negative mass fell through the encoder's positive-mass test and was written as a static body.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = target;
    request.node = "Scene Root";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::hull;
    request.massKg = -2.0;
    auto result = fo4ide::assets::NifCollisionService{}.set(request);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("body mass must be a finite number of kilograms") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));
}

// Detects: a COLL name the info reply drops, or a layer-row gap or out-of-range row that fails the whole reply, so the panel shows numbers only.
TEST_CASE("NifCollisionService info names a body's layer from the COLL rows it is given") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto nif = root / "box.nif";
    importPlain(obj, nif, fo4ide::assets::NifCollisionShape::box);
    const std::vector<fo4::nif::HavokLayerRow> named{{0, "L_UNIDENTIFIED"}, {1, "L_STATIC"}, {300, "L_OUT_OF_RANGE"}};
    const std::vector<fo4::nif::HavokLayerRow> gapped{{3, "L_ANIMSTATIC"}};

    auto withNames = fo4ide::assets::NifCollisionService{}.info(nif, named);
    REQUIRE(withNames.has_value());
    CHECK(nlohmann::json::parse(*withNames).at("systems")[0].at("bodies")[0].at("layerName") == "L_STATIC");

    auto withGap = fo4ide::assets::NifCollisionService{}.info(nif, gapped);
    REQUIRE(withGap.has_value());
    CHECK(nlohmann::json::parse(*withGap).at("systems")[0].at("bodies")[0].at("layerName") == "");
}

// Detects: a donor_body that is ignored, so the first donor body is always copied, or an index past the donor's bodies that still writes a NIF.
TEST_CASE("NifCollisionService set copies the donor body the request names") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto blank = root / "blank.nif";
    fo4ide::test::writeBytes(blank, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {1U, 2U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU)},
         {"NiNode", fo4ide::test::nodeBlock(2U, 2.0f, {3U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU, 2.0f)}},
        {"Tri", "Root", "Other"}));

    const auto withRoot = root / "with_root.nif";
    fo4ide::assets::NifCollisionSet first;
    first.nifPath = blank;
    first.node = "Root";
    first.outputPath = withRoot;
    first.shape = fo4ide::assets::NifCollisionShape::box;
    REQUIRE(fo4ide::assets::NifCollisionService{}.set(first).has_value());

    const auto donor = root / "donor.nif";
    fo4ide::assets::NifCollisionSet second;
    second.nifPath = withRoot;
    second.node = "Other";
    second.outputPath = donor;
    second.shape = fo4ide::assets::NifCollisionShape::box;
    second.massKg = 3.0;
    REQUIRE(fo4ide::assets::NifCollisionService{}.set(second).has_value());

    const auto donorSystems = collisionInfo(donor).at("systems");
    REQUIRE(donorSystems.size() == 2);
    const auto motion = [](const nlohmann::json& system) {
        return system.at("bodies")[0].at("motion").get<std::string>();
    };
    REQUIRE(motion(donorSystems[0]) != motion(donorSystems[1]));

    const auto copy = [&](std::optional<std::size_t> body, const std::filesystem::path& out) {
        fo4ide::assets::NifCollisionSet request;
        request.nifPath = target;
        request.node = "Scene Root";
        request.outputPath = out;
        request.fromDonor = true;
        request.donorPath = donor;
        request.donorBody = body;
        return fo4ide::assets::NifCollisionService{}.set(request);
    };

    const auto chosen = root / "chosen.nif";
    REQUIRE(copy(std::size_t{1}, chosen).has_value());
    CHECK(collisionInfo(chosen).at("systems")[0].at("bodies")[0].at("motion").get<std::string>() ==
          motion(donorSystems[1]));

    const auto defaulted = root / "defaulted.nif";
    REQUIRE(copy(std::nullopt, defaulted).has_value());
    CHECK(collisionInfo(defaulted).at("systems")[0].at("bodies")[0].at("motion").get<std::string>() ==
          motion(donorSystems[0]));

    const auto refused = root / "refused.nif";
    auto outOfRange = copy(std::size_t{2}, refused);
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().message.find("out of range") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(refused));

    fo4ide::assets::NifCollisionSet kind;
    kind.nifPath = target;
    kind.node = "Scene Root";
    kind.outputPath = root / "kind.nif";
    kind.shape = fo4ide::assets::NifCollisionShape::box;
    kind.donorBody = std::size_t{0};
    auto refusedKind = fo4ide::assets::NifCollisionService{}.set(kind);
    REQUIRE_FALSE(refusedKind.has_value());
    CHECK(refusedKind.error().message == "donor_body applies only to a donor_nif source");
}
