#include <fo4/graph/graph_exec_flow.hpp>

#include <algorithm>
#include <cctype>
#include <climits>
#include <stack>

namespace fo4::graph {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<PinDefinition> pinOf(const NodeDefinition& definition, const GraphNode& node,
                                   const GraphDocument& document, std::string_view id) {
    const auto pins = definition.pinsFor(node, &document);
    for (const auto& pin : pins) {
        if (iequals(pin.id, id)) return pin;
    }
    return std::nullopt;
}

bool isLoopKind(GraphNodeKind kind) {
    return kind == GraphNodeKind::While || kind == GraphNodeKind::ForEach;
}

}

GraphExecFlow::GraphExecFlow(const GraphDocument& document,
                             const std::map<std::string, const NodeDefinition*>& definitions,
                             std::string entryNodeId)
    : entryNodeId_(std::move(entryNodeId)) {
    build(document, definitions);
    walk(entryNodeId_);
    computePostDominators();
    computeDominators();
}

void GraphExecFlow::build(const GraphDocument& document,
                          const std::map<std::string, const NodeDefinition*>& definitions) {
    for (const auto& wire : document.wires) {
        const auto* fromNode = document.node(wire.from.node);
        const auto* toNode = document.node(wire.to.node);
        if (fromNode == nullptr || toNode == nullptr) continue;
        const auto fromDef = definitions.find(fromNode->id);
        const auto toDef = definitions.find(toNode->id);
        if (fromDef == definitions.end() || toDef == definitions.end()) continue;

        const auto fromPin = pinOf(*fromDef->second, *fromNode, document, wire.from.pin);
        const auto toPin = pinOf(*toDef->second, *toNode, document, wire.to.pin);

        if (!fromPin || !toPin) continue;
        if (fromPin->kind != PinKind::Exec || toPin->kind != PinKind::Exec) continue;
        if (fromPin->direction != PinDirection::Out || toPin->direction != PinDirection::In) {
            continue;
        }

        successors_[fromNode->id].push_back({fromPin->id, toNode->id});
        predecessors_[toNode->id].push_back(fromNode->id);
    }

    for (const auto& node : document.nodes) {
        const auto found = definitions.find(node.id);
        if (found == definitions.end()) continue;

        const auto& definition = *found->second;
        std::vector<std::string> declared;
        for (const auto* pin : definition.execOutputs()) declared.push_back(pin->id);
        if (declared.empty()) continue;

        std::set<std::string, std::less<>> wired;
        for (const auto& [pin, target] : successorsOf(node.id)) {
            std::string lowered;
            for (const char c : pin) {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            wired.insert(lowered);
        }

        for (const auto& pin : declared) {
            std::string lowered;
            for (const char c : pin) {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (!wired.contains(lowered)) {
                openExit_.insert(node.id);
                break;
            }
        }
    }
}

void GraphExecFlow::walk(std::string_view entry) {
    std::map<std::string, int> state;
    std::stack<std::pair<std::string, int>> stack;

    state[std::string(entry)] = 1;
    stack.push({std::string(entry), 0});

    while (!stack.empty()) {
        const auto [node, index] = stack.top();
        stack.pop();
        const auto& successors = successorsOf(node);

        if (index < static_cast<int>(successors.size())) {
            stack.push({node, index + 1});
            const auto& next = successors[static_cast<std::size_t>(index)].second;

            const auto seen = state.find(next);
            if (seen == state.end()) {
                state[next] = 1;
                stack.push({next, 0});
            } else if (seen->second == 1) {
                backEdges_.insert({node, next});
            }
            continue;
        }

        state[node] = 2;
        order_[node] = static_cast<int>(reverseFinish_.size());
        reverseFinish_.push_back(node);
    }
}

void GraphExecFlow::computePostDominators() {
    const auto nodes = reverseFinish_;
    std::map<std::string, std::optional<std::string>> idom;
    for (const auto& node : nodes) idom[node] = std::nullopt;

    std::map<std::string, int> rpoIndex;
    for (std::size_t i = 0; i < nodes.size(); ++i) rpoIndex[nodes[i]] = static_cast<int>(i);

    bool changed = true;
    std::set<std::string> processed;

    const auto intersect = [&](const std::string& aIn, const std::string& bIn) -> std::string {
        auto a = aIn;
        auto b = bIn;
        while (a != b) {
            if (a == kExit || b == kExit) return std::string(kExit);

            const auto aiFound = rpoIndex.find(a);
            const auto biFound = rpoIndex.find(b);
            const auto ai = aiFound != rpoIndex.end() ? aiFound->second : INT_MAX;
            const auto bi = biFound != rpoIndex.end() ? biFound->second : INT_MAX;

            if (ai > bi) {
                const auto found = idom.find(a);
                if (found == idom.end() || !found->second) return std::string(kExit);
                a = *found->second;
            } else {
                const auto found = idom.find(b);
                if (found == idom.end() || !found->second) return std::string(kExit);
                b = *found->second;
            }
        }
        return a;
    };

    while (changed) {
        changed = false;
        for (const auto& node : nodes) {
            std::vector<std::string> successors;
            for (const auto& [pin, target] : successorsOf(node)) {
                if (backEdges_.contains({node, target})) continue;
                if (!rpoIndex.contains(target)) continue;
                if (std::find(successors.begin(), successors.end(), target) == successors.end()) {
                    successors.push_back(target);
                }
            }

            if (openExit_.contains(node)) {
                if (idom[node] != std::string(kExit)) {
                    idom[node] = std::string(kExit);
                    changed = true;
                }
                processed.insert(node);
                continue;
            }

            if (successors.empty()) {
                if (idom[node] != std::string(kExit)) {
                    idom[node] = std::string(kExit);
                    changed = true;
                }
                processed.insert(node);
                continue;
            }

            std::optional<std::string> candidate;
            for (const auto& successor : successors) {
                const auto found = idom.find(successor);
                if ((found == idom.end() || !found->second) && !processed.contains(successor)) {
                    continue;
                }
                candidate = candidate ? std::optional<std::string>(intersect(*candidate, successor))
                                      : std::optional<std::string>(successor);
            }

            if (!candidate) continue;
            if (idom[node] != candidate) {
                idom[node] = candidate;
                changed = true;
            }
            processed.insert(node);
        }
    }

    for (const auto& [node, dominator] : idom) {
        immediatePostDominator_[node] =
            dominator == std::string(kExit) ? std::nullopt : dominator;
    }
}

void GraphExecFlow::computeDominators() {
    const auto nodes = [&] {
        std::vector<std::string> out;
        out.reserve(reverseFinish_.size());
        for (auto it = reverseFinish_.rbegin(); it != reverseFinish_.rend(); ++it) {
            out.push_back(*it);
        }
        return out;
    }();
    if (nodes.empty()) return;

    std::map<std::string, int> rpoIndex;
    for (std::size_t i = 0; i < nodes.size(); ++i) rpoIndex[nodes[i]] = static_cast<int>(i);

    std::map<std::string, std::optional<std::string>> idom;
    for (const auto& node : nodes) idom[node] = std::nullopt;
    idom[entryNodeId_] = entryNodeId_;

    const auto intersect = [&](const std::string& aIn, const std::string& bIn) -> std::string {
        auto a = aIn;
        auto b = bIn;
        while (a != b) {
            while (rpoIndex[a] > rpoIndex[b]) {
                const auto found = idom.find(a);
                a = found != idom.end() && found->second ? *found->second : entryNodeId_;
            }
            while (rpoIndex[b] > rpoIndex[a]) {
                const auto found = idom.find(b);
                b = found != idom.end() && found->second ? *found->second : entryNodeId_;
            }
        }
        return a;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& node : nodes) {
            if (node == entryNodeId_) continue;

            std::optional<std::string> candidate;
            for (const auto& predecessor : predecessorsOf(node)) {
                if (!rpoIndex.contains(predecessor)) continue;
                const auto found = idom.find(predecessor);
                if (found == idom.end() || !found->second) continue;
                candidate = candidate
                                ? std::optional<std::string>(intersect(predecessor, *candidate))
                                : std::optional<std::string>(predecessor);
            }

            if (!candidate) continue;
            if (idom[node] == candidate) continue;

            idom[node] = candidate;
            changed = true;
        }
    }

    for (const auto& [node, dominator] : idom) {
        if (dominator) immediateDominator_[node] = *dominator;
    }
}

const std::vector<std::pair<std::string, std::string>>& GraphExecFlow::successorsOf(
    std::string_view nodeId) const {
    static const std::vector<std::pair<std::string, std::string>> kEmpty;
    const auto found = successors_.find(std::string(nodeId));
    return found == successors_.end() ? kEmpty : found->second;
}

std::optional<std::string> GraphExecFlow::targetOf(std::string_view nodeId,
                                                   std::string_view pinId) const {
    for (const auto& [pin, target] : successorsOf(nodeId)) {
        if (iequals(pin, pinId)) return target;
    }
    return std::nullopt;
}

bool GraphExecFlow::isReachable(std::string_view nodeId) const {
    return order_.contains(std::string(nodeId));
}

bool GraphExecFlow::dominates(std::string_view dominator, std::string_view node) const {
    if (!order_.contains(std::string(node)) || !order_.contains(std::string(dominator))) {
        return false;
    }

    auto current = std::string(node);
    while (true) {
        if (current == dominator) return true;
        const auto found = immediateDominator_.find(current);
        if (found == immediateDominator_.end()) return false;
        if (found->second == current) return false;
        current = found->second;
    }
}

std::vector<std::string> GraphExecFlow::predecessorsOf(std::string_view nodeId) const {
    const auto found = predecessors_.find(std::string(nodeId));
    if (found == predecessors_.end()) return {};
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& predecessor : found->second) {
        if (seen.insert(predecessor).second) out.push_back(predecessor);
    }
    return out;
}

