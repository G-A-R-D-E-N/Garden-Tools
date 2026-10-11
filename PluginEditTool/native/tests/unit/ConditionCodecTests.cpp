#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>

namespace {

fo4::Plugin context() {
    fo4::Plugin plugin;
    plugin.key = fo4::PluginKey::from_filename("Condition.esp");
    return plugin;
}

fo4::CtdaCondition storedCondition() {
    fo4::CtdaCondition condition;
    condition.function = 8;
    condition.comparison_raw = 0x3f800000;
    condition.opaque_prefix = {std::byte{0xac}, std::byte{0x93}, std::byte{0x13}};
    condition.unknown2 = 0x1234;
    condition.unknown3 = 123;
    condition.parameter1_string = "first";
    condition.parameter2_string = "second";
    return condition;
}

void checkSame(const fo4::CtdaCondition& actual, const fo4::CtdaCondition& expected) {
    CHECK(fo4::serialize_ctda_condition(actual) == fo4::serialize_ctda_condition(expected));
    CHECK(actual.parameter1_string == expected.parameter1_string);
    CHECK(actual.parameter2_string == expected.parameter2_string);
}

}

TEST_CASE("condition codec preserves unchanged stored rows across JSON transport", "[condition-preservation]") {
    auto original = storedCondition();
    original.comparison_raw = GENERATE(0x3f800000u, 0x80000000u, 0x7fc01234u);
    original.function = GENERATE(std::uint16_t{8}, std::uint16_t{65000});
    original.flags_byte = GENERATE(std::uint8_t{0}, std::uint8_t{0xc0});
    const auto plugin = context();
    const auto json = nlohmann::json::parse(fo4ide::plugin::describeCondition(original, plugin).dump());
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(failure.empty());
    checkSame(*rebuilt, original);
}

TEST_CASE("condition codec changes a scalar while retaining stored fields", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    json["value"] = 2.0;
    original.comparison_raw = 0x40000000;
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    checkSame(*rebuilt, original);
}

TEST_CASE("condition codec distinguishes browser zero normalization from an explicit edit", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.comparison_raw = 0x80000000;
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    json["value"] = 0;
    std::string failure;
    const auto unchanged = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(unchanged.has_value());
    checkSame(*unchanged, original);
    json["_edited"] = nlohmann::json::array({"value"});
    const auto edited = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(edited.has_value());
    original.comparison_raw = 0;
    checkSame(*edited, original);
}

TEST_CASE("condition codec retains row identity when rows move", "[condition-preservation]") {
    const auto plugin = context();
    const auto first = storedCondition();
    auto second = first;
    second.opaque_prefix[0] = std::byte{0x55};
    second.parameter1_string = "other";
    auto rows = nlohmann::json::array({fo4ide::plugin::describeCondition(first, plugin),
                                      fo4ide::plugin::describeCondition(second, plugin)});
    std::swap(rows[0], rows[1]);
    std::string failure;
    const auto a = fo4ide::plugin::buildCondition(rows[0], plugin, {}, failure);
    const auto b = fo4ide::plugin::buildCondition(rows[1], plugin, {}, failure);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    checkSame(*a, second);
    checkSame(*b, first);
}

TEST_CASE("condition codec keeps event shape unchanged and refuses unsupported event edits", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.function = fo4::k_ctda_event_function_index;
    original.comparison_raw = 0;
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    checkSame(*rebuilt, original);
    json["value"] = 2;
    CHECK_FALSE(fo4ide::plugin::buildCondition(json, plugin, {}, failure).has_value());
    CHECK_FALSE(failure.empty());
    CHECK_FALSE(fo4ide::plugin::buildCondition({{"function", "GetEventData"}}, plugin, {}, failure).has_value());
}

TEST_CASE("condition codec refuses malformed preservation data and invalid edits", "[condition-preservation]") {
    const auto plugin = context();
    const auto original = storedCondition();
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    const auto change = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    if (change == 0) json["_native"] = "bad";
    if (change == 1) json["_native"]["ctda"] = nlohmann::json::array({999});
    if (change == 2) json["value"] = "bad";
    if (change == 3) json["operator"] = "bogus";
    if (change == 4) json["reference"] = "NotFound";
    if (change == 5) json["param1"] = 0x100000000ll;
    if (change == 6) json["_edited"] = "value";
    if (change == 7) json["_edited"] = nlohmann::json::array({"_native"});
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    CHECK_FALSE(rebuilt.has_value());
    CHECK_FALSE(failure.empty());
}
