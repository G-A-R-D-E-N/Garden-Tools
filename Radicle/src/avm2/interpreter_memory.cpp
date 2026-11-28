#include <fo4/avm2/interpreter.hpp>

#include "interpreter_internal.hpp"

#include <fo4/avm2/bytearray.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/domain.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::avm2 {

using namespace detail;

namespace {

std::size_t domain_width(std::uint8_t opcode) {
  switch (opcode) {
    case op::k_li8:
    case op::k_si8:
      return 1;
    case op::k_li16:
    case op::k_si16:
      return 2;
    case op::k_lf64:
    case op::k_sf64:
      return 8;
    default:
      return 4;
  }
}

InterpreterStatus check_domain_memory(ByteArrayObject* memory, const Value& address,
                                      std::size_t width, std::size_t& offset,
                                      std::string& message) {
  if (memory == nullptr) {
    message = "domain memory is not set on the current application domain";
    return InterpreterStatus::NullReference;
  }
  const std::uint64_t index = to_uint32(address);
  if (index + width > memory->length()) {
    message = "domain memory access of " + std::to_string(width) + " bytes at " +
              std::to_string(index) + " is outside the " + std::to_string(memory->length()) +
              " byte domain memory";
    return InterpreterStatus::InvalidArgument;
  }
  offset = static_cast<std::size_t>(index);
  return InterpreterStatus::Ok;
}

}  // namespace

InterpreterStatus Interpreter::domain_memory_load(std::uint8_t opcode, const Value& address,
                                                  Value& value, std::string& message) {
  const std::size_t width = domain_width(opcode);
  std::size_t offset = 0;
  ByteArrayObject* memory = runtime_.current_domain()->domain_memory().get();
  const InterpreterStatus status = check_domain_memory(memory, address, width, offset, message);
  if (status != InterpreterStatus::Ok) {
    return status;
  }
  std::uint64_t bits = 0;
  for (std::size_t index = 0; index < width; ++index) {
    bits |= std::uint64_t{memory->byte_at(offset + index)} << (8 * index);
  }
  switch (opcode) {
    case op::k_lf32:
      value = Value::number(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
      break;
    case op::k_lf64:
      value = Value::number(std::bit_cast<double>(bits));
      break;
    default:
      value = Value::int32(static_cast<std::int32_t>(bits));
      break;
  }
  return InterpreterStatus::Ok;
}

InterpreterStatus Interpreter::domain_memory_store(std::uint8_t opcode, const Value& address,
                                                   const Value& value, std::string& message) {
  const std::size_t width = domain_width(opcode);
  std::size_t offset = 0;
  ByteArrayObject* memory = runtime_.current_domain()->domain_memory().get();
  const InterpreterStatus status = check_domain_memory(memory, address, width, offset, message);
  if (status != InterpreterStatus::Ok) {
    return status;
  }
  std::uint64_t bits = 0;
  switch (opcode) {
    case op::k_sf32:
      bits = std::bit_cast<std::uint32_t>(static_cast<float>(to_number(value)));
      break;
    case op::k_sf64:
      bits = std::bit_cast<std::uint64_t>(to_number(value));
      break;
    default:
      bits = static_cast<std::uint32_t>(to_int32(value));
      break;
  }
  for (std::size_t index = 0; index < width; ++index) {
    memory->set_byte_at(offset + index, static_cast<std::uint8_t>(bits >> (8 * index)));
  }
  return InterpreterStatus::Ok;
}

}  // namespace fo4::avm2
