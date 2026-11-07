#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>

#include <fo4ide/assets/VertexColorSurface.hpp>

namespace {

nlohmann::json rpc_args(const nlohmann::json& block, const nlohmann::json& kind,
                        const nlohmann::json& channel = nullptr, const nlohmann::json& axis = nullptr,
                        const nlohmann::json& value = nullptr, const nlohmann::json& color = nullptr,
                        const nlohmann::json& vertices = nullptr) {
    return nlohmann::json::array({"shape.nif", block, kind, channel, axis, value, color, nullptr, vertices, "out.nif"});
}

fo4::nif::Shape coloured_shape() {
    fo4::nif::Shape shape;
    shape.block = 4;
    shape.name = "Body";
    shape.verts = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    shape.colors = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.5f};
    return shape;
}

}  // namespace

TEST_CASE("VertexColor parser reads a fill with a colour and a vertex list") {
    const auto op = fo4ide::assets::parseVertexColorOp(rpc_args(7, "fill", nullptr, nullptr, nullptr,
                                                                nlohmann::json::array({1.0, 0.5, 0.25, 0.0}),
                                                                nlohmann::json::array({0, 2})));
    REQUIRE(op.has_value());
    CHECK(op->kind == fo4::nif::VertexColorKind::fill);
    CHECK(op->block == 7U);
    CHECK(op->color[1] == Catch::Approx(0.5f));
    CHECK(op->vertices == std::vector<std::uint32_t>{0U, 2U});
}

TEST_CASE("VertexColor parser fills in the defaults for channel, axis, value and colours") {
    const auto op = fo4ide::assets::parseVertexColorOp(rpc_args(3, "channel", "alpha", nullptr, 0.25));
    REQUIRE(op.has_value());
    CHECK(op->kind == fo4::nif::VertexColorKind::channel);
    CHECK(op->channel == fo4::nif::ColorChannel::alpha);
    CHECK(op->value == Catch::Approx(0.25f));
    CHECK(op->axis == fo4::nif::GradientAxis::x);
    CHECK(op->color == std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(op->end_color == std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    CHECK(op->vertices.empty());
}

TEST_CASE("VertexColor parser refuses unknown kinds, channels, axes and malformed arguments") {
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(rpc_args(7, "paint")).has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(rpc_args(-1, "fill")).has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(rpc_args(7, "channel", "cyan")).has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(rpc_args(7, "gradient", nullptr, "w")).has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(rpc_args(7, "channel", nullptr, nullptr, "bright")).has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(
                    rpc_args(7, "fill", nullptr, nullptr, nullptr, nlohmann::json::array({1.0, 0.0, 0.0})))
                    .has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(
                    rpc_args(7, "fill", nullptr, nullptr, nullptr, nullptr, nlohmann::json::array({-1})))
                    .has_value());
    CHECK_FALSE(fo4ide::assets::parseVertexColorOp(nlohmann::json::array({"shape.nif"})).has_value());
}

TEST_CASE("VertexColor summary reports per-channel range and mean, and absent colours") {
    fo4::nif::Shape plain;
    plain.block = 2;
    plain.verts = {0.0f, 0.0f, 0.0f};
    const auto json = fo4ide::assets::vertexColorSummaryJson({coloured_shape(), plain});
    REQUIRE(json.at("shape_count").get<std::size_t>() == 2U);
    const auto& first = json.at("shapes").at(0);
    CHECK(first.at("block").get<std::uint32_t>() == 4U);
    CHECK(first.at("vertices").get<std::size_t>() == 2U);
    CHECK(first.at("color").at("present").get<bool>());
    CHECK(first.at("color").at("values").get<std::size_t>() == 8U);
    CHECK(first.at("color").at("channels").at("red").at("max").get<double>() == Catch::Approx(1.0));
    CHECK(first.at("color").at("channels").at("green").at("min").get<double>() == Catch::Approx(0.0));
    CHECK(first.at("color").at("channels").at("alpha").at("mean").get<double>() == Catch::Approx(0.75));
    const auto& second = json.at("shapes").at(1);
    CHECK_FALSE(second.at("color").at("present").get<bool>());
    CHECK(second.at("color").at("channels").empty());
}
