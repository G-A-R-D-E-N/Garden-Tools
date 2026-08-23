#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>

using fo4ide::graph::GraphDocument;
using fo4ide::graph::GraphKind;
using fo4ide::graph::GraphNode;
using fo4ide::graph::GraphNodeKind;
using fo4ide::graph::GraphPinValue;
using fo4ide::graph::GraphScriptHeader;
using fo4ide::graph::GraphVariable;
using fo4ide::graph::GraphWire;
using fo4ide::graph::PinRef;
namespace builtinDefs = fo4ide::graph::builtinDefs;
namespace codes = fo4ide::graph::codes;
namespace documentJson = fo4ide::graph::documentJson;
namespace pinIds = fo4ide::graph::pinIds;

namespace {

GraphDocument sample() {
    GraphDocument document;
    document.id = "doc1";
    document.header = GraphScriptHeader{
        .scriptName = "MyMod:DoorScript",
        .extends = "ObjectReference",
        .flags = {"Conditional"},
        .imports = {"Debug"},
        .docComment = "A door.",
        .autoState = "Waiting",
    };
    GraphVariable variable;
    variable.name = "Target";
    variable.type = "ObjectReference";
    variable.isProperty = true;
    document.variables.push_back(std::move(variable));

    GraphNode eventNode;
    eventNode.id = "n1";
    eventNode.definition = "event:ObjectReference.OnActivate";
    eventNode.kind = GraphNodeKind::EventEntry;
    eventNode.x = 40;
    eventNode.y = 120;

    GraphNode callNode;
    callNode.id = "n2";
    callNode.definition = "global:Debug.Notification";
    callNode.kind = GraphNodeKind::Call;
    callNode.x = 380;
    callNode.y = 120;
    callNode.pinValues["arg:asNotificationText"] =
        GraphPinValue{.type = "string", .value = "\"opened\""};

    document.nodes.push_back(std::move(eventNode));
    document.nodes.push_back(std::move(callNode));

    document.wires.push_back(GraphWire{
        .id = "w1",
        .from = PinRef{"n1", std::string(pinIds::kExec)},
        .to = PinRef{"n2", std::string(pinIds::kExec)},
    });
    return document;
}

}  // namespace

TEST_CASE("A document survives a round trip unchanged") {
    const auto json = documentJson::serialize(sample());
    auto result = documentJson::tryDeserialize(json);
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.error.has_value());

    const auto& back = *result.document;
    CHECK(back.id == "doc1");
    CHECK(back.schema == GraphDocument::kCurrentSchema);
    CHECK(back.header.scriptName == "MyMod:DoorScript");
    CHECK(back.header.extends == "ObjectReference");
    REQUIRE(back.header.flags.size() == 1);
    CHECK(back.header.flags[0] == "Conditional");
    REQUIRE(back.header.imports.size() == 1);
    CHECK(back.header.imports[0] == "Debug");
    CHECK(back.header.docComment == "A door.");
    CHECK(back.header.autoState == "Waiting");
    REQUIRE(back.variables.size() == 1);
    CHECK(back.variables[0].isProperty);

    REQUIRE(back.nodes.size() == 2);
    const auto* n2 = back.node("n2");
    REQUIRE(n2 != nullptr);
    const auto it = n2->pinValues.find("arg:asNotificationText");
    REQUIRE(it != n2->pinValues.end());
    CHECK(it->second.value == "\"opened\"");

    REQUIRE(back.wires.size() == 1);
    CHECK(back.wires[0].from == PinRef("n1", "exec"));
    CHECK(back.wires[0].to == PinRef("n2", "exec"));
}

TEST_CASE("The json is camel cased to match the rest of the bridge") {
    const auto json = nlohmann::json::parse(documentJson::serialize(sample()));

    CHECK(json.contains("schema"));
    CHECK(json.contains("header"));
    CHECK(json.at("header").at("scriptName").is_string());
    CHECK(json.at("nodes")[0].at("def").is_string());
    CHECK(json.at("wires")[0].at("from").at("node").is_string());
}

TEST_CASE("Pins are not stored because they are re-derived from the palette") {
    const auto json = nlohmann::json::parse(documentJson::serialize(sample()));
    CHECK(json.at("nodes")[0].find("pins") == json.at("nodes")[0].end());
}

