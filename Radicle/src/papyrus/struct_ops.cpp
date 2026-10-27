#include "fo4/papyrus/interpreter.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr int kMaxInheritanceHops = 64;

}  // namespace

const pex::PexStruct* Interpreter::findStruct(const pex::PexObject* object,
                                              const std::string& name) const {
    const pex::PexObject* current = object;
    for (int hop = 0; current != nullptr && hop < kMaxInheritanceHops; ++hop) {
        for (const pex::PexStruct& structure : current->structs) {
            if (sameName(structure.name, name)) return &structure;
        }
        if (current->parentClassName.empty()) return nullptr;
        current = host_.findObject(current->parentClassName);
    }
    return nullptr;
}

Status Interpreter::makeStruct(const pex::PexObject* object, const std::string& typeName,
                               Value& out) const {
    const pex::PexStruct* definition = findStruct(object, typeName);
    if (definition == nullptr) return Status::UnknownIdentifier;
    auto structure = std::make_shared<Struct>();
    structure->typeName = definition->name;
    for (const pex::PexStructMember& member : definition->members) {
        Value memberValue = defaultValueFor(member.type);
        if (member.defaultValue.has_value() &&
            member.defaultValue->type != pex::PexValueType::Identifier) {
            Value literal;
            const pex::PexValue& source = *member.defaultValue;
            switch (source.type) {
                case pex::PexValueType::None:
                    literal = noneValue();
                    break;
                case pex::PexValueType::String:
                    literal = stringValue(source.str);
                    break;
                case pex::PexValueType::Integer:
                    literal = integerValue(source.int_);
                    break;
                case pex::PexValueType::Float:
                    literal = floatValue(source.float_);
                    break;
                case pex::PexValueType::Bool:
                    literal = boolValue(source.bool_);
                    break;
                case pex::PexValueType::Identifier:
                    literal = noneValue();
                    break;
            }
            if (!isNoneLike(literal)) {
                const Status coerced = coerce(literal, member.type, memberValue);
                if (coerced != Status::Ok) return coerced;
            }
        }
        structure->members.emplace(lowerAscii(member.name), std::move(memberValue));
    }
    out = structValue(std::move(structure));
    return Status::Ok;
}

Status Interpreter::structGet(Frame& frame, const pex::PexValue& destination,
                              const pex::PexValue& structureOperand, const std::string& member) {
    Value structure;
    Status status = readOperand(frame, structureOperand, structure);
    if (status != Status::Ok) return status;
    if (isNoneLike(structure)) return Status::NullObject;
    if (structure.type != ValueType::Struct) return Status::TypeMismatch;
    const auto found = structure.structure->members.find(lowerAscii(member));
    if (found == structure.structure->members.end()) return Status::UnknownIdentifier;
    return storeResult(frame, destination, found->second);
}

Status Interpreter::structSet(Frame& frame, const pex::PexValue& structureOperand,
                              const std::string& member, const pex::PexValue& valueOperand) {
    Value structure;
    Status status = readOperand(frame, structureOperand, structure);
    if (status != Status::Ok) return status;
    if (isNoneLike(structure)) return Status::NullObject;
    if (structure.type != ValueType::Struct) return Status::TypeMismatch;
    const pex::PexStruct* definition = findStruct(frame.object, structure.structure->typeName);
    if (definition == nullptr) return Status::UnknownIdentifier;
    const pex::PexStructMember* memberDefinition = nullptr;
    for (const pex::PexStructMember& candidate : definition->members) {
        if (sameName(candidate.name, member)) {
            memberDefinition = &candidate;
            break;
        }
    }
    if (memberDefinition == nullptr) return Status::UnknownIdentifier;
    Value value;
    status = readOperand(frame, valueOperand, value);
    if (status != Status::Ok) return status;
    Value coerced;
    status = coerce(value, memberDefinition->type, coerced);
    if (status != Status::Ok) return status;
    structure.structure->members[lowerAscii(memberDefinition->name)] = std::move(coerced);
    return Status::Ok;
}

Status Interpreter::findStructInArray(Frame& frame, const pex::PexInstruction& instruction,
                                      bool reverse) {
    Value array;
    Status status = readOperand(frame, instruction.args[0], array);
    if (status != Status::Ok) return status;
    if (instruction.args[2].type != pex::PexValueType::String) return Status::TypeMismatch;
    const std::string member = lowerAscii(instruction.args[2].str);
    Value wanted;
    status = readOperand(frame, instruction.args[3], wanted);
    if (status != Status::Ok) return status;
    Value start;
    status = readOperand(frame, instruction.args[4], start);
    if (status != Status::Ok) return status;
    if (isNoneLike(array)) return storeResult(frame, instruction.args[1], integerValue(-1));
    if (array.type != ValueType::Array) return Status::TypeMismatch;

    const std::int32_t size = static_cast<std::int32_t>(array.array->elements.size());
    if (size == 0) return storeResult(frame, instruction.args[1], integerValue(-1));
    const pex::PexStruct* definition = findStruct(frame.object, array.array->elementTypeName);
    if (definition == nullptr) return Status::TypeMismatch;
    std::int32_t position = reverse ? size - 1 : 0;
    if (!isNoneLike(start)) {
        if (start.type != ValueType::Integer) return Status::TypeMismatch;
        position = start.integer;
        if (reverse && position == -1) position = size - 1;
    }
    const pex::PexStructMember* declared = nullptr;
    for (const pex::PexStructMember& candidate : definition->members) {
        if (sameName(candidate.name, instruction.args[2].str)) {
            declared = &candidate;
            break;
        }
    }
    const std::int32_t step = reverse ? -1 : 1;
    for (std::int32_t i = position; i >= 0 && i < size; i += step) {
        const Value& element = array.array->elements[static_cast<std::size_t>(i)];
        if (isNoneLike(element)) {
            if (isNoneLike(wanted)) return storeResult(frame, instruction.args[1], integerValue(i));
            continue;
        }
        if (element.type != ValueType::Struct) return Status::TypeMismatch;
        if (declared == nullptr) return Status::UnknownIdentifier;
        const auto found = element.structure->members.find(member);
        if (found == element.structure->members.end()) return Status::UnknownIdentifier;
        const ValueType expected = findStruct(frame.object, declared->type) != nullptr
                                       ? ValueType::Struct
                                       : declaredValueType(declared->type);
        if (!isNoneLike(wanted) && expected != ValueType::None && expected != wanted.type) {
            return Status::TypeMismatch;
        }
        if (equalValues(found->second, wanted)) {
            return storeResult(frame, instruction.args[1], integerValue(i));
        }
    }
    return storeResult(frame, instruction.args[1], integerValue(-1));
}


}  // namespace fo4::papyrus
