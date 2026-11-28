#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/nif/havok_names.hpp>
#include <fo4/nif/hknp_encode.hpp>
#include <fo4/nif/hknp_packfile.hpp>
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

constexpr std::uint32_t kBoxMaterialCrc = 0x064003D4U;

std::vector<std::byte> twoBodyPackfile() {
    fo4::nif::HknpSystemBody first;
    first.shape = fo4::nif::make_hknp_box({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, kBoxMaterialCrc);
    fo4::nif::HknpSystemBody second;
    second.shape = fo4::nif::make_hknp_box({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, kBoxMaterialCrc);
    second.mass = 2.0f;
    return fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_system({first, second}));
}

std::vector<std::byte> objectBytes(std::int32_t target, std::int32_t system, std::uint32_t bodyId) {
    std::vector<std::byte> out;
    fo4ide::test::putUint(out, static_cast<std::uint32_t>(target), 4);
    fo4ide::test::putUint(out, 0x80, 2);
    fo4ide::test::putUint(out, static_cast<std::uint32_t>(system), 4);
    fo4ide::test::putUint(out, bodyId, 4);
    return out;
}

std::vector<std::byte> systemBytes(const std::vector<std::byte>& packfile) {
    std::vector<std::byte> out;
    fo4ide::test::putUint(out, packfile.size(), 4);
    out.insert(out.end(), packfile.begin(), packfile.end());
    return out;
}

void writeSharedSystemNif(const std::filesystem::path& path, std::uint32_t firstBody = 0, std::uint32_t secondBody = 1) {
    fo4ide::test::writeBytes(path, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(0U, 0.0f, {1U, 2U})},
         {"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {})},
         {"NiNode", fo4ide::test::nodeBlock(2U, 5.0f, {})},
         {"bhkNPCollisionObject", objectBytes(1, 5, firstBody)},
         {"bhkNPCollisionObject", objectBytes(2, 5, secondBody)},
         {"bhkPhysicsSystem", systemBytes(twoBodyPackfile())}},
        {"Root", "A", "B"}));
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

TEST_CASE("NifCollisionService refuses an existing output until overwrite is allowed") {
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
    auto refused = fo4ide::assets::NifCollisionService{}.set(request);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "OutputExists");
    CHECK(fileBytes(out) == "old bytes");
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
    request.overwrite = true;
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

    auto result = fo4ide::assets::NifCollisionService{}.remove(boxed, "Scene Root", out, false);

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

TEST_CASE("NifCollisionService info and geometry keep every owner of a shared physics system") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto shared = root / "shared.nif";
    writeSharedSystemNif(shared);

    const auto info = collisionInfo(shared);
    REQUIRE(info.at("systems").size() == 2);
    CHECK(info.at("systems")[0].at("node") == "A");
    CHECK(info.at("systems")[0].at("bodyId") == 0);
    CHECK(info.at("systems")[1].at("node") == "B");
    CHECK(info.at("systems")[1].at("bodyId") == 1);
    CHECK(info.at("systems")[1].at("owned") == true);

    auto geometry = fo4ide::assets::NifCollisionService{}.geometry(shared);
    REQUIRE(geometry.has_value());
    const auto bodies = nlohmann::json::parse(*geometry).at("bodies");
    REQUIRE(bodies.size() == 2);
    CHECK(bodies[0].at("node") == "A");
    CHECK(bodies[0].at("transform").at("t")[0] == 0.0);
    CHECK(bodies[1].at("node") == "B");
    CHECK(bodies[1].at("transform").at("t")[0] == 5.0);
}

TEST_CASE("NifCollisionService donor_body reaches the second owner of a shared physics system") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto shared = root / "shared.nif";
    writeSharedSystemNif(shared);

    const auto copy = [&](std::size_t body, const std::filesystem::path& out) {
        fo4ide::assets::NifCollisionSet request;
        request.nifPath = target;
        request.node = "Scene Root";
        request.outputPath = out;
        request.fromDonor = true;
        request.donorPath = shared;
        request.donorBody = body;
        return fo4ide::assets::NifCollisionService{}.set(request);
    };

    const auto second = root / "second.nif";
    REQUIRE(copy(1, second).has_value());
    CHECK(collisionInfo(second).at("systems")[0].at("bodyId") == 1);

    auto outOfRange = copy(2, root / "refused.nif");
    REQUIRE_FALSE(outOfRange.has_value());
    CHECK(outOfRange.error().message.find("out of range") != std::string::npos);
}

TEST_CASE("NifCollisionService refuses a node name that two nodes share and writes nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto twin = root / "twin.nif";
    fo4ide::test::writeBytes(twin, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(0U, 0.0f, {1U, 2U})},
         {"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {})},
         {"NiNode", fo4ide::test::nodeBlock(1U, 3.0f, {})}},
        {"Root", "CollisionNode"}));
    const auto before = fileBytes(twin);
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = twin;
    request.node = "CollisionNode";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::box;
    auto set = fo4ide::assets::NifCollisionService{}.set(request);
    REQUIRE_FALSE(set.has_value());
    CHECK(set.error().message.find("more than one node is named 'CollisionNode'") != std::string::npos);

    auto removed = fo4ide::assets::NifCollisionService{}.remove(twin, "CollisionNode", out, false);
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().message.find("more than one node is named 'CollisionNode'") != std::string::npos);

    CHECK_FALSE(std::filesystem::exists(out));
    CHECK(fileBytes(twin) == before);
}

