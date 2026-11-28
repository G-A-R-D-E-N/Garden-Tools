#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace fo4::papyrus {

enum class TypeKind {
    None,
    Bool,
    Int,
    Float,
    String,
    Var,
    Object,
    Struct,
    Array,
    Error,
};

struct Type {
    TypeKind kind{TypeKind::Error};
    std::string name;
    std::shared_ptr<const Type> element;

    static const Type& none();
    static const Type& bool_();
    static const Type& int_();
    static const Type& float_();
    static const Type& string_();
    static const Type& var_();
    static const Type& error();

    static Type object(std::string scriptName);
    static Type structOf(std::string owningScript, std::string structName);
    static Type arrayOf(Type element);

    static const Type* primitive(std::string_view name);

    bool isArray() const { return kind == TypeKind::Array; }
    bool isReference() const {
        return kind == TypeKind::Object || kind == TypeKind::Struct || kind == TypeKind::Array;
    }

    std::string toString() const;

    bool operator==(const Type& other) const;
    bool operator!=(const Type& other) const { return !(*this == other); }
};

bool isImplicit(const Type& from, const Type& to,
                const std::function<bool(const std::string&, const std::string&)>& inherits = {});
bool isExplicit(const Type& from, const Type& to,
                const std::function<bool(const std::string&, const std::string&)>& inherits = {});

}
