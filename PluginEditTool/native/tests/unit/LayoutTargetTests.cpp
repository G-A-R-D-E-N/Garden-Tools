#include <initializer_list>
#include <map>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <fo4ide/app/LayoutTarget.hpp>

using Catch::Matchers::WithinAbs;
using fo4ide::app::GeometryProvider;
using fo4ide::app::LayoutTarget;
using nlohmann::json;

namespace {

class StubBackend final : public fo4ide::plugin::IPluginBackend {
public:
    json rows = json::array();
    json references = json::array();
    std::map<std::string, json> details;
    mutable json listArgs = json::array();
    json buildArgs = json::array();
    json editArgs = json::array();
    int buildCalls = 0;
    int editCalls = 0;

    fo4ide::plugin::Capabilities capabilities() const noexcept override { return {}; }

    fo4ide::Result<json> invoke(std::string_view, const json&) override {
        return std::unexpected(fo4ide::Error{"Unused", "not used by layout tests"});
    }

    fo4ide::Result<json> listModelBases(const json& args) const override {
        listArgs = args;
        return json(json{{"rows", rows}}.dump());
    }

    std::string cell = "000100:Test.esm";

    fo4ide::Result<json> getPlacedReferences(const json& args) const override {
        const auto cellId = args.at(0).get<std::string>();
        if (cellId != cell) return json(json{{"error", "Cell not found: " + cellId}}.dump());
        return json(json{{"references", references}}.dump());
    }

    fo4ide::Result<json> getReferenceDetails(const json& args) const override {
        const auto key = args.at(0).get<std::string>();
        const auto found = details.find(key);
        if (found == details.end()) return json(json{{"error", "Reference not found: " + key}}.dump());
        return json(found->second.dump());
    }
    fo4ide::Result<json> buildRoom(const json& args) override {
        ++buildCalls;
        buildArgs = args;
        return json{{"forwarded", args}};
    }

    fo4ide::Result<json> editPlacedReferences(const json& args) override {
        ++editCalls;
        editArgs = args;
        return json{{"plugin", args.at(0)}, {"edited", args.at(1).size()}};
    }
};

struct Models {
    std::map<std::string, json> geometry;
    int calls{0};
};

GeometryProvider providerFor(Models& models) {
    return [&models](const std::string& path) -> fo4ide::Result<json> {
        ++models.calls;
        const auto found = models.geometry.find(path);
        if (found == models.geometry.end()) {
            return std::unexpected(fo4ide::Error{"ModelNotFound", "no model " + path});
        }
        return found->second;
    };
}

json boxGeometry(double x0, double y0, double z0, double x1, double y1, double z1) {
    return json{{"fo4", false},
                {"shapes", json::array({json{{"name", "box"},
                                             {"verts", json::array({x0, y0, z0, x1, y1, z1})},
                                             {"tris", json::array()}}})}};
}

void requireVec3(const json& actual, double x, double y, double z) {
    REQUIRE(actual.is_array());
    REQUIRE(actual.size() == 3);
    CHECK_THAT(actual[0].get<double>(), WithinAbs(x, 1e-6));
    CHECK_THAT(actual[1].get<double>(), WithinAbs(y, 1e-6));
    CHECK_THAT(actual[2].get<double>(), WithinAbs(z, 1e-6));
}

json baseRow(const std::string& key, const std::string& editorId, const std::string& modelPath,
             bool deleted = false) {
    return json{{"key", key}, {"editorId", editorId}, {"modelPath", modelPath}, {"deleted", deleted}};
}

}

