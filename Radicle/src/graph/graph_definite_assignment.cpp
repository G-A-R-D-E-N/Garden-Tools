#include <fo4/graph/graph_definite_assignment.hpp>

#include <algorithm>
#include <cctype>
#include <set>

namespace fo4::graph {
namespace definiteAssignment {
namespace {

bool isLoopKind(GraphNodeKind kind) {
    return kind == GraphNodeKind::While || kind == GraphNodeKind::ForEach;
}

}

std::vector<Source> sources(const GraphDocument& document,
                            const std::map<std::string, const NodeDefinition*>& definitions,
                            const GraphNode& consumer,
                            const NodeDefinition& consumerDefinition) {
    std::vector<Source> found;

    const auto pins = consumerDefinition.pinsFor(consumer, &document);
    for (const auto& inputPin : pins) {
        if (inputPin.kind != PinKind::Data || inputPin.direction != PinDirection::In) continue;

        const auto reportAt = PinRef{consumer.id, inputPin.id};
        std::set<std::string> visited;

        const auto collect = [&](auto&& self, const PinRef& target, const PinRef& report) -> void {
            for (const auto* wire : document.into(target)) {
                const auto& sourceId = wire->from.node;
                const auto* source = document.node(sourceId);
                if (source == nullptr) continue;
                const auto sourceDef = definitions.find(sourceId);
                if (sourceDef == definitions.end()) continue;

                if (!sourceDef->second->isPure) {
                    found.push_back(Source{sourceId, report.node, report.pin});
                    continue;
                }

                if (!visited.insert(sourceId).second) continue;

                const auto upstream = sourceDef->second->pinsFor(*source, &document);
                for (const auto& pin : upstream) {
                    if (pin.kind != PinKind::Data || pin.direction != PinDirection::In) continue;
                    self(self, PinRef{sourceId, pin.id}, report);
                }
            }
        };

        collect(collect, reportAt, reportAt);
    }

    std::vector<Source> out;
    for (const auto& entry : found) {
        const auto already = std::find_if(out.begin(), out.end(), [&](const Source& other) {
            return other.producer == entry.producer && other.node == entry.node &&
                   other.pin == entry.pin;
        });
        if (already == out.end()) out.push_back(entry);
    }
    return out;
}

std::vector<GraphDiagnostic> check(const GraphDocument& document,
                                   const std::map<std::string, const NodeDefinition*>& definitions,
                                   const GraphExecFlow& flow) {
    std::vector<GraphDiagnostic> problems;

    for (const auto& consumerId : flow.reachable()) {
        const auto* consumer = document.node(consumerId);
        if (consumer == nullptr) continue;
        const auto consumerDef = definitions.find(consumerId);
        if (consumerDef == definitions.end()) continue;

        if (consumerDef->second->kind == GraphNodeKind::EventEntry ||
            consumerDef->second->kind == GraphNodeKind::FunctionEntry) {
            continue;
        }

        for (const auto& entry : sources(document, definitions, *consumer, *consumerDef->second)) {
            const auto& producerId = entry.producer;
            if (producerId == consumerId) continue;
            if (!flow.isReachable(producerId)) continue;
            if (flow.dominates(producerId, consumerId)) continue;

            const auto isLoop = isLoopKind(consumerDef->second->kind);
            if (isLoop && flow.dominates(consumerId, producerId)) {
                problems.push_back(GraphDiagnostic{
                    std::string(codes::kLoopConditionFromLoopBody), GraphSeverity::Error,
                    "This loop reads a value produced inside its own body, so the value does "
                    "not exist the first time the condition is tested. Produce it before the "
                    "loop instead.",
                    consumerId, entry.pin, std::nullopt, {producerId}, 0, std::nullopt});
            } else {
                problems.push_back(GraphDiagnostic{
                    std::string(codes::kUseBeforeAssignment), GraphSeverity::Error,
                    "This value comes from a node that does not run on every path that reaches "
                    "here, so on some paths it would read as zero rather than fail to compile.",
                    consumerId, entry.pin, std::nullopt, {producerId}, 0, std::nullopt});
            }
        }
    }

    return problems;
}

}
}
