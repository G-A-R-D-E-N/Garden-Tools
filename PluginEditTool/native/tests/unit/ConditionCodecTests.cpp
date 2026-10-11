#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>

TEST_CASE("condition codec: GetEventData rows keep param1 and param2 through describe and build") {
    fo4::CtdaCondition condition;
    condition.function = 576;
    condition.param1 = 0x4C430041;
    condition.param2 = 0x00012345;
    const fo4::Plugin plugin{};

    const auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("function") == "GetEventData");
    REQUIRE(described.contains("param1"));
    REQUIRE(described.contains("param2"));
    REQUIRE(described.at("param1").get<std::int32_t>() == condition.param1);
    REQUIRE(described.at("param2").get<std::int32_t>() == condition.param2);

    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(described, plugin, {}, failure);
    REQUIRE(failure.empty());
    REQUIRE(rebuilt.has_value());
    REQUIRE(rebuilt->function == 576);
    REQUIRE(rebuilt->param1 == condition.param1);
    REQUIRE(rebuilt->param2 == condition.param2);
}
