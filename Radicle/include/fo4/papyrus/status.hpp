#pragma once

#include <cstdint>

namespace fo4::papyrus {

enum class Status : std::uint8_t {
    Ok = 0,
    UnknownOpcode,
    UnsupportedOpcode,
    UnknownIdentifier,
    TypeMismatch,
    DivisionByZero,
    IndexOutOfRange,
    InvalidCount,
    ArrayTooLong,
    NullArray,
    NullObject,
    BadJumpTarget,
    StackUnderflow,
    CallDepthExceeded,
    InstructionBudgetExceeded,
    FunctionNotFound,
    ArgumentCountMismatch,
    HostError,
    InternalError,
    StringTooLong,
    GateRefused,
    Paused,
    Latent,
};

const char* statusName(Status status);
bool isFatal(Status status);

}
