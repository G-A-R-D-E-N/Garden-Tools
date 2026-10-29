#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/binding.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type.hpp>

namespace fo4::papyrus {

class TypeChecker {
public:
    explicit TypeChecker(ScriptIndex& index) : index_(index) {}

    std::vector<Diagnostic> check(const Resolution& resolution);

private:
    struct State {
        const Resolution* resolution{nullptr};
        std::optional<std::string> file;
        std::vector<Diagnostic> diagnostics;
        std::optional<Type> returnType;

        void report(DiagnosticCode code, std::string message, const Span& span);
    };

    void checkCallable(State& state, const CallableDecl& callable);
    void checkOverride(State& state, const FunctionDecl& fn);
    void checkStatements(State& state, const std::vector<StatementPtr>& body);
    void checkStatement(State& state, const Statement* statement);
    void checkInitializer(State& state, const TypeRef& declared, const Node* initializer);
    void checkExpression(State& state, const Node* expression);
    void checkCall(State& state, const CallExpression& call);
    Type parameterType(const Parameter& parameter, const Script* owner);
    void checkAssignable(State& state, const Type& target, const Type& value, const Span& span,
                         std::string_view what);
    void checkComparable(State& state, const Type& left, const Type& right, const Span& span);
    bool comparable(const Type& left, const Type& right);
    bool inherits(const std::string& child, const std::string& ancestor);

    ScriptIndex& index_;
    std::unordered_map<std::string, bool> inheritsCache_;
};

}
