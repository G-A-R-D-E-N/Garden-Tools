#pragma once

#include <fo4/abc/abc.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::abc {

enum class AbcOperandEncoding : std::uint8_t {
  None = 0,
  U30,
  S24,
  S8,
  U8,
  Switch,
};

enum class AbcOperandRole : std::uint8_t {
  None = 0,
  Multiname,
  String,
  Namespace,
  Method,
  Class,
  Int,
  UInt,
  Double,
  Count,
  Immediate,
  Offset,
  Switch,
};

struct AbcOperandSpec {
  AbcOperandEncoding encoding{AbcOperandEncoding::None};
  AbcOperandRole role{AbcOperandRole::None};
};

struct AbcOpcodeInfo {
  const char* mnemonic{nullptr};
  std::uint8_t operand_count{0};
  AbcOperandSpec operands[4];
};

const AbcOpcodeInfo* opcode_info(std::uint8_t opcode) noexcept;

struct AbcDecodedOperand {
  AbcOperandRole role{AbcOperandRole::None};
  std::uint32_t value{0};
  std::int32_t immediate{0};
  std::int32_t target{0};
  std::vector<std::int32_t> case_targets;
};

struct AbcInstruction {
  std::size_t offset{0};
  std::uint8_t opcode{0};
  const char* mnemonic{""};
  std::size_t length{0};
  std::vector<AbcDecodedOperand> operands;
};

AbcStatus decode_code(const std::uint8_t* code, std::size_t size,
                      std::vector<AbcInstruction>& out,
                      std::span<const AbcException> exceptions = {});

AbcStatus decode_code(const std::vector<std::uint8_t>& code,
                      std::vector<AbcInstruction>& out,
                      std::span<const AbcException> exceptions = {});

/* Invariant: offsets and lengths are the decoded ones, so the size equals instruction.length. */
AbcStatus encode_code(const std::vector<AbcInstruction>& code, std::vector<std::uint8_t>& out);

AbcStatus validate_operand_pools(const AbcFile& file,
                                 const std::vector<AbcInstruction>& code);

std::string render_instruction(const AbcFile& file, const AbcInstruction& instruction);

std::string render_listing(const AbcFile& file,
                           const std::vector<AbcInstruction>& code);

AbcStatus disassemble(const AbcFile& file, const std::vector<std::uint8_t>& code,
                      std::string& out, std::span<const AbcException> exceptions = {});

}
