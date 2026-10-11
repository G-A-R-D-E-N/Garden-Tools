#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/MaterialTarget.hpp>
#include <fo4ide/material/MaterialService.hpp>

#include "../helpers/ServiceTargetFakes.hpp"

using namespace fo4ide::app::test;

TEST_CASE("MaterialTarget refuses a dropped payload that decodes past the 16 MiB cap") {
    fo4ide::material::MaterialService service;
    FakePlatform platform;
    fo4ide::app::MaterialTarget target(service, platform);

    const std::string payload(22369623, 'A');
    const auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"mesh.bgsm", payload}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().starts_with("ERR:"));
}
