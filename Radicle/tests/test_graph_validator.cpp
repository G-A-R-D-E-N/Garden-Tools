#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/graph/graph_validator.hpp>
#include <fo4/graph/node_palette.hpp>
#include <fo4/papyrus/script_index.hpp>

using namespace fo4::graph;
namespace papyrus = fo4::papyrus;

namespace {

std::filesystem::path baseStubs() {
    return std::filesystem::path(FO4_TESTDATA_DIR) / "graph" / "BaseStubs";
}

struct GraphBuilder {
    GraphDocument document;
    int sequence{0};

    GraphBuilder(std::string scriptName = "Fixture",
                 std::optional<std::string> extends = std::nullopt) {
        document.id = "test";
        document.header.scriptName = std::move(scriptName);
        document.header.extends = std::move(extends);
    }

    GraphBuilder& autoState(std::string state) {
        document.header.autoState = std::move(state);
        return *this;
    }

    GraphBuilder& customEvent(std::string name) {
        document.customEvents.push_back(std::move(name));
        return *this;
    }

    GraphBuilder& variable(std::string name, std::string type, bool isProperty = false,
                           std::optional<std::string> initial = std::nullopt) {
        GraphVariable v;
        v.name = std::move(name);
        v.type = std::move(type);
        v.isProperty = isProperty;
        v.initial = std::move(initial);
        document.variables.push_back(std::move(v));
        return *this;
    }

    std::string node(NodePalette& palette, std::string_view definitionId,
                     std::vector<std::pair<std::string, std::string>> config = {}) {
        const auto* definition = palette.find(definitionId);
        if (definition == nullptr) throw std::runtime_error("unknown node definition " + std::string(definitionId));
        return nodeRaw(definitionId, definition->kind, std::move(config));
    }

    std::string nodeRaw(std::string_view definitionId, GraphNodeKind kind,
                        std::vector<std::pair<std::string, std::string>> config = {}) {
        const auto id = "n" + std::to_string(++sequence);
        GraphNode node;
        node.id = id;
        node.definition = std::string(definitionId);
        node.kind = kind;
        for (auto& [key, value] : config) node.config[key] = std::move(value);
        document.nodes.push_back(std::move(node));
        document.invalidate();
        return id;
    }

    GraphBuilder& value(const std::string& nodeId, std::string_view pinId, std::string type,
                        std::string value) {
        const auto* node = document.node(nodeId);
        if (node == nullptr) throw std::runtime_error("unknown node " + nodeId);
        GraphPinValue pin;
        pin.type = std::move(type);
        pin.value = std::move(value);
        document.node(nodeId)->pinValues[std::string(pinId)] = std::move(pin);
        return *this;
    }

    GraphBuilder& wire(const std::string& fromNode, std::string_view fromPin,
                       const std::string& toNode, std::string_view toPin) {
        GraphWire w;
        w.id = "w" + std::to_string(++sequence);
        w.from.node = fromNode;
        w.from.pin = std::string(fromPin);
        w.to.node = toNode;
        w.to.pin = std::string(toPin);
        document.wires.push_back(std::move(w));
        return *this;
    }
};

struct Env {
    papyrus::ScriptIndex index;
    NodePalette palette;

    Env() : palette(index) { index.addRoot(baseStubs()); }

    GraphValidation validate(GraphBuilder& builder) {
        GraphValidator validator(index, palette);
        return validator.validate(builder.document);
    }
};

GraphDiagnostic refused(Env& env, GraphBuilder& builder, std::string_view expectedCode) {
    const auto result = env.validate(builder);
    if (result.ok()) throw std::runtime_error("expected validation to refuse the graph");

    const GraphDiagnostic* match = nullptr;
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == expectedCode) match = &diagnostic;
    }
    SCOPED_TRACE((::testing::Message() << "expected " << expectedCode).GetString());
    if (match == nullptr) throw std::runtime_error("expected " + std::string(expectedCode));
    return *match;
}

}

TEST(GraphValidator, AnActorWiredToAnIntInputIsRefusedNamingTheTargetPin) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto player = graph.node(env.palette, "global:Game.GetPlayer");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, player, pinIds::kExec);
    graph.wire(player, pinIds::kThen, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(player, pinIds::kReturn, add, "arg:aiCount");

    const auto& diagnostic = refused(env, graph, codes::kIncompatibleWireType);

    ASSERT_TRUE(diagnostic.nodeId == add);
    ASSERT_TRUE(diagnostic.pinId == "arg:aiCount");
    ASSERT_TRUE(std::find(diagnostic.relatedNodes.begin(), diagnostic.relatedNodes.end(), player) !=
            diagnostic.relatedNodes.end());
}

TEST(GraphValidator, ANarrowingWireDemandsACastRatherThanBeingInsertedSilently) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);
    const auto number = graph.node(env.palette, "literal.float");

    graph.value(number, pinIds::kValue, "float", "1.5");
    graph.wire(entry, pinIds::kExec, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(number, pinIds::kValue, add, "arg:aiCount");

    const auto& diagnostic = refused(env, graph, codes::kNarrowingWireNeedsCast);

    ASSERT_TRUE(diagnostic.nodeId == add);
    ASSERT_TRUE(diagnostic.pinId == "arg:aiCount");
    ASSERT_TRUE(diagnostic.message.find("Cast") != std::string::npos);
}