TEST_CASE("NifCollisionService set reports a published NIF whose own check already fails") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto source = root / "stale.nif";
    auto repeated = fo4ide::test::triangleShapeBlock(0xFFFFFFFFU);
    repeated[repeated.size() - 2] = std::byte{1};
    fo4ide::test::writeBytes(source, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {1U})},
         {"BSTriShape", repeated}},
        {"Tri", "Root"}));
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = source;
    request.node = "Root";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::box;
    auto result = fo4ide::assets::NifCollisionService{}.set(request);

    REQUIRE(result.has_value());
    CHECK(result->starts_with("RESULT: collision set PUBLISHED WITH FAILED CHECKS (in-process)\nSaved: " + out.string()));
    CHECK(result->find("[FAIL] no triangles name a vertex twice") != std::string::npos);
    CHECK(std::filesystem::exists(out));
}

TEST_CASE("NifCollisionService refuses a source NIF replaced at the same path after it was inspected") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto tri = root / "tri.obj";
    touch(tri, kTriangleObj);
    const auto tetra = root / "tetra.obj";
    touch(tetra, kTetraObj);
    const auto target = root / "target.nif";
    importPlain(tri, target);
    const auto inspected = collisionInfo(target).at("source").get<std::string>();
    std::filesystem::remove(target);
    importPlain(tetra, target);
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = target;
    request.node = "Scene Root";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::box;
    request.expectedSource = inspected;
    auto refused = fo4ide::assets::NifCollisionService{}.set(request);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("changed on disk") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));

    auto removed = fo4ide::assets::NifCollisionService{}.remove(target, "Scene Root", out, false, inspected);
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().message.find("changed on disk") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));

    request.expectedSource = collisionInfo(target).at("source").get<std::string>();
    REQUIRE(fo4ide::assets::NifCollisionService{}.set(request).has_value());
    CHECK(std::filesystem::exists(out));
}

TEST_CASE("NifCollisionService refuses a donor reordered at the same path after it was listed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, kTriangleObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);
    const auto shared = root / "shared.nif";
    writeSharedSystemNif(shared);
    const auto listed = collisionInfo(shared).at("source").get<std::string>();
    writeSharedSystemNif(shared, 1, 0);

    const auto copy = [&](const std::string& token, const std::filesystem::path& out) {
        fo4ide::assets::NifCollisionSet request;
        request.nifPath = target;
        request.node = "Scene Root";
        request.outputPath = out;
        request.fromDonor = true;
        request.donorPath = shared;
        request.donorBody = std::size_t{1};
        request.expectedDonorSource = token;
        return fo4ide::assets::NifCollisionService{}.set(request);
    };

    const auto refused = root / "refused.nif";
    auto stale = copy(listed, refused);
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().message.find("changed on disk") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(refused));

    const auto chosen = root / "chosen.nif";
    REQUIRE(copy(collisionInfo(shared).at("source").get<std::string>(), chosen).has_value());
    CHECK(collisionInfo(chosen).at("systems")[0].at("bodyId") == 0);
}

TEST_CASE("NifCollisionService set refuses a positive mass that a float cannot hold and keeps zero and dynamic masses") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, kTetraObj);
    const auto target = root / "target.nif";
    importPlain(obj, target);

    const auto withMass = [&](double massKg, const std::filesystem::path& out) {
        fo4ide::assets::NifCollisionSet request;
        request.nifPath = target;
        request.node = "Scene Root";
        request.outputPath = out;
        request.shape = fo4ide::assets::NifCollisionShape::hull;
        request.massKg = massKg;
        return fo4ide::assets::NifCollisionService{}.set(request);
    };

    const auto underflow = root / "underflow.nif";
    auto tiny = withMass(1e-50, underflow);
    REQUIRE_FALSE(tiny.has_value());
    CHECK(tiny.error().message.find("too small or too large") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(underflow));

    const auto overflow = root / "overflow.nif";
    auto huge = withMass(1e39, overflow);
    REQUIRE_FALSE(huge.has_value());
    CHECK_FALSE(std::filesystem::exists(overflow));

    const auto zero = root / "zero.nif";
    REQUIRE(withMass(0.0, zero).has_value());
    CHECK(collisionInfo(zero).at("systems")[0].at("bodies")[0].at("motion") == "static");

    const auto dynamic = root / "dynamic.nif";
    REQUIRE(withMass(3.0, dynamic).has_value());
    CHECK(collisionInfo(dynamic).at("systems")[0].at("bodies")[0].at("motion") == "dynamic");
}

TEST_CASE("NifCollisionService set refuses a non-finite vertex after a finite one and writes nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto bad = root / "nan.nif";
    fo4ide::test::writeBytes(bad, fo4ide::test::nifFile(
        {{"NiNode", fo4ide::test::nodeBlock(1U, 0.0f, {1U, 2U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU)},
         {"NiNode", fo4ide::test::nodeBlock(2U, std::numeric_limits<float>::quiet_NaN(), {3U})},
         {"BSTriShape", fo4ide::test::triangleShapeBlock(0xFFFFFFFFU)}},
        {"Tri", "Root", "Bad"}));
    const auto out = root / "out.nif";

    fo4ide::assets::NifCollisionSet request;
    request.nifPath = bad;
    request.node = "Root";
    request.outputPath = out;
    request.shape = fo4ide::assets::NifCollisionShape::box;
    auto result = fo4ide::assets::NifCollisionService{}.set(request);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("not finite") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out));
}
