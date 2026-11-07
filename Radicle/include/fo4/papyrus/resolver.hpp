#pragma once

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/binding.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type.hpp>

namespace fo4::papyrus {

class Resolver {
public:
    explicit Resolver(ScriptIndex& index) : index_(index) {}

    Resolution resolve(const Script* script) const;

    static std::optional<Type> resolveTypeName(ScriptIndex& index, const std::string& name,
                                               const Script* relativeTo);

private:
    struct Context {
        const Script* script{nullptr};
        std::vector<const Script*> chain;
        std::vector<const Script*> imports;
        std::unordered_map<const Node*, Binding> bindings;
        std::unordered_map<const Node*, Type> types;
        std::unordered_map<const TypeRef*, Type> typeRefs;
        std::vector<Diagnostic> diagnostics;
        bool baseChainComplete{true};

        std::vector<std::unordered_map<std::string, Binding>> scopes;
        std::optional<Type> selfType;

        std::deque<Binding> ownedBindings;
        std::deque<Type> ownedTypes;
    };

    void resolveDeclarations(Context& ctx) const;
    void resolveBodies(Context& ctx) const;

    void resolveCallable(Context& ctx, const CallableDecl& callable, bool isGlobal) const;
    void resolveBlock(Context& ctx, const std::vector<StatementPtr>& body) const;
    void resolveStatement(Context& ctx, const Statement* statement) const;

    const Type* resolveMemberTarget(Context& ctx, const Node* target) const;
    const Type* resolveCallee(Context& ctx, const Node* callee) const;
    const Type* resolveExpression(Context& ctx, const Node* expression) const;
    const Type* computeType(Context& ctx, const Node* expression) const;

    static Type binaryResult(TokenKind op, const Type& left, const Type& right);

    const Binding* lookupName(Context& ctx, const std::string& name, bool inCallPosition) const;
    const Binding* lookupOnChain(Context& ctx, const std::vector<const Script*>& chain,
                                 const std::string& name, bool globalsOnly) const;
    const Binding* lookupMember(Context& ctx, const Node* target, const Type& targetType,
                                const std::string& name) const;
    static std::optional<Binding> arrayMember(const Type& arrayType, const std::string& name);

    const Binding* bindingFor(Context& ctx, const Declaration* declaration,
                              const Script* owner) const;
    const Type* typeOfRef(Context& ctx, const TypeRef& reference,
                          const Script* relativeTo = nullptr) const;

    static void reportUnresolved(Context& ctx, const std::string& name, const Span& span);
    static void reportUnknownMember(Context& ctx, const std::string& name, const Type& targetType,
                                    const Span& span);

    ScriptIndex& index_;
};

}
