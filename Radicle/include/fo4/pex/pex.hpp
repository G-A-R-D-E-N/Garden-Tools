#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::pex {

enum class PexValueType : std::uint8_t {
    None = 0,
    Identifier = 1,
    String = 2,
    Integer = 3,
    Float = 4,
    Bool = 5,
};

struct PexValue {
    PexValueType type{PexValueType::None};
    std::string str;
    std::int32_t int_ = 0;
    float float_ = 0.0F;
    bool bool_ = false;

    bool isNone() const { return type == PexValueType::None; }
    std::string toString() const;
};

struct PexInstruction {
    std::uint8_t opCode{0};
    std::string mnemonic;
    int fixedArgCount{0};
    bool hasVarArgs{false};
    std::vector<PexValue> args;
    int line{0};
};

struct PexTypedName {
    std::string name;
    std::string type;
};

struct PexFunction {
    std::string name;
    std::string returnType{"None"};
    std::string docString;
    std::uint32_t userFlags{0};
    bool isGlobal{false};
    bool isNative{false};
    std::vector<PexTypedName> params;
    std::vector<PexTypedName> locals;
    std::vector<PexInstruction> instructions;
};

struct PexState {
    std::string name;
    std::vector<PexFunction> functions;
};

struct PexVariable {
    std::string name;
    std::string type;
    std::uint32_t userFlags{0};
    std::optional<PexValue> defaultValue;
    bool isConst{false};
};

struct PexProperty {
    std::string name;
    std::string type;
    std::string docString;
    std::uint32_t userFlags{0};
    std::uint8_t flags{0};

    bool isAutoVar() const { return (flags & 0x04) != 0; }
    bool canRead() const { return (flags & 0x01) != 0; }
    bool canWrite() const { return (flags & 0x02) != 0; }

    std::string autoVarName;
    std::optional<PexFunction> readHandler;
    std::optional<PexFunction> writeHandler;
};

struct PexStructMember {
    std::string name;
    std::string type;
    std::uint32_t userFlags{0};
    std::optional<PexValue> defaultValue;
    bool isConst{false};
    std::string docString;
};

struct PexStruct {
    std::string name;
    std::vector<PexStructMember> members;
};

struct PexObject {
    std::string name;
    std::string parentClassName;
    std::string docString;
    bool isConst{false};
    std::uint32_t userFlags{0};
    std::string autoStateName;

    bool sizeIncludesItself{true};

    std::vector<PexStruct> structs;
    std::vector<PexVariable> variables;
    std::vector<PexProperty> properties;
    std::vector<PexState> states;
};

struct PexUserFlag {
    std::string name;
    std::uint8_t index{0};
};

struct PexDebugFunction {
    std::string objectName;
    std::string stateName;
    std::string functionName;
    std::uint8_t functionType{0};
    std::vector<std::uint16_t> lineNumbers;
};

struct PexPropertyGroup {
    std::string objectName;
    std::string groupName;
    std::string docString;
    std::uint32_t userFlags{0};
    std::vector<std::string> propertyNames;
};

struct PexStructOrder {
    std::string objectName;
    std::string orderName;
    std::vector<std::string> memberNames;
};

struct PexFile {
    std::vector<std::uint8_t> trailingBytes;

    std::uint8_t majorVersion{0};
    std::uint8_t minorVersion{0};
    std::uint16_t gameId{0};
    std::int64_t compilationTime{0};
    std::string sourceFileName;
    std::string userName;
    std::string computerName;
    std::vector<std::string> stringTable;
    bool hasDebugInfo{false};
    std::int64_t modificationTime{0};
    std::vector<PexDebugFunction> debugFunctions;
    std::vector<PexPropertyGroup> propertyGroups;
    std::vector<PexStructOrder> structOrders;
    std::vector<PexUserFlag> userFlags;
    std::vector<PexObject> objects;

    struct OpMeta {
        const char* name;
        int args;
        bool varargs;
    };
    static constexpr OpMeta kOpCodes[] = {
        {"nop", 0, false},          {"iadd", 3, false},          {"fadd", 3, false},
        {"isub", 3, false},         {"fsub", 3, false},          {"imul", 3, false},
        {"fmul", 3, false},         {"idiv", 3, false},          {"fdiv", 3, false},
        {"imod", 3, false},         {"not", 2, false},           {"ineg", 2, false},
        {"fneg", 2, false},         {"assign", 2, false},        {"cast", 2, false},
        {"cmp_eq", 3, false},       {"cmp_lt", 3, false},        {"cmp_lte", 3, false},
        {"cmp_gt", 3, false},       {"cmp_gte", 3, false},       {"jmp", 1, false},
        {"jmpt", 2, false},         {"jmpf", 2, false},          {"callmethod", 3, true},
        {"callparent", 2, true},    {"callstatic", 3, true},     {"return", 1, false},
        {"strcat", 3, false},       {"propget", 3, false},       {"propset", 3, false},
        {"array_create", 2, false}, {"array_length", 2, false},  {"array_getelement", 3, false},
        {"array_setelement", 3, false}, {"array_findelement", 4, false},
        {"array_rfindelement", 4, false}, {"is", 3, false},      {"struct_create", 1, false},
        {"struct_get", 3, false},   {"struct_set", 3, false},    {"array_findstruct", 5, false},
        {"array_rfindstruct", 5, false}, {"array_add", 3, false}, {"array_insert", 3, false},
        {"array_removelast", 1, false}, {"array_remove", 3, false}, {"array_clear", 1, false},
    };

    static PexFile fromBytes(const std::vector<std::uint8_t>& bytes);

    std::vector<std::uint8_t> toBytes() const;

    void rebuildStringTable();
};

}
