#include "interpreter_internal.hpp"

#include <fo4/avm2/coercion.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace fo4::avm2::detail {

std::uint32_t operand_at(const abc::AbcInstruction& instruction, std::size_t index) {
  return index < instruction.operands.size() ? instruction.operands[index].value : 0;
}

std::int32_t immediate_at(const abc::AbcInstruction& instruction, std::size_t index) {
  return index < instruction.operands.size() ? instruction.operands[index].immediate : 0;
}

std::uint32_t count_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::Count) {
      return operand.value;
    }
  }
  return 0;
}

std::uint32_t multiname_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::Multiname) {
      return operand.value;
    }
  }
  return 0;
}

std::uint32_t string_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::String) {
      return operand.value;
    }
  }
  return 0;
}

std::uint32_t int_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::Int) {
      return operand.value;
    }
  }
  return 0;
}

std::uint32_t uint_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::UInt) {
      return operand.value;
    }
  }
  return 0;
}

std::uint32_t double_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::Double) {
      return operand.value;
    }
  }
  return 0;
}

std::int32_t offset_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::Offset) {
      return operand.target;
    }
  }
  return 0;
}

const abc::AbcDecodedOperand* switch_operand(const abc::AbcInstruction& instruction) {
  for (const abc::AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == abc::AbcOperandRole::Switch) {
      return &operand;
    }
  }
  return nullptr;
}

Value numeric_result(double value) { return Value::number(value); }

Value int_range_result(double exact) {
  if (exact >= -2147483648.0 && exact <= 2147483647.0) {
    return Value::int32(static_cast<std::int32_t>(exact));
  }
  return numeric_result(exact);
}

Value uint_range_result(double exact) {
  if (exact >= 0.0 && exact <= 4294967295.0) {
    return Value::uint32(static_cast<std::uint32_t>(exact));
  }
  return numeric_result(exact);
}

Value add_values(const Value& left, const Value& right) {
  const Value left_primitive = to_primitive(left, PrimitiveHint::Default);
  const Value right_primitive = to_primitive(right, PrimitiveHint::Default);
  if (left_primitive.is_string() || right_primitive.is_string()) {
    return Value::string(to_string(left_primitive) + to_string(right_primitive));
  }
  if (left_primitive.is_int32() && right_primitive.is_int32()) {
    return int_range_result(static_cast<double>(left_primitive.as_int32()) +
                            static_cast<double>(right_primitive.as_int32()));
  }
  if (left_primitive.is_uint32() && right_primitive.is_uint32()) {
    return uint_range_result(static_cast<double>(left_primitive.as_uint32()) +
                             static_cast<double>(right_primitive.as_uint32()));
  }
  return numeric_result(to_number(left_primitive) + to_number(right_primitive));
}

Value subtract_values(const Value& left, const Value& right) {
  const Value l = to_primitive(left, PrimitiveHint::Number);
  const Value r = to_primitive(right, PrimitiveHint::Number);
  if (l.is_int32() && r.is_int32()) {
    return int_range_result(static_cast<double>(l.as_int32()) -
                            static_cast<double>(r.as_int32()));
  }
  if (l.is_uint32() && r.is_uint32()) {
    return uint_range_result(static_cast<double>(l.as_uint32()) -
                             static_cast<double>(r.as_uint32()));
  }
  return numeric_result(to_number(l) - to_number(r));
}

Value multiply_values(const Value& left, const Value& right) {
  const Value l = to_primitive(left, PrimitiveHint::Number);
  const Value r = to_primitive(right, PrimitiveHint::Number);
  if (l.is_int32() && r.is_int32()) {
    return int_range_result(static_cast<double>(l.as_int32()) *
                            static_cast<double>(r.as_int32()));
  }
  if (l.is_uint32() && r.is_uint32()) {
    return uint_range_result(static_cast<double>(l.as_uint32()) *
                             static_cast<double>(r.as_uint32()));
  }
  return numeric_result(to_number(l) * to_number(r));
}

Value divide_values(const Value& left, const Value& right) {
  return numeric_result(to_number(left) / to_number(right));
}

Value modulo_values(const Value& left, const Value& right) {
  const Value l = to_primitive(left, PrimitiveHint::Number);
  const Value r = to_primitive(right, PrimitiveHint::Number);
  if (l.is_int32() && r.is_int32()) {
    const std::int32_t divisor = r.as_int32();
    if (divisor == 0) {
      return Value::number(std::numeric_limits<double>::quiet_NaN());
    }
    return Value::int32(l.as_int32() % divisor);
  }
  if (l.is_uint32() && r.is_uint32()) {
    const std::uint32_t divisor = r.as_uint32();
    if (divisor == 0) {
      return Value::number(std::numeric_limits<double>::quiet_NaN());
    }
    return Value::uint32(l.as_uint32() % divisor);
  }
  return numeric_result(std::fmod(to_number(l), to_number(r)));
}

Value negate_value(const Value& value) {
  if (value.is_int32()) {
    return int_range_result(-static_cast<double>(value.as_int32()));
  }
  return numeric_result(-to_number(value));
}

Value increment_value(const Value& value) {
  if (value.is_int32()) {
    return int_range_result(static_cast<double>(value.as_int32()) + 1.0);
  }
  if (value.is_uint32()) {
    return uint_range_result(static_cast<double>(value.as_uint32()) + 1.0);
  }
  return numeric_result(to_number(value) + 1.0);
}

Value decrement_value(const Value& value) {
  if (value.is_int32()) {
    return int_range_result(static_cast<double>(value.as_int32()) - 1.0);
  }
  if (value.is_uint32()) {
    return uint_range_result(static_cast<double>(value.as_uint32()) - 1.0);
  }
  return numeric_result(to_number(value) - 1.0);
}

Value bitwise_and(const Value& left, const Value& right) {
  return Value::int32(to_int32(left) & to_int32(right));
}

Value bitwise_or(const Value& left, const Value& right) {
  return Value::int32(to_int32(left) | to_int32(right));
}

Value bitwise_xor(const Value& left, const Value& right) {
  return Value::int32(to_int32(left) ^ to_int32(right));
}

Value shift_left(const Value& left, const Value& right) {
  const auto shift = static_cast<std::uint32_t>(to_int32(right)) & 0x1Fu;
  return Value::int32(static_cast<std::int32_t>(static_cast<std::uint32_t>(to_int32(left))
                                                << shift));
}

Value shift_right(const Value& left, const Value& right) {
  const auto shift = static_cast<std::uint32_t>(to_int32(right)) & 0x1Fu;
  return Value::int32(to_int32(left) >> shift);
}

Value shift_unsigned(const Value& left, const Value& right) {
  const auto shift = static_cast<std::uint32_t>(to_int32(right)) & 0x1Fu;
  return Value::uint32(to_uint32(left) >> shift);
}

std::shared_ptr<Object> owner_of(Object& object) {
  try {
    return object.shared_from_this();
  } catch (const std::bad_weak_ptr&) {
    return nullptr;
  }
}

std::string property_text(const Multiname& multiname) { return render_multiname(multiname); }

bool is_array_index(const std::string& name, std::uint32_t& out_index) {
  if (name.empty() || name.size() > 10) {
    return false;
  }
  std::uint64_t value = 0;
  for (const char digit : name) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::uint64_t>(digit - '0');
  }
  if (value > 0xFFFFFFFEull) {
    return false;
  }
  if (name.size() > 1 && name[0] == '0') {
    return false;
  }
  out_index = static_cast<std::uint32_t>(value);
  return true;
}

}  // namespace fo4::avm2::detail
