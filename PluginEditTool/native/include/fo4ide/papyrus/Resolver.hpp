#pragma once

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Binding.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Type.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusResolver: binds every identifier/member/call site to a
// declaration (or keyword) and computes the type of every expression. All
// diagnostics are gated on the base chain being complete (a missing parent or
// import suppresses name reporting, mirroring the C# oracle).
class Resolver {
public:
    explicit Resolver(ScriptIndex& index) : index_(index) {}

    Resolution resolve(const Script* script) const;

    // Static type-name resolution used by both the resolver and the type
    // checker: primitives, local structs on the base chain, Owner:Struct, and
    // scripts; nullopt when unresolvable.
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
        // Scope stack; keys are lowercased (case-insensitive like the C#).
        std::vector<std::unordered_map<std::string, Binding>> scopes;
        std::optional<Type> selfType;
        // Deques so element references stay stable as more are appended
        // (lookupName/bindingFor/computeType hand out pointers into these).
        std::deque<Binding> ownedBindings;
        std::deque<Type> ownedTypes;
    };

    void resolveDeclarations(Context& ctx) const;
    void resolveBodies(Context& ctx) const;

    void resolveCallable(Context& ctx, const CallableDecl& callable, bool isGlobal) const;
    void resolveBlock(Context& ctx, const std::vector<StatementPtr>& body) const;
    void resolveStatement(Context& ctx, const Statement* statement) const;

    // Expressions are stored as unique_ptr<Node> in the AST, so the entry
    // points take Node* and downcast internally.
    const Type* resolveMemberTarget(Context& ctx, const Node* target) const;
    const Type* resolveCallee(Context& ctx, const Node* callee) const;
    const Type* resolveMemberExpression(Context& ctx, const MemberExpression* member,
                                        bool callPosition) const;
    const Type* resolveExpression(Context& ctx, const Node* expression) const;
    const Type* computeType(Context& ctx, const Node* expression) const;

    static Type binaryResult(TokenKind op, const Type& left, const Type& right);

    // callPosition: a callee must bind to a function or event before any same-named property.
    const Binding* lookupName(Context& ctx, const std::string& name, bool inCallPosition) const;
    const Binding* lookupOnChain(Context& ctx, const std::vector<const Script*>& chain,
                                 const std::string& name, bool globalsOnly,
                                 bool callPosition = false) const;
    const Binding* lookupMember(Context& ctx, const Node* target, const Type& targetType,
                                const std::string& name, bool callPosition = false) const;
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

}  // namespace fo4ide::papyrus
