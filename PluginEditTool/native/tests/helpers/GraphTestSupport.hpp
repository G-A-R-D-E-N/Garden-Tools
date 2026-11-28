#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/node_palette.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace fo4::graph::test {

inline std::filesystem::path fixtureRoot(std::string_view name) {
    return std::filesystem::path(FO4IDE_TEST_FIXTURES_DIR) / std::string(name);
}

inline std::filesystem::path baseStubs() { return fixtureRoot("BaseStubs"); }
inline std::filesystem::path graphScripts() { return fixtureRoot("GraphScripts"); }

struct GraphBuilder {
    GraphDocument document;
    int sequence{0};

    GraphBuilder(std::string scriptName = "Fixture",
                 std::optional<std::string> extends = std::nullopt) {
        document.id = "test";
        document.header.scriptName = std::move(scriptName);
        document.header.extends = std::move(extends);
    }

    std::string node(NodePalette& palette, std::string_view definitionId) {
        const auto* definition = palette.find(definitionId);
        REQUIRE(definition != nullptr);
        const auto id = "n" + std::to_string(++sequence);
        GraphNode node;
        node.id = id;
        node.definition = std::string(definitionId);
        node.kind = definition->kind;
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

struct Env {
    papyrus::ScriptIndex index;
    NodePalette palette;

    Env(bool withGraphScripts = false) : palette(index) {
        index.addRoot(baseStubs());
        if (withGraphScripts) index.addRoot(graphScripts());
    }
};

}  // namespace fo4::graph::test