TEST(GraphValidator, TwoWiresIntoOneValueInputAreRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    const auto first = graph.node(env.palette, "literal.float");
    const auto second = graph.node(env.palette, "literal.float");

    graph.value(first, pinIds::kValue, "float", "1.0");
    graph.value(second, pinIds::kValue, "float", "2.0");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);
    graph.wire(first, pinIds::kValue, wait, "arg:afSeconds");
    graph.wire(second, pinIds::kValue, wait, "arg:afSeconds");

    const auto& diagnostic = refused(env, graph, codes::kMultipleDataSources);

    ASSERT_TRUE(diagnostic.nodeId == wait);
    ASSERT_TRUE(diagnostic.pinId == "arg:afSeconds");
}

TEST(GraphValidator, TwoWiresOutOfOneControlFlowOutputAreRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto first = graph.node(env.palette, "global:Debug.Notification");
    const auto second = graph.node(env.palette, "global:Debug.Notification");

    graph.value(first, "arg:asNotificationText", "string", "\"a\"");
    graph.value(second, "arg:asNotificationText", "string", "\"b\"");
    graph.wire(entry, pinIds::kExec, first, pinIds::kExec);
    graph.wire(entry, pinIds::kExec, second, pinIds::kExec);

    const auto& diagnostic = refused(env, graph, codes::kMultipleExecSuccessors);

    ASSERT_TRUE(diagnostic.nodeId == entry);
    ASSERT_TRUE(diagnostic.pinId == pinIds::kExec);
}

TEST(GraphValidator, AControlFlowPinWiredToAValuePinIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, "arg:afSeconds");

    const auto& diagnostic = refused(env, graph, codes::kPinKindMismatch);

    ASSERT_TRUE(diagnostic.nodeId == wait);
    ASSERT_TRUE(diagnostic.pinId == "arg:afSeconds");
}

TEST(GraphValidator, AWireBetweenTwoOutputsIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto first = graph.node(env.palette, "literal.int");
    const auto second = graph.node(env.palette, "literal.int");
    graph.wire(first, pinIds::kValue, second, pinIds::kValue);

    refused(env, graph, codes::kWireDirection);
}

TEST(GraphValidator, AMissingRequiredInputIsRefusedNamingThePin) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);

    const auto& diagnostic = refused(env, graph, codes::kMissingRequiredInput);

    ASSERT_TRUE(diagnostic.nodeId == wait);
    ASSERT_TRUE(diagnostic.pinId == "arg:afSeconds");
}

TEST(GraphValidator, ANodeWhoseDefinitionIsNotOnThePaletteIsRefusedRatherThanGuessed) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto missing = graph.nodeRaw("call:SomeModsScript.DoThing", GraphNodeKind::Call);
    graph.wire(entry, pinIds::kExec, missing, pinIds::kExec);

    const auto& diagnostic = refused(env, graph, codes::kUnknownNodeDefinition);

    ASSERT_TRUE(diagnostic.nodeId == missing);
    ASSERT_TRUE(diagnostic.message.find("import roots") != std::string::npos);
}

TEST(GraphValidator, AWireToAPinThatNoLongerExistsIsRefusedNamingTheNode) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    graph.wire(entry, "param:wasRenamedAway", notify, "arg:asNotificationText");

    const auto& diagnostic = refused(env, graph, codes::kDanglingWire);

    ASSERT_TRUE(diagnostic.nodeId == entry);
    ASSERT_TRUE(diagnostic.pinId == "param:wasRenamedAway");
}

TEST(GraphValidator, AGraphWithNoEntryNodeIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.node(env.palette, "global:Debug.Notification");

    refused(env, graph, codes::kNoEntryNodes);
}

TEST(GraphValidator, TwoHandlersForTheSameEventAreRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto first = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto second = graph.node(env.palette, "event:ObjectReference.OnLoad");

    const auto& diagnostic = refused(env, graph, codes::kDuplicateDeclaration);

    ASSERT_TRUE(diagnostic.nodeId == first);
    ASSERT_TRUE(std::find(diagnostic.relatedNodes.begin(), diagnostic.relatedNodes.end(), second) !=
            diagnostic.relatedNodes.end());
}

TEST(GraphValidator, ADuplicateVariableNameIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Counter", "int").variable("counter", "float");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    refused(env, graph, codes::kDuplicateVariableName);
}

TEST(GraphValidator, AnUnknownParentScriptIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "NoSuchBaseScript");
    graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});

    refused(env, graph, codes::kUndeclaredReference);
}

TEST(GraphValidator, AGraphWithNoScriptNameIsRefused) {
    Env env;
    GraphBuilder graph;
    graph.document.header.scriptName = "";

    refused(env, graph, codes::kInvalidScriptHeader);
}

TEST(GraphValidator, AnAutoStateThatNoEntryDeclaresIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.autoState("Combat");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    const auto& diagnostic = refused(env, graph, codes::kInvalidScriptHeader);
    ASSERT_TRUE(diagnostic.message.find("Combat") != std::string::npos);
}

TEST(GraphValidator, AGlobalFunctionPlacedInAStateIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.node(env.palette, builtinDefs::kFunctionEntry,
               {{"name", "Go"}, {"state", "Combat"}, {"global", "true"}});

    const auto& diagnostic = refused(env, graph, codes::kInvalidScriptHeader);
    ASSERT_TRUE(diagnostic.message.find("state") != std::string::npos);
}

TEST(GraphValidator, ADuplicateCustomEventIsRefused) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.customEvent("MyEvent").customEvent("myevent");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    refused(env, graph, codes::kDuplicateDeclaration);
}

TEST(GraphValidator, ACleanMinimalGraphValidates) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"hi\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = env.validate(graph);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.sourcesComplete);
}

TEST(GraphValidator, ACleanFunctionGraphWithALocalVariableValidates) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Count", "int");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"go\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = env.validate(graph);
    ASSERT_TRUE(result.ok());
}