TEST_CASE("LayoutTarget.GetModelBoundsPlacesBillboardShapesThroughTheirWorldMatrix") {
    StubBackend backend;
    Models models;
    models.geometry["Interiors\\Mixed.nif"] = json{
        {"fo4", true},
        {"shapes", json::array({
            json{{"name", "plain"}, {"verts", json::array({0, 0, 0, 256, 16, 300})}, {"tris", json::array()}},
            json{{"name", "sprite"},
                 {"verts", json::array({0, 0, 0, 10, 0, 0, 0, 0, 5})},
                 {"billboard", true},
                 {"world", json::array({0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 300, -5, 10, 1})},
                 {"tris", json::array()}}})}};
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke("GetModelBounds", json::array({json::array({"Interiors\\Mixed.nif"}), nullptr}));
    REQUIRE(result.has_value());
    const auto& entry = result->at("models")[0];
    requireVec3(entry.at("bounds").at("min"), 0, -5, 0);
    requireVec3(entry.at("bounds").at("max"), 300, 16, 300);
    requireVec3(entry.at("bounds").at("size"), 300, 21, 300);

    const int callsAfterFirst = models.calls;
    const auto again = target.invoke("GetModelBounds", json::array({json::array({"Interiors\\Mixed.nif"}), nullptr}));
    REQUIRE(again.has_value());
    CHECK(models.calls == callsAfterFirst);
}

TEST_CASE("LayoutTarget.GetModelBoundsResolvesBaseFormKeys") {
    StubBackend backend;
    backend.rows = json::array({baseRow("000800:Base.esm", "Wall_256", "Interiors\\Wall256.nif")});
    Models models;
    models.geometry["Interiors\\Wall256.nif"] = boxGeometry(-128, -8, 0, 128, 8, 300);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke("GetModelBounds",
                                      json::array({nullptr, json::array({"000800:base.esm", "000900:Missing.esm"})}));
    REQUIRE(result.has_value());
    const auto& list = result->at("models");
    REQUIRE(list.size() == 2);
    CHECK(list[0].at("editor_id") == "Wall_256");
    requireVec3(list[0].at("bounds").at("size"), 256, 16, 300);
    CHECK(list[1].contains("error"));
}

TEST_CASE("LayoutTarget.RefusesAmbiguousOrUnknownRequests") {
    StubBackend backend;
    Models models;
    LayoutTarget target(backend, providerFor(models));

    CHECK_FALSE(target.invoke("GetModelBounds", json::array({nullptr, nullptr})).has_value());
    CHECK_FALSE(target.invoke("GetModelBounds", json::array({json::array({"a.nif"}), json::array({"000800:Base.esm"})}))
                    .has_value());
    CHECK_FALSE(target.invoke("NoSuchMethod", json::array()).has_value());
}

TEST_CASE("LayoutTarget.DescribeKitFindsGridAndCategoriesByModelDir") {
    StubBackend backend;
    backend.rows = json::array({
        baseRow("000801:Vault.esm", "Vault_Floor_256", "Interiors\\Vault\\Rooms\\Floor256.nif"),
        baseRow("000802:Vault.esm", "Vault_Wall_256", "Interiors\\Vault\\Rooms\\Wall256.nif"),
        baseRow("000803:Vault.esm", "Vault_Corner", "Interiors\\Vault\\Rooms\\Corner.nif"),
        baseRow("000804:Vault.esm", "Vault_Light", "Interiors\\Vault\\Rooms\\Light.nif"),
        baseRow("000805:Other.esm", "Other_Floor", "Interiors\\Other\\Floor.nif"),
        baseRow("000806:Vault.esm", "Vault_Old", "Interiors\\Vault\\Rooms\\Old.nif", true),
    });
    Models models;
    models.geometry["Interiors\\Vault\\Rooms\\Floor256.nif"] = boxGeometry(0, 0, 0, 256, 256, 16);
    models.geometry["Interiors\\Vault\\Rooms\\Wall256.nif"] = boxGeometry(-128, -8, 0, 128, 8, 300);
    models.geometry["Interiors\\Vault\\Rooms\\Corner.nif"] = boxGeometry(-8, -8, 0, 8, 8, 300);
    models.geometry["Interiors\\Other\\Floor.nif"] = boxGeometry(0, 0, 0, 256, 256, 16);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke(
        "DescribeKit", json::array({nullptr, nullptr, "Meshes\\Interiors\\Vault\\Rooms\\"}));
    REQUIRE(result.has_value());
    CHECK(result->at("count") == 3);
    CHECK(result->at("pieces").size() == 3);
    CHECK(result->at("skipped").size() == 1);
    CHECK(result->at("categories").at("floor") == 1);
    CHECK(result->at("categories").at("wall") == 1);
    CHECK(result->at("categories").at("corner") == 1);
    CHECK(result->at("grid").at("grid") == 256);
    CHECK(result->at("grid").at("votes") == 2);
    CHECK(result->at("grid").at("pieces") == 3);
}

