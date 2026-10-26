#include "fo4/papyrus/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fo4::papyrus {

Result Interpreter::startFunction(const std::string& scriptName, const std::string& functionName,
                                  const std::vector<Value>& args, std::size_t sliceBudget) {
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

    Result failed;
    const pex::PexObject* object = host_.findObject(scriptName);
    if (object == nullptr) {
        failed.status = Status::FunctionNotFound;
        return failed;
    }
    const std::string state = object->autoStateName;
    const pex::PexObject* owner = nullptr;
    const pex::PexFunction* function = findFunction(*object, state, functionName, &owner);
    if (function == nullptr) {
        failed.status = Status::FunctionNotFound;
        return failed;
    }
    if (function->isNative) {
        Value out;
        Status status = Status::Ok;
        if (!host_.callNative(object->name, function->name, args, out, status)) {
            failed.status = status == Status::Ok ? Status::FunctionNotFound : status;
            return failed;
        }
        failed.status = coerce(out, function->returnType, failed.value);
        return failed;
    }
    const Value self = objectValue(0, object->name, state);
    const Status pushed = pushFrame(owner, *function, state, self, pex::PexValue{}, args);
    if (pushed != Status::Ok) {
        failed.status = pushed;
        return failed;
    }
    return finish(runLoop(sliceBudget == 0 ? limits_.maxInstructions : sliceBudget));
}

Result Interpreter::startFunction(const Value& self, const std::string& functionName,
                                  const std::vector<Value>& args, std::size_t sliceBudget) {
    Result failed;
    if (self.type != ValueType::Object || self.object == nullptr) {
        failed.status = Status::NullObject;
        return failed;
    }
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

    const pex::PexObject* object = host_.findObject(self.object->type);
    if (object == nullptr) {
        failed.status = Status::FunctionNotFound;
        return failed;
    }
    const std::string state = self.object->state.empty() ? object->autoStateName : self.object->state;
    const pex::PexObject* owner = nullptr;
    const pex::PexFunction* function = findFunction(*object, state, functionName, &owner);
    if (function == nullptr) {
        failed.status = Status::FunctionNotFound;
        return failed;
    }
    if (function->isNative) {
        Value out;
        Status status = Status::Ok;
        if (!host_.callNative(object->name, function->name, args, out, status)) {
            failed.status = status == Status::Ok ? Status::FunctionNotFound : status;
            return failed;
        }
        failed.status = coerce(out, function->returnType, failed.value);
        return failed;
    }
    const Status pushed = pushFrame(owner, *function, state, self, pex::PexValue{}, args);
    if (pushed != Status::Ok) {
        failed.status = pushed;
        return failed;
    }
    return finish(runLoop(sliceBudget == 0 ? limits_.maxInstructions : sliceBudget));
}

Status Interpreter::runLoop(std::size_t sliceBudget) {
    sliceInstructions_ = 0;
    paused_ = false;
    while (!frames_.empty() && firstError_ == Status::Ok) {
        if (instructions_ >= limits_.maxInstructions) {
            record(Status::InstructionBudgetExceeded);
            break;
        }
        if (sliceInstructions_ >= sliceBudget) {
            paused_ = true;
            break;
        }
        const std::size_t frameIndex = frames_.size() - 1;
        Frame& frame = frames_[frameIndex];
        if (frame.function == nullptr || frame.ip >= frame.function->instructions.size()) {
            hasReturn_ = false;
            returnValue_ = Value{};
            popFrame(noneValue());
            continue;
        }
        const pex::PexInstruction& instruction = frame.function->instructions[frame.ip];
        std::size_t next = frame.ip + 1;
        ++instructions_;
        ++sliceInstructions_;
        const Status status = execute(instruction, frame, next);
        if (status == Status::Latent) {
            latent_ = true;
            break;
        }
        if (status != Status::Ok) {
            record(status);
            break;
        }
        if (hasReturn_) {
            hasReturn_ = false;
            const Value value = returnValue_;
            returnValue_ = Value{};
            popFrame(value);
            continue;
        }
        frames_[frameIndex].ip = next;
    }
    if (firstError_ != Status::Ok) return firstError_;
    if (latent_) return Status::Latent;
    if (paused_ && !frames_.empty()) return Status::Paused;
    return Status::Ok;
}

Result Interpreter::finish(Status status) const {
    Result result;
    result.status = status;
    result.value = status == Status::Ok ? result_ : noneValue();
    result.instructions = instructions_;
    result.callDepth = maxDepth_;
    result.latentToken = latent_ ? latentToken_ : 0;
    return result;
}

Result Interpreter::resumeSlice(std::size_t sliceBudget) {
    if (latent_) {
        Result waiting;
        waiting.status = Status::Latent;
        waiting.instructions = instructions_;
        waiting.latentToken = latentToken_;
        return waiting;
    }
    if (frames_.empty()) return finish(firstError_);
    return finish(runLoop(sliceBudget == 0 ? limits_.maxInstructions : sliceBudget));
}

Result Interpreter::completeLatent(std::uint64_t token, const Value& value,
                                   std::size_t sliceBudget) {
    Result failed;
    if (!latent_ || token != latentToken_ || frames_.empty()) {
        failed.status = Status::InvalidCount;
        return failed;
    }
    const Status wrote = writeOperand(frames_.back(), latentDestination_, value);
    if (wrote != Status::Ok) {
        record(wrote);
        failed.status = wrote;
        return failed;
    }
    latent_ = false;
    latentToken_ = 0;
    latentDestination_ = pex::PexValue{};
    return finish(runLoop(sliceBudget == 0 ? limits_.maxInstructions : sliceBudget));
}

void Interpreter::abort() {
    frames_.clear();
    paused_ = false;
    latent_ = false;
    latentToken_ = 0;
    firstError_ = Status::Ok;
    result_ = Value{};
}

Status Interpreter::suspendLatent(const pex::PexValue& destination, std::uint64_t token) {
    latent_ = true;
    latentToken_ = token;
    latentDestination_ = destination;
    return Status::Latent;
}


}  // namespace fo4::papyrus
