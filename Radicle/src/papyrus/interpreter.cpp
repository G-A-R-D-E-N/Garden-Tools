#include "fo4/papyrus/interpreter.hpp"

#include "fo4/papyrus/array_ops.hpp"
#include "fo4/papyrus/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {
namespace {

constexpr int kMaxInheritanceHops = 64;

const pex::PexFunction* findInState(const pex::PexState& state, const std::string& name) {
    for (const pex::PexFunction& function : state.functions) {
        if (sameName(function.name, name)) return &function;
    }
    return nullptr;
}

const pex::PexFunction* findInStates(const pex::PexObject& object, const std::string& state,
                                    const std::string& name) {
    if (!state.empty()) {
        for (const pex::PexState& candidate : object.states) {
            if (!sameName(candidate.name, state)) continue;
            const pex::PexFunction* found = findInState(candidate, name);
            if (found != nullptr) return found;
        }
    }
    for (const pex::PexState& candidate : object.states) {
        if (!candidate.name.empty()) continue;
        const pex::PexFunction* found = findInState(candidate, name);
        if (found != nullptr) return found;
    }
    return nullptr;
}

std::string literalTypeName(const pex::PexValue& operand) {
    switch (operand.type) {
        case pex::PexValueType::String:
            return "String";
        case pex::PexValueType::Integer:
            return "Int";
        case pex::PexValueType::Float:
            return "Float";
        case pex::PexValueType::Bool:
            return "Bool";
        case pex::PexValueType::Identifier:
        case pex::PexValueType::None:
            return "None";
    }
    return "None";
}

const ObjectRef* objectOf(const Value& value) {
    return value.type == ValueType::Object && value.object != nullptr ? value.object.get() : nullptr;
}

Result hostFailure() {
    Result failed;
    failed.status = Status::HostError;
    return failed;
}

}

Interpreter::Interpreter(Host& host, Limits limits) : host_(host), limits_(limits) {}

Result Interpreter::runFunction(const std::string& scriptName, const std::string& functionName,
                                const std::vector<Value>& args) {
    try {
        Result result;
        const pex::PexObject* object = host_.findObject(scriptName);
        if (object == nullptr) {
            result.status = Status::FunctionNotFound;
            return result;
        }
        const std::string state = object->autoStateName;
        const pex::PexObject* owner = nullptr;
        const pex::PexFunction* function = findFunction(*object, state, functionName, &owner);
        if (function == nullptr) {
            result.status = Status::FunctionNotFound;
            return result;
        }
        const Value self = objectValue(0, object->name, state);
        return run(owner, *function, state, self, args);
    } catch (...) {
        return hostFailure();
    }
}

Result Interpreter::runFunction(const Value& self, const std::string& functionName,
                                const std::vector<Value>& args) {
    try {
        Result result;
        if (self.type != ValueType::Object || self.object == nullptr) {
            result.status = Status::NullObject;
            return result;
        }
        const pex::PexObject* object = host_.findObject(self.object->type);
        if (object == nullptr) {
            result.status = Status::FunctionNotFound;
            return result;
        }
        const std::string state = self.object->state;
        const pex::PexObject* owner = nullptr;
        const pex::PexFunction* function = findFunction(*object, state, functionName, &owner);
        if (function == nullptr) {
            result.status = Status::FunctionNotFound;
            return result;
        }
        return run(owner, *function, state, self, args);
    } catch (...) {
        return hostFailure();
    }
}

Result Interpreter::runObjectFunction(const pex::PexObject& object, const pex::PexFunction& function,
                                      const Value& self, const std::vector<Value>& args) {
    try {
        std::string state = object.autoStateName;
        if (self.type == ValueType::Object && self.object != nullptr &&
            !self.object->state.empty()) {
            state = self.object->state;
        }
        return run(&object, function, state, self, args);
    } catch (...) {
        return hostFailure();
    }
}

