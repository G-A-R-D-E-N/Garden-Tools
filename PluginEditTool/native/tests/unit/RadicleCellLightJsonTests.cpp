#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadicleLightJson.hpp>

#include <fo4/environment/base_records.hpp>
#include <fo4/environment/world.hpp>

namespace {

using fo4ide::plugin::cellLightingJson;
using fo4ide::plugin::lightJson;

fo4::env::LightRecordData lightWithFlags(std::uint32_t flags) {
    fo4::env::LightRecordData record;
    record.data = fo4::env::LightData{};
    record.data->radius = 512;
    record.data->flags = flags;
    return record;
}

}  // namespace

TEST_CASE("LIGH flags map to the view booleans the web conversion reads", "[cell][light]") {
    SECTION("a spot with a shadow flag casts shadows") {
        const auto json = lightJson(lightWithFlags(0x0000'0400U));
        CHECK(json["isSpot"] == true);
        CHECK(json["castsShadow"] == true);
    }
    SECTION("a non-shadow spot is a spot without shadows") {
        const auto json = lightJson(lightWithFlags(0x0000'4000U));
        CHECK(json["isSpot"] == true);
        CHECK(json["castsShadow"] == false);
    }
    SECTION("hemisphere and omni shadow flags cast shadows but are not spots") {
        CHECK(lightJson(lightWithFlags(0x0000'0800U))["castsShadow"] == true);
        CHECK(lightJson(lightWithFlags(0x0000'1000U))["castsShadow"] == true);
        CHECK(lightJson(lightWithFlags(0x0000'0800U))["isSpot"] == false);
    }
    SECTION("ambient-only, off-by-default, flicker, pulse and non-specular bits") {
        CHECK(lightJson(lightWithFlags(0x0010'0000U))["ambientOnly"] == true);
        CHECK(lightJson(lightWithFlags(0x0000'0020U))["offByDefault"] == true);
        CHECK(lightJson(lightWithFlags(0x0000'0008U))["flicker"] == true);
        CHECK(lightJson(lightWithFlags(0x0000'0080U))["pulse"] == true);
        CHECK(lightJson(lightWithFlags(0x0000'8000U))["nonSpecular"] == true);
        const auto plain = lightJson(lightWithFlags(0U));
        CHECK(plain["isSpot"] == false);
        CHECK(plain["castsShadow"] == false);
        CHECK(plain["ambientOnly"] == false);
        CHECK(plain["flicker"] == false);
    }
}

TEST_CASE("LIGH colour and fade are read as stored", "[cell][light]") {
    auto record = lightWithFlags(0U);
    record.data->color = fo4::env::ByteColor{255, 128, 0};
    record.fade = 0.5F;
    const auto json = lightJson(record);
    CHECK(json["color"]["r"].get<float>() == Catch::Approx(1.0F));
    CHECK(json["color"]["g"].get<float>() == Catch::Approx(128.0F / 255.0F));
    CHECK(json["color"]["b"].get<float>() == Catch::Approx(0.0F));
    CHECK(json["fade"].get<float>() == Catch::Approx(0.5F));
    CHECK(json["radius"] == 512);
}

TEST_CASE("a LIGH without fade or DATA keeps null values", "[cell][light]") {
    auto record = lightWithFlags(0U);
    CHECK(lightJson(record)["fade"].is_null());
    CHECK(lightJson(record)["exponent"].is_null());
    fo4::env::LightRecordData empty;
    CHECK(lightJson(empty).is_null());
}

TEST_CASE("cell lighting uses the cell's own XCLL without a template", "[cell][lighting]") {
    fo4::env::CellLighting own{};
    own.ambient = fo4::env::ByteColor{10, 20, 30};
    own.directional = fo4::env::ByteColor{200, 180, 160};
    own.fog_near_color = fo4::env::ByteColor{1, 2, 3};
    own.fog_near = 100.0F;
    own.fog_far = 4000.0F;
    own.directional_rotation_xy = 45;
    own.directional_fade = 0.75F;

    const auto json = cellLightingJson(own, std::nullopt);
    CHECK(json["ambient"] == nlohmann::json::array({10, 20, 30}));
    CHECK(json["directional"] == nlohmann::json::array({200, 180, 160}));
    CHECK(json["fogNearColor"] == nlohmann::json::array({1, 2, 3}));
    CHECK(json["fogNear"].get<float>() == Catch::Approx(100.0F));
    CHECK(json["fogFar"].get<float>() == Catch::Approx(4000.0F));
    CHECK(json["directionalRotationXY"] == 45);
    CHECK(json["directionalSource"] == "cell");
    CHECK(json["hasTemplate"] == false);
}

TEST_CASE("template inherit bits replace only the flagged cell values", "[cell][lighting]") {
    fo4::env::CellLighting own{};
    own.ambient = fo4::env::ByteColor{10, 10, 10};
    own.fog_near_color = fo4::env::ByteColor{5, 5, 5};
    own.fog_near = 1.0F;
    own.fog_far = 2.0F;
    own.inherits = 0x01U | 0x08U;

    fo4::env::CellLighting templ{};
    templ.ambient = fo4::env::ByteColor{70, 80, 90};
    templ.fog_near_color = fo4::env::ByteColor{7, 7, 7};
    templ.fog_near = 300.0F;
    templ.fog_far = 900.0F;

    const auto json = cellLightingJson(own, templ);
    CHECK(json["ambient"] == nlohmann::json::array({70, 80, 90}));
    CHECK(json["fogNear"].get<float>() == Catch::Approx(300.0F));
    CHECK(json["fogFar"].get<float>() == Catch::Approx(2.0F));
    CHECK(json["fogNearColor"] == nlohmann::json::array({5, 5, 5}));
    CHECK(json["hasTemplate"] == true);
}

TEST_CASE("fog colour inherits with its own bit and directional stays with the cell", "[cell][lighting]") {
    fo4::env::CellLighting own{};
    own.fog_near_color = fo4::env::ByteColor{5, 5, 5};
    own.directional = fo4::env::ByteColor{99, 99, 99};
    own.inherits = 0x04U | 0x10U;

    fo4::env::CellLighting templ{};
    templ.fog_near_color = fo4::env::ByteColor{7, 7, 7};
    templ.directional = fo4::env::ByteColor{1, 1, 1};
    templ.fog_far = 555.0F;

    const auto json = cellLightingJson(own, templ);
    CHECK(json["fogNearColor"] == nlohmann::json::array({7, 7, 7}));
    CHECK(json["fogFar"].get<float>() == Catch::Approx(555.0F));
    CHECK(json["directional"] == nlohmann::json::array({99, 99, 99}));
}

TEST_CASE("a cell without its own XCLL takes its values from the template", "[cell][lighting]") {
    fo4::env::CellLighting templ{};
    templ.ambient = fo4::env::ByteColor{3, 4, 5};
    templ.directional = fo4::env::ByteColor{6, 7, 8};
    const auto json = cellLightingJson(std::nullopt, templ);
    CHECK(json["ambient"] == nlohmann::json::array({3, 4, 5}));
    CHECK(json["directional"] == nlohmann::json::array({6, 7, 8}));
    CHECK(json["directionalSource"] == "template");
    CHECK(json["ownLighting"] == false);
}

TEST_CASE("no cell lighting and no template yields null", "[cell][lighting]") {
    CHECK(cellLightingJson(std::nullopt, std::nullopt).is_null());
}
