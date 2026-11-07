#include "fo4/papyrus/interpreter.hpp"

#include "fo4/papyrus/array_ops.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr int kMaxInheritanceHops = 64;

bool isNoneIdentifier(const pex::PexValue& operand) {
    return operand.type == pex::PexValueType::None ||
           (operand.type == pex::PexValueType::Identifier && sameName(operand.str, "::NoneVar"));
}

}

Status Interpreter::execute(const pex::PexInstruction& instruction, Frame& frame,
                            std::size_t& next) {
    const std::size_t opcodeCount =
        sizeof(pex::PexFile::kOpCodes) / sizeof(pex::PexFile::kOpCodes[0]);
    if (static_cast<std::size_t>(instruction.opCode) >= opcodeCount) {
        return Status::UnknownOpcode;
    }
    const pex::PexFile::OpMeta& meta = pex::PexFile::kOpCodes[instruction.opCode];
    if (!instruction.mnemonic.empty() && instruction.mnemonic != meta.name) {
        return Status::UnknownOpcode;
    }
    if (instruction.args.size() < static_cast<std::size_t>(meta.args)) {
        return Status::StackUnderflow;
    }
    const char* op = meta.name;

    auto jumpTo = [&frame, &next](std::int32_t offset) -> Status {
        const std::int64_t target =
            static_cast<std::int64_t>(frame.ip) + static_cast<std::int64_t>(offset);
        if (target < 0 || target > static_cast<std::int64_t>(frame.function->instructions.size())) {
            return Status::BadJumpTarget;
        }
        next = static_cast<std::size_t>(target);
        return Status::Ok;
    };

    auto findProperty = [this](const pex::PexObject* start, const std::string& name,
                               const pex::PexObject*& owner) -> const pex::PexProperty* {
        const pex::PexObject* current = start;
        for (int hop = 0; current != nullptr && hop < kMaxInheritanceHops; ++hop) {
            for (const pex::PexProperty& property : current->properties) {
                if (sameName(property.name, name)) {
                    owner = current;
                    return &property;
                }
            }
            if (current->parentClassName.empty()) return nullptr;
            current = host_.findObject(current->parentClassName);
        }
        return nullptr;
    };

    if (std::strcmp(op, "nop") == 0) return Status::Ok;

    if (std::strcmp(op, "jmp") == 0) {
        std::int32_t offset = 0;
        const Status status = readIntegerOperand(frame, instruction.args[0], offset);
        if (status != Status::Ok) return status;
        return jumpTo(offset);
    }
    if (std::strcmp(op, "jmpt") == 0 || std::strcmp(op, "jmpf") == 0) {
        const bool jumpOnTrue = std::strcmp(op, "jmpt") == 0;
        bool condition = false;
        Status status = readBoolOperand(frame, instruction.args[0], condition);
        if (status != Status::Ok) return status;
        if (condition != jumpOnTrue) return Status::Ok;
        std::int32_t offset = 0;
        status = readIntegerOperand(frame, instruction.args[1], offset);
        if (status != Status::Ok) return status;
        return jumpTo(offset);
    }

    if (std::strcmp(op, "return") == 0) {
        Value value;
        if (!isNoneIdentifier(instruction.args[0])) {
            const Status readStatus = readOperand(frame, instruction.args[0], value);
            if (readStatus != Status::Ok) return readStatus;
        }
        Value coerced;
        const Status coerceStatus = coerce(value, frame.function->returnType, coerced);
        if (coerceStatus != Status::Ok) return coerceStatus;
        returnValue_ = coerced;
        hasReturn_ = true;
        return Status::Ok;
    }

    if (std::strcmp(op, "callmethod") == 0 || std::strcmp(op, "callstatic") == 0 ||
        std::strcmp(op, "callparent") == 0) {
        std::vector<Value> callArgs;
        const std::size_t firstArgument =
            std::strcmp(op, "callparent") == 0 ? std::size_t{2} : std::size_t{3};
        for (std::size_t i = firstArgument; i < instruction.args.size(); ++i) {
            Value argument;
            const Status status = readOperand(frame, instruction.args[i], argument);
            if (status != Status::Ok) return status;
            callArgs.push_back(std::move(argument));
        }

        if (std::strcmp(op, "callmethod") == 0) {
            Value receiver;
            const Status status = readOperand(frame, instruction.args[1], receiver);
            if (status != Status::Ok) return status;
            if (receiver.type != ValueType::Object || receiver.object == nullptr) {
                return Status::NullObject;
            }
            const pex::PexObject* script = host_.findObject(receiver.object->type);
            return invoke(frame, receiver, script, receiver.object->state, receiver.object->type,
                          instruction.args[0].str, instruction.args[2], callArgs);
        }
        if (std::strcmp(op, "callstatic") == 0) {
            const std::string scriptName = instruction.args[0].str;
            const pex::PexObject* script = host_.findObject(scriptName);
            const std::string state = script == nullptr ? std::string() : script->autoStateName;
            const Value self =
                script == nullptr ? noneValue() : objectValue(0, script->name, state);
            return invoke(frame, self, script, state, scriptName, instruction.args[1].str,
                          instruction.args[2], callArgs);
        }
        const pex::PexObject* parent = nullptr;
        std::string parentName;
        if (frame.object != nullptr) {
            parentName = frame.object->parentClassName;
            if (!parentName.empty()) parent = host_.findObject(parentName);
        }
        return invoke(frame, frame.self, parent, frame.state, parentName, instruction.args[0].str,
                      instruction.args[1], callArgs);
    }

    if (std::strcmp(op, "propget") == 0 || std::strcmp(op, "propset") == 0) {
        const bool isGetter = std::strcmp(op, "propget") == 0;
        Value receiver;
        Status status = readOperand(frame, instruction.args[1], receiver);
        if (status != Status::Ok) return status;
        if (receiver.type != ValueType::Object || receiver.object == nullptr) {
            return Status::NullObject;
        }
        const pex::PexObject* script = host_.findObject(receiver.object->type);
        const pex::PexObject* owner = nullptr;
        const pex::PexProperty* property = findProperty(script, instruction.args[0].str, owner);

        Value operand;
        if (!isGetter) {
            status = readOperand(frame, instruction.args[2], operand);
            if (status != Status::Ok) return status;
        }

        if (property != nullptr && property->isAutoVar()) {
            Status autoStatus = Status::Ok;
            if (isGetter) {
                Value stored;
                if (host_.getVariable(*receiver.object, property->autoVarName, stored, autoStatus)) {
                    return storeResult(frame, instruction.args[2], stored);
                }
            } else if (host_.setVariable(*receiver.object, property->autoVarName, operand,
                                         autoStatus)) {
                return Status::Ok;
            }
        }
        if (property != nullptr) {
            if (isGetter && property->readHandler.has_value() && !property->readHandler->isNative) {
                return pushFrame(owner, *property->readHandler, receiver.object->state, receiver,
                                 instruction.args[2], {});
            }
            if (!isGetter && property->writeHandler.has_value() &&
                !property->writeHandler->isNative) {
                return pushFrame(owner, *property->writeHandler, receiver.object->state, receiver,
                                 pex::PexValue{}, {operand});
            }
        }

        Status hostStatus = Status::Ok;
        if (isGetter) {
            Value out;
            if (host_.getProperty(*receiver.object, instruction.args[0].str, out, hostStatus)) {
                return storeResult(frame, instruction.args[2], out);
            }
        } else if (host_.setProperty(*receiver.object, instruction.args[0].str, operand, hostStatus)) {
            return Status::Ok;
        }
        return hostStatus == Status::Ok ? Status::UnknownIdentifier : hostStatus;
    }

    if (std::strcmp(op, "array_setelement") == 0 || std::strcmp(op, "array_add") == 0 ||
        std::strcmp(op, "array_insert") == 0 || std::strcmp(op, "array_remove") == 0) {
        Value array;
        Value first;
        Value second;
        const Status status = readPair(frame, instruction.args[1], instruction.args[2], first, second);
        if (status != Status::Ok) return status;
        const Status arrayStatus = readOperand(frame, instruction.args[0], array);
        if (arrayStatus != Status::Ok) return arrayStatus;

        if (std::strcmp(op, "array_setelement") == 0) return arraySetElement(array, first, second);
        if (std::strcmp(op, "array_add") == 0) {
            return arrayAdd(array, first, second, limits_.maxArrayLength);
        }
        if (std::strcmp(op, "array_insert") == 0) {
            return arrayInsert(array, first, second, limits_.maxArrayLength);
        }
        return arrayRemove(array, first, second);
    }
    if (std::strcmp(op, "array_removelast") == 0) {
        Value array;
        const Status status = readOperand(frame, instruction.args[0], array);
        if (status != Status::Ok) return status;
        return arrayRemoveLast(array);
    }
    if (std::strcmp(op, "array_clear") == 0) {
        Value array;
        const Status status = readOperand(frame, instruction.args[0], array);
        if (status != Status::Ok) return status;
        return arrayClear(array);
    }

    if (std::strcmp(op, "array_findelement") == 0 || std::strcmp(op, "array_rfindelement") == 0) {
        const bool reverse = std::strcmp(op, "array_rfindelement") == 0;
        Value array;
        Value wanted;
        Value start;
        Status status = readOperand(frame, instruction.args[0], array);
        if (status != Status::Ok) return status;
        status = readOperand(frame, instruction.args[2], wanted);
        if (status != Status::Ok) return status;
        status = readOperand(frame, instruction.args[3], start);
        if (status != Status::Ok) return status;
        Value found;
        status = arrayFindElement(array, wanted, start, reverse, found);
        if (status != Status::Ok) return status;
        return storeResult(frame, instruction.args[1], found);
    }

    if (std::strcmp(op, "struct_create") == 0) {
        const std::string declared = declaredOperandType(frame, instruction.args[0]);
        Value created;
        const Status status = makeStruct(frame.object, declared, created);
        if (status != Status::Ok) return status;
        return storeResult(frame, instruction.args[0], created);
    }
    if (std::strcmp(op, "struct_get") == 0) {
        if (instruction.args[2].type != pex::PexValueType::String) return Status::TypeMismatch;
        return structGet(frame, instruction.args[0], instruction.args[1], instruction.args[2].str);
    }
    if (std::strcmp(op, "struct_set") == 0) {
        if (instruction.args[1].type != pex::PexValueType::String) return Status::TypeMismatch;
        return structSet(frame, instruction.args[0], instruction.args[1].str, instruction.args[2]);
    }
    if (std::strcmp(op, "array_findstruct") == 0 ||
        std::strcmp(op, "array_rfindstruct") == 0) {
        return findStructInArray(frame, instruction, std::strcmp(op, "array_rfindstruct") == 0);
    }

    if (std::strcmp(op, "is") == 0) {
        Value subject;
        const Status status = readOperand(frame, instruction.args[1], subject);
        if (status != Status::Ok) return status;
        if (subject.type != ValueType::Object && !isNoneLike(subject)) return Status::TypeMismatch;
        const std::string target = instruction.args[2].str;
        bool result = false;
        if (subject.type == ValueType::Object && subject.object != nullptr) {
            result = sameName(target, "ScriptObject") || isObjectOfType(subject, target);
        }
        return storeResult(frame, instruction.args[0], boolValue(result));
    }

    if (std::strcmp(op, "not") == 0 || std::strcmp(op, "ineg") == 0 ||
        std::strcmp(op, "fneg") == 0 || std::strcmp(op, "assign") == 0 ||
        std::strcmp(op, "cast") == 0 || std::strcmp(op, "array_create") == 0 ||
        std::strcmp(op, "array_length") == 0) {
        Value single;
        const Status status = readOperand(frame, instruction.args[1], single);
        if (status != Status::Ok) return status;

        if (std::strcmp(op, "not") == 0) {
            bool flag = false;
            const Status boolStatus = toBool(single, flag);
            if (boolStatus != Status::Ok) return boolStatus;
            return storeResult(frame, instruction.args[0], boolValue(!flag));
        }
        if (std::strcmp(op, "ineg") == 0) {
            if (single.type != ValueType::Integer) return Status::TypeMismatch;
            return storeResult(frame, instruction.args[0],
                               integerValue(static_cast<std::int32_t>(
                                   0u - static_cast<std::uint32_t>(single.integer))));
        }
        if (std::strcmp(op, "fneg") == 0) {
            if (single.type != ValueType::Float) return Status::TypeMismatch;
            return storeResult(frame, instruction.args[0], floatValue(-single.real));
        }
        if (std::strcmp(op, "assign") == 0) {
            return storeResult(frame, instruction.args[0], single);
        }
        if (std::strcmp(op, "cast") == 0) {
            Value converted;
            const Status castStatus =
                castTo(single, declaredOperandType(frame, instruction.args[1]),
                       declaredOperandType(frame, instruction.args[0]), converted);
            if (castStatus != Status::Ok) return castStatus;
            return storeResult(frame, instruction.args[0], converted);
        }
        if (std::strcmp(op, "array_create") == 0) {
            if (single.type != ValueType::Integer) return Status::TypeMismatch;
            const std::string declared = declaredOperandType(frame, instruction.args[0]);
            const std::string elementName = arrayElementTypeName(declared);
            Value created;
            const Status createStatus =
                arrayCreate(single, declaredValueType(elementName), elementName,
                            limits_.maxArrayLength, created);
            if (createStatus != Status::Ok) return createStatus;
            return storeResult(frame, instruction.args[0], created);
        }
        Value length;
        const Status lengthStatus = arrayLength(single, length);
        if (lengthStatus != Status::Ok) return lengthStatus;
        return storeResult(frame, instruction.args[0], length);
    }

    Value left;
    Value right;
    const Status pairStatus = readPair(frame, instruction.args[1], instruction.args[2], left, right);
    if (pairStatus != Status::Ok) return pairStatus;

    if (std::strcmp(op, "iadd") == 0 || std::strcmp(op, "isub") == 0 ||
        std::strcmp(op, "imul") == 0) {
        if (left.type != ValueType::Integer || right.type != ValueType::Integer) {
            return Status::TypeMismatch;
        }
        std::int32_t computed = 0;
        if (std::strcmp(op, "iadd") == 0) {
            computed = static_cast<std::int32_t>(static_cast<std::uint32_t>(left.integer) +
                                                 static_cast<std::uint32_t>(right.integer));
        } else if (std::strcmp(op, "isub") == 0) {
            computed = static_cast<std::int32_t>(static_cast<std::uint32_t>(left.integer) -
                                                 static_cast<std::uint32_t>(right.integer));
        } else {
            computed = static_cast<std::int32_t>(static_cast<std::uint32_t>(left.integer) *
                                                 static_cast<std::uint32_t>(right.integer));
        }
        return storeResult(frame, instruction.args[0], integerValue(computed));
    }

    if (std::strcmp(op, "idiv") == 0 || std::strcmp(op, "imod") == 0) {
        if (left.type != ValueType::Integer || right.type != ValueType::Integer) {
            return Status::TypeMismatch;
        }
        if (right.integer == 0) return Status::DivisionByZero;
        const bool divide = std::strcmp(op, "idiv") == 0;
        std::int32_t computed = 0;
        if (left.integer == std::numeric_limits<std::int32_t>::min() && right.integer == -1) {
            computed = divide ? std::numeric_limits<std::int32_t>::min() : 0;
        } else {
            computed = divide ? left.integer / right.integer : left.integer % right.integer;
        }
        return storeResult(frame, instruction.args[0], integerValue(computed));
    }

    if (std::strcmp(op, "fadd") == 0 || std::strcmp(op, "fsub") == 0 ||
        std::strcmp(op, "fmul") == 0 || std::strcmp(op, "fdiv") == 0) {
        if (left.type != ValueType::Float || right.type != ValueType::Float) {
            return Status::TypeMismatch;
        }
        if (std::strcmp(op, "fdiv") == 0) {
            if (right.real == 0.0F) return Status::DivisionByZero;
            return storeResult(frame, instruction.args[0], floatValue(left.real / right.real));
        }
        if (std::strcmp(op, "fadd") == 0) {
            return storeResult(frame, instruction.args[0], floatValue(left.real + right.real));
        }
        if (std::strcmp(op, "fsub") == 0) {
            return storeResult(frame, instruction.args[0], floatValue(left.real - right.real));
        }
        return storeResult(frame, instruction.args[0], floatValue(left.real * right.real));
    }

    if (std::strcmp(op, "strcat") == 0) {
        Value leftText;
        Value rightText;
        if (!castScalar(left, ValueType::String, leftText) ||
            !castScalar(right, ValueType::String, rightText)) {
            return Status::TypeMismatch;
        }
        if (leftText.str.size() + rightText.str.size() > limits_.maxStringLength) {
            return Status::StringTooLong;
        }
        return storeResult(frame, instruction.args[0],
                           stringValue(leftText.str + rightText.str));
    }

    if (std::strcmp(op, "cmp_eq") == 0) {
        return storeResult(frame, instruction.args[0], boolValue(equalValues(left, right)));
    }
    if (std::strcmp(op, "cmp_lt") == 0 || std::strcmp(op, "cmp_lte") == 0 ||
        std::strcmp(op, "cmp_gt") == 0 || std::strcmp(op, "cmp_gte") == 0) {
        int order = 0;
        if (!orderValues(left, right, order)) return Status::TypeMismatch;
        bool result = false;
        if (std::strcmp(op, "cmp_lt") == 0) result = order < 0;
        if (std::strcmp(op, "cmp_lte") == 0) result = order <= 0;
        if (std::strcmp(op, "cmp_gt") == 0) result = order > 0;
        if (std::strcmp(op, "cmp_gte") == 0) result = order >= 0;
        return storeResult(frame, instruction.args[0], boolValue(result));
    }

    if (std::strcmp(op, "array_getelement") == 0) {
        Value element;
        const Status status = arrayGetElement(left, right, element);
        if (status != Status::Ok) return status;
        return storeResult(frame, instruction.args[0], element);
    }

    return Status::UnknownOpcode;
}

}
