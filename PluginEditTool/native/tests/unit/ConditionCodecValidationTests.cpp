#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>

namespace {

using fo4ide::plugin::EditorIdResolver;

std::optional<fo4::RawFormId> resolveTestEditorId(std::string_view text) {
    if (text == "CondWeapon") return fo4::RawFormId{0x00000800u};
    return std::nullopt;
}

std::optional<fo4::CtdaCondition> buildFrom(const nlohmann::json& element, std::string& failure) {
    const fo4::Plugin plugin{};
    const EditorIdResolver resolver = resolveTestEditorId;
    return fo4ide::plugin::buildCondition(element, plugin, resolver, failure);
}

std::string failureOf(const nlohmann::json& element) {
    std::string failure;
    REQUIRE_FALSE(buildFrom(element, failure).has_value());
    return failure;
}

}  // namespace

TEST_CASE("condition codec: each rejected entry fails with its own reason") {
    const std::vector<std::pair<nlohmann::json, std::string>> rejected = {
        {nlohmann::json{{"function", "GetAngle"}, {"operator", "~="}, {"value", 1}}, "unknown comparison operator"},
        {nlohmann::json{{"function", "GetAngle"}, {"operator", 2}}, "operator must be a string"},
        {nlohmann::json{{"function", "GetAngle"}, {"compareGlobal", "MissingGlobal"}}, "unresolved comparison global"},
        {nlohmann::json{{"function", "GetAngle"}, {"reference", "MissingRef"}}, "unresolved condition reference"},
        {nlohmann::json{{"function", "GetAngle"}, {"value", "5"}}, "comparison value must be a finite number"},
        {nlohmann::json{{"function", "GetAngle"}, {"value", 1e39}}, "comparison value must be a finite number"},
        {nlohmann::json{{"function", "GetAngle"}, {"runOn", "Nowhere"}}, "unknown runOn value"},
        {nlohmann::json{{"function", "GetAngle"}, {"flags", "OR, Bogus"}}, "unknown flags or UseGlobal outside compareGlobal"},
        {nlohmann::json{{"function", "GetAngle"}, {"flags", 3}}, "flags must be a string"},
        {nlohmann::json{{"function", "GetAngle"}, {"flags", "4"}}, "unknown flags or UseGlobal outside compareGlobal"},
        {nlohmann::json{{"function", "GetAngle"}, {"flags", "32"}}, "unknown flags or UseGlobal outside compareGlobal"},
        {nlohmann::json{{"function", "GetAngle"}, {"param1", 1.5}}, "param1 must be an integer or string"},
    };
    for (const auto& [element, reason] : rejected) {
        INFO("expected failure: " << reason);
        CHECK(failureOf(element) == reason);
    }
}

TEST_CASE("condition codec: every accepted operator spelling keeps its compare bits") {
    const std::array<std::pair<const char*, int>, 14> spellings{{
        {"==", 0}, {"=", 0}, {"EqualTo", 0},
        {"!=", 1}, {"<>", 1}, {"NotEqualTo", 1},
        {">", 2}, {"GreaterThan", 2},
        {">=", 3}, {"GreaterThanOrEqualTo", 3},
        {"<", 4}, {"LessThan", 4},
        {"<=", 5}, {"LessThanOrEqualTo", 5},
    }};
    for (const auto& [spelling, bits] : spellings) {
        const nlohmann::json element = {{"function", "GetAngle"}, {"operator", spelling}};
        std::string failure;
        const auto condition = buildFrom(element, failure);
        REQUIRE(condition.has_value());
        CHECK(condition->compare_operator() == bits);
    }

    const nlohmann::json absent = {{"function", "GetAngle"}};
    std::string failure;
    const auto defaulted = buildFrom(absent, failure);
    REQUIRE(defaulted.has_value());
    CHECK(defaulted->compare_operator() == 0);
}

TEST_CASE("condition codec: numeric runOn keeps the raw value that GetConditions emits") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"runOn", "8"}};
    std::string failure;
    const auto condition = buildFrom(element, failure);
    REQUIRE(condition.has_value());
    CHECK(condition->run_on == 8u);
}

TEST_CASE("condition codec: numeric flag equal to a named flag is accepted without touching the operator") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"operator", ">="}, {"flags", "8"}};
    std::string failure;
    const auto condition = buildFrom(element, failure);
    REQUIRE(condition.has_value());
    CHECK(failure.empty());
    CHECK(condition->flags() == 0x08);
    CHECK(condition->compare_operator() == 3);
    CHECK_FALSE(condition->use_global());
}

TEST_CASE("condition codec: compareGlobal resolves to a use-global entry and value defaults to 0") {
    std::string failure;

    const nlohmann::json global = {{"function", "GetAngle"}, {"compareGlobal", "CondWeapon"}};
    const auto globalCondition = buildFrom(global, failure);
    REQUIRE(globalCondition.has_value());
    CHECK(globalCondition->use_global());
    CHECK(globalCondition->comparison_raw == 0x00000800u);

    const nlohmann::json plain = {{"function", "GetAngle"}};
    const auto plainCondition = buildFrom(plain, failure);
    REQUIRE(plainCondition.has_value());
    CHECK_FALSE(plainCondition->use_global());
    CHECK(plainCondition->comparison_float() == 0.0f);
}

TEST_CASE("condition codec: a valid entry builds the same stored fields") {
    const nlohmann::json element = {
        {"function", "GetAngle"},
        {"operator", ">="},
        {"value", 5},
        {"param1", 2},
        {"param2", "CondWeapon"},
        {"runOn", "Reference"},
        {"reference", "CondWeapon"},
        {"flags", "OR, UsePackData"},
    };
    std::string failure;
    const auto condition = buildFrom(element, failure);
    REQUIRE(condition.has_value());
    CHECK(failure.empty());
    CHECK(condition->function == 8);
    CHECK(condition->compare_operator() == 3);
    CHECK(condition->flags() == 0x09);
    CHECK(condition->comparison_float() == 5.0f);
    CHECK(condition->param1 == 2);
    CHECK(condition->param2 == 0x800);
    CHECK(condition->run_on == 2u);
    CHECK(condition->reference == 0x800u);
    CHECK(condition->unknown2 == 0);
    CHECK(condition->unknown3 == -1);
    CHECK_FALSE(condition->use_global());
    CHECK_FALSE(condition->parameter1_string.has_value());
}

TEST_CASE("condition codec: an integer param string that is neither integer nor form ID fails the entry") {
    const nlohmann::json unknown = {{"function", "GetAngle"}, {"param1", "NoSuchForm"}};
    CHECK(failureOf(unknown) == "unresolved condition parameter");

    std::string failure;
    const nlohmann::json empty = {{"function", "GetAngle"}, {"param2", ""}};
    const auto emptyCondition = buildFrom(empty, failure);
    REQUIRE(emptyCondition.has_value());
    CHECK(emptyCondition->param2 == 0);

    const nlohmann::json integerText = {{"function", "GetAngle"}, {"param1", "12"}};
    const auto integer = buildFrom(integerText, failure);
    REQUIRE(integer.has_value());
    CHECK(integer->param1 == 12);

    const nlohmann::json placeholder = {{"function", "GetAngle"}, {"param1", "0A000801:"}};
    const auto placed = buildFrom(placeholder, failure);
    REQUIRE(placed.has_value());
    CHECK(static_cast<std::uint32_t>(placed->param1) == 0x0A000801u);
}