TEST_CASE("A node kind this build does not know loads as unknown") {
    const std::string text =
        R"({ "schema": 1, "nodes": [ { "id": "n1", "def": "future:Thing", "kind": "timeTravel" } ] })";
    auto result = documentJson::tryDeserialize(text);
    REQUIRE(result.document.has_value());
    REQUIRE(result.document->nodes.size() == 1);
    CHECK(result.document->nodes[0].kind == GraphNodeKind::Unknown);
}

TEST_CASE("An unknown field from a newer canvas is ignored rather than fatal") {
    const std::string text =
        R"({ "schema": 1, "somethingNew": 42, "nodes": [ { "id": "n1", "def": "branch", "kind": "branch", "wobble": true } ] })";
    auto result = documentJson::tryDeserialize(text);
    REQUIRE(result.document.has_value());
    REQUIRE(result.document->nodes.size() == 1);
    CHECK(result.document->nodes[0].kind == GraphNodeKind::Branch);
}

TEST_CASE("A document from a future schema is refused with a named code") {
    auto json = documentJson::serialize(sample());
    const auto pos = json.find("\"schema\": 1");
    REQUIRE(pos != std::string::npos);
    json.replace(pos, 11, "\"schema\": 99");

    auto result = documentJson::tryDeserialize(json);
    CHECK_FALSE(result.document.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == codes::kUnsupportedSchema);
    CHECK(result.error->message.find("99") != std::string::npos);
}

TEST_CASE("Malformed json is a diagnostic rather than an exception") {
    auto result = documentJson::tryDeserialize("{ this is not json");
    CHECK_FALSE(result.document.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == codes::kMalformedDocument);
}

TEST_CASE("An empty document is a diagnostic") {
    auto result = documentJson::tryDeserialize("");
    CHECK_FALSE(result.document.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == codes::kMalformedDocument);
}

TEST_CASE("A wire naming a node that does not exist still loads") {
    const std::string text =
        R"({ "schema": 1, "nodes": [], "wires": [ { "id": "w1", "from": { "node": "gone", "pin": "exec" }, "to": { "node": "alsoGone", "pin": "exec" } } ] })";
    auto result = documentJson::tryDeserialize(text);
    REQUIRE(result.document.has_value());
    REQUIRE(result.document->wires.size() == 1);
    CHECK(result.document->node("gone") == nullptr);
}

TEST_CASE("Missing optional sections default to empty rather than null") {
    auto result = documentJson::tryDeserialize(R"({ "schema": 1 })");
    REQUIRE(result.document.has_value());
    CHECK(result.document->nodes.empty());
    CHECK(result.document->wires.empty());
    CHECK(result.document->variables.empty());
    CHECK(result.document->header.scriptName.empty());
}

TEST_CASE("Wire lookups find both ends and ignore pin case") {
    const auto document = sample();
    CHECK(document.into(PinRef{"n2", "EXEC"}).size() == 1);
    CHECK(document.outOf(PinRef{"n1", "exec"}).size() == 1);
    CHECK(document.touching("n1").size() == 1);
    CHECK(document.touching("nobody").empty());
}

TEST_CASE("Node lookup is cached and can be invalidated") {
    auto document = sample();
    CHECK(document.node("n1") != nullptr);

    GraphNode extra;
    extra.id = "n3";
    extra.definition = std::string(builtinDefs::kBranch);
    extra.kind = GraphNodeKind::Branch;
    document.nodes.push_back(std::move(extra));
    CHECK(document.node("n3") == nullptr);

    document.invalidate();
    CHECK(document.node("n3") != nullptr);
}

TEST_CASE("Node configuration is a free form bag") {
    GraphNode node;
    node.id = "n1";
    node.definition = std::string(builtinDefs::kVariableGet);
    node.config["name"] = "Counter";

    GraphDocument document;
    document.nodes.push_back(std::move(node));

    const auto json = documentJson::serialize(document);
    auto result = documentJson::tryDeserialize(json);
    REQUIRE(result.document.has_value());
    CHECK(result.document->nodes[0].configString("name") == "Counter");
    CHECK(result.document->nodes[0].configString("absent") == std::nullopt);
}