std::vector<std::string> GraphExecFlow::pathsLeavingWithoutReturn(
    const std::map<std::string, const NodeDefinition*>& definitions,
    const std::optional<std::string>& start) const {
    if (!start || !isReachable(*start)) return {entryNodeId_};

    std::map<std::string, bool> returns;
    for (const auto& node : reverseFinish_) returns[node] = true;
    std::set<std::string> leaks;

    const auto escapes = [&](const std::string& node) {
        leaks.insert(node);
        return false;
    };

    const auto evaluate = [&](const std::string& node) -> bool {
        const auto found = definitions.find(node);
        if (found == definitions.end()) return escapes(node);

        const auto& definition = *found->second;
        if (definition.kind == GraphNodeKind::Return) return true;
        if (definition.kind == GraphNodeKind::Break ||
            definition.kind == GraphNodeKind::Continue) {
            return true;
        }

        std::vector<std::string> exits;
        if (isLoopKind(definition.kind)) {
            exits.push_back(std::string(pinIds::kCompleted));
        } else {
            for (const auto* pin : definition.execOutputs()) exits.push_back(pin->id);
        }

        if (exits.empty()) return escapes(node);

        bool result = true;
        for (const auto& pin : exits) {
            const auto target = targetOf(node, pin);
            if (!target) {
                escapes(node);
                result = false;
                continue;
            }
            const auto value = returns.find(*target);
            if (value != returns.end() && !value->second) result = false;
        }
        return result;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        leaks.clear();
        for (const auto& node : reverseFinish_) {
            const auto value = evaluate(node);
            if (value == returns[node]) continue;
            returns[node] = value;
            changed = true;
        }
    }

    if (returns[*start]) return {};

    std::vector<std::string> out;
    for (const auto& node : leaks) out.push_back(node);
    std::sort(out.begin(), out.end(),
              [&](const std::string& a, const std::string& b) { return order_.at(a) < order_.at(b); });
    return out;
}

