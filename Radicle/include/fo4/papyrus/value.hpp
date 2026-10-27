#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fo4::papyrus {

enum class ValueType : std::uint8_t {
    None = 0,
    String = 1,
    Integer = 2,
    Float = 3,
    Bool = 4,
    Object = 5,
    Array = 6,
    Struct = 7,
};

struct ObjectRef {
    std::uint64_t handle{0};
    std::string type;
    std::string state;
};

struct Array;
struct Struct;

struct Value {
    ValueType type{ValueType::None};
    std::string str;
    std::int32_t integer{0};
    float real{0.0F};
    bool boolean{false};
    std::shared_ptr<ObjectRef> object;
    std::shared_ptr<Array> array;
    std::shared_ptr<Struct> structure;
};

struct Array {
    ValueType elementType{ValueType::None};
    std::string elementTypeName{"Var"};
    std::vector<Value> elements;
};

// Invariant: a struct is a reference value and its members are keyed by the folded name.
struct Struct {
    std::string typeName;
    std::map<std::string, Value> members;
};

Value noneValue();
Value stringValue(std::string text);
Value integerValue(std::int32_t number);
Value floatValue(float number);
Value boolValue(bool flag);
Value objectValue(std::uint64_t handle, std::string type, std::string state);
Value arrayValue(std::shared_ptr<Array> array);
Value structValue(std::shared_ptr<Struct> structure);

bool isNoneLike(const Value& value);
std::string typeName(ValueType type);
ValueType declaredValueType(const std::string& declaredType);
bool isArrayTypeName(const std::string& declaredType);
std::string arrayElementTypeName(const std::string& declaredType);
Value defaultValueFor(const std::string& declaredType);

bool equalValues(const Value& left, const Value& right);
bool orderValues(const Value& left, const Value& right, int& order);
std::string valueToString(const Value& value);
std::string lowerAscii(const std::string& text);
bool sameName(const std::string& left, const std::string& right);

bool castScalar(const Value& source, ValueType target, Value& out);
void parseInteger(const std::string& text, std::int32_t& out);
void parseFloat(const std::string& text, float& out);
bool equalFloats(float left, float right);

}
