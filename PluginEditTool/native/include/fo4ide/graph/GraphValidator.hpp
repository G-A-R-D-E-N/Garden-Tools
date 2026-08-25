#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphTypeResolver.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::graph {

// Port of GraphValidation: the validator's findings plus the resolved
// definitions and generic bindings the compiler consumes.
struct GraphValidation {
    std::vector<GraphDiagnostic> diagnostics;
    // Node id -> definition, only for nodes that resolved on the palette.
    std::map<std::string, const NodeDefinition*> definitions;
    // Node id -> solved generics.
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

// Port of GraphValidator: header shape, definition resolution, wire shape and
// type checking, variable/custom-event/state declarations, required inputs
// and entry presence. The exec-flow / definite-assignment / lowering stages
// are separate passes (ported in later milestones).
class GraphValidator {
public:
    GraphValidator(papyrus::ScriptIndex& index, NodePalette& palette);

    GraphValidation validate(GraphDocument& document);

    // Port of EntryNameOf: the entry's declared name (function config) or its
    // definition's member name.
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

}  // namespace fo4ide::graph
