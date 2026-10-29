#include "fo4/papyrus/value.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace fo4::papyrus {
namespace {

constexpr float kFloatEpsilon = 9.999995e-05F;
constexpr float kFloatBoolFloor = 9.9999997e-06F;

}

Value noneValue() { return Value{}; }

Value stringValue(std::string text) {
    Value value;
    value.type = ValueType::String;
    value.str = std::move(text);
    return value;
}

Value integerValue(std::int32_t number) {
    Value value;
    value.type = ValueType::Integer;
    value.integer = number;
    return value;
}

Value floatValue(float number) {
    Value value;
    value.type = ValueType::Float;
    value.real = number;
    return value;
}

Value boolValue(bool flag) {
    Value value;
    value.type = ValueType::Bool;
    value.boolean = flag;
    return value;
}

Value objectValue(std::uint64_t handle, std::string type, std::string state) {
    Value value;
    value.type = ValueType::Object;
    value.object = std::make_shared<ObjectRef>();
    value.object->handle = handle;
    value.object->type = std::move(type);
    value.object->state = std::move(state);
    return value;
}

Value arrayValue(std::shared_ptr<Array> array) {
    Value value;
    value.type = ValueType::Array;
    value.array = std::move(array);
    return value;
}

Value structValue(std::shared_ptr<Struct> structure) {
    Value value;
    value.type = ValueType::Struct;
    value.structure = std::move(structure);
    return value;
}

bool isNoneLike(const Value& value) {
    if (value.type == ValueType::None) return true;
    if (value.type == ValueType::Object) return value.object == nullptr;
    if (value.type == ValueType::Array) return value.array == nullptr;
    if (value.type == ValueType::Struct) return value.structure == nullptr;
    return false;
}

std::string lowerAscii(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
    }
    return out;
}

bool sameName(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const char a = left[i] >= 'A' && left[i] <= 'Z' ? static_cast<char>(left[i] - 'A' + 'a') : left[i];
        const char b = right[i] >= 'A' && right[i] <= 'Z' ? static_cast<char>(right[i] - 'A' + 'a') : right[i];
        if (a != b) return false;
    }
    return true;
}

std::string typeName(ValueType type) {
    switch (type) {
        case ValueType::None:
            return "None";
        case ValueType::String:
            return "String";
        case ValueType::Integer:
            return "Int";
        case ValueType::Float:
            return "Float";
        case ValueType::Bool:
            return "Bool";
        case ValueType::Object:
            return "Object";
        case ValueType::Array:
            return "Array";
        case ValueType::Struct:
            return "Struct";
    }
    return "None";
}

bool isArrayTypeName(const std::string& declaredType) {
    return declaredType.size() >= 2 && declaredType.compare(declaredType.size() - 2, 2, "[]") == 0;
}

std::string arrayElementTypeName(const std::string& declaredType) {
    if (!isArrayTypeName(declaredType)) return declaredType;
    return declaredType.substr(0, declaredType.size() - 2);
}

ValueType declaredValueType(const std::string& declaredType) {
    if (isArrayTypeName(declaredType)) return ValueType::Array;
    const std::string folded = lowerAscii(declaredType);
    if (folded == "int") return ValueType::Integer;
    if (folded == "float") return ValueType::Float;
    if (folded == "bool") return ValueType::Bool;
    if (folded == "string") return ValueType::String;
    if (folded == "var" || folded == "none" || folded.empty()) return ValueType::None;
    return ValueType::Object;
}

Value defaultValueFor(const std::string& declaredType) {
    switch (declaredValueType(declaredType)) {
        case ValueType::String:
            return stringValue(std::string());
        case ValueType::Integer:
            return integerValue(0);
        case ValueType::Float:
            return floatValue(0.0F);
        case ValueType::Bool:
            return boolValue(false);
        case ValueType::Array:
            return arrayValue(nullptr);
        case ValueType::Struct:
            return structValue(nullptr);
        case ValueType::Object:
        case ValueType::None:
            return noneValue();
    }
    return noneValue();
}

