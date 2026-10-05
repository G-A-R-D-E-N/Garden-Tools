#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::graph::test {

// The BaseStubs and GraphScripts fixture trees live next to the test
// sources: native/tests/fixtures/BaseStubs and .../GraphScripts.
inline std::filesystem::path fixtureRoot(std::string_view name) {
    return std::filesystem::path(FO4IDE_TEST_FIXTURES_DIR) / std::string(name);
}

inline std::filesystem::path baseStubs() { return fixtureRoot("BaseStubs"); }
inline std::filesystem::path graphScripts() { return fixtureRoot("GraphScripts"); }

// Port of the C# GraphBuilder: the minimal document builder the graph tests
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

// Port of GraphTestEnvironment: index over the BaseStubs tree (the graph
// tests that compile against scripts use GraphScripts too).
struct Env {
    papyrus::ScriptIndex index;
    NodePalette palette;

    Env(bool withGraphScripts = false) : palette(index) {
        index.addRoot(baseStubs());
        if (withGraphScripts) index.addRoot(graphScripts());
    }
};

}  // namespace fo4ide::graph::test