// Frames hold pointers into the host-owned pex objects, so those must outlive the Run.
Result Interpreter::run(const pex::PexObject* object, const pex::PexFunction& function,
                        const std::string& state, const Value& self,
                        const std::vector<Value>& args) {
    instructions_ = 0;
    maxDepth_ = 0;
    firstError_ = Status::Ok;
    hasReturn_ = false;
    returnValue_ = Value{};
    result_ = Value{};
    frames_.clear();
    paused_ = false;
    latent_ = false;
    latentToken_ = 0;
    latentDestination_ = pex::PexValue{};
    sliceInstructions_ = 0;

    if (function.isNative) {
        Result nativeResult;
        if (object == nullptr) {
            nativeResult.status = Status::FunctionNotFound;
            return nativeResult;
        }
        NativeResult native;
        if (!host_.callNativeEx(NativeCall{self, object->name, function.name, args, false}, native)) {
            nativeResult.status = native.status == Status::Ok ? Status::FunctionNotFound : native.status;
            return nativeResult;
        }
        if (native.latentToken != 0) {
            nativeResult.status = Status::HostError;
            return nativeResult;
        }
        const Status coerced = coerce(native.value, function.returnType, nativeResult.value);
        if (coerced != Status::Ok) nativeResult.status = coerced;
        return nativeResult;
    }

    const Status pushed = pushFrame(object, function, state, self, pex::PexValue{}, args);
    if (pushed != Status::Ok) {
        Result failed;
        failed.status = pushed;
        return failed;
    }
    return finish(runLoop(limits_.maxInstructions));
}

Status Interpreter::pushFrame(const pex::PexObject* object, const pex::PexFunction& function,
                              const std::string& state, const Value& self,
                              const pex::PexValue& destination, const std::vector<Value>& args) {
    if (frames_.size() >= limits_.maxCallDepth) return Status::CallDepthExceeded;
    if (args.size() != function.params.size()) return Status::ArgumentCountMismatch;

    Frame frame;
    frame.object = object;
    frame.function = &function;
    frame.state = state;
    frame.self = self;
    frame.destination = destination;
    frame.ip = 0;

    for (std::size_t i = 0; i < function.params.size(); ++i) {
        Slot slot;
        slot.name = function.params[i].name;
        slot.declaredType = function.params[i].type;
        const Status status = coerce(args[i], slot.declaredType, slot.value);
        if (status != Status::Ok) return status;
        frame.slots.push_back(std::move(slot));
    }
    for (const pex::PexTypedName& local : function.locals) {
        Slot slot;
        slot.name = local.name;
        slot.declaredType = local.type;
        slot.value = defaultValueFor(local.type);
        frame.slots.push_back(std::move(slot));
    }

    frames_.push_back(std::move(frame));
    if (frames_.size() > maxDepth_) maxDepth_ = frames_.size();
    return Status::Ok;
}

void Interpreter::popFrame(const Value& value) {
    const pex::PexValue destination = frames_.back().destination;
    frames_.pop_back();
    if (frames_.empty()) {
        result_ = value;
        return;
    }
    if (destination.type != pex::PexValueType::Identifier) return;
    const Status status = writeOperand(frames_.back(), destination, value);
    if (status != Status::Ok) record(status);
}

Status Interpreter::invoke(Frame& frame, const Value& self, const pex::PexObject* script,
                           const std::string& state, const std::string& nativeScript,
                           const std::string& functionName, const pex::PexValue& destination,
                           const std::vector<Value>& args) {
    const pex::PexObject* owner = nullptr;
    const pex::PexFunction* function =
        script == nullptr ? nullptr : findFunction(*script, state, functionName, &owner);
    if (function != nullptr && !function->isNative) {
        return pushFrame(owner, *function, state, self, destination, args);
    }
    // Invariant: GoToState changes the receiver's state and returns None.
    if (sameName(functionName, "GotoState")) {
        return gotoState(frame, self, args, destination);
    }

    std::string target = nativeScript;
    if (target.empty() && script != nullptr) target = script->name;

    NativeResult native;
    if (host_.callNativeEx(NativeCall{self, target, functionName, args, true}, native)) {
        if (native.latentToken != 0) return suspendLatent(destination, native.latentToken, functionName);
        return storeResult(frame, destination, native.value);
    }
    if (function != nullptr) return Status::FunctionNotFound;
    return native.status == Status::Ok ? Status::FunctionNotFound : native.status;
}