bool equalFloats(float left, float right) {
    const float difference = left - right;
    const float magnitude = difference < 0.0F ? -difference : difference;
    return magnitude <= kFloatEpsilon;
}

void parseInteger(const std::string& text, std::int32_t& out) {
    std::size_t index = 0;
    while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\n' ||
                                   text[index] == '\r' || text[index] == '\f' || text[index] == '\v')) {
        ++index;
    }
    bool negative = false;
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
        negative = text[index] == '-';
        ++index;
    }
    std::uint64_t accumulated = 0;
    std::size_t digits = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        accumulated = accumulated * 10u + static_cast<std::uint64_t>(text[index] - '0');
        ++index;
        ++digits;
    }
    if (digits == 0) {
        out = 0;
        return;
    }
    const auto wrapped = static_cast<std::uint32_t>(accumulated);
    const auto bits = negative ? static_cast<std::uint32_t>(0u - wrapped) : wrapped;
    out = static_cast<std::int32_t>(bits);
}

void parseFloat(const std::string& text, float& out) {
    out = std::strtof(text.c_str(), nullptr);
}

bool castScalar(const Value& source, ValueType target, Value& out) {
    switch (target) {
        case ValueType::Integer:
            switch (source.type) {
                case ValueType::Integer:
                    out = source;
                    return true;
                case ValueType::Float: {
                    const float number = source.real;
                    if (number != number || number == std::numeric_limits<float>::infinity() ||
                        number == -std::numeric_limits<float>::infinity() ||
                        number >= 2147483648.0F || number < -2147483648.0F) {
                        out = integerValue(std::numeric_limits<std::int32_t>::min());
                        return true;
                    }
                    out = integerValue(static_cast<std::int32_t>(number));
                    return true;
                }
                case ValueType::Bool:
                    out = integerValue(source.boolean ? 1 : 0);
                    return true;
                case ValueType::String: {
                    std::int32_t number = 0;
                    parseInteger(source.str, number);
                    out = integerValue(number);
                    return true;
                }
                default:
                    return false;
            }
        case ValueType::Float:
            switch (source.type) {
                case ValueType::Float:
                    out = source;
                    return true;
                case ValueType::Integer:
                    out = floatValue(static_cast<float>(source.integer));
                    return true;
                case ValueType::Bool:
                    out = floatValue(source.boolean ? 1.0F : 0.0F);
                    return true;
                case ValueType::String: {
                    float number = 0.0F;
                    parseFloat(source.str, number);
                    out = floatValue(number);
                    return true;
                }
                default:
                    return false;
            }
        case ValueType::Bool:
            switch (source.type) {
                case ValueType::Bool:
                    out = source;
                    return true;
                case ValueType::Integer:
                    out = boolValue(source.integer != 0);
                    return true;
                case ValueType::Float: {
                    const float magnitude = source.real < 0.0F ? -source.real : source.real;
                    out = boolValue(!(magnitude <= kFloatBoolFloor));
                    return true;
                }
                case ValueType::String:
                    return false;
                default:
                    return false;
            }
        case ValueType::String: {
            switch (source.type) {
                case ValueType::String:
                    out = source;
                    return true;
                case ValueType::Integer:
                    out = stringValue(std::to_string(source.integer));
                    return true;
                case ValueType::Float: {
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(source.real));
                    out = stringValue(std::string(buffer));
                    return true;
                }
                case ValueType::Bool:
                    out = stringValue(source.boolean ? "True" : "False");
                    return true;
                default:
                    return false;
            }
        }
        default:
            return false;
    }
}

std::string valueToString(const Value& value) {
    switch (value.type) {
        case ValueType::None:
            return "None";
        case ValueType::String:
            return value.str;
        case ValueType::Integer:
            return std::to_string(value.integer);
        case ValueType::Float: {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(value.real));
            return std::string(buffer);
        }
        case ValueType::Bool:
            return value.boolean ? "True" : "False";
        case ValueType::Object:
            return value.object == nullptr ? "None" : "[Object]";
        case ValueType::Array: {
            if (value.array == nullptr) return "[]";
            std::string text = "[";
            for (std::size_t i = 0; i < value.array->elements.size(); ++i) {
                if (i > 0) text += ", ";
                const Value& element = value.array->elements[i];
                if (element.type == ValueType::String) {
                    text += "\"" + element.str + "\"";
                } else {
                    text += valueToString(element);
                }
            }
            text += "]";
            return text;
        }
        case ValueType::Struct:
            return value.structure == nullptr ? "None" : "[Struct]";
    }
    return "None";
}

