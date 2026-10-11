#include <cstdint>
#include <cstring>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>

namespace {

std::int32_t floatBits(float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<std::int32_t>(bits);
}

}  // namespace

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

TEST_CASE("condition codec: float parameter is a JSON number") {
    fo4::CtdaCondition condition;
    condition.function = fo4ide::plugin::functionIndex("GetWithinDistance").value();
    condition.param1 = 0x00000800;
    condition.param2 = floatBits(5.5f);
    const fo4::Plugin plugin{};

    const auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("param2").is_number_float());
    CHECK(described.at("param2") == 5.5);

    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(described, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->param2 == condition.param2);
}

TEST_CASE("condition codec: integer parameter round trips as a number") {
    const fo4::Plugin plugin{};
    for (const std::int32_t axis : {1, 10}) {
        fo4::CtdaCondition condition;
        condition.function = fo4ide::plugin::functionIndex("GetAngle").value();
        condition.param1 = axis;

        const auto described = fo4ide::plugin::describeCondition(condition, plugin);
        REQUIRE(described.at("param1").is_number_integer());
        CHECK(described.at("param1") == axis);

        std::string failure;
        const auto rebuilt = fo4ide::plugin::buildCondition(described, plugin, {}, failure);
        REQUIRE(rebuilt.has_value());
        CHECK(rebuilt->param1 == axis);
    }
}

TEST_CASE("condition codec: alias parameter under ParametersUseAliases is an integer") {
    fo4::CtdaCondition condition;
    condition.function = fo4ide::plugin::functionIndex("GetDistance").value();
    condition.param1 = 3;
    const fo4::Plugin plugin{};

    condition.flags_byte = fo4::k_ctda_flag_aliases;
    CHECK(fo4ide::plugin::describeCondition(condition, plugin).at("param1") == 3);

    condition.flags_byte = 0;
    CHECK(fo4ide::plugin::describeCondition(condition, plugin).at("param1").is_string());
}

TEST_CASE("condition codec: build accepts the old string forms and JSON integers on float slots") {
    const fo4::Plugin plugin{};
    std::string failure;

    const nlohmann::json angle{{"function", "GetAngle"}, {"param1", "7"}};
    const auto axis = fo4ide::plugin::buildCondition(angle, plugin, {}, failure);
    REQUIRE(axis.has_value());
    CHECK(axis->param1 == 7);

    nlohmann::json distance{{"function", "GetWithinDistance"}, {"param2", "2.5"}};
    const auto fromText = fo4ide::plugin::buildCondition(distance, plugin, {}, failure);
    REQUIRE(fromText.has_value());
    CHECK(fromText->param2 == floatBits(2.5f));

    distance["param2"] = 3;
    const auto fromInteger = fo4ide::plugin::buildCondition(distance, plugin, {}, failure);
    REQUIRE(fromInteger.has_value());
    CHECK(fromInteger->param2 == floatBits(3.0f));
}

TEST_CASE("condition codec: non-finite float round trips through 0x hex") {
    fo4::CtdaCondition condition;
    condition.function = fo4ide::plugin::functionIndex("GetWithinDistance").value();
    condition.param2 = 0x7FC00000;
    const fo4::Plugin plugin{};

    const auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("param2") == "0x7FC00000");

    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(described, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->param2 == condition.param2);
}

TEST_CASE("condition codec: GetIsCurrentPackage param1 under aliases is form ID text only on Run On 5") {
    fo4::CtdaCondition condition;
    condition.function = fo4ide::plugin::functionIndex("GetIsCurrentPackage").value();
    condition.param1 = 0x00000812;
    condition.flags_byte = fo4::k_ctda_flag_aliases;
    const fo4::Plugin plugin{};

    condition.run_on = 5;
    CHECK(fo4ide::plugin::describeCondition(condition, plugin).at("param1").is_string());

    condition.run_on = 0;
    CHECK(fo4ide::plugin::describeCondition(condition, plugin).at("param1") == 0x812);
}

TEST_CASE("condition codec: GetDistance reference param1 with only UsePackData is an integer") {
    fo4::CtdaCondition condition;
    condition.function = fo4ide::plugin::functionIndex("GetDistance").value();
    condition.param1 = 3;
    condition.flags_byte = fo4::k_ctda_flag_pack_data;
    const fo4::Plugin plugin{};

    CHECK(fo4ide::plugin::describeCondition(condition, plugin).at("param1") == 3);
}

TEST_CASE("condition codec: negative and hex integer params on an Integer slot") {
    const fo4::Plugin plugin{};
    std::string failure;

    fo4::CtdaCondition condition;
    condition.function = fo4ide::plugin::functionIndex("GetAngle").value();
    condition.param1 = -3;
    const auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("param1") == -3);
    const auto rebuilt = fo4ide::plugin::buildCondition(described, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->param1 == -3);

    const nlohmann::json hex{{"function", "GetAngle"}, {"param1", "0x0000000A"}};
    const auto fromHex = fo4ide::plugin::buildCondition(hex, plugin, {}, failure);
    REQUIRE(fromHex.has_value());
    CHECK(fromHex->param1 == 10);
}

TEST_CASE("condition codec: unsigned integral JSON double on an Integer slot keeps its bits") {
    const fo4::Plugin plugin{};
    std::string failure;

    const nlohmann::json element{{"function", "GetAngle"}, {"param1", 3000000000.0}};
    const auto built = fo4ide::plugin::buildCondition(element, plugin, {}, failure);
    REQUIRE(built.has_value());
    CHECK(built->param1 == static_cast<std::int32_t>(0xB2D05E00u));
}
