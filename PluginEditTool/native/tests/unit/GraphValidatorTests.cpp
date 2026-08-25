#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/graph/GraphValidator.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

using namespace fo4ide::graph;
namespace papyrus = fo4ide::papyrus;

namespace {

std::filesystem::path baseStubs() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / "BaseStubs";
}

// Port of GraphBuilder: the minimal document builder the C# negative tests
// use. Node ids are n1..nN, wire ids w1..wN, mirroring the sequence counter.
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

    // Port of Node(palette, definitionId, config): resolves the definition
    // through the palette and stamps the node kind from it.
    std::string node(NodePalette& palette, std::string_view definitionId,
                     std::vector<std::pair<std::string, std::string>> config = {}) {
        const auto* definition = palette.find(definitionId);
        REQUIRE(definition != nullptr);
        return nodeRaw(definitionId, definition->kind, std::move(config));
    }

    // Port of Node(definitionId, kind, config): for intentionally missing
    // definitions (the UnknownNodeDefinition test).
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
        REQUIRE(node != nullptr);
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

// Port of GraphTestEnvironment: index over the BaseStubs tree only (the
// validator cases never need the round-trip GraphScripts fixtures).
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
    REQUIRE_FALSE(result.ok());

    const GraphDiagnostic* match = nullptr;
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == expectedCode) match = &diagnostic;
    }
    INFO("expected " << expectedCode);
    REQUIRE(match != nullptr);
    // Return a copy: the validation result dies with this frame.
    return *match;
}

}  // namespace

TEST_CASE("An actor wired to an int input is refused naming the target pin") {
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

    REQUIRE(diagnostic.nodeId == add);
    REQUIRE(diagnostic.pinId == "arg:aiCount");
    REQUIRE(std::find(diagnostic.relatedNodes.begin(), diagnostic.relatedNodes.end(), player) !=
            diagnostic.relatedNodes.end());
}

TEST_CASE("A narrowing wire demands a cast rather than being inserted silently") {
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

    REQUIRE(diagnostic.nodeId == add);
    REQUIRE(diagnostic.pinId == "arg:aiCount");
    REQUIRE(diagnostic.message.find("Cast") != std::string::npos);
}

TEST_CASE("Two wires into one value input are refused") {
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

    REQUIRE(diagnostic.nodeId == wait);
    REQUIRE(diagnostic.pinId == "arg:afSeconds");
}

TEST_CASE("Two wires out of one control flow output are refused") {
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

    REQUIRE(diagnostic.nodeId == entry);
    REQUIRE(diagnostic.pinId == pinIds::kExec);
}

TEST_CASE("A control flow pin wired to a value pin is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, "arg:afSeconds");

    const auto& diagnostic = refused(env, graph, codes::kPinKindMismatch);

    REQUIRE(diagnostic.nodeId == wait);
    REQUIRE(diagnostic.pinId == "arg:afSeconds");
}

TEST_CASE("A wire between two outputs is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto first = graph.node(env.palette, "literal.int");
    const auto second = graph.node(env.palette, "literal.int");
    graph.wire(first, pinIds::kValue, second, pinIds::kValue);

    refused(env, graph, codes::kWireDirection);
}

TEST_CASE("A missing required input is refused naming the pin") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);

    const auto& diagnostic = refused(env, graph, codes::kMissingRequiredInput);

    REQUIRE(diagnostic.nodeId == wait);
    REQUIRE(diagnostic.pinId == "arg:afSeconds");
}

TEST_CASE("A node whose definition is not on the palette is refused rather than guessed") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto missing = graph.nodeRaw("call:SomeModsScript.DoThing", GraphNodeKind::Call);
    graph.wire(entry, pinIds::kExec, missing, pinIds::kExec);

    const auto& diagnostic = refused(env, graph, codes::kUnknownNodeDefinition);

    REQUIRE(diagnostic.nodeId == missing);
    REQUIRE(diagnostic.message.find("import roots") != std::string::npos);
}

TEST_CASE("A wire to a pin that no longer exists is refused naming the node") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    graph.wire(entry, "param:wasRenamedAway", notify, "arg:asNotificationText");

    const auto& diagnostic = refused(env, graph, codes::kDanglingWire);

    REQUIRE(diagnostic.nodeId == entry);
    REQUIRE(diagnostic.pinId == "param:wasRenamedAway");
}

TEST_CASE("A graph with no entry node is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.node(env.palette, "global:Debug.Notification");

    refused(env, graph, codes::kNoEntryNodes);
}

TEST_CASE("Two handlers for the same event are refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto first = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto second = graph.node(env.palette, "event:ObjectReference.OnLoad");

    const auto& diagnostic = refused(env, graph, codes::kDuplicateDeclaration);

    REQUIRE(diagnostic.nodeId == first);
    REQUIRE(std::find(diagnostic.relatedNodes.begin(), diagnostic.relatedNodes.end(), second) !=
            diagnostic.relatedNodes.end());
}

TEST_CASE("A duplicate variable name is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Counter", "int").variable("counter", "float");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    refused(env, graph, codes::kDuplicateVariableName);
}

TEST_CASE("An unknown parent script is refused") {
    Env env;
    GraphBuilder graph("Fixture", "NoSuchBaseScript");
    graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});

    refused(env, graph, codes::kUndeclaredReference);
}

TEST_CASE("A graph with no script name is refused") {
    Env env;
    GraphBuilder graph;
    graph.document.header.scriptName = "";

    refused(env, graph, codes::kInvalidScriptHeader);
}

TEST_CASE("An auto state that no entry declares is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.autoState("Combat");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    const auto& diagnostic = refused(env, graph, codes::kInvalidScriptHeader);
    REQUIRE(diagnostic.message.find("Combat") != std::string::npos);
}

TEST_CASE("A global function placed in a state is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.node(env.palette, builtinDefs::kFunctionEntry,
               {{"name", "Go"}, {"state", "Combat"}, {"global", "true"}});

    const auto& diagnostic = refused(env, graph, codes::kInvalidScriptHeader);
    REQUIRE(diagnostic.message.find("state") != std::string::npos);
}

TEST_CASE("A duplicate custom event is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.customEvent("MyEvent").customEvent("myevent");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    refused(env, graph, codes::kDuplicateDeclaration);
}

TEST_CASE("A clean minimal graph validates") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"hi\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = env.validate(graph);
    REQUIRE(result.ok());
    REQUIRE(result.sourcesComplete);
}

TEST_CASE("A clean function graph with a local variable validates") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Count", "int");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"go\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = env.validate(graph);
    REQUIRE(result.ok());
}
