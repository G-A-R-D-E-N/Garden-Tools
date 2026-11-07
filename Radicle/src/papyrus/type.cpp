#include <fo4/papyrus/type.hpp>

#include <cctype>

namespace fo4::papyrus {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

}

const Type& Type::none() {
    static const Type value{TypeKind::None, "None", nullptr};
    return value;
}

const Type& Type::bool_() {
    static const Type value{TypeKind::Bool, "bool", nullptr};
    return value;
}

const Type& Type::int_() {
    static const Type value{TypeKind::Int, "int", nullptr};
    return value;
}

const Type& Type::float_() {
    static const Type value{TypeKind::Float, "float", nullptr};
    return value;
}

const Type& Type::string_() {
    static const Type value{TypeKind::String, "string", nullptr};
    return value;
}

const Type& Type::var_() {
    static const Type value{TypeKind::Var, "var", nullptr};
    return value;
}

const Type& Type::error() {
    static const Type value{TypeKind::Error, "<unknown>", nullptr};
    return value;
}

Type Type::object(std::string scriptName) {
    return Type{TypeKind::Object, std::move(scriptName), nullptr};
}

Type Type::structOf(std::string owningScript, std::string structName) {
    return Type{TypeKind::Struct, owningScript + ":" + structName, nullptr};
}

Type Type::arrayOf(Type element) {
    return Type{TypeKind::Array, element.name, std::make_shared<const Type>(std::move(element))};
}

const Type* Type::primitive(std::string_view name) {
    if (iequals(name, "bool")) return &bool_();
    if (iequals(name, "int")) return &int_();
    if (iequals(name, "float")) return &float_();
    if (iequals(name, "string")) return &string_();
    if (iequals(name, "var")) return &var_();
    if (iequals(name, "none")) return &none();
    return nullptr;
}

std::string Type::toString() const {
    return isArray() ? element->toString() + "[]" : name;
}

bool Type::operator==(const Type& other) const {
    if (kind != other.kind) return false;
    if (!iequals(name, other.name)) return false;
    if (element && other.element) return *element == *other.element;
    return !element && !other.element;
}

bool isImplicit(const Type& from, const Type& to,
                const std::function<bool(const std::string&, const std::string&)>& inherits) {
    const auto alwaysFalse = [](const std::string&, const std::string&) { return false; };
    const auto& inheritsImpl = inherits ? inherits : std::function(alwaysFalse);

    if (from.kind == TypeKind::Error || to.kind == TypeKind::Error) return true;
    if (from == to) return true;

    switch (to.kind) {
        case TypeKind::Bool:
        case TypeKind::String:
            return true;

        case TypeKind::Float:
            return from.kind == TypeKind::Int;

        case TypeKind::Int:
            return false;

        case TypeKind::Var:
            return !from.isArray();

        case TypeKind::Object:
            if (from.kind == TypeKind::None) return true;
            return from.kind == TypeKind::Object && inheritsImpl(from.name, to.name);

        case TypeKind::Struct:
        case TypeKind::Array:
            return from.kind == TypeKind::None;

        case TypeKind::None:
            return from.kind == TypeKind::None;

        default:
            return false;
    }
}

bool isExplicit(const Type& from, const Type& to,
                const std::function<bool(const std::string&, const std::string&)>& inherits) {
    const auto alwaysFalse = [](const std::string&, const std::string&) { return false; };
    const auto& inheritsImpl = inherits ? inherits : std::function(alwaysFalse);

    if (isImplicit(from, to, inheritsImpl)) return true;
    if (from.kind == TypeKind::Var || to.kind == TypeKind::Var) return !from.isArray() || to.isArray();

    switch (to.kind) {
        case TypeKind::Int:
        case TypeKind::Float:
            return from.kind == TypeKind::Int || from.kind == TypeKind::Float ||
                   from.kind == TypeKind::String || from.kind == TypeKind::Bool;

        case TypeKind::Object:
            return from.kind == TypeKind::Object && inheritsImpl(to.name, from.name);

        case TypeKind::Array:
            return from.isArray() && isExplicit(*from.element, *to.element, inheritsImpl);

        case TypeKind::Struct:
            return false;

        default:
            return false;
    }
}

}
