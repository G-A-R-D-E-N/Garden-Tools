#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

TEST_CASE("condition codec: unknown operator fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"operator", "~="}, {"value", 1}};
    CHECK(failureOf(element) == "unknown operator '~='");
}

TEST_CASE("condition codec: non-string operator fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"operator", 2}};
    CHECK(failureOf(element) == "unknown operator '2'");
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

TEST_CASE("condition codec: unresolved compareGlobal fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"compareGlobal", "MissingGlobal"}};
    CHECK(failureOf(element) == "compareGlobal 'MissingGlobal' does not resolve to a form ID");
}

TEST_CASE("condition codec: unresolved reference fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"reference", "MissingRef"}};
    CHECK(failureOf(element) == "reference 'MissingRef' does not resolve to a form ID");
}

TEST_CASE("condition codec: string value fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"value", "5"}};
    CHECK(failureOf(element) == "value must be a number");
}

TEST_CASE("condition codec: value outside the float range fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"value", 1e39}};
    CHECK(failureOf(element) == "value out of float range");
}

TEST_CASE("condition codec: unknown runOn fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"runOn", "Nowhere"}};
    CHECK(failureOf(element) == "unknown runOn 'Nowhere'");
}

TEST_CASE("condition codec: numeric runOn keeps the raw value that GetConditions emits") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"runOn", "8"}};
    std::string failure;
    const auto condition = buildFrom(element, failure);
    REQUIRE(condition.has_value());
    CHECK(condition->run_on == 8u);
}

TEST_CASE("condition codec: unknown flag name fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"flags", "OR, Bogus"}};
    CHECK(failureOf(element) == "unknown flag 'Bogus'");
}

TEST_CASE("condition codec: non-string flags fail the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"flags", 3}};
    CHECK(failureOf(element) == "flags must be a string");
}

TEST_CASE("condition codec: numeric flag carrying the Use Global bit fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"flags", "4"}};
    CHECK(failureOf(element) == "unknown flag '4'");
}

TEST_CASE("condition codec: numeric flag carrying an operator bit fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"flags", "32"}};
    CHECK(failureOf(element) == "unknown flag '32'");
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

TEST_CASE("condition codec: integer params store the 32-bit range edges") {
    std::string failure;

    const nlohmann::json low = {{"function", "GetAngle"},
                                {"param1", std::numeric_limits<std::int32_t>::min()}};
    const auto lowCondition = buildFrom(low, failure);
    REQUIRE(lowCondition.has_value());
    CHECK(lowCondition->param1 == std::numeric_limits<std::int32_t>::min());
    CHECK(static_cast<std::uint32_t>(lowCondition->param1) == 0x80000000u);

    const nlohmann::json high = {{"function", "GetAngle"},
                                 {"param2", std::numeric_limits<std::uint32_t>::max()}};
    const auto highCondition = buildFrom(high, failure);
    REQUIRE(highCondition.has_value());
    CHECK(highCondition->param2 == -1);
    CHECK(static_cast<std::uint32_t>(highCondition->param2) == 0xFFFFFFFFu);

    const nlohmann::json above = {{"function", "GetAngle"}, {"param1", 3000000000u}};
    const auto aboveCondition = buildFrom(above, failure);
    REQUIRE(aboveCondition.has_value());
    CHECK(static_cast<std::uint32_t>(aboveCondition->param1) == 3000000000u);
}

TEST_CASE("condition codec: integer params reject values outside the 32-bit range") {
    const nlohmann::json below = {
        {"function", "GetAngle"},
        {"param1", static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - 1}};
    CHECK(failureOf(below) == "param1 -2147483649 is outside [-2147483648, 4294967295]");

    const nlohmann::json above = {
        {"function", "GetAngle"},
        {"param2", static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1}};
    CHECK(failureOf(above) == "param2 4294967296 is outside [-2147483648, 4294967295]");

    const nlohmann::json textAbove = {{"function", "GetAngle"}, {"param1", "4294967296"}};
    CHECK(failureOf(textAbove) == "param1 4294967296 is outside [-2147483648, 4294967295]");
}

TEST_CASE("condition codec: non-integer param fails the entry") {
    const nlohmann::json element = {{"function", "GetAngle"}, {"param1", 1.5}};
    CHECK(failureOf(element) == "param1 must be an integer or form ID text");
}

TEST_CASE("condition codec: compareGlobal resolves to a use-global entry and value defaults to 1") {
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
    CHECK(plainCondition->comparison_float() == 1.0f);
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

TEST_CASE("condition codec: unresolvable form IDs round trip through describeCondition exactly") {
    fo4::Plugin plugin{};
    plugin.key = fo4::PluginKey::from_filename("CondEdit.esp");

    fo4::CtdaCondition original;
    original.function = 8;
    original.flags_byte = 0x44;
    original.comparison_raw = 0x0B000802u;
    original.param1 = static_cast<std::int32_t>(0x0A000801u);
    original.reference = 0x0A000803u;
    original.unknown3 = -1;

    const auto described = fo4ide::plugin::describeCondition(original, plugin);
    CHECK(described.at("param1") == "0A000801:");
    CHECK(described.at("compareGlobal") == "0B000802:");
    CHECK(described.at("reference") == "0A000803:");

    std::string failure;
    const auto rebuilt = buildFrom(described, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(failure.empty());
    CHECK(rebuilt->function == original.function);
    CHECK(rebuilt->flags_byte == original.flags_byte);
    CHECK(rebuilt->comparison_raw == original.comparison_raw);
    CHECK(rebuilt->param1 == original.param1);
    CHECK(rebuilt->param2 == original.param2);
    CHECK(rebuilt->run_on == original.run_on);
    CHECK(rebuilt->reference == original.reference);
    CHECK(rebuilt->unknown2 == original.unknown2);
    CHECK(rebuilt->unknown3 == original.unknown3);
}

TEST_CASE("condition codec: a zero compareGlobal round trips as raw zero") {
    fo4::Plugin plugin{};
    plugin.key = fo4::PluginKey::from_filename("CondEdit.esp");

    fo4::CtdaCondition original;
    original.function = 8;
    original.flags_byte = 0x04;

    const auto described = fo4ide::plugin::describeCondition(original, plugin);
    CHECK(described.at("compareGlobal") == "00000000:");

    std::string failure;
    const auto rebuilt = buildFrom(described, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->use_global());
    CHECK(rebuilt->comparison_raw == 0u);
}

TEST_CASE("condition codec: only the exact placeholder form is accepted for unresolvable form IDs") {
    for (const char* text : {"", "000801:", "0A000801", "0A0008010:", "0x0A000801:", "0A00080G:"}) {
        const nlohmann::json element = {{"function", "GetAngle"}, {"reference", text}};
        CHECK(failureOf(element) == std::string("reference '") + text + "' does not resolve to a form ID");
    }
}

TEST_CASE("condition codec: an integer param string that is neither integer nor form ID fails the entry") {
    const nlohmann::json unknown = {{"function", "GetAngle"}, {"param1", "NoSuchForm"}};
    CHECK(failureOf(unknown) == "param1 'NoSuchForm' is not an integer or form ID");

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
