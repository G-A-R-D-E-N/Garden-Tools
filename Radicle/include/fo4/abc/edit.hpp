#pragma once

#include <fo4/abc/abc.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace fo4::abc {

enum class AbcEditStatus : std::uint8_t {
  Ok = 0,
  NoSuchBody,
  DecodeFailed,
  NotAnInstructionBoundary,
  BadInsertedInstruction,
  InsertedInstructionBranches,
  BranchTargetsDeletedInstruction,
  ExceptionRangeAffected,
  EncodeFailed,
  VerifierRefused,
};

const char* edit_status_name(AbcEditStatus status) noexcept;

// Requires a body index and an instruction boundary; offset==code.size() appends after the last instruction.
AbcEditStatus insert_instruction(AbcFile& file, std::uint32_t body, std::size_t offset,
                                 std::span<const std::uint8_t> instruction,
                                 std::string& message);

// Requires an instruction boundary; a branch or exception that targets the deleted instruction must be refused.
AbcEditStatus delete_instruction(AbcFile& file, std::uint32_t body, std::size_t offset,
                                 std::string& message);

// Requires an instruction boundary and one branch-free instruction; a branch that targets the replaced offset keeps naming it.
AbcEditStatus replace_instruction(AbcFile& file, std::uint32_t body, std::size_t offset,
                                  std::span<const std::uint8_t> instruction, std::string& message);

// Requires a body index; raises local_count and max_stack to what the verifier measures and leaves the body unchanged on refusal.
AbcEditStatus refresh_body_limits(AbcFile& file, std::uint32_t body, std::string& message);

}  // namespace fo4::abc
