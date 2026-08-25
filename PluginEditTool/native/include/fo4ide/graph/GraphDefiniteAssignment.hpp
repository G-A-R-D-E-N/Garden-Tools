#pragma once

#include <map>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphExecFlow.hpp>
#include <fo4ide/graph/NodeDefinition.hpp>

namespace fo4ide::graph {

// Port of GraphDefiniteAssignment: a value is "definitely assigned" when
// every path that reaches the consumer runs the producing node first (the
// producer dominates the consumer). Sources are traced through pure nodes,
// so an operator between a call and its use does not hide the call.
namespace definiteAssignment {

// (Producer, report-at pin) pairs feeding the consumer's data inputs,
// tracing through pure nodes.
struct Source {
    std::string producer;
    std::string node;
    std::string pin;
};

std::vector<Source> sources(const GraphDocument& document,
                            const std::map<std::string, const NodeDefinition*>& definitions,
                            const GraphNode& consumer,
                            const NodeDefinition& consumerDefinition);

std::vector<GraphDiagnostic> check(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    const GraphExecFlow& flow);

}  // namespace definiteAssignment

}  // namespace fo4ide::graph
