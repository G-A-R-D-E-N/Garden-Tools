#pragma once

#include "fo4/pex/pex.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::pex_analysis {

inline constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

enum class PexOp : std::uint8_t {
  Nop = 0,
  IAdd = 1,
  FAdd = 2,
  ISub = 3,
  FSub = 4,
  IMul = 5,
  FMul = 6,
  IDiv = 7,
  FDiv = 8,
  IMod = 9,
  Not = 10,
  INeg = 11,
  FNeg = 12,
  Assign = 13,
  Cast = 14,
  CmpEq = 15,
  CmpLt = 16,
  CmpLte = 17,
  CmpGt = 18,
  CmpGte = 19,
  Jmp = 20,
  JmpT = 21,
  JmpF = 22,
  CallMethod = 23,
  CallParent = 24,
  CallStatic = 25,
  Return = 26,
  StrCat = 27,
  PropGet = 28,
  PropSet = 29,
  ArrayCreate = 30,
  ArrayLength = 31,
  ArrayGetElement = 32,
  ArraySetElement = 33,
  ArrayFindElement = 34,
  ArrayRFindElement = 35,
  Is = 36,
  StructCreate = 37,
  StructGet = 38,
  StructSet = 39,
  ArrayFindStruct = 40,
  ArrayRFindStruct = 41,
  ArrayAdd = 42,
  ArrayInsert = 43,
  ArrayRemoveLast = 44,
  ArrayRemove = 45,
  ArrayClear = 46,
};

inline bool pex_is_jump(std::uint8_t op) {
  return op >= static_cast<std::uint8_t>(PexOp::Jmp) &&
         op <= static_cast<std::uint8_t>(PexOp::JmpF);
}

inline bool pex_is_call(std::uint8_t op) {
  return op >= static_cast<std::uint8_t>(PexOp::CallMethod) &&
         op <= static_cast<std::uint8_t>(PexOp::CallStatic);
}

enum class PexOperandRole : std::uint8_t {
  None = 0,
  FunctionName = 1,
  Receiver = 2,
  OwningScript = 3,
  Result = 4,
};

// Invariant: only the three call opcodes carry roles, and only for their fixed operands.
// Invariant: an index at or past those fixed operands is None, so a variadic argument never has a role.
PexOperandRole pex_call_operand_role(std::uint8_t op, std::size_t index);

// Invariant: a role the opcode's table does not carry yields kNoIndex, never a guessed position.
std::size_t pex_call_operand_index(std::uint8_t op, PexOperandRole role);

inline bool pex_is_conditional_jump(std::uint8_t op) {
  return op == static_cast<std::uint8_t>(PexOp::JmpT) ||
         op == static_cast<std::uint8_t>(PexOp::JmpF);
}

inline bool pex_jump_offset(const fo4::pex::PexInstruction& instruction,
                            std::int64_t* out_offset) {
  if (!pex_is_jump(instruction.opCode)) return false;
  const std::size_t index =
      instruction.opCode == static_cast<std::uint8_t>(PexOp::Jmp) ? 0 : 1;
  if (instruction.args.size() <= index) return false;
  const auto& argument = instruction.args[index];
  if (argument.type != fo4::pex::PexValueType::Integer) return false;
  if (out_offset != nullptr) *out_offset = argument.int_;
  return true;
}

std::string pex_op_name(std::uint8_t op);
std::string pex_value_text(const fo4::pex::PexValue& value);

}
