#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/node_definition.hpp>

namespace fo4::graph {

class GraphExecFlow {
public:
    GraphExecFlow(const GraphDocument& document,
                  const std::map<std::string, const NodeDefinition*>& definitions,
                  std::string entryNodeId);

    const std::string& entryNodeId() const { return entryNodeId_; }

    const std::vector<std::string>& reachable() const { return reverseFinish_; }

    const std::set<std::pair<std::string, std::string>>& backEdges() const {
        return backEdges_;
    }

    const std::map<std::string, std::optional<std::string>>& immediatePostDominator() const {
        return immediatePostDominator_;
    }

    const std::vector<std::pair<std::string, std::string>>& successorsOf(
        std::string_view nodeId) const;

    std::optional<std::string> targetOf(std::string_view nodeId, std::string_view pinId) const;

    bool isReachable(std::string_view nodeId) const;

    const std::map<std::string, std::string>& immediateDominator() const {
        return immediateDominator_;
    }

    bool dominates(std::string_view dominator, std::string_view node) const;

    std::vector<std::string> predecessorsOf(std::string_view nodeId) const;

    std::vector<std::string> pathsLeavingWithoutReturn(
        const std::map<std::string, const NodeDefinition*>& definitions,
        const std::optional<std::string>& start) const;

    std::vector<GraphDiagnostic> checkStructured(
        const std::map<std::string, const NodeDefinition*>& definitions) const;

    static std::vector<GraphDiagnostic> checkReachability(
        const GraphDocument& document,
        const std::map<std::string, const NodeDefinition*>& definitions,
        const std::vector<const GraphExecFlow*>& flows);

private:
    void build(const GraphDocument& document,
               const std::map<std::string, const NodeDefinition*>& definitions);
    void walk(std::string_view entry);
    void computePostDominators();
    void computeDominators();

    static constexpr std::string_view kExit = "\0exit";

    std::string entryNodeId_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> successors_;
    std::map<std::string, std::vector<std::string>> predecessors_;
    std::map<std::string, int> order_;
    std::vector<std::string> reverseFinish_;
    std::set<std::pair<std::string, std::string>> backEdges_;
    std::map<std::string, std::optional<std::string>> immediatePostDominator_;
    std::map<std::string, std::string> immediateDominator_;
    std::set<std::string> openExit_;
};

}
