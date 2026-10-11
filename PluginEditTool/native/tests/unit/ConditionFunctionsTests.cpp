#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <fo4ide/plugin/ConditionFunctions.hpp>

// The C# has no dedicated ConditionFunctions test class; these cases pin the
// native port against the oracle's observable contract: the table contents
// (extracted verbatim from ConditionFunctions.cs), the ParamsOf/EditorFor
// lookups, the AsJson shape, and the Mutagen enum name lists that
// WriteService.ConditionFunctionNames / ConditionRunOnNames serialize.

namespace {

using fo4ide::plugin::ConditionFunctions;
using fo4ide::plugin::conditionFunctionNames;
using fo4ide::plugin::conditionRunOnNames;

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

nlohmann::json parseJson(const std::string& text) {
    return nlohmann::json::parse(text);
}

}  // namespace

TEST_CASE("condition functions: the table has the oracle's 220 entries, all unique") {
    CHECK(ConditionFunctions::count() == 220);

    // Every name is unique case-insensitively (the C# ByName dictionary would
    // silently collapse duplicates).
    std::set<std::string> seen;
    const auto json = parseJson(ConditionFunctions::asJson());
    REQUIRE(json.is_object());
    CHECK(json.size() == 220);
    for (const auto& [name, params] : json.items()) {
        INFO("function: " + name);
        CHECK_FALSE(name.empty());
        CHECK(seen.insert(lower(name)).second);
        CHECK(params.is_array());
    }
}

TEST_CASE("condition functions: paramsOf recovers the declared parameter types") {
    const auto angle = ConditionFunctions::paramsOf("GetAngle");
    REQUIRE(angle.size() == 1);
    CHECK(angle[0] == "Axis");

    const auto eventData = ConditionFunctions::paramsOf("GetEventData");
    REQUIRE(eventData.size() == 2);
    CHECK(eventData[0] == "Event");
    CHECK(eventData[1] == "EventData");

    const auto keyword = ConditionFunctions::paramsOf("HasKeyword");
    REQUIRE(keyword.size() == 1);
    CHECK(keyword[0] == "Keyword");

    const auto playerControls = ConditionFunctions::paramsOf("GetPlayerControlsDisabled");
    REQUIRE(playerControls.size() == 3);
    CHECK(playerControls[0] == "Integer");
    CHECK(playerControls[1] == "Integer");
    CHECK(playerControls[2] == "Integer");

    const auto none = ConditionFunctions::paramsOf("IsSwimming");
    CHECK(none.empty());
}

TEST_CASE("condition functions: paramsOf lookup is case-insensitive") {
    CHECK(ConditionFunctions::paramsOf("getangle") == ConditionFunctions::paramsOf("GetAngle"));
    CHECK(ConditionFunctions::paramsOf("GETANGLE") == ConditionFunctions::paramsOf("GetAngle"));
    CHECK(ConditionFunctions::paramsOf("noSuchFunction").empty());
    CHECK(ConditionFunctions::paramsOf("").empty());
}

TEST_CASE("condition functions: editorFor classifies parameter types") {
    CHECK(ConditionFunctions::editorFor("Actor") ==
          std::make_pair(std::string("record"), std::string("PlacedNpc")));
    CHECK(ConditionFunctions::editorFor("Reference") ==
          std::make_pair(std::string("record"), std::string("PlacedObject,PlacedNpc")));
    CHECK(ConditionFunctions::editorFor("EffectItem") ==
          std::make_pair(std::string("record"),
                         std::string("Ingestible,ObjectEffect,Ingredient,Spell")));
    CHECK(ConditionFunctions::editorFor("BaseObject") ==
          std::make_pair(std::string("record"), std::string("")));
    CHECK(ConditionFunctions::editorFor("WorldSpace") ==
          std::make_pair(std::string("record"), std::string("Worldspace")));

    CHECK(ConditionFunctions::editorFor("String") ==
          std::make_pair(std::string("text"), std::string("")));
    CHECK(ConditionFunctions::editorFor("Event") ==
          std::make_pair(std::string("text"), std::string("")));

    CHECK(ConditionFunctions::editorFor("Integer") ==
          std::make_pair(std::string("number"), std::string("")));
    CHECK(ConditionFunctions::editorFor("Float") ==
          std::make_pair(std::string("number"), std::string("")));
    CHECK(ConditionFunctions::editorFor("Axis") ==
          std::make_pair(std::string("number"), std::string("")));
    CHECK(ConditionFunctions::editorFor("Unknown") ==
          std::make_pair(std::string("number"), std::string("")));
}

TEST_CASE("condition functions: editorFor is case-insensitive") {
    CHECK(ConditionFunctions::editorFor("actor") == ConditionFunctions::editorFor("Actor"));
    CHECK(ConditionFunctions::editorFor("STRING") == ConditionFunctions::editorFor("String"));
}

TEST_CASE("condition functions: asJson matches the per-function descriptor contract") {
    const auto json = parseJson(ConditionFunctions::asJson());
    REQUIRE(json.is_object());

    // Every entry is an array of {label, kind, types} descriptors whose
    // contents agree with ParamsOf and EditorFor.
    for (const auto& [name, params] : json.items()) {
        const auto declared = ConditionFunctions::paramsOf(name);
        REQUIRE(params.size() == declared.size());
        for (std::size_t i = 0; i < params.size(); i++) {
            const auto& descriptor = params[i];
            REQUIRE(descriptor.is_object());
            const auto kind = descriptor.value("kind", "");
            const auto types = descriptor.value("types", "");
            const auto [expectedKind, expectedTypes] = ConditionFunctions::editorFor(declared[i]);
            CHECK(kind == expectedKind);
            CHECK(types == expectedTypes);
        }
    }
}

