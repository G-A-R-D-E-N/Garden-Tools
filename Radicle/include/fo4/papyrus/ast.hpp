#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/token.hpp>

namespace fo4::papyrus {

// Every node must carry the source span it was parsed from.

enum class NodeKind {
    Import,
    VariableDecl,
    StructDecl,
    CustomEventDecl,
    Parameter,
    FunctionDecl,
    EventDecl,
    PropertyDecl,
    GroupDecl,
    StateDecl,
    Script,
    DefineStatement,
    AssignStatement,
    ExpressionStatement,
    ReturnStatement,
    IfStatement,
    WhileStatement,
    LiteralExpression,
    IdentifierExpression,
    MemberExpression,
    IndexExpression,
    Argument,
    CallExpression,
    UnaryExpression,
    BinaryExpression,
    CastExpression,
    TypeCheckExpression,
    NewArrayExpression,
    NewStructExpression,
    ErrorExpression,
};

struct Node {
    Span span;
    virtual ~Node() = default;
    // The explicit noexcept move must stay: a user-declared destructor would suppress it.
    Node() = default;
    Node(const Node&) = default;
    Node(Node&&) noexcept = default;
    Node& operator=(const Node&) = default;
    Node& operator=(Node&&) noexcept = default;
    virtual NodeKind nodeKind() const = 0;
};

struct TypeRef {
    std::string name;
    bool isArray{false};
    Span span;

    std::string toString() const { return isArray ? name + "[]" : name; }
};

// The signature must stay byte-identical to the C# PapyrusDeclaration.Signature text.
struct Declaration : Node {
    std::string name;
    Span nameSpan;
    std::vector<std::string> flags;
    std::optional<std::string> documentation;

    bool hasFlag(std::string_view flag) const {
        for (const auto& candidate : flags) {
            if (candidate.size() == flag.size()) {
                bool equal = true;
                for (std::size_t i = 0; i < candidate.size(); ++i) {
                    const auto a = candidate[i];
                    const auto b = flag[i];
                    if ((a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) !=
                        (b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b)) {
                        equal = false;
                        break;
                    }
                }
                if (equal) return true;
            }
        }
        return false;
    }

    virtual std::string signature() const = 0;
};

struct Import : Node {
    NodeKind nodeKind() const override { return NodeKind::Import; }
    std::string name;
    Span nameSpan;
};

struct VariableDecl : Declaration {
    NodeKind nodeKind() const override { return NodeKind::VariableDecl; }
    TypeRef type;
    std::unique_ptr<Node> initializer;

    std::string signature() const override {
        std::string text = type.toString() + " " + name;
        if (!flags.empty()) {
            text += " ";
            for (std::size_t i = 0; i < flags.size(); ++i) {
                if (i != 0) text += " ";
                text += flags[i];
            }
        }
        return text;
    }
};

struct StructDecl : Declaration {
    NodeKind nodeKind() const override { return NodeKind::StructDecl; }
    std::vector<VariableDecl> members;

    std::string signature() const override { return "Struct " + name; }
};

struct CustomEventDecl : Declaration {
    NodeKind nodeKind() const override { return NodeKind::CustomEventDecl; }
    std::string signature() const override { return "CustomEvent " + name; }
};

enum class ParameterSemantic { None, ScriptEventName, CustomEventName, StructVarName };

struct Parameter : Declaration {
    NodeKind nodeKind() const override { return NodeKind::Parameter; }
    TypeRef type;
    std::unique_ptr<Node> defaultValue;

    ParameterSemantic semantic() const {
        if (type.name == "scripteventname") return ParameterSemantic::ScriptEventName;
        if (type.name == "customeventname") return ParameterSemantic::CustomEventName;
        if (type.name == "structvarname") return ParameterSemantic::StructVarName;
        return ParameterSemantic::None;
    }

    std::string signature() const override {
        return defaultValue ? type.toString() + " " + name + " = ..."
                            : type.toString() + " " + name;
    }
};

struct Statement : Node {};

struct Expression : Node {};

using StatementPtr = std::unique_ptr<Statement>;
using ExpressionPtr = std::unique_ptr<Expression>;

// Parameters must stay in source order, and a default value makes the parameter optional.
struct CallableDecl : Declaration {
    std::vector<Parameter> parameters;
    std::vector<StatementPtr> body;
    bool isNative{false};
    std::optional<std::string> stateName;

    std::string parameterList() const {
        std::string text;
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0) text += ", ";
            text += parameters[i].signature();
        }
        return text;
    }
};

struct FunctionDecl : CallableDecl {
    NodeKind nodeKind() const override { return NodeKind::FunctionDecl; }
    std::optional<TypeRef> returnType;
    bool isGlobal{false};

    std::string signature() const override {
        std::string text = returnType ? returnType->toString() + " " : "";
        text += "Function " + name + "(" + parameterList() + ")";
        if (isGlobal) text += " global";
        if (isNative) text += " native";
        return text;
    }
};

struct EventDecl : CallableDecl {
    NodeKind nodeKind() const override { return NodeKind::EventDecl; }
    std::optional<std::string> remoteObjectType;

    std::string signature() const override {
        const auto qualified = remoteObjectType ? *remoteObjectType + "." + name : name;
        std::string text = "Event " + qualified + "(" + parameterList() + ")";
        if (isNative) text += " native";
        return text;
    }
};

enum class PropertyKind { Auto, AutoReadOnly, Full };

struct PropertyDecl : Declaration {
    NodeKind nodeKind() const override { return NodeKind::PropertyDecl; }
    TypeRef type;
    PropertyKind kind{PropertyKind::Full};
    std::unique_ptr<Node> initializer;
    std::optional<FunctionDecl> getter;
    std::optional<FunctionDecl> setter;
    std::optional<std::string> groupName;