TEST_CASE("LayoutTarget.DescribeKitFiltersByNameAndPluginAndRefusesEmpty") {
    StubBackend backend;
    backend.rows = json::array({
        baseRow("000801:Vault.esm", "Vault_Floor_256", "Interiors\\Vault\\Rooms\\Floor256.nif"),
        baseRow("000805:Other.esm", "Other_Floor", "Interiors\\Other\\Floor.nif"),
    });
    Models models;
    models.geometry["Interiors\\Vault\\Rooms\\Floor256.nif"] = boxGeometry(0, 0, 0, 256, 256, 16);
    models.geometry["Interiors\\Other\\Floor.nif"] = boxGeometry(0, 0, 0, 128, 128, 16);
    LayoutTarget target(backend, providerFor(models));

    const auto byName = target.invoke("DescribeKit", json::array({"Other_", nullptr, nullptr}));
    REQUIRE(byName.has_value());
    CHECK(byName->at("pieces").size() == 1);
    CHECK(byName->at("grid").at("grid") == 128);

    const auto byPlugin = target.invoke("DescribeKit", json::array({nullptr, "vault.esm", nullptr}));
    REQUIRE(byPlugin.has_value());
    CHECK(byPlugin->at("pieces").size() == 1);
    CHECK(byPlugin->at("grid").at("grid") == 256);

    CHECK_FALSE(target.invoke("DescribeKit", json::array({"Nothing_", nullptr, nullptr})).has_value());
    CHECK_FALSE(target.invoke("DescribeKit", json::array({nullptr, nullptr, nullptr})).has_value());
}

TEST_CASE("LayoutTarget.SnapTransformSnapsToGridThenButtsNeighbours") {
    StubBackend backend;
    backend.rows = json::array({baseRow("000802:Vault.esm", "Vault_Wall_256", "Interiors\\Vault\\Rooms\\Wall256.nif")});
    backend.details["000900:Vault.esm"] = json{
        {"formKey", "000900:Vault.esm"},
        {"base", json{{"model", "Interiors\\Vault\\Rooms\\Wall256.nif"}}},
        {"transform", json{{"position", json{{"x", 630}, {"y", 520}, {"z", 0}}},
                           {"rotation", json{{"x", 0}, {"y", 0}, {"z", 0}}},
                           {"scale", 1.0}}}};
    Models models;
    models.geometry["Interiors\\Vault\\Rooms\\Wall256.nif"] = boxGeometry(-128, -8, 0, 128, 8, 300);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke(
        "SnapTransform",
        json::array({"000802:Vault.esm", json{{"x", 1000}, {"y", 500}, {"z", 0}}, 256,
                     json::array({"000900:Vault.esm", "000999:Gone.esm"})}));
    REQUIRE(result.has_value());
    CHECK(result->at("snapped") == true);
    CHECK(result->at("neighbours_used").size() == 1);
    CHECK(result->at("neighbours_skipped").size() == 1);
    CHECK(result->at("transform").at("x") == 886);
    CHECK(result->at("transform").at("y") == 520);
    CHECK(result->at("transform").at("z") == 0);
    requireVec3(result->at("bounds").at("min"), 758, 512, 0);
}

