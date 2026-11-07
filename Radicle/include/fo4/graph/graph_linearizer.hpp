#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/graph_definite_assignment.hpp>
#include <fo4/graph/graph_diagnostic.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/graph_exec_flow.hpp>
#include <fo4/graph/graph_ir.hpp>
#include <fo4/graph/graph_name_table.hpp>
#include <fo4/graph/graph_type_resolver.hpp>
#include <fo4/graph/graph_validator.hpp>
#include <fo4/papyrus/script_index.hpp>

namespace fo4::graph {

class GraphLinearizer {
public:
    GraphLinearizer(const GraphDocument& document, const GraphValidation& validation,
                    GraphTypeResolver& types, const papyrus::Script* owner);

    const std::vector<GraphDiagnostic>& diagnostics() const { return problems_; }

    std::unique_ptr<ir::IrScript> lower(papyrus::ScriptIndex& index);

private:
    struct Frame {
        GraphNameTable names;
        GraphExecFlow flow;
        std::map<std::string, std::string> resultLocals;
        std::vector<ir::IrLocal> locals;
        std::set<std::string> declaredLocals;
        std::optional<std::string> returnTypeName;
        bool returnIsArray{false};
        struct Loop {
            std::string nodeId;
            std::optional<std::string> sentinel;
        };
        std::vector<Loop> loops;

        Frame(const GraphDocument& document,
              const std::map<std::string, const NodeDefinition*>& definitions,
              std::string entryNodeId, GraphNameTable names)
            : names(std::move(names)), flow(document, definitions, std::move(entryNodeId)) {}
    };

    std::unique_ptr<ir::IrCallable> lowerCallable(const GraphNode& entry,
                                                  const NodeDefinition& definition,
                                                  papyrus::ScriptIndex& index,
                                                  std::vector<GraphExecFlow>& flows);

    std::vector<std::shared_ptr<ir::IrStatement>> lowerRegion(std::string nodeId,
                                                              std::string stopAt,
                                                              Frame& frame);
    std::vector<std::shared_ptr<ir::IrStatement>> lowerForEach(const GraphNode& node,
                                                               Frame& frame,
                                                               std::vector<std::shared_ptr<ir::IrStatement>>& into);
    std::vector<std::shared_ptr<ir::IrStatement>> lowerSimple(
        const GraphNode& node, const NodeDefinition& definition, Frame& frame,
        std::vector<std::shared_ptr<ir::IrStatement>>& into);

    void collapseTemporaries(std::vector<std::shared_ptr<ir::IrStatement>>& body, Frame& frame);
    void collapseIn(std::vector<std::shared_ptr<ir::IrStatement>>& block,
                    std::vector<std::shared_ptr<ir::IrStatement>>& whole, Frame& frame);
    static int countReads(const std::vector<std::shared_ptr<ir::IrStatement>>& body,
                          std::string_view name);

    std::optional<std::string> step(std::string_view nodeId, std::string_view pinId,
                                    Frame& frame) const;
    bool inLoop(std::string_view nodeId, std::string_view what, Frame& frame);
    static std::string declareSentinel(Frame& frame);
    static std::shared_ptr<ir::IrExpression> guardedBy(
        const Frame::Loop& loop, std::shared_ptr<ir::IrExpression> condition,
        std::vector<std::shared_ptr<ir::IrStatement>>& before);
    std::optional<std::string> nextOf(std::string_view nodeId, const NodeDefinition& definition,
                                      Frame& frame) const;
    static std::shared_ptr<ir::IrIf> fold(std::shared_ptr<ir::IrExpression> condition,
                                          std::vector<std::shared_ptr<ir::IrStatement>> thenBody,
                                          std::vector<std::shared_ptr<ir::IrStatement>> elseBody,
                                          std::string_view nodeId);
    static std::shared_ptr<ir::IrExpression> negate(std::shared_ptr<ir::IrExpression> condition);

    std::shared_ptr<ir::IrExpression> pull(const PinRef& pin, Frame& frame,
                                           std::vector<std::shared_ptr<ir::IrStatement>>& into);
    std::shared_ptr<ir::IrExpression> buildPure(const GraphNode& node,
                                                const NodeDefinition& definition,
                                                std::string_view outputPin, Frame& frame,
                                                std::vector<std::shared_ptr<ir::IrStatement>>& into);
    std::shared_ptr<ir::IrCall> buildCall(const GraphNode& node, const NodeDefinition& definition,
                                          Frame& frame,
                                          std::vector<std::shared_ptr<ir::IrStatement>>& into);
    std::shared_ptr<ir::IrExpression> pullReceiver(const GraphNode& node,
                                                   const NodeDefinition& definition, Frame& frame,
                                                   std::vector<std::shared_ptr<ir::IrStatement>>& into);
    std::string bind(std::string_view nodeId, Frame& frame, std::string_view hint,
                     std::string_view typeName, bool isArray, std::shared_ptr<ir::IrExpression> value,
                     std::vector<std::shared_ptr<ir::IrStatement>>& into, bool declareOnly = false);

    GenericBinding genericsFor(std::string_view nodeId) const;
    std::shared_ptr<ir::IrExpression> literalFor(const PinRef& pin);
    static std::string literalTextOf(const GraphNode& node, const NodeDefinition& definition);
    static std::string baseType(std::string_view written);
    static bool isArray(std::string_view written);

    const GraphDocument& document_;
    const GraphValidation& validation_;
    GraphTypeResolver& types_;
    const papyrus::Script* owner_;
    std::vector<GraphDiagnostic> problems_;
};

}
