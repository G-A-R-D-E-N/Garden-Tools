#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fo4ide/plugin/ConditionCodec.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>

#include <cstring>
#include <limits>

namespace {

std::uint32_t floatBits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

fo4::Plugin context() {
    fo4::Plugin plugin;
    plugin.key = fo4::PluginKey::from_filename("Condition.esp");
    return plugin;
}

fo4::CtdaCondition storedCondition() {
    fo4::CtdaCondition condition;
    condition.function = 8;
    condition.comparison_raw = 0x3f800000;
    condition.padding = {std::byte{0xac}, std::byte{0x93}, std::byte{0x13}};
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
    second.padding[0] = std::byte{0x55};
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

TEST_CASE("condition codec stores integer params across the 32-bit range", "[condition-preservation]") {
    const auto plugin = context();
    std::string failure;

    const nlohmann::json low = {{"function", "GetAngle"}, {"param1", std::numeric_limits<std::int32_t>::min()}};
    const auto lowCondition = fo4ide::plugin::buildCondition(low, plugin, {}, failure);
    REQUIRE(lowCondition.has_value());
    CHECK(static_cast<std::uint32_t>(lowCondition->param1) == 0x80000000u);

    const nlohmann::json high = {{"function", "GetAngle"}, {"param2", std::numeric_limits<std::uint32_t>::max()}};
    const auto highCondition = fo4ide::plugin::buildCondition(high, plugin, {}, failure);
    REQUIRE(highCondition.has_value());
    CHECK(static_cast<std::uint32_t>(highCondition->param2) == 0xFFFFFFFFu);

    const nlohmann::json above = {{"function", "GetAngle"}, {"param1", 3000000000u}};
    const auto aboveCondition = fo4ide::plugin::buildCondition(above, plugin, {}, failure);
    REQUIRE(aboveCondition.has_value());
    CHECK(static_cast<std::uint32_t>(aboveCondition->param1) == 3000000000u);
}

TEST_CASE("condition codec rejects integer params outside the 32-bit range", "[condition-preservation]") {
    const auto plugin = context();
    std::string failure;

    const nlohmann::json below = {{"function", "GetAngle"},
                                  {"param1", static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - 1}};
    CHECK_FALSE(fo4ide::plugin::buildCondition(below, plugin, {}, failure).has_value());
    CHECK(failure == "param1 -2147483649 is outside [-2147483648, 4294967295]");

    const nlohmann::json above = {{"function", "GetAngle"},
                                  {"param2", static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1}};
    CHECK_FALSE(fo4ide::plugin::buildCondition(above, plugin, {}, failure).has_value());
    CHECK(failure == "param2 4294967296 is outside [-2147483648, 4294967295]");

    const nlohmann::json textAbove = {{"function", "GetAngle"}, {"param1", "4294967296"}};
    CHECK_FALSE(fo4ide::plugin::buildCondition(textAbove, plugin, {}, failure).has_value());
    CHECK(failure == "param1 4294967296 is outside [-2147483648, 4294967295]");
}

TEST_CASE("condition codec round trips unresolvable form IDs through describe", "[condition-preservation]") {
    fo4::Plugin plugin;
    plugin.key = fo4::PluginKey::from_filename("CondEdit.esp");

    fo4::CtdaCondition original;
    original.function = 8;
    original.flags_byte = 0x44;
    original.comparison_raw = 0x0B000802u;
    original.param1 = static_cast<std::int32_t>(0x0A000801u);
    original.run_on = 2;
    original.reference = 0x0A000803u;
    original.unknown3 = -1;

    auto json = fo4ide::plugin::describeCondition(original, plugin);
    CHECK(json.at("param1") == "0A000801:");
    CHECK(json.at("compareGlobal") == "0B000802:");
    CHECK(json.at("reference") == "0A000803:");
    json.erase("_native");

    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(failure.empty());
    checkSame(*rebuilt, original);
}

TEST_CASE("condition codec round trips a zero compareGlobal as raw zero", "[condition-preservation]") {
    fo4::Plugin plugin;
    plugin.key = fo4::PluginKey::from_filename("CondEdit.esp");

    fo4::CtdaCondition original;
    original.function = 8;
    original.flags_byte = 0x04;

    auto json = fo4ide::plugin::describeCondition(original, plugin);
    CHECK(json.at("compareGlobal") == "00000000:");
    json.erase("_native");

    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->use_global());
    CHECK(rebuilt->comparison_raw == 0u);
}

TEST_CASE("condition codec zeroes a reference when runOn leaves Reference", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.run_on = 2;
    original.reference = 0x0A000803u;
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    json["runOn"] = "Subject";
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(failure.empty());
    CHECK(rebuilt->run_on == 0u);
    CHECK(rebuilt->reference == 0u);
}

TEST_CASE("condition codec refuses a reference set alongside a non-Reference runOn", "[condition-preservation]") {
    const auto plugin = context();
    auto json = fo4ide::plugin::describeCondition(storedCondition(), plugin);
    json["runOn"] = "Subject";
    json["reference"] = "0A000803:";
    std::string failure;
    CHECK_FALSE(fo4ide::plugin::buildCondition(json, plugin, {}, failure).has_value());
    CHECK(failure == "reference requires runOn Reference");
}

TEST_CASE("condition codec keeps a reference on a Reference runOn row when only param1 changes", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.run_on = 2;
    original.reference = 0x0A000803u;
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    json["param1"] = 7;
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(failure.empty());
    CHECK(rebuilt->run_on == 2u);
    CHECK(rebuilt->reference == 0x0A000803u);
}

TEST_CASE("condition codec stores Float params as float bits", "[condition-preservation]") {
    const auto plugin = context();
    std::string failure;

    const nlohmann::json fractional = {{"function", "GetWithinDistance"}, {"param2", 100.5}};
    const auto fractionalCondition = fo4ide::plugin::buildCondition(fractional, plugin, {}, failure);
    REQUIRE(fractionalCondition.has_value());
    CHECK(static_cast<std::uint32_t>(fractionalCondition->param2) == floatBits(100.5f));

    const nlohmann::json whole = {{"function", "GetWithinDistance"}, {"param2", 100}};
    const auto wholeCondition = fo4ide::plugin::buildCondition(whole, plugin, {}, failure);
    REQUIRE(wholeCondition.has_value());
    CHECK(static_cast<std::uint32_t>(wholeCondition->param2) == floatBits(100.0f));

    const nlohmann::json text = {{"function", "GetWithinDistance"}, {"param2", "100.5"}};
    const auto textCondition = fo4ide::plugin::buildCondition(text, plugin, {}, failure);
    REQUIRE(textCondition.has_value());
    CHECK(static_cast<std::uint32_t>(textCondition->param2) == floatBits(100.5f));

    const nlohmann::json notNumber = {{"function", "GetWithinDistance"}, {"param2", "far"}};
    CHECK_FALSE(fo4ide::plugin::buildCondition(notNumber, plugin, {}, failure).has_value());
    CHECK(failure == "param2 far is not a number");

    const nlohmann::json notFinite = {{"function", "GetWithinDistance"}, {"param2", "nan"}};
    CHECK_FALSE(fo4ide::plugin::buildCondition(notFinite, plugin, {}, failure).has_value());
    CHECK(failure == "param2 must be a finite number");
}

TEST_CASE("condition codec round trips an unedited Float param through describe", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.function = *fo4ide::plugin::functionIndex("GetWithinDistance");
    original.run_on = 2;
    original.reference = 0x0A000803u;
    original.param2 = static_cast<std::int32_t>(floatBits(100.5f));
    auto json = nlohmann::json::parse(fo4ide::plugin::describeCondition(original, plugin).dump());
    CHECK(json.at("param2") == 100.5);
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(failure.empty());
    checkSame(*rebuilt, original);
}

TEST_CASE("condition codec accepts only the exact placeholder form for unresolvable form IDs", "[condition-preservation]") {
    const auto plugin = context();
    for (const char* text : {"", "000801:", "0A000801", "0A0008010:", "0x0A000801:", "0A00080G:"}) {
        std::string failure;
        const nlohmann::json element = {{"function", "GetAngle"}, {"reference", text}};
        CHECK_FALSE(fo4ide::plugin::buildCondition(element, plugin, {}, failure).has_value());
        CHECK_FALSE(failure.empty());
    }
}
