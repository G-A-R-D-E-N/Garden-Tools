#pragma once

#include "fo4/papyrus/host.hpp"
#include "fo4/papyrus/status.hpp"
#include "fo4/papyrus/value.hpp"
#include "fo4/pex/pex.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace fo4::papyrus {

struct Limits {
    std::size_t maxInstructions{200000};
    std::size_t maxCallDepth{64};
    std::size_t maxArrayLength{128};
    std::size_t maxStringLength{1048576};
};

struct ThreadSnapshot {
    std::string function;
    std::int32_t pc{0};
    std::vector<std::string> locals;
    std::optional<std::uint64_t> latentToken;
    std::string latentNative;
};

struct Result {
    Status status{Status::Ok};
    Value value;
    std::size_t instructions{0};
    std::size_t callDepth{0};
    std::uint64_t latentToken{0};

    bool ok() const { return status == Status::Ok; }
};

class Interpreter {
public:
    explicit Interpreter(Host& host, Limits limits = Limits{});

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    Result runFunction(const std::string& scriptName, const std::string& functionName,
                       const std::vector<Value>& args = {});
    Result runFunction(const Value& self, const std::string& functionName,
                       const std::vector<Value>& args = {});
    Result runObjectFunction(const pex::PexObject& object, const pex::PexFunction& function,
                             const Value& self, const std::vector<Value>& args = {});
    // Invariant: a slice budget is per call and never more than Limits::maxInstructions in total.
    Result startFunction(const std::string& scriptName, const std::string& functionName,
                         const std::vector<Value>& args, std::size_t sliceBudget);
    Result startFunction(const Value& self, const std::string& functionName,
                         const std::vector<Value>& args, std::size_t sliceBudget);
    Result resumeSlice(std::size_t sliceBudget);
    // Invariant: only the token the run reported resumes it, and a value resumes it exactly once.
    Result completeLatent(std::uint64_t token, const Value& value, std::size_t sliceBudget);
    bool suspended() const { return !frames_.empty(); }
    ThreadSnapshot snapshot() const;
    std::uint64_t latentToken() const { return latentToken_; }
    void abort();

private:
    struct Slot {
        std::string name;
        std::string declaredType;
        Value value;
    };

    struct Frame {
        const pex::PexObject* object{nullptr};
        const pex::PexFunction* function{nullptr};
        std::string state;
        Value self;
        pex::PexValue destination;
        std::vector<Slot> slots;
        std::size_t ip{0};
    };

    Result run(const pex::PexObject* object, const pex::PexFunction& function, const std::string& state,
               const Value& self, const std::vector<Value>& args);
    Status runLoop(std::size_t sliceBudget);
    Result finish(Status status) const;
    Status suspendLatent(const pex::PexValue& destination, std::uint64_t token,
                         const std::string& native);
    Status execute(const pex::PexInstruction& instruction, Frame& frame, std::size_t& next);
    Status invoke(Frame& frame, const Value& self, const pex::PexObject* script,
                  const std::string& state, const std::string& nativeScript,
                  const std::string& functionName, const pex::PexValue& destination,
                  const std::vector<Value>& args);
    Status pushFrame(const pex::PexObject* object, const pex::PexFunction& function,
                     const std::string& state, const Value& self, const pex::PexValue& destination,
                     const std::vector<Value>& args);
    void popFrame(const Value& value);
    Status readOperand(Frame& frame, const pex::PexValue& operand, Value& out);
    Status readPair(Frame& frame, const pex::PexValue& left, const pex::PexValue& right,
                    Value& leftValue, Value& rightValue);
    Status readIntegerOperand(Frame& frame, const pex::PexValue& operand, std::int32_t& out);
    Status readBoolOperand(Frame& frame, const pex::PexValue& operand, bool& out);
    Status resolveIdentifier(Frame& frame, const std::string& name, Value& out);
    Status writeOperand(Frame& frame, const pex::PexValue& operand, const Value& value);
    Status storeResult(Frame& frame, const pex::PexValue& destination, const Value& value);
    Slot* findSlot(Frame& frame, const std::string& name);
    std::string declaredVariableType(const Frame& frame, const std::string& name) const;
    std::string declaredOperandType(const Frame& frame, const pex::PexValue& operand) const;
    const pex::PexFunction* findFunction(const pex::PexObject& object, const std::string& state,
                                         const std::string& name,
                                         const pex::PexObject** owner) const;
    const pex::PexStruct* findStruct(const pex::PexObject* object, const std::string& name) const;
    Status makeStruct(const pex::PexObject* object, const std::string& typeName,
                      std::size_t depth, Value& out) const;
    Status structGet(Frame& frame, const pex::PexValue& destination, const pex::PexValue& structure,
                     const std::string& member);
    Status structSet(Frame& frame, const pex::PexValue& structure, const std::string& member,
                     const pex::PexValue& value);
    Status findStructInArray(Frame& frame, const pex::PexInstruction& instruction, bool reverse);
    Status gotoState(Frame& frame, const Value& receiver, const std::vector<Value>& args,
                     const pex::PexValue& destination);
    bool isObjectOfType(const Value& value, const std::string& typeName) const;
    Status coerce(const Value& value, const std::string& declaredType, Value& out) const;
    Status castTo(const Value& source, const std::string& sourceType,
                  const std::string& targetType, Value& out) const;
    Status toBool(const Value& value, bool& out) const;
    void record(Status status);

    Host& host_;
    Limits limits_;
    std::deque<Frame> frames_;
    std::size_t instructions_{0};
    std::size_t maxDepth_{0};
    Value result_;
    Value returnValue_;
    bool hasReturn_{false};
    Status firstError_{Status::Ok};
    bool paused_{false};
    bool latent_{false};
    std::uint64_t latentToken_{0};
    pex::PexValue latentDestination_;
    std::string latentNative_;
    std::size_t sliceInstructions_{0};
};

}