Status Interpreter::gotoState(Frame& frame, const Value& receiver, const std::vector<Value>& args,
                              const pex::PexValue& destination) {
    if (!host_.gate("vm.states.gotostate")) return Status::GateRefused;
    if (args.size() != 1) return Status::ArgumentCountMismatch;
    if (args[0].type != ValueType::String) return Status::TypeMismatch;
    if (receiver.type != ValueType::Object || receiver.object == nullptr) {
        return Status::NullObject;
    }
    Status status = Status::Ok;
    if (!host_.setState(*receiver.object, args[0].str, status)) {
        return status == Status::Ok ? Status::HostError : status;
    }
    return storeResult(frame, destination, noneValue());
}

const pex::PexFunction* Interpreter::findFunction(const pex::PexObject& object,
                                                  const std::string& state,
                                                  const std::string& name,
                                                  const pex::PexObject** owner) const {
    const pex::PexObject* current = &object;
    for (int hop = 0; current != nullptr && hop < kMaxInheritanceHops; ++hop) {
        const pex::PexFunction* found = findInStates(*current, state, name);
        if (found != nullptr) {
            if (owner != nullptr) *owner = current;
            return found;
        }
        if (current->parentClassName.empty()) return nullptr;
        current = host_.findObject(current->parentClassName);
    }
    return nullptr;
}

Interpreter::Slot* Interpreter::findSlot(Frame& frame, const std::string& name) {
    for (Slot& slot : frame.slots) {
        if (sameName(slot.name, name)) return &slot;
    }
    return nullptr;
}

std::string Interpreter::declaredVariableType(const Frame& frame, const std::string& name) const {
    for (const Slot& slot : frame.slots) {
        if (sameName(slot.name, name)) return slot.declaredType;
    }
    if (frame.object != nullptr) {
        for (const pex::PexVariable& variable : frame.object->variables) {
            if (sameName(variable.name, name)) return variable.type;
        }
    }
    return std::string();
}

std::string Interpreter::declaredOperandType(const Frame& frame, const pex::PexValue& operand) const {
    if (operand.type != pex::PexValueType::Identifier) return literalTypeName(operand);
    const std::string declared = declaredVariableType(frame, operand.str);
    if (!declared.empty()) return declared;
    return std::string("Var");
}

Status Interpreter::readOperand(Frame& frame, const pex::PexValue& operand, Value& out) {
    switch (operand.type) {
        case pex::PexValueType::None:
            out = noneValue();
            return Status::Ok;
        case pex::PexValueType::String:
            out = stringValue(operand.str);
            return Status::Ok;
        case pex::PexValueType::Integer:
            out = integerValue(operand.int_);
            return Status::Ok;
        case pex::PexValueType::Float:
            out = floatValue(operand.float_);
            return Status::Ok;
        case pex::PexValueType::Bool:
            out = boolValue(operand.bool_);
            return Status::Ok;
        case pex::PexValueType::Identifier:
            return resolveIdentifier(frame, operand.str, out);
    }
    return Status::InternalError;
}

Status Interpreter::readPair(Frame& frame, const pex::PexValue& left, const pex::PexValue& right,
                             Value& leftValue, Value& rightValue) {
    const Status status = readOperand(frame, left, leftValue);
    if (status != Status::Ok) return status;
    return readOperand(frame, right, rightValue);
}

Status Interpreter::readIntegerOperand(Frame& frame, const pex::PexValue& operand,
                                       std::int32_t& out) {
    Value value;
    const Status status = readOperand(frame, operand, value);
    if (status != Status::Ok) return status;
    if (value.type != ValueType::Integer) return Status::TypeMismatch;
    out = value.integer;
    return Status::Ok;
}

