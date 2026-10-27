#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/value.hpp>

#include "native_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fo4::avm2 {

namespace {

double number_argument(const std::vector<Value>& arguments, std::size_t index) {
  return to_number(argument_at(arguments, index));
}

ExecutionResult math_unary(const std::vector<Value>& arguments, double (*operation)(double)) {
  return native_value(Value::number(operation(number_argument(arguments, 0))));
}

ExecutionResult math_abs(const std::vector<Value>& arguments) {
  return native_value(Value::number(std::fabs(number_argument(arguments, 0))));
}

ExecutionResult math_min(const std::vector<Value>& arguments) {
  if (arguments.empty()) {
    return native_value(Value::number(std::numeric_limits<double>::infinity()));
  }
  double result = number_argument(arguments, 0);
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const double value = number_argument(arguments, index);
    if (std::isnan(value)) {
      return native_value(Value::number(value));
    }
    result = value < result ? value : result;
  }
  return native_value(Value::number(result));
}

ExecutionResult math_max(const std::vector<Value>& arguments) {
  if (arguments.empty()) {
    return native_value(Value::number(-std::numeric_limits<double>::infinity()));
  }
  double result = number_argument(arguments, 0);
  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const double value = number_argument(arguments, index);
    if (std::isnan(value)) {
      return native_value(Value::number(value));
    }
    result = value > result ? value : result;
  }
  return native_value(Value::number(result));
}

ExecutionResult math_round(const std::vector<Value>& arguments) {
  const double value = number_argument(arguments, 0);
  if (std::isnan(value) || std::isinf(value)) {
    return native_value(Value::number(value));
  }
  return native_value(Value::number(std::floor(value + 0.5)));
}

ExecutionResult math_pow(const std::vector<Value>& arguments) {
  return native_value(Value::number(
      std::pow(number_argument(arguments, 0), number_argument(arguments, 1))));
}

ExecutionResult math_atan2(const std::vector<Value>& arguments) {
  return native_value(Value::number(
      std::atan2(number_argument(arguments, 0), number_argument(arguments, 1))));
}

}  // namespace

bool try_math_native(Interpreter& interpreter, native::Id id, const Value& self,
                     const std::vector<Value>& arguments, std::size_t depth,
                     ExecutionResult& out) {
  (void)self;
  (void)depth;
  switch (id) {
    case native::Id::math_abs:
      out = math_abs(arguments);
      return true;
    case native::Id::math_min:
      out = math_min(arguments);
      return true;
    case native::Id::math_max:
      out = math_max(arguments);
      return true;
    case native::Id::math_floor:
      out = math_unary(arguments, [](double value) { return std::floor(value); });
      return true;
    case native::Id::math_ceil:
      out = math_unary(arguments, [](double value) { return std::ceil(value); });
      return true;
    case native::Id::math_round:
      out = math_round(arguments);
      return true;
    case native::Id::math_sqrt:
      out = math_unary(arguments, [](double value) { return std::sqrt(value); });
      return true;
    case native::Id::math_pow:
      out = math_pow(arguments);
      return true;
    case native::Id::math_random:
      out = native_value(Value::number(interpreter.next_random()));
      return true;
    case native::Id::math_sin:
      out = math_unary(arguments, [](double value) { return std::sin(value); });
      return true;
    case native::Id::math_cos:
      out = math_unary(arguments, [](double value) { return std::cos(value); });
      return true;
    case native::Id::math_tan:
      out = math_unary(arguments, [](double value) { return std::tan(value); });
      return true;
    case native::Id::math_atan2:
      out = math_atan2(arguments);
      return true;
    case native::Id::math_acos:
      out = math_unary(arguments, [](double value) { return std::acos(value); });
      return true;
    case native::Id::math_asin:
      out = math_unary(arguments, [](double value) { return std::asin(value); });
      return true;
    case native::Id::math_atan:
      out = math_unary(arguments, [](double value) { return std::atan(value); });
      return true;
    case native::Id::math_log:
      out = math_unary(arguments, [](double value) { return std::log(value); });
      return true;
    case native::Id::math_exp:
      out = math_unary(arguments, [](double value) { return std::exp(value); });
      return true;
    default:
      return false;
  }
}

}  // namespace fo4::avm2