TEST_CASE("LayoutTarget.CheckOverlapsSkipsMarkersAndMissingModels") {
    StubBackend backend;
    backend.references = json::array({
        json{{"formKey", "000A00:Test.esm"}, {"modelPath", "Interiors\\Wall256.nif"},
             {"position", json{{"x", 0}, {"y", 0}, {"z", 0}}}, {"rotation", json{{"x", 0}, {"y", 0}, {"z", 0}}},
             {"scale", 1.0}, {"isMarker", false}},
        json{{"formKey", "000A01:Test.esm"}, {"modelPath", "Interiors\\Wall256.nif"},
             {"position", json{{"x", 100}, {"y", 0}, {"z", 0}}}, {"rotation", json{{"x", 0}, {"y", 0}, {"z", 0}}},
             {"scale", nullptr}, {"isMarker", false}},
        json{{"formKey", "000A02:Test.esm"}, {"modelPath", "Interiors\\Wall256.nif"},
             {"position", json{{"x", 0}, {"y", 0}, {"z", 0}}}, {"rotation", json{{"x", 0}, {"y", 0}, {"z", 0}}},
             {"scale", 1.0}, {"isMarker", true}},
        json{{"formKey", "000A03:Test.esm"}, {"modelPath", "Interiors\\Missing.nif"},
             {"position", json{{"x", 0}, {"y", 0}, {"z", 0}}}, {"rotation", json{{"x", 0}, {"y", 0}, {"z", 0}}},
             {"scale", 1.0}, {"isMarker", false}},
        json{{"formKey", "000A04:Test.esm"}, {"modelPath", "Interiors\\Wall256.nif"},
             {"position", json{{"x", 1000}, {"y", 0}, {"z", 0}}}, {"rotation", json{{"x", 0}, {"y", 0}, {"z", 0}}},
             {"scale", 1.0}, {"isMarker", false}},
    });
    Models models;
    models.geometry["Interiors\\Wall256.nif"] = boxGeometry(-128, -8, 0, 128, 8, 300);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke("CheckOverlaps", json::array({"000100:Test.esm", nullptr}));
    REQUIRE(result.has_value());
    CHECK(result->at("checked") == 3);
    CHECK(result->at("skipped").size() == 2);
    REQUIRE(result->at("overlaps").size() == 1);
    const auto& pair = result->at("overlaps")[0];
    CHECK(pair.at("a") == "000A00:Test.esm");
    CHECK(pair.at("b") == "000A01:Test.esm");
    requireVec3(pair.at("depth"), 156, 16, 300);

    const auto loose = target.invoke("CheckOverlaps", json::array({"000100:Test.esm", 200}));
    REQUIRE(loose.has_value());
    CHECK(loose->at("overlap_count") == 0);

    CHECK_FALSE(target.invoke("CheckOverlaps", json::array({"000100:Test.esm", -1})).has_value());
}

TEST_CASE("LayoutTarget.CheckOverlapsReportsBackendRefusal") {
    StubBackend backend;
    Models models;
    LayoutTarget target(backend, providerFor(models));
    CHECK_FALSE(target.invoke("CheckOverlaps", json::array({"999999:Nope.esm", nullptr})).has_value());
}

namespace {

json placedDetails(const std::string& key, const std::string& model, double x, double y, double z,
                   double rotationZ = 0.0) {
    return json{{"formKey", key},
                {"base", json{{"model", model}}},
                {"transform", json{{"position", json{{"x", x}, {"y", y}, {"z", z}}},
                                   {"rotation", json{{"x", 0}, {"y", 0}, {"z", rotationZ}}},
                                   {"scale", 1.0}}}};
}

json kitPiece(const std::string& key, json low, json high) {
    return json{{"baseFormKey", key}, {"min", low}, {"max", high}};
}

}

TEST_CASE("LayoutTarget.BaseRowsPassKeyFilterToBackend") {
    StubBackend backend;
    backend.rows = json::array({baseRow("000800:Base.esm", "Wall_256", "Interiors\\Wall256.nif")});
    Models models;
    models.geometry["Interiors\\Wall256.nif"] = boxGeometry(-128, -8, 0, 128, 8, 300);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke("GetModelBounds", json::array({nullptr, json::array({"000800:base.esm"})}));
    REQUIRE(result.has_value());
    REQUIRE(backend.listArgs.size() == 1);
    CHECK(backend.listArgs.at(0).at("keys") == json::array({"000800:base.esm"}));
}