Status Interpreter::readBoolOperand(Frame& frame, const pex::PexValue& operand, bool& out) {
    Value value;
    const Status status = readOperand(frame, operand, value);
    if (status != Status::Ok) return status;
    return toBool(value, out);
}

Status Interpreter::resolveIdentifier(Frame& frame, const std::string& name, Value& out) {
    if (Slot* slot = findSlot(frame, name)) {
        out = slot->value;
        return Status::Ok;
    }
    if (sameName(name, "::NoneVar")) {
        out = noneValue();
        return Status::Ok;
    }
    if (sameName(name, "self")) {
        out = frame.self;
        return Status::Ok;
    }
    if (frame.object != nullptr) {
        for (const pex::PexVariable& variable : frame.object->variables) {
            if (!sameName(variable.name, name)) continue;
            const ObjectRef* self = objectOf(frame.self);
            if (self == nullptr) return Status::NullObject;
            Status hostStatus = Status::Ok;
            if (host_.getVariable(*self, name, out, hostStatus)) return Status::Ok;
            out = defaultValueFor(variable.type);
            if (!variable.defaultValue.has_value()) return Status::Ok;
            Value literal;
            const Status status = readOperand(frame, *variable.defaultValue, literal);
            if (status != Status::Ok) return status;
            return coerce(literal, variable.type, out);
        }
    }
    if (!name.empty() && name[0] == ':') {
        out = noneValue();
        return Status::Ok;
    }
    const ObjectRef* self = objectOf(frame.self);
    if (self == nullptr) return Status::NullObject;
    Status status = Status::Ok;
    if (host_.getVariable(*self, name, out, status)) return Status::Ok;
    return status == Status::Ok ? Status::UnknownIdentifier : status;
}

Status Interpreter::writeOperand(Frame& frame, const pex::PexValue& operand, const Value& value) {
    if (operand.type != pex::PexValueType::Identifier) return Status::InternalError;
    if (sameName(operand.str, "::NoneVar")) return Status::Ok;
    if (Slot* slot = findSlot(frame, operand.str)) {
        Value coerced;
        const Status status = coerce(value, slot->declaredType, coerced);
        if (status != Status::Ok) return status;
        slot->value = coerced;
        return Status::Ok;
    }
    if (frame.object != nullptr) {
        for (const pex::PexVariable& variable : frame.object->variables) {
            if (!sameName(variable.name, operand.str)) continue;
            Value coerced;
            const Status status = coerce(value, variable.type, coerced);
            if (status != Status::Ok) return status;
            const ObjectRef* self = objectOf(frame.self);
            if (self == nullptr) return Status::NullObject;
            Status hostStatus = Status::Ok;
            if (host_.setVariable(*self, operand.str, coerced, hostStatus)) return Status::Ok;
            return hostStatus == Status::Ok ? Status::UnknownIdentifier : hostStatus;
        }
    }
    if (!operand.str.empty() && operand.str[0] == ':') {
        Slot slot;
        slot.name = operand.str;
        slot.declaredType = "Var";
        slot.value = value;
        frame.slots.push_back(std::move(slot));
        return Status::Ok;
    }
    const ObjectRef* self = objectOf(frame.self);
    if (self == nullptr) return Status::NullObject;
    Status status = Status::Ok;
    if (host_.setVariable(*self, operand.str, value, status)) return Status::Ok;
    return status == Status::Ok ? Status::UnknownIdentifier : status;
}

Status Interpreter::storeResult(Frame& frame, const pex::PexValue& destination,
                                const Value& value) {
    if (destination.type != pex::PexValueType::Identifier) return Status::Ok;
    Value coerced;
    const Status status = coerce(value, declaredOperandType(frame, destination), coerced);
    if (status != Status::Ok) return status;
    return writeOperand(frame, destination, coerced);
}

