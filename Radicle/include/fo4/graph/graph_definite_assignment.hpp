#pragma once

#include <map>
#include <string>
#include <vector>

#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/graph_exec_flow.hpp>
#include <fo4/graph/node_definition.hpp>

namespace fo4::graph {

namespace definiteAssignment {

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

}

}
