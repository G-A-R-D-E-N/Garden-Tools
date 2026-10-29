#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/type.hpp>

namespace fo4::papyrus {

enum class BindingKind {
    Local,
    Parameter,
    ScriptVariable,
    Property,
    Function,
    Event,
    CustomEvent,
    Struct,
    StructMember,
    Script,
    ArrayMember,
    SelfKeyword,
    ParentKeyword,
};

struct Binding {
    BindingKind kind{};
    std::string name;
    Type type;
    const Declaration* declaration{nullptr};
    const Script* owner{nullptr};

    std::string toString() const;
};

// Invariant: the Script must outlive the Resolution, because bindings and types are keyed by node address.
struct Resolution {
    const Script* script{nullptr};
    std::unordered_map<const Node*, Binding> bindings;
    std::unordered_map<const Node*, Type> types;
    std::unordered_map<const TypeRef*, Type> typeRefs;
    std::vector<Diagnostic> diagnostics;
    bool baseChainComplete{true};

    const Binding* bindingFor(const Node* node) const;
    const Type* typeOf(const Node* node) const;
    const Type* typeOf(const TypeRef* reference) const;
};

}
