#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/graph_type_resolver.hpp>
#include <fo4/graph/node_palette.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace fo4::graph {

struct GraphValidation {
    std::vector<GraphDiagnostic> diagnostics;
    std::map<std::string, const NodeDefinition*> definitions;
    std::map<std::string, GenericBinding> generics;
    bool sourcesComplete{true};

    bool ok() const {
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == GraphSeverity::Error) return false;
        }
        return true;
    }

    std::vector<const GraphDiagnostic*> errors() const {
        std::vector<const GraphDiagnostic*> out;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == GraphSeverity::Error) out.push_back(&diagnostic);
        }
        return out;
    }
};

class GraphValidator {
public:
    GraphValidator(papyrus::ScriptIndex& index, NodePalette& palette);

    GraphValidation validate(GraphDocument& document);

    static std::string entryNameOf(const GraphNode& node, const NodeDefinition& definition);

    static bool isIdentifier(std::string_view text);

private:
    std::optional<papyrus::Script> ownerScriptFor(const GraphDocument& document) const;
    void validateHeader(const GraphDocument& document, std::vector<GraphDiagnostic>& problems) const;
    void resolveDefinitions(const GraphDocument& document,
                            std::map<std::string, const NodeDefinition*>& definitions,
                            std::vector<GraphDiagnostic>& problems) const;
    void validateWires(const GraphDocument& document,
                       const std::map<std::string, const NodeDefinition*>& definitions,
                       std::vector<GraphDiagnostic>& problems) const;
    void validateDeclarations(const GraphDocument& document,
                              std::vector<GraphDiagnostic>& problems) const;
    std::map<std::string, GenericBinding> solveGenerics(
        const GraphDocument& document,
        const std::map<std::string, const NodeDefinition*>& definitions,
        GraphTypeResolver& types) const;
    void validateWireTypes(const GraphDocument& document,
                           const std::map<std::string, const NodeDefinition*>& definitions,
                           const std::map<std::string, GenericBinding>& generics,
                           GraphTypeResolver& types, const papyrus::Script* owner,
                           std::vector<GraphDiagnostic>& problems) const;
    void validateRequiredInputs(const GraphDocument& document,
                                const std::map<std::string, const NodeDefinition*>& definitions,
                                std::vector<GraphDiagnostic>& problems) const;
    void validateEntries(const GraphDocument& document,
                         const std::map<std::string, const NodeDefinition*>& definitions,
                         std::vector<GraphDiagnostic>& problems) const;
    void validateStates(const GraphDocument& document, const std::vector<const GraphNode*>& entries,
                        std::vector<GraphDiagnostic>& problems) const;
    void validateCustomEvents(const GraphDocument& document,
                              std::vector<GraphDiagnostic>& problems) const;

    papyrus::ScriptIndex& index_;
    NodePalette& palette_;
};

}
