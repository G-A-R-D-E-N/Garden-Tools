#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphDiagnostic.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Binding.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::graph {

// Port of GraphLiftResult.
struct GraphLiftResult {
    std::optional<GraphDocument> document;
    std::vector<GraphDiagnostic> diagnostics;

    bool success() const {
        if (!document) return false;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == GraphSeverity::Error) return false;
        }
        return true;
    }
};

// Port of GraphLifter: turns a parsed Papyrus script back into a
// GraphDocument (the inverse of the compiler). Nodes are laid out in a pen
// walk per callable; constructs the graph cannot express are refused with
// GRA0001 rather than mangled.
class GraphLifter {
public:
    explicit GraphLifter(papyrus::ScriptIndex& index);

    GraphLiftResult lift(const papyrus::Script& script);

private:
    struct Value {
        std::optional<PinRef> pin;
        std::optional<GraphPinValue> literal;
    };

    void liftHeader();
    void liftDeclarations();

    std::optional<std::string> entryNode(const papyrus::Declaration& declaration,
                                         const std::optional<std::string>& state);
    std::optional<std::string> declaringScriptOf(const std::string& eventName);

    std::vector<PinRef> liftBlock(const std::vector<papyrus::StatementPtr>& body,
                                  std::vector<PinRef> open);
    std::vector<PinRef> liftStatement(const papyrus::Statement& statement,
                                      std::vector<PinRef> open);
    std::vector<PinRef> liftAssign(const papyrus::AssignStatement& assign,
                                   std::vector<PinRef> open);
    std::vector<PinRef> liftIf(const papyrus::IfStatement& statement,
                               std::vector<PinRef> open, std::size_t arm);
    std::vector<PinRef> liftWhile(const papyrus::WhileStatement& loop,
                                  std::vector<PinRef> open);
    static bool containsCall(const papyrus::Node& node);

    std::optional<Value> liftExpression(const papyrus::Expression& expression,
                                        std::vector<PinRef>& open);
    std::optional<Value> liftIdentifier(const papyrus::IdentifierExpression& name);
    std::optional<Value> liftMember(const papyrus::MemberExpression& member,
                                    std::vector<PinRef>& open);
    std::optional<Value> liftCall(const papyrus::CallExpression& call,
                                  std::vector<PinRef>& open);

    std::string node(std::string_view definitionId, GraphNodeKind kind);
    void config(const std::string& nodeId, std::string_view key,
                const std::optional<std::string>& value);
    void enter(std::vector<PinRef>& open, const std::string& nodeId);
    void wire(const PinRef& from, const PinRef& to);
    void attach(const Value& value, const PinRef& target);

    void refuse(const std::string& message, const papyrus::Span& span);
    std::string typeName(const papyrus::Node& node);
    static std::string written(const papyrus::TypeRef* type);
    static bool isNone(const papyrus::TypeRef& type);
    static std::optional<std::string> text(const papyrus::Node* node);
    static std::string literalText(const papyrus::LiteralExpression& literal);
    static std::string literalType(const papyrus::LiteralExpression& literal);
    static std::optional<std::string> compoundId(papyrus::TokenKind op);
    static std::optional<std::string> binaryId(papyrus::TokenKind op);

    papyrus::ScriptIndex& index_;
    const papyrus::Script* script_{nullptr};
    std::optional<papyrus::Resolution> resolution_;
    std::optional<GraphDocument> doc_;
    std::vector<GraphDiagnostic> problems_;
    int sequence_{0};
    double penX_{0};
    double penY_{0};
};

}  // namespace fo4ide::graph