TEST_CASE("LayoutTarget.BuildRoomSnapsOriginAndForwardsDryRun") {
    StubBackend backend;
    Models models;
    LayoutTarget target(backend, providerFor(models));
    const json kit{{"floor", kitPiece("000801:Vault.esm", json::array({-128, -128, 0}), json::array({128, 128, 16}))},
                   {"wall", kitPiece("000802:Vault.esm", json::array({-128, -8, 0}), json::array({128, 8, 300}))}};
    const json size{{"cells_x", 3}, {"cells_y", 3}, {"height_cells", 1}};

    const auto result = target.invoke(
        "BuildRoom", json::array({"Test.esm", "000100:Test.esm", kit, size, json{{"x", 1000}, {"y", 500}, {"z", 0}},
                                  256, true}));
    REQUIRE(result.has_value());
    REQUIRE(backend.buildCalls == 1);
    CHECK(backend.buildArgs.at(0) == "Test.esm");
    CHECK(backend.buildArgs.at(1) == "000100:Test.esm");
    CHECK(backend.buildArgs.at(2) == kit);
    CHECK(backend.buildArgs.at(3) == size);
    CHECK(backend.buildArgs.at(4).at("x") == 1024);
    CHECK(backend.buildArgs.at(4).at("y") == 512);
    CHECK(backend.buildArgs.at(4).at("z") == 0);
    CHECK(backend.buildArgs.at(5) == true);
    CHECK(result->at("forwarded").at(5) == true);
}

TEST_CASE("LayoutTarget.BuildRoomKeepsOriginWithoutGridAndRefusesIncompleteKit") {
    StubBackend backend;
    Models models;
    LayoutTarget target(backend, providerFor(models));
    const json size{{"cells_x", 1}, {"cells_y", 1}};
    const json origin{{"x", 1000}, {"y", 500}, {"z", 0}};
    const json wallOnly{{"wall", kitPiece("000802:Vault.esm", json::array({-128, -8, 0}), json::array({128, 8, 300}))}};
    const json noKey{{"floor", json{{"min", json::array({-128, -128, 0})}, {"max", json::array({128, 128, 16})}}},
                     {"wall", kitPiece("000802:Vault.esm", json::array({-128, -8, 0}), json::array({128, 8, 300}))}};

    CHECK_FALSE(target.invoke("BuildRoom", json::array({"Test.esm", "000100:Test.esm", wallOnly, size, origin})).has_value());
    CHECK_FALSE(target.invoke("BuildRoom", json::array({"Test.esm", "000100:Test.esm", noKey, size, origin})).has_value());
    CHECK(backend.buildCalls == 0);

    const json fullKit{{"floor", kitPiece("000801:Vault.esm", json::array({-128, -128, 0}), json::array({128, 128, 16}))},
                       {"wall", kitPiece("000802:Vault.esm", json::array({-128, -8, 0}), json::array({128, 8, 300}))}};
    const auto result = target.invoke("BuildRoom", json::array({"Test.esm", "000100:Test.esm", fullKit, size, origin}));
    REQUIRE(result.has_value());
    REQUIRE(backend.buildCalls == 1);
    CHECK(backend.buildArgs.at(4).at("x") == 1000);
    CHECK(backend.buildArgs.at(5) == false);
}

TEST_CASE("LayoutTarget.AlignFloorLowersEveryReferenceToTheLowestBase") {
    StubBackend backend;
    Models models;
    models.geometry["Interiors\\Slab.nif"] = boxGeometry(0, 0, 0, 256, 256, 16);
    models.geometry["Interiors\\Column.nif"] = boxGeometry(-8, -8, 0, 8, 8, 300);
    backend.details["000A00:Test.esm"] = placedDetails("000A00:Test.esm", "Interiors\\Slab.nif", 0, 0, 20, 0.5);
    backend.details["000A01:Test.esm"] = placedDetails("000A01:Test.esm", "Interiors\\Column.nif", 300, 0, 4, 0.5);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke(
        "AlignReferences", json::array({"Test.esm", json::array({"000A00:Test.esm", "000A01:Test.esm"}), "floor"}));
    REQUIRE(result.has_value());
    REQUIRE(backend.editCalls == 1);
    CHECK(backend.editArgs.at(0) == "Test.esm");
    const auto& items = backend.editArgs.at(1);
    REQUIRE(items.size() == 2);
    CHECK_THAT(items[0].at("transform").at("z").get<double>(), WithinAbs(4, 1e-6));
    CHECK_THAT(items[1].at("transform").at("z").get<double>(), WithinAbs(4, 1e-6));
    CHECK(items[0].at("transform").at("rz").get<double>() == 0.5);
    CHECK_THAT(items[0].at("transform").at("x").get<double>(), WithinAbs(0, 1e-6));
    CHECK(result->at("mode") == "floor");
}

