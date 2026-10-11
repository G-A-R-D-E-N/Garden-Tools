#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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

}  // namespace

TEST_CASE("condition codec: GetEventData rows keep param1 and param2 through describe and build") {
    fo4::CtdaCondition condition;
    condition.function = 576;
    condition.param1 = 0x4C430041;
    condition.param2 = 0x00012345;
    const fo4::Plugin plugin{};

    auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("function") == "GetEventData");
    REQUIRE(described.contains("param1"));
    REQUIRE(described.contains("param2"));
    REQUIRE(described.at("param1").get<std::int32_t>() == condition.param1);
    REQUIRE(described.at("param2").get<std::int32_t>() == condition.param2);
    described.erase("_native");

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
    condition.param1 = 0;
    condition.param2 = floatBits(5.5f);
    const fo4::Plugin plugin{};

    auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("param2").is_number_float());
    CHECK(described.at("param2") == 5.5);
    described.erase("_native");

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

        auto described = fo4ide::plugin::describeCondition(condition, plugin);
        REQUIRE(described.at("param1").is_number_integer());
        CHECK(described.at("param1") == axis);
        described.erase("_native");

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

    auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("param2") == "0x7FC00000");
    described.erase("_native");

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
    auto described = fo4ide::plugin::describeCondition(condition, plugin);
    REQUIRE(described.at("param1") == -3);
    described.erase("_native");
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
    CHECK(fo4ide::plugin::buildCondition({{"function", "GetEventData"}}, plugin, {}, failure).has_value());
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

    const nlohmann::json wrapped = {{"function", "GetAngle"}, {"param1", std::numeric_limits<std::uint64_t>::max()}};
    CHECK_FALSE(fo4ide::plugin::buildCondition(wrapped, plugin, {}, failure).has_value());
    CHECK(failure == "param1 18446744073709551615 is outside [-2147483648, 4294967295]");
}

TEST_CASE("condition codec round trips unresolvable form IDs through describe", "[condition-preservation]") {
    fo4::Plugin plugin;
    plugin.key = fo4::PluginKey::from_filename("CondEdit.esp");

    fo4::CtdaCondition original;
    original.function = *fo4ide::plugin::functionIndex("GetIsID");
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

TEST_CASE("condition codec refuses non-numeric and non-finite Float params", "[condition-preservation]") {
    const auto plugin = context();
    std::string failure;

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
    original.param2 = floatBits(100.5f);
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
        const nlohmann::json element = {{"function", "GetAngle"}, {"runOn", "Reference"}, {"reference", text}};
        CHECK_FALSE(fo4ide::plugin::buildCondition(element, plugin, {}, failure).has_value());
        CHECK(failure == "unresolved condition reference");
    }
}

TEST_CASE("condition codec keeps CIS text as UTF-8 in JSON and Windows-1252 on disk", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.function = *fo4ide::plugin::functionIndex("HasVMScript");
    original.parameter1_string = "caf\xE9";
    original.parameter2_string.reset();
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    CHECK_NOTHROW(json.dump());
    CHECK(json.at("cis1") == "caf\xC3\xA9");
    CHECK(json.at("_native").at("cis1") == "caf\xC3\xA9");
    std::string failure;
    const auto unchanged = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(unchanged.has_value());
    CHECK(unchanged->parameter1_string == "caf\xE9");
    CHECK_FALSE(unchanged->parameter2_string.has_value());
    json["cis1"] = "na\xC3\xAFve";
    const auto edited = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(edited.has_value());
    CHECK(edited->parameter1_string == "na\xEFve");
    json["cis1"] = "\xE4\xB8\xAD";
    CHECK_FALSE(fo4ide::plugin::buildCondition(json, plugin, {}, failure).has_value());
    CHECK(failure == "cis1 is not valid UTF-8 text representable in Windows-1252");
    const nlohmann::json fresh = {{"function", "HasVMScript"}, {"cis1", "caf\xC3\xA9"}};
    const auto built = fo4ide::plugin::buildCondition(fresh, plugin, {}, failure);
    REQUIRE(built.has_value());
    CHECK(built->parameter1_string == "caf\xE9");
    CHECK_FALSE(fo4ide::plugin::buildCondition({{"function", "GetDistance"}, {"cis1", "x"}}, plugin, {}, failure).has_value());
    CHECK(failure == "cis1 is only valid when parameter 1 is String or Event");
}

TEST_CASE("condition codec round trips CIS2 on a String slot both ways", "[condition-preservation]") {
    const auto plugin = context();
    fo4::CtdaCondition condition;
    condition.function = *fo4ide::plugin::functionIndex("GetVMScriptVariable");
    condition.parameter2_string = "caf\xE9";
    auto json = fo4ide::plugin::describeCondition(condition, plugin);
    CHECK(json.at("cis2") == "caf\xC3\xA9");
    CHECK_FALSE(json.contains("cis1"));
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->parameter2_string == "caf\xE9");
    const nlohmann::json fresh = {{"function", "GetVMScriptVariable"}, {"cis2", "caf\xC3\xA9"}};
    const auto built = fo4ide::plugin::buildCondition(fresh, plugin, {}, failure);
    REQUIRE(built.has_value());
    CHECK(built->parameter2_string == "caf\xE9");
    CHECK_FALSE(built->parameter1_string.has_value());
}

TEST_CASE("condition codec restores a preserved CIS1 from native metadata alone", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.function = *fo4ide::plugin::functionIndex("HasVMScript");
    original.parameter1_string = "caf\xE9";
    original.parameter2_string.reset();
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    json.erase("cis1");
    std::string failure;
    const auto rebuilt = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(rebuilt.has_value());
    checkSame(*rebuilt, original);
    CHECK(rebuilt->parameter1_string == "caf\xE9");
}

TEST_CASE("condition codec refuses a top-level CIS1 with an embedded NUL", "[condition-preservation]") {
    const auto plugin = context();
    const auto element = nlohmann::json::parse(R"({"function":"HasVMScript","cis1":"\u0000"})");
    std::string failure;
    CHECK_FALSE(fo4ide::plugin::buildCondition(element, plugin, {}, failure).has_value());
    CHECK(failure == "cis1 is not valid UTF-8 text representable in Windows-1252");
}

TEST_CASE("condition codec clears a preserved CIS1 when cis1 is omitted and listed as edited", "[condition-preservation]") {
    const auto plugin = context();
    auto original = storedCondition();
    original.function = *fo4ide::plugin::functionIndex("HasVMScript");
    original.parameter1_string = "caf\xE9";
    original.parameter2_string.reset();
    auto json = fo4ide::plugin::describeCondition(original, plugin);
    json.erase("cis1");
    json["_edited"] = nlohmann::json::array({"cis1"});
    std::string failure;
    const auto cleared = fo4ide::plugin::buildCondition(json, plugin, {}, failure);
    REQUIRE(cleared.has_value());
    CHECK_FALSE(cleared->parameter1_string.has_value());
}