TEST_CASE("condition functions: labels insert a space at pascal-case boundaries") {
    // A descriptor describes the parameter TYPE, not the function: the label
    // is the spaced type name, and the editor classifies the type.
    const auto json = parseJson(ConditionFunctions::asJson());
    const auto angle = json["GetAngle"];
    REQUIRE(angle.is_array());
    REQUIRE(angle.size() == 1);
    CHECK(angle[0]["label"] == "Axis");
    CHECK(angle[0]["kind"] == "number");
    CHECK(angle[0]["types"] == "");

    const auto data = json["GetEventData"];
    REQUIRE(data.is_array());
    REQUIRE(data.size() == 2);
    CHECK(data[0]["label"] == "Event");
    CHECK(data[0]["kind"] == "text");
    CHECK(data[1]["label"] == "Event Data");
    CHECK(data[1]["kind"] == "record");
    CHECK(data[1]["types"] == "Location,Keyword,FormList");

    const auto keyword = json["HasKeyword"];
    REQUIRE(keyword.is_array());
    REQUIRE(keyword.size() == 1);
    CHECK(keyword[0]["label"] == "Keyword");
    CHECK(keyword[0]["kind"] == "record");
    CHECK(keyword[0]["types"] == "Keyword");
}

TEST_CASE("condition functions: the enum name list is the full 479, sorted case-insensitively") {
    const auto json = parseJson(conditionFunctionNames());
    REQUIRE(json.is_array());
    CHECK(json.size() == 479);

    std::set<std::string> seen;
    std::string previous;
    for (const auto& name : json) {
        const auto text = name.get<std::string>();
        INFO("name: " + text);
        CHECK(seen.insert(text).second);  // every enum name is unique
        CHECK_FALSE(text.empty());
        CHECK(std::isalpha(static_cast<unsigned char>(text[0])) != 0);
        if (!previous.empty()) {
            CHECK(lower(previous) <= lower(text));  // OrdinalIgnoreCase sort
        }
        previous = text;
    }

    // Spot-check the ends of the alphabet.
    CHECK(json[0] == "CanFlyHere");
    CHECK(json[json.size() - 1] == "WouldRefuseCommand");
}

TEST_CASE("condition functions: the run-on type list is the 11 enum names in declaration order") {
    const auto json = parseJson(conditionRunOnNames());
    REQUIRE(json.is_array());
    const std::vector<std::string> expected = {
        "Subject",       "Target",       "Reference",    "CombatTarget",  "LinkedReference",
        "QuestAlias",    "PackageData",  "EventData",    "CommandTarget", "EventCameraRef",
        "MyKiller",
    };
    REQUIRE(json.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); i++) {
        CHECK(json[i] == expected[i]);
    }
}

TEST_CASE("condition functions: index<->name lookups match the Mutagen enum") {
    using namespace fo4ide::plugin;

    // Known spot checks from the enum: GetWantBlocking=0, GetDistance=1,
    // GetAngle=8. 4672 is not a CTDA function index (GetEventData is 576), so it has no name.
    CHECK(functionName(0) == "GetWantBlocking");
    CHECK(functionName(1) == "GetDistance");
    CHECK(functionName(8) == "GetAngle");
    CHECK(functionName(4672).empty());
    CHECK(functionName(9999).empty());

    CHECK(functionIndex("GetDistance") == std::optional<std::uint16_t>{std::uint16_t{1}});
    CHECK(functionIndex("getdistance") == std::optional<std::uint16_t>{std::uint16_t{1}});
    CHECK(functionIndex("GetAngle") == std::optional<std::uint16_t>{std::uint16_t{8}});
    CHECK(functionIndex("NotAFunction") == std::nullopt);

    // The 5 names the 474-count list was missing are present and resolvable.
    for (const auto* name : {"EPMagic_SpellHasKeyword", "EPMagic_SpellHasSkill",
                             "ObjectTemplateItem_GetLevel", "ObjectTemplateItem_HasKeyword",
                             "ObjectTemplateItem_HasUniqueKeyword"}) {
        CHECK(functionIndex(name).has_value());
        CHECK(functionName(*functionIndex(name)) == name);
    }

    // Every JSON name round-trips through the index mapping.
    const auto json = parseJson(conditionFunctionNames());
    for (const auto& name : json) {
        const auto text = name.get<std::string>();
        const auto idx = functionIndex(text);
        REQUIRE(idx.has_value());
        CHECK(functionName(*idx) == text);
    }
}

TEST_CASE("condition functions: run-on value<->name lookups match the Mutagen enum") {
    using namespace fo4ide::plugin;

    CHECK(runOnName(0) == "Subject");
    CHECK(runOnName(7) == "EventData");
    CHECK(runOnName(8) == "CommandTarget");
    CHECK(runOnName(9) == "EventCameraRef");
    CHECK(runOnName(10) == "MyKiller");
    CHECK(runOnName(11).empty());
    CHECK(runOnName(99).empty());

    CHECK(runOnValue("Target") == std::optional<std::uint32_t>{1});
    CHECK(runOnValue("reference") == std::optional<std::uint32_t>{2});  // ignoreCase
    CHECK(runOnValue("CommandTarget") == std::optional<std::uint32_t>{8});
    CHECK(runOnValue("MyKiller") == std::optional<std::uint32_t>{10});
    CHECK(runOnValue("NotARunOn") == std::nullopt);
}
