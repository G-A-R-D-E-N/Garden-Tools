#include <fo4ide/papyrus/Binding.hpp>

namespace fo4ide::papyrus {

namespace {

const char* kindString(BindingKind kind) {
    switch (kind) {
        case BindingKind::Local: return "Local";
        case BindingKind::Parameter: return "Parameter";
        case BindingKind::ScriptVariable: return "ScriptVariable";
        case BindingKind::Property: return "Property";
        case BindingKind::Function: return "Function";
        case BindingKind::Event: return "Event";
        case BindingKind::CustomEvent: return "CustomEvent";
        case BindingKind::Struct: return "Struct";
        case BindingKind::StructMember: return "StructMember";
        case BindingKind::Script: return "Script";
        case BindingKind::ArrayMember: return "ArrayMember";
        case BindingKind::SelfKeyword: return "SelfKeyword";
        case BindingKind::ParentKeyword: return "ParentKeyword";
    }
    return "?";
}

}  // namespace

std::string Binding::toString() const {
    return std::string(kindString(kind)) + " " + name + " : " + type.toString();
}

const Binding* Resolution::bindingFor(const Node* node) const {
    if (!node) return nullptr;
    const auto it = bindings.find(node);
    return it == bindings.end() ? nullptr : &it->second;
}

const Type* Resolution::typeOf(const Node* node) const {
    if (!node) return &Type::error();
    const auto it = types.find(node);
    return it == types.end() ? &Type::error() : &it->second;
}

const Type* Resolution::typeOf(const TypeRef* reference) const {
    if (!reference) return &Type::error();
    const auto it = typeRefs.find(reference);
    return it == typeRefs.end() ? &Type::error() : &it->second;
}

}  // namespace fo4ide::papyrus
