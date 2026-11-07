#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fo4/nif/nif.hpp>

#include <fo4ide/assets/UvSurface.hpp>

namespace {

fo4::nif::Shape triangle_shape() {
    fo4::nif::Shape shape;
    shape.block = 7;
    shape.name = "Tri";
    shape.verts = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    shape.tris = {0, 1, 2};
    shape.uvs = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    return shape;
}

}  // namespace

TEST_CASE("UvSurface summary reports shape vertex count, UV presence and one island") {
    const auto json = fo4ide::assets::uvSummaryJson({triangle_shape()});
    REQUIRE(json.at("shape_count").get<std::size_t>() == 1U);
    const auto& shape = json.at("shapes").at(0);
    CHECK(shape.at("block").get<std::uint32_t>() == 7U);
    CHECK(shape.at("vertices").get<std::size_t>() == 3U);
    CHECK(shape.at("triangles").get<std::size_t>() == 1U);
    CHECK(shape.at("uv0").at("present").get<bool>());
    CHECK_FALSE(shape.at("uv2").at("present").get<bool>());
    CHECK(shape.at("uv0").at("island_count").get<std::size_t>() == 1U);
    CHECK(shape.at("uv0").at("islands").at(0).at("vertices").get<std::size_t>() == 3U);
    CHECK(shape.at("uv0").at("islands").at(0).at("uv_area").get<double>() == Catch::Approx(0.5).epsilon(1e-9));
}

TEST_CASE("UvSurface summary separates disconnected islands") {
    auto shape = triangle_shape();
    shape.verts = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                   5.0f, 0.0f, 0.0f, 6.0f, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f};
    shape.tris = {0, 1, 2, 3, 4, 5};
    shape.uvs = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    shape.uvs2 = shape.uvs;
    const auto json = fo4ide::assets::uvSummaryJson({shape});
    const auto& item = json.at("shapes").at(0);
    CHECK(item.at("uv0").at("island_count").get<std::size_t>() == 2U);
    CHECK(item.at("uv2").at("island_count").get<std::size_t>() == 2U);
}

TEST_CASE("UvSurface transform rotates, scales and translates about the tile centre") {
    const std::vector<float> uvs{0.5f, 0.5f, 0.75f, 0.5f, 0.5f, 0.75f};
    fo4ide::assets::UvTransform transform;
    transform.rotation_degrees = 90.0f;
    transform.du = 0.1f;
    const auto out = fo4ide::assets::transformedUvs(uvs, transform);
    REQUIRE(out.has_value());
    CHECK((*out)[0] == Catch::Approx(0.6).epsilon(1e-5));
    CHECK((*out)[1] == Catch::Approx(0.5).epsilon(1e-5));
    CHECK((*out)[2] == Catch::Approx(0.6).epsilon(1e-5));
    CHECK((*out)[3] == Catch::Approx(0.75).epsilon(1e-5));
}

TEST_CASE("UvSurface transform refuses empty, odd, non-finite and out-of-range inputs") {
    fo4ide::assets::UvTransform identity;
    CHECK_FALSE(fo4ide::assets::transformedUvs({}, identity).has_value());
    CHECK_FALSE(fo4ide::assets::transformedUvs({0.0f, 0.0f, 1.0f}, identity).has_value());
    auto nan = identity;
    nan.du = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(fo4ide::assets::transformedUvs({0.0f, 0.0f}, nan).has_value());
    auto huge = identity;
    huge.sx = 1.0e6f;
    CHECK_FALSE(fo4ide::assets::transformedUvs({1.0f, 0.0f}, huge).has_value());
}
