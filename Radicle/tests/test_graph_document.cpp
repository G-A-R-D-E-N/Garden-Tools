#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>

using fo4::graph::GraphDocument;
using fo4::graph::GraphKind;
using fo4::graph::GraphNode;
using fo4::graph::GraphNodeKind;
using fo4::graph::GraphPinValue;
using fo4::graph::GraphScriptHeader;
using fo4::graph::GraphVariable;
using fo4::graph::GraphWire;
using fo4::graph::PinRef;
namespace builtinDefs = fo4::graph::builtinDefs;
namespace codes = fo4::graph::codes;
namespace documentJson = fo4::graph::documentJson;
namespace pinIds = fo4::graph::pinIds;

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

}

TEST(GraphDocument, ADocumentSurvivesARoundTripUnchanged) {
    const auto json = documentJson::serialize(sample());
    auto result = documentJson::tryDeserialize(json);
    ASSERT_TRUE(result.document.has_value());
    ASSERT_FALSE(result.error.has_value());

    const auto& back = *result.document;
    EXPECT_TRUE(back.id == "doc1");
    EXPECT_TRUE(back.schema == GraphDocument::kCurrentSchema);
    EXPECT_TRUE(back.header.scriptName == "MyMod:DoorScript");
    EXPECT_TRUE(back.header.extends == "ObjectReference");
    ASSERT_TRUE(back.header.flags.size() == 1);
    EXPECT_TRUE(back.header.flags[0] == "Conditional");
    ASSERT_TRUE(back.header.imports.size() == 1);
    EXPECT_TRUE(back.header.imports[0] == "Debug");
    EXPECT_TRUE(back.header.docComment == "A door.");
    EXPECT_TRUE(back.header.autoState == "Waiting");
    ASSERT_TRUE(back.variables.size() == 1);
    EXPECT_TRUE(back.variables[0].isProperty);

    ASSERT_TRUE(back.nodes.size() == 2);
    const auto* n2 = back.node("n2");
    ASSERT_TRUE(n2 != nullptr);
    const auto it = n2->pinValues.find("arg:asNotificationText");
    ASSERT_TRUE(it != n2->pinValues.end());
    EXPECT_TRUE(it->second.value == "\"opened\"");

    ASSERT_TRUE(back.wires.size() == 1);
    EXPECT_TRUE(back.wires[0].from == PinRef("n1", "exec"));
    EXPECT_TRUE(back.wires[0].to == PinRef("n2", "exec"));
}

TEST(GraphDocument, TheJsonIsCamelCasedToMatchTheRestOfTheBridge) {
    const auto json = nlohmann::json::parse(documentJson::serialize(sample()));

    EXPECT_TRUE(json.contains("schema"));
    EXPECT_TRUE(json.contains("header"));
    EXPECT_TRUE(json.at("header").at("scriptName").is_string());
    EXPECT_TRUE(json.at("nodes")[0].at("def").is_string());
    EXPECT_TRUE(json.at("wires")[0].at("from").at("node").is_string());
}

TEST(GraphDocument, PinsAreNotStoredBecauseTheyAreReDerivedFromThePalette) {
    const auto json = nlohmann::json::parse(documentJson::serialize(sample()));
    EXPECT_TRUE(json.at("nodes")[0].find("pins") == json.at("nodes")[0].end());
}

TEST(GraphDocument, ANodeKindThisBuildDoesNotKnowLoadsAsUnknown) {
    const std::string text =
        R"({ "schema": 1, "nodes": [ { "id": "n1", "def": "future:Thing", "kind": "timeTravel" } ] })";
    auto result = documentJson::tryDeserialize(text);
    ASSERT_TRUE(result.document.has_value());
    ASSERT_TRUE(result.document->nodes.size() == 1);
    EXPECT_TRUE(result.document->nodes[0].kind == GraphNodeKind::Unknown);
}

TEST(GraphDocument, AnUnknownFieldFromANewerCanvasIsIgnoredRatherThanFatal) {
    const std::string text =
        R"({ "schema": 1, "somethingNew": 42, "nodes": [ { "id": "n1", "def": "branch", "kind": "branch", "wobble": true } ] })";
    auto result = documentJson::tryDeserialize(text);
    ASSERT_TRUE(result.document.has_value());
    ASSERT_TRUE(result.document->nodes.size() == 1);
    EXPECT_TRUE(result.document->nodes[0].kind == GraphNodeKind::Branch);
}

