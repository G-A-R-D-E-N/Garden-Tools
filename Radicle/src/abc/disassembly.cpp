#include <fo4/abc/opcodes.hpp>

#include <span>
#include <string>

namespace fo4::abc {

namespace {

std::string quote(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 2);
  out += '"';
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      out += '\\';
    }
    out += ch;
  }
  out += '"';
  return out;
}

std::string pool_int(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.ints.size()) {
    return "#" + std::to_string(index);
  }
  return std::to_string(file.pool.ints[index]);
}

std::string pool_uint(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.uints.size()) {
    return "#" + std::to_string(index);
  }
  return std::to_string(file.pool.uints[index]);
}

std::string pool_double(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.doubles.size()) {
    return "#" + std::to_string(index);
  }
  return std::to_string(file.pool.doubles[index]);
}

std::string named(const std::string& name, const char* prefix, std::uint32_t index) {
  if (name.empty()) {
    return std::string{prefix} + std::to_string(index);
  }
  return name;
}

std::string render_operand(const AbcFile& file, const AbcDecodedOperand& operand) {
  switch (operand.role) {
    case AbcOperandRole::None:
      return std::string{};
    case AbcOperandRole::Multiname:
      return named(file.multiname_at(operand.value), "multiname#", operand.value);
    case AbcOperandRole::String:
      return quote(file.string_at(operand.value));
    case AbcOperandRole::Namespace:
      return named(file.namespace_at(operand.value), "namespace#", operand.value);
    case AbcOperandRole::Method:
      return named(file.method_name(operand.value), "method#", operand.value);
    case AbcOperandRole::Class:
      return named(file.class_name(operand.value), "class#", operand.value);
    case AbcOperandRole::Int:
      return pool_int(file, operand.value);
    case AbcOperandRole::UInt:
      return pool_uint(file, operand.value);
    case AbcOperandRole::Double:
      return pool_double(file, operand.value);
    case AbcOperandRole::Count:
      return std::to_string(operand.value);
    case AbcOperandRole::Immediate:
      return std::to_string(operand.immediate);
    case AbcOperandRole::Offset:
      return std::to_string(operand.target);
    case AbcOperandRole::Switch: {
      std::string out = "default " + std::to_string(operand.target) + " cases [";
      for (std::size_t i = 0; i < operand.case_targets.size(); ++i) {
        if (i != 0) {
          out += ", ";
        }
        out += std::to_string(operand.case_targets[i]);
      }
      out += "]";
      return out;
    }
  }
  return std::string{};
}

}

std::string render_instruction(const AbcFile& file, const AbcInstruction& instruction) {
  std::string out = std::to_string(instruction.offset) + ": " + instruction.mnemonic;
  for (const AbcDecodedOperand& operand : instruction.operands) {
    out += " ";
    out += render_operand(file, operand);
  }
  out += "\n";
  return out;
}

std::string render_listing(const AbcFile& file, const std::vector<AbcInstruction>& code) {
  std::string out;
  for (const AbcInstruction& instruction : code) {
    out += render_instruction(file, instruction);
  }
  return out;
}

AbcStatus disassemble(const AbcFile& file, const std::vector<std::uint8_t>& code,
                      std::string& out, std::span<const AbcException> exceptions) {
  out.clear();
  std::vector<AbcInstruction> instructions;
  const AbcStatus status = decode_code(code, instructions, exceptions);
  if (status != AbcStatus::Ok) {
    return status;
  }
  const AbcStatus pool_status = validate_operand_pools(file, instructions);
  if (pool_status != AbcStatus::Ok) {
    return pool_status;
  }
  out = render_listing(file, instructions);
  return AbcStatus::Ok;
}

}
