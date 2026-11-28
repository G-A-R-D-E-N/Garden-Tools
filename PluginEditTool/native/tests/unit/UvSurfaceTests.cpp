#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fo4/nif/nif.hpp>

#include <fo4ide/assets/NifRender.hpp>
#include <fo4ide/assets/UvSurface.hpp>

#include "../helpers/NifFixture.hpp"
#include "../helpers/TempRoot.hpp"

namespace {

struct UvScratch {
    std::filesystem::path root;
    ~UvScratch() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(raw));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

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

TEST_CASE("UvSurface transform scales about the pivot before it rotates, as the editor does") {
    const std::vector<float> uvs{0.75f, 0.5f};
    fo4ide::assets::UvTransform transform;
    transform.sx = 2.0f;
    transform.sy = 1.0f;
    transform.rotation_degrees = 90.0f;
    const auto out = fo4ide::assets::transformedUvs(uvs, transform);
    REQUIRE(out.has_value());
    CHECK((*out)[0] == Catch::Approx(0.5).epsilon(1e-5));
    CHECK((*out)[1] == Catch::Approx(1.0).epsilon(1e-5));
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

TEST_CASE("UV transform in place publishes the coordinates it derived from the unchanged source and leaves a predictable staging sibling untouched") {
    const UvScratch scratch{std::filesystem::temp_directory_path() / ("fo4ide_uv_" + fo4ide_test::uniqueTempName())};
    const auto nif = scratch.root / "uv.nif";
    const auto staging = scratch.root / "uv.nif.niftmp";
    fo4ide::test::writeBytes(nif, fo4ide::test::dualUvTriangleNif());
    {
        std::ofstream sentinel(staging, std::ios::binary);
        sentinel << "sentinel";
    }
    const auto before = fo4::nif::read_shapes(fo4::nif::read_nif(readFileBytes(nif)));
    REQUIRE(before.size() == 1U);
    REQUIRE_FALSE(before[0].uvs.empty());

    fo4ide::assets::UvTransform transform;
    transform.du = 0.25f;
    const auto result = fo4ide::assets::applyUvTransform(nif, before[0].block, 0, transform, {});

    REQUIRE(result.has_value());
    const auto sentinelBytes = readFileBytes(staging);
    CHECK(std::string(reinterpret_cast<const char*>(sentinelBytes.data()), sentinelBytes.size()) == "sentinel");
    const auto after = fo4::nif::read_shapes(fo4::nif::read_nif(readFileBytes(nif)));
    REQUIRE(after.size() == 1U);
    REQUIRE(after[0].uvs.size() == before[0].uvs.size());
    for (std::size_t i = 0; i + 1 < before[0].uvs.size(); i += 2) {
        CHECK(after[0].uvs[i] == Catch::Approx(before[0].uvs[i] + 0.25).epsilon(1e-4));
        CHECK(after[0].uvs[i + 1] == Catch::Approx(before[0].uvs[i + 1]).epsilon(1e-4));
    }
}

TEST_CASE("UV commit refuses an in-place write, including an explicit output spelled as the input, when the NIF changed after it was read") {
    const UvScratch scratch{std::filesystem::temp_directory_path() / ("fo4ide_uv_" + fo4ide_test::uniqueTempName())};
    const auto nif = scratch.root / "uv.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::dualUvTriangleNif());
    const auto current = readFileBytes(nif);
    auto stale = current;
    stale.back() ^= std::byte{0xFF};

    const auto refused = fo4ide::assets::commitUvTransform(nif, stale, current, {});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "UvTransformFailed");
    CHECK(readFileBytes(nif) == current);

    const auto explicitSame = fo4ide::assets::commitUvTransform(nif, stale, current, nif);
    REQUIRE_FALSE(explicitSame.has_value());
    CHECK(readFileBytes(nif) == current);

    const auto copy = scratch.root / "copy.nif";
    const auto written = fo4ide::assets::commitUvTransform(nif, stale, current, copy);
    REQUIRE(written.has_value());
    CHECK(readFileBytes(copy) == current);
}
