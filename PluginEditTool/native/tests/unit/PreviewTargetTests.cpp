#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/app/PreviewTarget.hpp>

using fo4ide::Error;
using fo4ide::Result;
using nlohmann::json;

namespace {

json quadGeometry() {
    json shape = json::object();
    shape["verts"] = json::array({-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 1.0, 1.0, 0.0, -1.0, 1.0, 0.0});
    shape["tris"] = json::array({0, 1, 2, 0, 2, 3});
    json model = json::object();
    model["shapes"] = json::array({shape});
    return model;
}

json cellReply(bool withTrigger) {
    json ref = json::object();
    ref["formKey"] = "000001:Test.esp";
    ref["baseType"] = "STAT";
    ref["modelPath"] = "quad.nif";
    ref["isMarker"] = false;
    ref["position"] = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
    ref["rotation"] = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
    ref["scale"] = 1.0;
    json root = json::object();
    root["cellFormKey"] = "00ABCD:Test.esp";
    root["references"] = json::array({ref});
    if (withTrigger) {
        json trigger = ref;
        trigger["formKey"] = "000002:Test.esp";
        trigger["modelPath"] = "";
        trigger["baseEditorId"] = "TriggerBox";
        trigger["primitive"] = {{"type", 1}, {"bounds", {{"x", 40.0}, {"y", 40.0}, {"z", 40.0}}}};
        root["references"].push_back(trigger);
    }
    return root;
}

class FakeCells final : public fo4ide::rpc::Target {
public:
    bool withTrigger{false};

    Result<json> invoke(std::string_view method, const json& args) override {
        if (method == "GetPlacedReferences") {
            if (args.at(0) == "Missing") return json(R"({"error":"Cell not found"})");
            return json(cellReply(withTrigger).dump());
        }
        if (method == "GetGeometryBatch") {
            const auto paths = json::parse(args.at(0).get<std::string>());
            json geometry = json::object();
            for (const auto& path : paths) {
                geometry[path.get<std::string>()] = path == "quad.nif" ? quadGeometry() : json{{"error", "Not found"}};
            }
            json reply = json::object();
            reply["count"] = paths.size();
            reply["geometry"] = geometry;
            return json(reply.dump());
        }
        return std::unexpected(Error{"UnknownMethod", std::string(method)});
    }
};

json options(json fields) {
    return json::array({std::move(fields)});
}

}  // namespace

TEST_CASE("Preview target renders a cell to a PNG with a report") {
    FakeCells cells;
    fo4ide::app::PreviewTarget target(cells);
    const auto reply = target.invoke("RenderCell", options({{"cell_id", "00ABCD:Test.esp"}, {"width", 64}, {"height", 64},
                                                            {"highlight_keys", json::array({"000001:Test.esp"})}}));
    REQUIRE(reply.has_value());
    CHECK(reply->at("image").at("mimeType") == "image/png");
    CHECK(reply->at("image").at("base64").get<std::string>().rfind("iVBORw0KGgo", 0) == 0);
    CHECK(reply->at("report").at("drawn") == 1);
    CHECK(reply->at("report").at("cellFormKey") == "00ABCD:Test.esp");
    CHECK(reply->at("report").at("highlights").at(0).at("found") == true);
}

TEST_CASE("Preview target reports a missing cell and missing required arguments") {
    FakeCells cells;
    fo4ide::app::PreviewTarget target(cells);
    const auto missing = target.invoke("RenderCell", options({{"cell_id", "Missing"}}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message == "Cell not found");

    const auto noId = target.invoke("CellLayout", options({{"other", 1}}));
    REQUIRE_FALSE(noId.has_value());
    CHECK(noId.error().type == "InvalidArgument");
}

TEST_CASE("Preview target layout summary and model render use the cell geometry") {
    FakeCells cells;
    fo4ide::app::PreviewTarget target(cells);
    const auto layout = target.invoke("CellLayout", options({{"cell_id", "00ABCD:Test.esp"}}));
    REQUIRE(layout.has_value());
    CHECK(layout->at("references").at("total") == 1);
    CHECK(layout->at("counts").at("byType").at("STAT") == 1);

    const auto model = target.invoke("RenderModel", options({{"model_path", "quad.nif"}, {"size", 64}}));
    REQUIRE(model.has_value());
    CHECK(model->at("report").at("triangles") == 2);
    CHECK(model->at("report").at("modelPath") == "quad.nif");

    const auto absent = target.invoke("RenderModel", options({{"model_path", "absent.nif"}}));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().message == "Not found");
}

// Constraint, detects: the layers argument being dropped between the RPC and the render so a disabled layer is still drawn.
TEST_CASE("Preview target passes the layers argument through to the cell render") {
    FakeCells cells;
    cells.withTrigger = true;
    fo4ide::app::PreviewTarget target(cells);
    const auto shown = target.invoke("RenderCell", options({{"cell_id", "00ABCD:Test.esp"}, {"width", 64}, {"height", 64}}));
    REQUIRE(shown.has_value());
    CHECK(shown->at("report").at("volumes").at("trigger") == 1);

    const auto hidden = target.invoke("RenderCell", options({{"cell_id", "00ABCD:Test.esp"}, {"width", 64}, {"height", 64},
                                                             {"layers", {{"trigger", false}}}}));
    REQUIRE(hidden.has_value());
    CHECK(hidden->at("report").at("volumes").at("trigger") == 0);
    CHECK(hidden->at("report").at("layers").at("trigger") == false);

    const auto bad = target.invoke("RenderCell", options({{"cell_id", "00ABCD:Test.esp"}, {"layers", {{"bogus", true}}}}));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().type == "InvalidArgument");
}