TEST_CASE("LayoutTarget.AlignGridSnapsMinCornersToTheGrid") {
    StubBackend backend;
    Models models;
    models.geometry["Interiors\\Post.nif"] = boxGeometry(-8, -8, 0, 8, 8, 16);
    backend.details["000A00:Test.esm"] = placedDetails("000A00:Test.esm", "Interiors\\Post.nif", 130, 260, 0);
    backend.details["000A01:Test.esm"] = placedDetails("000A01:Test.esm", "Interiors\\Post.nif", 520, 10, 0);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke(
        "AlignReferences", json::array({"Test.esm", json::array({"000A00:Test.esm", "000A01:Test.esm"}), "grid", 256}));
    REQUIRE(result.has_value());
    REQUIRE(backend.editCalls == 1);
    const auto& items = backend.editArgs.at(1);
    CHECK_THAT(items[0].at("transform").at("x").get<double>(), WithinAbs(8, 1e-6));
    CHECK_THAT(items[0].at("transform").at("y").get<double>(), WithinAbs(264, 1e-6));
    CHECK_THAT(items[1].at("transform").at("x").get<double>(), WithinAbs(520, 1e-6));
    CHECK_THAT(items[1].at("transform").at("y").get<double>(), WithinAbs(8, 1e-6));
}

TEST_CASE("LayoutTarget.AlignDistributeSpacesMiddleReferencesEvenly") {
    StubBackend backend;
    Models models;
    models.geometry["Interiors\\Marker.nif"] = boxGeometry(-5, -5, 0, 5, 5, 10);
    backend.details["000A00:Test.esm"] = placedDetails("000A00:Test.esm", "Interiors\\Marker.nif", 0, 0, 0);
    backend.details["000A01:Test.esm"] = placedDetails("000A01:Test.esm", "Interiors\\Marker.nif", 100, 0, 0);
    backend.details["000A02:Test.esm"] = placedDetails("000A02:Test.esm", "Interiors\\Marker.nif", 500, 0, 0);
    LayoutTarget target(backend, providerFor(models));

    const auto result = target.invoke(
        "AlignReferences",
        json::array({"Test.esm", json::array({"000A00:Test.esm", "000A01:Test.esm", "000A02:Test.esm"}), "distribute_x"}));
    REQUIRE(result.has_value());
    REQUIRE(backend.editCalls == 1);
    const auto& items = backend.editArgs.at(1);
    CHECK_THAT(items[0].at("transform").at("x").get<double>(), WithinAbs(0, 1e-6));
    CHECK_THAT(items[1].at("transform").at("x").get<double>(), WithinAbs(250, 1e-6));
    CHECK_THAT(items[2].at("transform").at("x").get<double>(), WithinAbs(500, 1e-6));
}

TEST_CASE("LayoutTarget.AlignRefusesBadRequestsWithoutWriting") {
    StubBackend backend;
    Models models;
    models.geometry["Interiors\\Marker.nif"] = boxGeometry(-5, -5, 0, 5, 5, 10);
    backend.details["000A00:Test.esm"] = placedDetails("000A00:Test.esm", "Interiors\\Marker.nif", 0, 0, 0);
    backend.details["000A01:Test.esm"] = placedDetails("000A01:Test.esm", "Interiors\\Marker.nif", 100, 0, 0);
    LayoutTarget target(backend, providerFor(models));

    CHECK_FALSE(target.invoke("AlignReferences",
                              json::array({"Test.esm", json::array({"000A00:Test.esm", "000A01:Test.esm"}), "distribute_y"}))
                    .has_value());
    CHECK_FALSE(target.invoke("AlignReferences",
                              json::array({"Test.esm", json::array({"000A00:Test.esm", "000A01:Test.esm"}), "spiral"}))
                    .has_value());
    CHECK_FALSE(target.invoke("AlignReferences",
                              json::array({"Test.esm", json::array({"000A00:Test.esm", "000A99:Test.esm"}), "floor"}))
                    .has_value());

    json tooMany = json::array();
    for (int i = 0; i < 201; ++i) tooMany.push_back("000B" + std::to_string(i) + ":Test.esm");
    CHECK_FALSE(target.invoke("AlignReferences", json::array({"Test.esm", tooMany, "floor"})).has_value());
    CHECK(backend.editCalls == 0);
}
