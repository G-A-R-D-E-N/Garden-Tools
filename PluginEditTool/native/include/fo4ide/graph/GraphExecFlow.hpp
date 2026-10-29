#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/NodeDefinition.hpp>

namespace fo4ide::graph {

// Port of GraphExecFlow: the control-flow graph over exec wires — DFS walk
// with back-edge detection (reverse-finish order), immediate post-dominators
// and dominators over the reachable set, the "paths leaving without return"
// fixed point, and the structured-flow/exec-cycle checks. A "\0exit" sentinel
// pseudo-node stands in for "leaves the function" in the post-dominator
// lattice (nullopt in immediatePostDominator means Exit).
class GraphExecFlow {
public:
    GraphExecFlow(const GraphDocument& document,
                  const std::map<std::string, const NodeDefinition*>& definitions,
                  std::string entryNodeId);

    const std::string& entryNodeId() const { return entryNodeId_; }

    // Reachable nodes in reverse finish order (the C# Reachable).
    const std::vector<std::string>& reachable() const { return reverseFinish_; }

    const std::set<std::pair<std::string, std::string>>& backEdges() const {
        return backEdges_;
    }

    // Node id -> immediate post-dominator; nullopt when the node exits the
    // function (the Exit sentinel maps to nullopt).
    const std::map<std::string, std::optional<std::string>>& immediatePostDominator() const {
        return immediatePostDominator_;
    }

    const std::vector<std::pair<std::string, std::string>>& successorsOf(
        std::string_view nodeId) const;

    // The target of the exec wire leaving `nodeId` through `pinId` (pin ids
    // compared case-insensitively); nullopt when no such successor.
    std::optional<std::string> targetOf(std::string_view nodeId, std::string_view pinId) const;

    bool isReachable(std::string_view nodeId) const;

    // Immediate dominator of every reachable node except the entry (the entry
    // dominates itself).
    const std::map<std::string, std::string>& immediateDominator() const {
        return immediateDominator_;
    }

    bool dominates(std::string_view dominator, std::string_view node) const;

    // Distinct predecessors in insertion order.
    std::vector<std::string> predecessorsOf(std::string_view nodeId) const;

    // Port of PathsLeavingWithoutReturn: the nodes whose exits can leave the
    // function without hitting a Return, or the full entry list when the
    // start is unreachable. Empty when every path from `start` returns.
    std::vector<std::string> pathsLeavingWithoutReturn(
        const std::map<std::string, const NodeDefinition*>& definitions,
        const std::optional<std::string>& start) const;

    // Port of CheckStructured: an ExecCycle error for every back edge whose
    // target is not a While/ForEach loop (Papyrus has no goto).
    std::vector<GraphDiagnostic> checkStructured(
        const std::map<std::string, const NodeDefinition*>& definitions) const;

    // Port of CheckReachability: UnreachableExec warnings for exec-touching
    // nodes no flow reaches.
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

    static constexpr std::string_view kExit{"\0exit", 5};

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

}  // namespace fo4ide::graph
