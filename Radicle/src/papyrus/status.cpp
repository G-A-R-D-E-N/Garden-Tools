#include "fo4/papyrus/status.hpp"

namespace fo4::papyrus {

const char* statusName(Status status) {
    switch (status) {
        case Status::Ok:
            return "Ok";
        case Status::UnknownOpcode:
            return "UnknownOpcode";
        case Status::UnsupportedOpcode:
            return "UnsupportedOpcode";
        case Status::UnknownIdentifier:
            return "UnknownIdentifier";
        case Status::TypeMismatch:
            return "TypeMismatch";
        case Status::DivisionByZero:
            return "DivisionByZero";
        case Status::IndexOutOfRange:
            return "IndexOutOfRange";
        case Status::InvalidCount:
            return "InvalidCount";
        case Status::ArrayTooLong:
            return "ArrayTooLong";
        case Status::NullArray:
            return "NullArray";
        case Status::NullObject:
            return "NullObject";
        case Status::BadJumpTarget:
            return "BadJumpTarget";
        case Status::StackUnderflow:
            return "StackUnderflow";
        case Status::CallDepthExceeded:
            return "CallDepthExceeded";
        case Status::InstructionBudgetExceeded:
            return "InstructionBudgetExceeded";
        case Status::FunctionNotFound:
            return "FunctionNotFound";
        case Status::ArgumentCountMismatch:
            return "ArgumentCountMismatch";
        case Status::HostError:
            return "HostError";
        case Status::InternalError:
            return "InternalError";
        case Status::StringTooLong:
            return "StringTooLong";
        case Status::GateRefused:
            return "GateRefused";
        case Status::Paused:
            return "Paused";
        case Status::Latent:
            return "Latent";
    }
    return "Unknown";
}

bool isFatal(Status status) {
    switch (status) {
        case Status::UnknownOpcode:
        case Status::UnsupportedOpcode:
        case Status::BadJumpTarget:
        case Status::StackUnderflow:
        case Status::CallDepthExceeded:
        case Status::InstructionBudgetExceeded:
        case Status::HostError:
        case Status::InternalError:
        case Status::GateRefused:
            return true;
        case Status::Ok:
        case Status::UnknownIdentifier:
        case Status::TypeMismatch:
        case Status::DivisionByZero:
        case Status::IndexOutOfRange:
        case Status::InvalidCount:
        case Status::ArrayTooLong:
        case Status::NullArray:
        case Status::NullObject:
        case Status::FunctionNotFound:
        case Status::ArgumentCountMismatch:
        case Status::StringTooLong:
        case Status::Paused:
        case Status::Latent:
            return false;
    }
    return true;
}

}
