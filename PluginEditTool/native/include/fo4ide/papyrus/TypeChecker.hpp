#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Binding.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Type.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusTypeChecker: consumes a Resolution and reports type
// mismatches, argument-count/name problems, invalid casts, override mismatches
// and parameter-order errors. Reports nothing when the base chain is
// incomplete (mirrors the C# oracle).
class TypeChecker {
public:
    explicit TypeChecker(ScriptIndex& index) : index_(index) {}

    // Non-const throughout: inherits() mutates the cache (the C# has no const
    // methods either).
    std::vector<Diagnostic> check(const Resolution& resolution);

private:
    struct State {
        const Resolution* resolution{nullptr};
        std::optional<std::string> file;
        std::vector<Diagnostic> diagnostics;
        std::optional<Type> returnType;

        void report(DiagnosticCode code, std::string message, const Span& span,
                    Severity severity = Severity::Error);
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
    bool inherits(const std::string& child, const std::string& ancestor);

    ScriptIndex& index_;
    std::unordered_map<std::string, bool> inheritsCache_;
};

}  // namespace fo4ide::papyrus
