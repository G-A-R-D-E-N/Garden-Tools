#include <fo4/avm1/interpreter.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::avm1 {

const char* run_status_name(Avm1RunStatus status) noexcept {
  switch (status) {
    case Avm1RunStatus::Ok:
      return "ok";
    case Avm1RunStatus::InvalidStream:
      return "invalid stream";
    case Avm1RunStatus::Unsupported:
      return "unsupported opcode";
    case Avm1RunStatus::StackUnderflow:
      return "stack underflow";
    case Avm1RunStatus::ConstantOutOfRange:
      return "constant out of range";
    case Avm1RunStatus::BadBranch:
      return "branch target is not a record start";
    case Avm1RunStatus::StepLimit:
      return "step limit reached";
    case Avm1RunStatus::ResourceLimit:
      return "resource limit reached";
  }
  return "unknown";
}

namespace {

constexpr const char* kUnderflow = "the operand stack has too few values";
constexpr const char* kWhitespace = " \t\r\n";
constexpr std::size_t kMaxStringBytes = std::size_t{16} * 1024 * 1024;

struct Avm1State {
  std::vector<Avm1Data> stack;
  std::vector<std::string> constants;
  std::map<std::string, Avm1Data> variables;
  std::vector<std::string> trace;
};

Avm1Data number_value(double number) {
  return Avm1Data{.kind = Avm1DataKind::Number, .number = number};
}

Avm1Data boolean_value(bool boolean) {
  return Avm1Data{.kind = Avm1DataKind::Boolean, .boolean = boolean};
}

Avm1Data string_value(std::string text) {
  return Avm1Data{.kind = Avm1DataKind::String, .text = std::move(text)};
}

bool is_nullish(const Avm1Data& data) {
  return data.kind == Avm1DataKind::Undefined || data.kind == Avm1DataKind::Null;
}

double parse_number(const std::string& text) {
  const std::size_t first = text.find_first_not_of(kWhitespace);
  if (first == std::string::npos) {
    return 0.0;
  }
  const std::string trimmed = text.substr(first, text.find_last_not_of(kWhitespace) - first + 1);
  char* end = nullptr;
  const double value = std::strtod(trimmed.c_str(), &end);
  if (end == trimmed.c_str() || *end != '\0') {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return value;
}

double to_number(const Avm1Data& data) {
  switch (data.kind) {
    case Avm1DataKind::Undefined:
      return std::numeric_limits<double>::quiet_NaN();
    case Avm1DataKind::Null:
      return 0.0;
    case Avm1DataKind::Boolean:
      return data.boolean ? 1.0 : 0.0;
    case Avm1DataKind::Number:
      return data.number;
    case Avm1DataKind::String:
      return parse_number(data.text);
  }
  return std::numeric_limits<double>::quiet_NaN();
}

std::string number_text(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value < 0.0 ? "-Infinity" : "Infinity";
  }
  if (value == 0.0) {
    return "0";
  }
  const std::string scientific = std::format("{:.14e}", std::fabs(value));
  const std::size_t e_at = scientific.find('e');
  std::string digits = scientific.substr(0, 1) + scientific.substr(2, e_at - 2);
  while (digits.size() > 1 && digits.back() == '0') {
    digits.pop_back();
  }
  const int point = std::stoi(scientific.substr(e_at + 1)) + 1;
  const int count = static_cast<int>(digits.size());
  std::string out = value < 0.0 ? "-" : "";
  if (count <= point && point <= 21) {
    out += digits + std::string(static_cast<std::size_t>(point - count), '0');
  } else if (0 < point && point <= 21) {
    out += digits.substr(0, static_cast<std::size_t>(point)) + "." +
           digits.substr(static_cast<std::size_t>(point));
  } else if (-6 < point && point <= 0) {
    out += "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
  } else {
    out += digits.substr(0, 1);
    if (count > 1) {
      out += "." + digits.substr(1);
    }
    const int exponent = point - 1;
    out += exponent < 0 ? "e-" : "e+";
    out += std::to_string(std::abs(exponent));
  }
  return out;
}

std::string to_text(const Avm1Data& data) {
  switch (data.kind) {
    case Avm1DataKind::Undefined:
      return "undefined";
    case Avm1DataKind::Null:
      return "null";
    case Avm1DataKind::Boolean:
      return data.boolean ? "true" : "false";
    case Avm1DataKind::Number:
      return number_text(data.number);
    case Avm1DataKind::String:
      return data.text;
  }
  return "undefined";
}

bool to_boolean(const Avm1Data& data) {
  switch (data.kind) {
    case Avm1DataKind::Undefined:
    case Avm1DataKind::Null:
      return false;
    case Avm1DataKind::Boolean:
      return data.boolean;
    case Avm1DataKind::Number:
      return data.number != 0.0 && !std::isnan(data.number);
    case Avm1DataKind::String:
      return !data.text.empty();
  }
  return false;
}

bool loose_equal(const Avm1Data& left, const Avm1Data& right) {
  if (is_nullish(left) || is_nullish(right)) {
    return is_nullish(left) && is_nullish(right);
  }
  if (left.kind == right.kind) {
    if (left.kind == Avm1DataKind::Boolean) {
      return left.boolean == right.boolean;
    }
    if (left.kind == Avm1DataKind::Number) {
      return left.number == right.number;
    }
    return left.text == right.text;
  }
  if (left.kind == Avm1DataKind::Boolean) {
    return loose_equal(number_value(to_number(left)), right);
  }
  if (right.kind == Avm1DataKind::Boolean) {
    return loose_equal(left, number_value(to_number(right)));
  }
  return to_number(left) == to_number(right);
}

bool less_than(const Avm1Data& left, const Avm1Data& right) {
  if (left.kind == Avm1DataKind::String && right.kind == Avm1DataKind::String) {
    return left.text < right.text;
  }
  return to_number(left) < to_number(right);
}

Avm1Data add_values(const Avm1Data& left, const Avm1Data& right) {
  if (left.kind == Avm1DataKind::String || right.kind == Avm1DataKind::String) {
    return string_value(to_text(left) + to_text(right));
  }
  return number_value(to_number(left) + to_number(right));
}

std::string opcode_label(std::uint8_t code) {
  const char* name = action_mnemonic(code);
  return std::format("{} (0x{:02X})", name != nullptr ? name : "unknown",
                     static_cast<unsigned>(code));
}

Avm1RunStatus pop(Avm1State& state, Avm1Data& out, std::string& message) {
  if (state.stack.empty()) {
    message = kUnderflow;
    return Avm1RunStatus::StackUnderflow;
  }
  out = std::move(state.stack.back());
  state.stack.pop_back();
  return Avm1RunStatus::Ok;
}

Avm1RunStatus push_result(Avm1State& state, Avm1Data result, std::string& message) {
  if (result.kind == Avm1DataKind::String && result.text.size() > kMaxStringBytes) {
    message = "the result is " + std::to_string(result.text.size()) +
              " bytes, past the string limit of " + std::to_string(kMaxStringBytes);
    return Avm1RunStatus::ResourceLimit;
  }
  state.stack.push_back(std::move(result));
  return Avm1RunStatus::Ok;
}

template <typename Combine>
Avm1RunStatus apply_binary(Avm1State& state, std::string& message, Combine combine) {
  Avm1Data right;
  Avm1Data left;
  if (const Avm1RunStatus status = pop(state, right, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, left, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  return push_result(state, combine(left, right), message);
}

template <typename Transform>
Avm1RunStatus apply_unary(Avm1State& state, std::string& message, Transform transform) {
  Avm1Data operand;
  if (const Avm1RunStatus status = pop(state, operand, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  return push_result(state, transform(operand), message);
}

Avm1RunStatus push_values(const Avm1Push& push, Avm1State& state, std::string& message) {
  for (const Avm1Value& value : push.values) {
    switch (value.type) {
      case Avm1ValueType::String:
        state.stack.push_back(string_value(value.text));
        break;
      case Avm1ValueType::Float:
        state.stack.push_back(number_value(std::bit_cast<float>(value.float_bits)));
        break;
      case Avm1ValueType::Null:
        state.stack.push_back(Avm1Data{.kind = Avm1DataKind::Null});
        break;
      case Avm1ValueType::Undefined:
        state.stack.push_back(Avm1Data{});
        break;
      case Avm1ValueType::Register:
        message = "a Push of a register value is not in this slice";
        return Avm1RunStatus::Unsupported;
      case Avm1ValueType::Boolean:
        state.stack.push_back(boolean_value(value.boolean));
        break;
      case Avm1ValueType::Double:
        state.stack.push_back(number_value(std::bit_cast<double>(value.double_bits)));
        break;
      case Avm1ValueType::Integer:
        state.stack.push_back(number_value(value.integer));
        break;
      case Avm1ValueType::Constant8:
      case Avm1ValueType::Constant16:
        if (value.index >= state.constants.size()) {
          message = "constant " + std::to_string(value.index) + " is past the pool of " +
                    std::to_string(state.constants.size()) + " strings";
          return Avm1RunStatus::ConstantOutOfRange;
        }
        state.stack.push_back(string_value(state.constants[value.index]));
        break;
    }
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus get_variable(Avm1State& state, std::string& message) {
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const auto found = state.variables.find(to_text(name));
  state.stack.push_back(found == state.variables.end() ? Avm1Data{} : found->second);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus set_variable(Avm1State& state, std::string& message) {
  Avm1Data value;
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  state.variables[to_text(name)] = std::move(value);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus trace_value(Avm1State& state, std::string& message) {
  Avm1Data value;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  state.trace.push_back(to_text(value));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus duplicate_top(Avm1State& state, std::string& message) {
  if (state.stack.empty()) {
    message = kUnderflow;
    return Avm1RunStatus::StackUnderflow;
  }
  state.stack.push_back(state.stack.back());
  return Avm1RunStatus::Ok;
}

Avm1RunStatus swap_top(Avm1State& state, std::string& message) {
  if (state.stack.size() < 2) {
    message = kUnderflow;
    return Avm1RunStatus::StackUnderflow;
  }
  std::swap(state.stack[state.stack.size() - 1], state.stack[state.stack.size() - 2]);
  return Avm1RunStatus::Ok;
}

std::size_t clamp_to_length(const Avm1Data& value, std::size_t length) {
  const double number = to_number(value);
  if (std::isnan(number) || number <= 0.0) {
    return 0;
  }
  return number >= static_cast<double>(length) ? length : static_cast<std::size_t>(number);
}

Avm1RunStatus extract_string(Avm1State& state, std::string& message) {
  Avm1Data count;
  Avm1Data index;
  Avm1Data text;
  if (const Avm1RunStatus status = pop(state, count, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, index, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, text, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const std::string source = to_text(text);
  const std::size_t start = clamp_to_length(index, source.size());
  const std::size_t length = clamp_to_length(count, source.size() - start);
  return push_result(state, string_value(source.substr(start, length)), message);
}

Avm1RunStatus jump_index(const Avm1Stream& stream, std::size_t target, std::size_t& index,
                         std::string& message) {
  const auto found = std::lower_bound(
      stream.actions.begin(), stream.actions.end(), target,
      [](const Avm1Action& action, std::size_t offset) { return action.offset < offset; });
  if (found == stream.actions.end()) {
    const Avm1Action& last = stream.actions.back();
    const std::size_t size = last.offset + last.length + stream.trailing.size();
    if (target != size) {
      message = "the branch targets offset " + std::to_string(target) + ", past the " +
                std::to_string(size) + "-byte stream";
      return Avm1RunStatus::BadBranch;
    }
    index = stream.actions.size();
    return Avm1RunStatus::Ok;
  }
  if (found->offset != target) {
    message =
        "the branch targets offset " + std::to_string(target) + ", which is not a record start";
    return Avm1RunStatus::BadBranch;
  }
  index = static_cast<std::size_t>(found - stream.actions.begin());
  return Avm1RunStatus::Ok;
}

Avm1RunStatus branch_if(const Avm1Stream& stream, const Avm1Action& action, Avm1State& state,
                        std::size_t& next, std::string& message) {
  Avm1Data condition;
  if (const Avm1RunStatus status = pop(state, condition, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (!to_boolean(condition)) {
    return Avm1RunStatus::Ok;
  }
  return jump_index(stream, std::get<Avm1Branch>(action.payload).target, next, message);
}

Avm1RunStatus execute_action(const Avm1Stream& stream, const Avm1Action& action, Avm1State& state,
                             std::size_t& next, std::string& message) {
  switch (action.code) {
    case 0x00:
      next = stream.actions.size();
      return Avm1RunStatus::Ok;
    case 0x0A:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left) + to_number(right));
      });
    case 0x0B:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left) - to_number(right));
      });
    case 0x0C:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left) * to_number(right));
      });
    case 0x0D:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left) / to_number(right));
      });
    case 0x0F:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_number(left) < to_number(right));
      });
    case 0x10:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_boolean(left) && to_boolean(right));
      });
    case 0x11:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_boolean(left) || to_boolean(right));
      });
    case 0x12:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return boolean_value(!to_boolean(operand));
      });
    case 0x13:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_text(left) == to_text(right));
      });
    case 0x14:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return number_value(static_cast<double>(to_text(operand).size()));
      });
    case 0x15:
      return extract_string(state, message);
    case 0x17: {
      Avm1Data discarded;
      return pop(state, discarded, message);
    }
    case 0x18:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        const double number = to_number(operand);
        return number_value(std::isnan(number) ? 0.0 : std::trunc(number));
      });
    case 0x1C:
      return get_variable(state, message);
    case 0x1D:
      return set_variable(state, message);
    case 0x21:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return string_value(to_text(left) + to_text(right));
      });
    case 0x26:
      return trace_value(state, message);
    case 0x29:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_text(left) < to_text(right));
      });
    case 0x3F:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return number_value(std::fmod(to_number(left), to_number(right)));
      });
    case 0x47:
      return apply_binary(state, message, add_values);
    case 0x48:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(less_than(left, right));
      });
    case 0x49:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(loose_equal(left, right));
      });
    case 0x4A:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return number_value(to_number(operand));
      });
    case 0x4B:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return string_value(to_text(operand));
      });
    case 0x4C:
      return duplicate_top(state, message);
    case 0x4D:
      return swap_top(state, message);
    case 0x50:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return number_value(to_number(operand) + 1.0);
      });
    case 0x51:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return number_value(to_number(operand) - 1.0);
      });
    case 0x67:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(less_than(right, left));
      });
    case 0x68:
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_text(left) > to_text(right));
      });
    case 0x88:
      state.constants = std::get<Avm1ConstantPool>(action.payload).strings;
      return Avm1RunStatus::Ok;
    case 0x96:
      return push_values(std::get<Avm1Push>(action.payload), state, message);
    case 0x99:
      return jump_index(stream, std::get<Avm1Branch>(action.payload).target, next, message);
    case 0x9D:
      return branch_if(stream, action, state, next, message);
    default:
      message = "this opcode is not in the AVM1 interpreter slice";
      return Avm1RunStatus::Unsupported;
  }
}

}  // namespace