bool Interpreter::isObjectOfType(const Value& value, const std::string& typeName) const {
    if (value.type != ValueType::Object || value.object == nullptr) return false;
    if (value.object->type.empty() && (value.object->handle & kEngineHandleBit) != 0) return true;
    const pex::PexObject* current = host_.findObject(value.object->type);
    if (current == nullptr) return sameName(value.object->type, typeName);
    for (int hop = 0; current != nullptr && hop < kMaxInheritanceHops; ++hop) {
        if (sameName(current->name, typeName)) return true;
        if (current->parentClassName.empty()) return false;
        current = host_.findObject(current->parentClassName);
    }
    return false;
}

Status Interpreter::coerce(const Value& value, const std::string& declaredType, Value& out) const {
    const ValueType target = declaredValueType(declaredType);
    if (target == ValueType::None || isNoneLike(value)) {
        out = value;
        return Status::Ok;
    }
    if (value.type == ValueType::Struct) {
        if (value.structure != nullptr &&
            (sameName(declaredType, "Var") || sameName(declaredType, "ScriptObject") ||
             sameName(declaredType, value.structure->typeName))) {
            out = value;
            return Status::Ok;
        }
        return Status::TypeMismatch;
    }
    switch (target) {
        case ValueType::Array:
            if (value.type != ValueType::Array) return Status::TypeMismatch;
            out = value;
            return Status::Ok;
        case ValueType::Object:
            if (value.type != ValueType::Object) return Status::TypeMismatch;
            if (!sameName(declaredType, "ScriptObject") && !isObjectOfType(value, declaredType)) {
                return Status::TypeMismatch;
            }
            out = value;
            return Status::Ok;
        default:
            if (value.type == target) {
                out = value;
                return Status::Ok;
            }
            if (!castScalar(value, target, out)) return Status::TypeMismatch;
            return Status::Ok;
    }
}

Status Interpreter::castTo(const Value& source, const std::string& sourceType,
                           const std::string& targetType, Value& out) const {
    if (targetType.empty()) {
        out = source;
        return Status::Ok;
    }
    if (!sourceType.empty() && sameName(sourceType, targetType)) {
        out = source;
        return Status::Ok;
    }
    if (declaredValueType(targetType) == ValueType::Bool) {
        bool flag = false;
        const Status status = toBool(source, flag);
        if (status != Status::Ok) return status;
        out = boolValue(flag);
        return Status::Ok;
    }
    if (isNoneLike(source)) {
        out = noneValue();
        return Status::Ok;
    }
    if (source.type == ValueType::Struct) {
        if (source.structure != nullptr &&
            (sameName(targetType, "Var") || sameName(targetType, "ScriptObject") ||
             sameName(targetType, source.structure->typeName))) {
            out = source;
            return Status::Ok;
        }
        return Status::TypeMismatch;
    }
    switch (declaredValueType(targetType)) {
        case ValueType::None:
            out = source;
            return Status::Ok;
        case ValueType::Array:
            if (source.type != ValueType::Array) return Status::TypeMismatch;
            out = source;
            return Status::Ok;
        case ValueType::Object:
            if (source.type != ValueType::Object) return Status::TypeMismatch;
            if (!sameName(targetType, "ScriptObject") && !isObjectOfType(source, targetType)) {
                out = noneValue();
                return Status::Ok;
            }
            out = source;
            return Status::Ok;
        default:
            if (source.type == declaredValueType(targetType)) {
                out = source;
                return Status::Ok;
            }
            if (!castScalar(source, declaredValueType(targetType), out)) {
                return Status::TypeMismatch;
            }
            return Status::Ok;
    }
}

Status Interpreter::toBool(const Value& value, bool& out) const {
    if (value.type == ValueType::Bool) {
        out = value.boolean;
        return Status::Ok;
    }
    if (isNoneLike(value)) {
        out = false;
        return Status::Ok;
    }
    if (value.type == ValueType::Object || value.type == ValueType::Array ||
        value.type == ValueType::Struct) {
        out = true;
        return Status::Ok;
    }
    Value converted;
    if (!castScalar(value, ValueType::Bool, converted)) return Status::TypeMismatch;
    out = converted.boolean;
    return Status::Ok;
}

void Interpreter::record(Status status) {
    if (firstError_ == Status::Ok) firstError_ = status;
}

}