    std::string signature() const override {
        std::string kindText;
        switch (kind) {
            case PropertyKind::Auto: kindText = " Auto"; break;
            case PropertyKind::AutoReadOnly: kindText = " AutoReadOnly"; break;
            case PropertyKind::Full: break;
        }
        return type.toString() + " Property " + name + kindText;
    }
};

struct GroupDecl : Declaration {
    NodeKind nodeKind() const override { return NodeKind::GroupDecl; }
    // These aliases must stay valid across Script moves, so Script owns a deque.
    std::vector<PropertyDecl*> properties;

    std::string signature() const override { return "Group " + name; }
};

struct StateDecl : Declaration {
    NodeKind nodeKind() const override { return NodeKind::StateDecl; }
    bool isAuto{false};
    std::vector<FunctionDecl> functions;
    std::vector<EventDecl> events;

    std::string signature() const override {
        return (isAuto ? "Auto State " : "State ") + name;
    }
};

struct Script : Declaration {
    NodeKind nodeKind() const override { return NodeKind::Script; }
    // The explicit defaulted noexcept move must stay: a deque move is a pointer transfer.
    Script() = default;
    Script(Script&&) noexcept = default;
    Script& operator=(Script&&) noexcept = default;
    std::optional<std::string> filePath;
    std::optional<std::string> extends;
    Span extendsSpan;
    std::vector<Import> imports;
    std::vector<VariableDecl> variables;
    std::vector<StructDecl> structs;
    std::vector<CustomEventDecl> customEvents;
    // A deque must stay so appending a property never moves the ones a group points at.
    std::deque<PropertyDecl> properties;
    std::vector<GroupDecl> groups;
    std::vector<StateDecl> states;
    std::vector<FunctionDecl> functions;
    std::vector<EventDecl> events;
    std::vector<Diagnostic> diagnostics;

    bool hasErrors() const {
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == Severity::Error) return true;
        }
        return false;
    }

    std::string signature() const override {
        return extends ? "ScriptName " + name + " extends " + *extends
                       : "ScriptName " + name;
    }
};

// A local definition must keep its declared type for the later check passes.
struct DefineStatement : Statement {
    NodeKind nodeKind() const override { return NodeKind::DefineStatement; }
    TypeRef type;
    std::string name;
    Span nameSpan;
    std::unique_ptr<Node> initializer;
    std::vector<std::string> flags;
};

struct AssignStatement : Statement {
    NodeKind nodeKind() const override { return NodeKind::AssignStatement; }
    ExpressionPtr target;
    TokenKind op{TokenKind::Assign};
    ExpressionPtr value;
};

struct ExpressionStatement : Statement {
    NodeKind nodeKind() const override { return NodeKind::ExpressionStatement; }
    ExpressionPtr expression;
};

struct ReturnStatement : Statement {
    NodeKind nodeKind() const override { return NodeKind::ReturnStatement; }
    ExpressionPtr value;
};

struct IfBranch : Node {
    NodeKind nodeKind() const override { return NodeKind::IfStatement; }
    ExpressionPtr condition;
    std::vector<StatementPtr> body;
};

struct IfStatement : Statement {
    NodeKind nodeKind() const override { return NodeKind::IfStatement; }
    std::vector<IfBranch> branches;
    std::vector<StatementPtr> elseBody;
};

struct WhileStatement : Statement {
    NodeKind nodeKind() const override { return NodeKind::WhileStatement; }
    ExpressionPtr condition;
    std::vector<StatementPtr> body;
};

enum class LiteralKind { Int, Float, String, Bool, None };

struct LiteralExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::LiteralExpression; }
    LiteralKind kind{LiteralKind::Int};
    std::string text;
};

struct IdentifierExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::IdentifierExpression; }
    std::string name;
};

struct MemberExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::MemberExpression; }
    ExpressionPtr target;
    std::string name;
    Span nameSpan;
};

struct IndexExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::IndexExpression; }
    ExpressionPtr target;
    ExpressionPtr index;
};

struct Argument : Node {
    NodeKind nodeKind() const override { return NodeKind::Argument; }
    std::optional<std::string> name;
    Span nameSpan;
    ExpressionPtr value;
};

struct CallExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::CallExpression; }
    ExpressionPtr callee;
    std::vector<Argument> arguments;

    std::string functionName() const {
        if (const auto* identifier = dynamic_cast<const IdentifierExpression*>(callee.get())) {
            return identifier->name;
        }
        if (const auto* member = dynamic_cast<const MemberExpression*>(callee.get())) {
            return member->name;
        }
        return "";
    }
};

struct UnaryExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::UnaryExpression; }
    TokenKind op{TokenKind::Minus};
    ExpressionPtr operand;
};

struct BinaryExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::BinaryExpression; }
    ExpressionPtr left;
    TokenKind op{TokenKind::Plus};
    ExpressionPtr right;
};

struct CastExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::CastExpression; }
    ExpressionPtr operand;
    TypeRef type;
};

struct TypeCheckExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::TypeCheckExpression; }
    ExpressionPtr operand;
    TypeRef type;
};

struct NewArrayExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::NewArrayExpression; }
    TypeRef elementType;
    ExpressionPtr size;
};

struct NewStructExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::NewStructExpression; }
    TypeRef type;
};

struct ErrorExpression : Expression {
    NodeKind nodeKind() const override { return NodeKind::ErrorExpression; }
};

}