Avm1Result execute_actions(const Avm1Stream& stream, std::size_t max_steps) {
  Avm1Result result;
  if (!stream.ok()) {
    result.status = Avm1RunStatus::InvalidStream;
    result.message = "the stream did not decode: " + stream.message;
    return result;
  }
  Avm1State state;
  std::size_t index = 0;
  while (index < stream.actions.size()) {
    const Avm1Action& action = stream.actions[index];
    result.opcode = action.code;
    result.offset = action.offset;
    if (result.steps == max_steps) {
      result.status = Avm1RunStatus::StepLimit;
      result.message = "the step limit of " + std::to_string(max_steps) + " records was reached";
      break;
    }
    result.steps += 1;
    std::size_t next = index + 1;
    std::string detail;
    Avm1RunStatus status = Avm1RunStatus::Ok;
    try {
      status = execute_action(stream, action, state, next, detail);
    } catch (const std::bad_alloc&) {
      status = Avm1RunStatus::ResourceLimit;
      detail = "the interpreter ran out of memory";
    } catch (const std::exception& error) {
      status = Avm1RunStatus::InvalidStream;
      detail = error.what();
    }
    if (status != Avm1RunStatus::Ok) {
      result.status = status;
      result.message = opcode_label(action.code) + ": " + detail;
      break;
    }
    index = next;
  }
  result.trace = std::move(state.trace);
  result.variables = std::move(state.variables);
  return result;
}

}  // namespace fo4::avm1