TEST(GraphDocument, ADocumentFromAFutureSchemaIsRefusedWithANamedCode) {
    auto json = documentJson::serialize(sample());
    const auto pos = json.find("\"schema\": 1");
    ASSERT_TRUE(pos != std::string::npos);
    json.replace(pos, 11, "\"schema\": 99");

    auto result = documentJson::tryDeserialize(json);
    EXPECT_FALSE(result.document.has_value());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.error->code == codes::kUnsupportedSchema);
    EXPECT_TRUE(result.error->message.find("99") != std::string::npos);
}

TEST(GraphDocument, MalformedJsonIsADiagnosticRatherThanAnException) {
    auto result = documentJson::tryDeserialize("{ this is not json");
    EXPECT_FALSE(result.document.has_value());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.error->code == codes::kMalformedDocument);
}

TEST(GraphDocument, AnEmptyDocumentIsADiagnostic) {
    auto result = documentJson::tryDeserialize("");
    EXPECT_FALSE(result.document.has_value());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.error->code == codes::kMalformedDocument);
}

TEST(GraphDocument, AWireNamingANodeThatDoesNotExistStillLoads) {
    const std::string text =
        R"({ "schema": 1, "nodes": [], "wires": [ { "id": "w1", "from": { "node": "gone", "pin": "exec" }, "to": { "node": "alsoGone", "pin": "exec" } } ] })";
    auto result = documentJson::tryDeserialize(text);
    ASSERT_TRUE(result.document.has_value());
    ASSERT_TRUE(result.document->wires.size() == 1);
    EXPECT_TRUE(result.document->node("gone") == nullptr);
}

TEST(GraphDocument, MissingOptionalSectionsDefaultToEmptyRatherThanNull) {
    auto result = documentJson::tryDeserialize(R"({ "schema": 1 })");
    ASSERT_TRUE(result.document.has_value());
    EXPECT_TRUE(result.document->nodes.empty());
    EXPECT_TRUE(result.document->wires.empty());
    EXPECT_TRUE(result.document->variables.empty());
    EXPECT_TRUE(result.document->header.scriptName.empty());
}

TEST(GraphDocument, WireLookupsFindBothEndsAndIgnorePinCase) {
    const auto document = sample();
    EXPECT_TRUE(document.into(PinRef{"n2", "EXEC"}).size() == 1);
    EXPECT_TRUE(document.outOf(PinRef{"n1", "exec"}).size() == 1);
    EXPECT_TRUE(document.touching("n1").size() == 1);
    EXPECT_TRUE(document.touching("nobody").empty());
}

TEST(GraphDocument, NodeLookupIsCachedAndCanBeInvalidated) {
    auto document = sample();
    EXPECT_TRUE(document.node("n1") != nullptr);

    GraphNode extra;
    extra.id = "n3";
    extra.definition = std::string(builtinDefs::kBranch);
    extra.kind = GraphNodeKind::Branch;
    document.nodes.push_back(std::move(extra));
    EXPECT_TRUE(document.node("n3") == nullptr);

    document.invalidate();
    EXPECT_TRUE(document.node("n3") != nullptr);
}

TEST(GraphDocument, NodeConfigurationIsAFreeFormBag) {
    GraphNode node;
    node.id = "n1";
    node.definition = std::string(builtinDefs::kVariableGet);
    node.config["name"] = "Counter";

    GraphDocument document;
    document.nodes.push_back(std::move(node));

    const auto json = documentJson::serialize(document);
    auto result = documentJson::tryDeserialize(json);
    ASSERT_TRUE(result.document.has_value());
    EXPECT_TRUE(result.document->nodes[0].configString("name") == "Counter");
    EXPECT_TRUE(result.document->nodes[0].configString("absent") == std::nullopt);
}

TEST(GraphDocument, ADocumentWhoseSchemaIsNotAnIntegerIsRefused) {
    for (const char* schema : {"2.0", "4294967297", "\"1\"", "null"}) {
        auto json = documentJson::serialize(sample());
        const auto pos = json.find("\"schema\": 1");
        ASSERT_TRUE(pos != std::string::npos);
        json.replace(pos, 11, std::string("\"schema\": ") + schema);

        auto result = documentJson::tryDeserialize(json);
        EXPECT_FALSE(result.document.has_value());
        ASSERT_TRUE(result.error.has_value());
        EXPECT_TRUE(result.error->code == codes::kMalformedDocument);
    }
}
