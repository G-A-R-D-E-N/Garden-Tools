#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Type.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusBindingKind.
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

// Port of PapyrusBinding. `declaration` and `owner` point into the parsed
// Script owned by the ScriptIndex cache (which must outlive the resolution).
struct Binding {
    BindingKind kind{};
    std::string name;
    Type type;
    const Declaration* declaration{nullptr};
    const Script* owner{nullptr};

    std::string toString() const;
};

// Port of PapyrusResolution. Maps are keyed on node identity: expression and
// statement nodes are heap-allocated (stable addresses), declaration nodes are
// value members of the parsed Script (stable once parsing finished). TypeRefs
// are also value members, keyed by address.
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

}  // namespace fo4ide::papyrus