std::vector<GraphDiagnostic> GraphExecFlow::checkStructured(
    const std::map<std::string, const NodeDefinition*>& definitions) const {
    std::vector<GraphDiagnostic> problems;

    for (const auto& [from, to] : backEdges_) {
        const auto found = definitions.find(to);
        if (found != definitions.end() && isLoopKind(found->second->kind)) continue;

        problems.push_back(GraphDiagnostic{
            std::string(codes::kExecCycle), GraphSeverity::Error,
            "Control flow loops back to a node that is not a loop. Papyrus has no goto, so "
            "this cannot be written as a script.",
            to, std::nullopt, std::nullopt, {from}, 0, std::nullopt});
    }

    return problems;
}

std::vector<GraphDiagnostic> GraphExecFlow::checkReachability(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    const std::vector<const GraphExecFlow*>& flows) {
    std::set<std::string> reached;
    for (const auto* flow : flows) {
        for (const auto& node : flow->reachable()) reached.insert(node);
    }

    std::vector<GraphDiagnostic> problems;
    for (const auto& node : document.nodes) {
        const auto found = definitions.find(node.id);
        if (found == definitions.end()) continue;

        const auto& definition = *found->second;
        if (definition.execInputs().empty() && definition.execOutputs().empty()) continue;
        if (definition.kind == GraphNodeKind::EventEntry ||
            definition.kind == GraphNodeKind::FunctionEntry) {
            continue;
        }
        if (reached.contains(node.id)) continue;
        if (document.touching(node.id).empty()) continue;

        problems.push_back(GraphDiagnostic{
            std::string(codes::kUnreachableExec), GraphSeverity::Warning,
            "Nothing reaches this node, so it would never run.", node.id, std::nullopt,
            std::nullopt, {}, 0, std::nullopt});
    }

    return problems;
}

}
