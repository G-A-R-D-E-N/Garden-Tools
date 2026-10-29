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
  PoolEntryOutOfRange,
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

// Requires a body index; raises local_count, max_stack and max_scope_depth to what the verifier measures and leaves the body unchanged on refusal.
AbcEditStatus refresh_body_limits(AbcFile& file, std::uint32_t body, std::string& message);

// Invariant: an identical pool entry is reused and appends never move an existing index.
std::uint32_t intern_int(AbcFile& file, std::int32_t value);
std::uint32_t intern_uint(AbcFile& file, std::uint32_t value);
std::uint32_t intern_double(AbcFile& file, double value);
std::uint32_t intern_string(AbcFile& file, const std::string& value);
// Requires every pool index the entry names to lie inside its table; a refusal appends nothing and clears index.
AbcEditStatus intern_namespace(AbcFile& file, const AbcNamespace& value, std::uint32_t& index,
                               std::string& message);
AbcEditStatus intern_multiname(AbcFile& file, const AbcMultiname& value, std::uint32_t& index,
                               std::string& message);

// Requires a body index; the new code must verify, and a refusal leaves the body unchanged.
AbcEditStatus replace_body_code(AbcFile& file, std::uint32_t body, std::span<const std::uint8_t> code,
                                std::string& message);

// Requires method info the writer accepts; the appended method and body are removed again on refusal.
AbcEditStatus add_method(AbcFile& file, const AbcMethodInfo& method,
                         std::span<const std::uint8_t> code, std::uint32_t& method_index,
                         std::string& message);

}  // namespace fo4::abc