namespace {

constexpr int kMaxEqualDepth = 32;
constexpr int kMaxEqualStructVisits = 4096;

bool equalValuesWithin(const Value& left, const Value& right, int depth, int& budget) {
    if (depth > kMaxEqualDepth) return false;
    const bool leftNone = isNoneLike(left);
    const bool rightNone = isNoneLike(right);
    if (leftNone || rightNone) return leftNone && rightNone;

    if (left.type == ValueType::Integer && right.type == ValueType::Float) {
        return equalFloats(static_cast<float>(left.integer), right.real);
    }
    if (left.type == ValueType::Float && right.type == ValueType::Integer) {
        return equalFloats(left.real, static_cast<float>(right.integer));
    }
    if (left.type != right.type) return false;

    switch (left.type) {
        case ValueType::Integer:
            return left.integer == right.integer;
        case ValueType::Float:
            return equalFloats(left.real, right.real);
        case ValueType::Bool:
            return left.boolean == right.boolean;
        case ValueType::String:
            return lowerAscii(left.str) == lowerAscii(right.str);
        case ValueType::Object:
            return left.object->handle == right.object->handle &&
                   sameName(left.object->type, right.object->type);
        case ValueType::Array:
            if (left.array->elements.size() != right.array->elements.size()) return false;
            for (std::size_t i = 0; i < left.array->elements.size(); ++i) {
                if (!equalValuesWithin(left.array->elements[i], right.array->elements[i],
                                       depth + 1, budget)) {
                    return false;
                }
            }
            return true;
        case ValueType::Struct: {
            if (left.structure == right.structure) return true;
            if (!sameName(left.structure->typeName, right.structure->typeName)) return false;
            if (left.structure->members.size() != right.structure->members.size()) return false;
            if (--budget < 0) return false;
            for (const auto& entry : left.structure->members) {
                const auto found = right.structure->members.find(entry.first);
                if (found == right.structure->members.end()) return false;
                if (!equalValuesWithin(entry.second, found->second, depth + 1, budget)) {
                    return false;
                }
            }
            return true;
        }
        case ValueType::None:
            return true;
    }
    return false;
}

}

bool equalValues(const Value& left, const Value& right) {
    int budget = kMaxEqualStructVisits;
    return equalValuesWithin(left, right, 0, budget);
}

bool orderValues(const Value& left, const Value& right, int& order) {
    if (isNoneLike(left) || isNoneLike(right)) return false;

    if (left.type == ValueType::String && right.type == ValueType::String) {
        const std::string a = lowerAscii(left.str);
        const std::string b = lowerAscii(right.str);
        order = a == b ? 0 : (a < b ? -1 : 1);
        return true;
    }
    if (left.type == ValueType::Integer && right.type == ValueType::Integer) {
        order = left.integer == right.integer ? 0 : (left.integer < right.integer ? -1 : 1);
        return true;
    }
    if ((left.type == ValueType::Float || left.type == ValueType::Integer) &&
        (right.type == ValueType::Float || right.type == ValueType::Integer)) {
        const float a = left.type == ValueType::Float ? left.real : static_cast<float>(left.integer);
        const float b = right.type == ValueType::Float ? right.real : static_cast<float>(right.integer);
        if (equalFloats(a, b)) {
            order = 0;
            return true;
        }
        order = a < b ? -1 : 1;
        return true;
    }
    if (left.type == ValueType::Bool && right.type == ValueType::Bool) {
        if (left.boolean == right.boolean) {
            order = 0;
            return true;
        }
        order = left.boolean ? 1 : -1;
        return true;
    }
    return false;
}

}
