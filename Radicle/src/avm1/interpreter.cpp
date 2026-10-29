#include <fo4/avm1/interpreter.hpp>

#include "control_ops.hpp"
#include "function_ops.hpp"
#include "misc_ops.hpp"
#include "object_ops.hpp"
#include "state.hpp"
#include "text_ops.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <variant>

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
    case Avm1RunStatus::Thrown:
      return "an exception was not caught";
  }
  return "unknown";
}

namespace {

using text_ops::encode_code_unit;
using text_ops::first_code_point;
using text_ops::kCodePointVersion;
using text_ops::to_number;
using text_ops::to_text;
using text_ops::to_uint32;
using text_ops::unit_count;
using text_ops::unit_offset;

constexpr std::size_t kMaxStringBytes = std::size_t{16} * 1024 * 1024;

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

bool is_reference(const Avm1Data& data) {
  return data.kind == Avm1DataKind::Object || data.kind == Avm1DataKind::Function;
}

std::string type_name(const Avm1Data& data) {
  switch (data.kind) {
    case Avm1DataKind::Undefined:
      return "undefined";
    case Avm1DataKind::Null:
      return "null";
    case Avm1DataKind::Boolean:
      return "boolean";
    case Avm1DataKind::Number:
      return "number";
    case Avm1DataKind::String:
      return "string";
    case Avm1DataKind::Object:
      return "object";
    case Avm1DataKind::Function:
      return "function";
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
    case Avm1DataKind::Object:
    case Avm1DataKind::Function:
      return true;
  }
  return false;
}

bool loose_equal(const Avm1Data& left, const Avm1Data& right, std::uint8_t swf_version) {
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
    if (left.kind == Avm1DataKind::Object) {
      return left.object == right.object;
    }
    if (left.kind == Avm1DataKind::Function) {
      return left.function == right.function;
    }
    return left.text == right.text;
  }
  if (left.kind == Avm1DataKind::Boolean) {
    return loose_equal(number_value(to_number(left, swf_version)), right, swf_version);
  }
  if (right.kind == Avm1DataKind::Boolean) {
    return loose_equal(left, number_value(to_number(right, swf_version)), swf_version);
  }
  if (is_reference(left)) {
    return loose_equal(string_value(to_text(left, swf_version)), right, swf_version);
  }
  if (is_reference(right)) {
    return loose_equal(left, string_value(to_text(right, swf_version)), swf_version);
  }
  return to_number(left, swf_version) == to_number(right, swf_version);
}

bool strict_equal(const Avm1Data& left, const Avm1Data& right) {
  if (left.kind != right.kind) {
    return false;
  }
  switch (left.kind) {
    case Avm1DataKind::Undefined:
    case Avm1DataKind::Null:
      return true;
    case Avm1DataKind::Boolean:
      return left.boolean == right.boolean;
    case Avm1DataKind::Number:
      return left.number == right.number;
    case Avm1DataKind::String:
      return left.text == right.text;
    case Avm1DataKind::Object:
      return left.object == right.object;
    case Avm1DataKind::Function:
      return left.function == right.function;
  }
  return false;
}

bool less_than(const Avm1Data& left, const Avm1Data& right, std::uint8_t swf_version) {
  if (left.kind == Avm1DataKind::String && right.kind == Avm1DataKind::String) {
    return left.text < right.text;
  }
  return to_number(left, swf_version) < to_number(right, swf_version);
}

Avm1Data add_values(const Avm1Data& left, const Avm1Data& right, std::uint8_t swf_version) {
  if (left.kind == Avm1DataKind::String || right.kind == Avm1DataKind::String ||
      is_reference(left) || is_reference(right)) {
    return string_value(to_text(left, swf_version) + to_text(right, swf_version));
  }
  return number_value(to_number(left, swf_version) + to_number(right, swf_version));
}

Avm1Data int32_value(std::uint32_t bits) {
  return number_value(static_cast<double>(static_cast<std::int32_t>(bits)));
}

double char_to_ascii(const Avm1Data& operand, std::uint8_t swf_version) {
  const std::string text = to_text(operand, swf_version);
  if (text.empty()) {
    return 0.0;
  }
  if (swf_version < 6) {
    return static_cast<unsigned char>(text.front());
  }
  return first_code_point(text);
}

Avm1Data ascii_to_char(const Avm1Data& operand, std::uint8_t swf_version) {
  const std::uint32_t value = to_uint32(operand, swf_version);
  if (swf_version < 6) {
    return string_value(std::string(1, static_cast<char>(value & 0xFFu)));
  }
  return string_value(encode_code_unit(value & 0xFFFFu));
}

std::string opcode_label(std::uint8_t code) {
  const char* name = action_mnemonic(code);
  return std::format("{} (0x{:02X})", name != nullptr ? name : "unknown",
                     static_cast<unsigned>(code));
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

Avm1RunStatus unsupported(std::string& message) {
  message = "this opcode is not in the AVM1 interpreter slice";
  return Avm1RunStatus::Unsupported;
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

Avm1RunStatus push_values(const Avm1Push& push, Avm1State& state, const Avm1Block& block,
                          std::string& message) {
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
        if (const Avm1RunStatus status =
                function_ops::push_register(block, state, value.index, message);
            status != Avm1RunStatus::Ok) {
          return status;
        }
        break;
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

Avm1RunStatus trace_value(Avm1State& state, std::string& message) {
  Avm1Data value;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  state.trace.push_back(to_text(value, state.swf_version));
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

std::size_t clamp_to_length(double number, std::size_t length) {
  if (std::isnan(number) || number <= 0.0) {
    return 0;
  }
  return number >= static_cast<double>(length) ? length : static_cast<std::size_t>(number);
}

Avm1RunStatus extract_string(Avm1State& state, std::string& message, std::uint8_t unit_version) {
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
  const std::uint8_t version = state.swf_version;
  const std::string source = to_text(text, version);
  const std::size_t units = unit_count(source, unit_version);
  const std::size_t start = clamp_to_length(to_number(index, version) - 1.0, units);
  const std::size_t length = clamp_to_length(to_number(count, version), units - start);
  const std::size_t from = unit_offset(source, start, unit_version);
  const std::size_t to = unit_offset(source, start + length, unit_version);
  return push_result(state, string_value(source.substr(from, to - from)), message);
}

Avm1RunStatus branch_if(const Avm1Block& block, const Avm1Action& action, Avm1State& state,
                        std::size_t& next, std::string& message) {
  Avm1Data condition;
  if (const Avm1RunStatus status = pop(state, condition, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (!to_boolean(condition)) {
    return Avm1RunStatus::Ok;
  }
  return jump_index(block, std::get<Avm1Branch>(action.payload).target, next, message);
}

Avm1RunStatus execute_action(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                             std::size_t& next, std::string& message) {
  Avm1State& state = run.state;
  const std::uint8_t version = state.swf_version;
  switch (action.code) {
    case 0x00:
      next = block.actions.size();
      return Avm1RunStatus::Ok;
    case 0x0A:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left, version) + to_number(right, version));
      });
    case 0x0B:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left, version) - to_number(right, version));
      });
    case 0x0C:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left, version) * to_number(right, version));
      });
    case 0x0D:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return number_value(to_number(left, version) / to_number(right, version));
      });
    case 0x0E:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_number(left, version) == to_number(right, version));
      });
    case 0x0F:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_number(left, version) < to_number(right, version));
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
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_text(left, version) == to_text(right, version));
      });
    case 0x14:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(static_cast<double>(unit_count(to_text(operand, version), version)));
      });
    case 0x15:
      return extract_string(state, message, version);
    case 0x31:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(
            static_cast<double>(unit_count(to_text(operand, version), kCodePointVersion)));
      });
    case 0x35:
      return extract_string(state, message, kCodePointVersion);
    case 0x36:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(static_cast<double>(first_code_point(to_text(operand, version))));
      });
    case 0x37:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return string_value(encode_code_unit(to_uint32(operand, version) & 0xFFFFu));
      });
    case 0x3A:
      return object_ops::delete_member(state, message);
    case 0x3B:
      return object_ops::delete_variable(state, message);
    case 0x3C:
      return function_ops::define_local(block, state, message);
    case 0x3D:
      return function_ops::call_function(run, block, message);
    case 0x3E:
      return function_ops::return_value(block, state, next, message);
    case 0x17: {
      Avm1Data discarded;
      return pop(state, discarded, message);
    }
    case 0x18:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        const double number = to_number(operand, version);
        return number_value(std::isnan(number) ? 0.0 : std::trunc(number));
      });
    case 0x1C:
      return function_ops::get_variable(block, state, message);
    case 0x1D:
      return function_ops::set_variable(block, state, message);
    case 0x21:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return string_value(to_text(left, version) + to_text(right, version));
      });
    case 0x26:
      return trace_value(state, message);
    case 0x29:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_text(left, version) < to_text(right, version));
      });
    case 0x32:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(char_to_ascii(operand, version));
      });
    case 0x33:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return ascii_to_char(operand, version);
      });
    case 0x3F:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return number_value(std::fmod(to_number(left, version), to_number(right, version)));
      });
    case 0x40:
      return object_ops::new_object(state, message);
    case 0x41:
      return function_ops::define_local2(block, state, message);
    case 0x42:
      return object_ops::init_array(state, message);
    case 0x43:
      return object_ops::init_object(state, message);
    case 0x44:
      return apply_unary(state, message, [](const Avm1Data& operand) {
        return string_value(type_name(operand));
      });
    case 0x47:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return add_values(left, right, version);
      });
    case 0x48:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(less_than(left, right, version));
      });
    case 0x49:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(loose_equal(left, right, version));
      });
    case 0x4A:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(to_number(operand, version));
      });
    case 0x4B:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return string_value(to_text(operand, version));
      });
    case 0x4C:
      return duplicate_top(state, message);
    case 0x4D:
      return swap_top(state, message);
    case 0x4E:
      return object_ops::get_member(state, message);
    case 0x4F:
      return object_ops::set_member(state, message);
    case 0x50:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(to_number(operand, version) + 1.0);
      });
    case 0x51:
      return apply_unary(state, message, [version](const Avm1Data& operand) {
        return number_value(to_number(operand, version) - 1.0);
      });
    case 0x52:
      return function_ops::call_method(run, message);
    case 0x53:
      return function_ops::new_method(run, message);
    case 0x60:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return int32_value(to_uint32(left, version) & to_uint32(right, version));
      });
    case 0x61:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return int32_value(to_uint32(left, version) | to_uint32(right, version));
      });
    case 0x62:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return int32_value(to_uint32(left, version) ^ to_uint32(right, version));
      });
    case 0x63:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return int32_value(to_uint32(left, version) << (to_uint32(right, version) & 31u));
      });
    case 0x64:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        const auto value = static_cast<std::int32_t>(to_uint32(left, version));
        return number_value(static_cast<double>(value >> (to_uint32(right, version) & 31u)));
      });
    case 0x65:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return number_value(
            static_cast<double>(to_uint32(left, version) >> (to_uint32(right, version) & 31u)));
      });
    case 0x66:
      if (version != 0 && version < 6) {
        return unsupported(message);
      }
      return apply_binary(state, message, [](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(strict_equal(left, right));
      });
    case 0x67:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(less_than(right, left, version));
      });
    case 0x68:
      return apply_binary(state, message, [version](const Avm1Data& left, const Avm1Data& right) {
        return boolean_value(to_text(left, version) > to_text(right, version));
      });
    case 0x87:
      return function_ops::store_register(block, state, std::get<Avm1Byte>(action.payload).value,
                                          message);
    case 0x88:
      state.constants = std::get<Avm1ConstantPool>(action.payload).strings;
      return Avm1RunStatus::Ok;
    case 0x8E:
    case 0x9B:
      return function_ops::define_function(run, block, action, next);
    case 0x96:
      return push_values(std::get<Avm1Push>(action.payload), state, block, message);
    case 0x99:
      return jump_index(block, std::get<Avm1Branch>(action.payload).target, next, message);
    case 0x9D:
      return branch_if(block, action, state, next, message);
    default:
      return misc_ops::execute(run, block, action, next, message);
  }
}

}  // namespace

Avm1RunStatus jump_index(const Avm1Block& block, std::size_t target, std::size_t& index,
                         std::string& message) {
  const auto found = std::lower_bound(
      block.actions.begin(), block.actions.end(), target,
      [](const Avm1Action& action, std::size_t offset) { return action.offset < offset; });
  if (found == block.actions.end()) {
    if (target != block.end_offset) {
      message = "the branch targets offset " + std::to_string(target) + ", past the " +
                std::to_string(block.end_offset) + "-byte end of its block";
      return Avm1RunStatus::BadBranch;
    }
    index = block.actions.size();
    return Avm1RunStatus::Ok;
  }
  if (found->offset != target) {
    message =
        "the branch targets offset " + std::to_string(target) + ", which is not a record start";
    return Avm1RunStatus::BadBranch;
  }
  index = static_cast<std::size_t>(found - block.actions.begin());
  return Avm1RunStatus::Ok;
}

Avm1RunStatus run_range(Avm1Run& run, const Avm1Block& block, std::size_t first, std::size_t last,
                        std::size_t& resume) {
  std::size_t index = first;
  while (index >= first && index < last) {
    const Avm1Action& action = block.actions[index];
    if (run.result.steps == run.max_steps) {
      run.result.opcode = action.code;
      run.result.offset = action.offset;
      run.result.status = Avm1RunStatus::StepLimit;
      run.result.message =
          "the step limit of " + std::to_string(run.max_steps) + " records was reached";
      return Avm1RunStatus::StepLimit;
    }
    run.result.steps += 1;
    std::size_t next = index + 1;
    std::string detail;
    Avm1RunStatus status = Avm1RunStatus::Ok;
    try {
      status = execute_action(run, block, action, next, detail);
    } catch (const std::bad_alloc&) {
      status = Avm1RunStatus::ResourceLimit;
      detail = "the interpreter ran out of memory";
    } catch (const std::exception& error) {
      status = Avm1RunStatus::InvalidStream;
      detail = error.what();
    }
    if (status != Avm1RunStatus::Ok) {
      if (status == Avm1RunStatus::Thrown || run.result.status == Avm1RunStatus::Ok) {
        run.result.opcode = action.code;
        run.result.offset = action.offset;
      }
      if (status != Avm1RunStatus::Thrown && run.result.status == Avm1RunStatus::Ok) {
        run.result.status = status;
        run.result.message = opcode_label(action.code) + ": " + detail;
      }
      return status;
    }
    run.result.opcode = action.code;
    run.result.offset = action.offset;
    index = next;
  }
  resume = index;
  return Avm1RunStatus::Ok;
}

Avm1RunStatus run_block(Avm1Run& run, const Avm1Block& block) {
  std::size_t resume = 0;
  return run_range(run, block, 0, block.actions.size(), resume);
}

Avm1Result execute_actions(const Avm1Stream& stream, std::size_t max_steps,
                           std::uint8_t swf_version, const Avm1Environment& environment) {
  Avm1Result result;
  if (!stream.ok()) {
    result.status = Avm1RunStatus::InvalidStream;
    result.message = "the stream did not decode: " + stream.message;
    return result;
  }
  Avm1State state;
  state.swf_version = swf_version;
  state.random_state = environment.random_seed;
  state.elapsed_ms = environment.elapsed_ms;
  Avm1Run run{state, result, max_steps};
  const std::size_t end = stream.actions.empty() ? 0
                                                 : stream.actions.back().offset +
                                                       stream.actions.back().length +
                                                       stream.trailing.size();
  std::vector<Avm1Data> scopes;
  if (run_block(run, Avm1Block{stream.actions, end, nullptr, &scopes}) == Avm1RunStatus::Thrown) {
    result.status = Avm1RunStatus::Thrown;
    result.message = "the thrown value was not caught";
  }
  result.trace = std::move(state.trace);
  result.variables = std::move(state.variables);
  return result;
}

}  // namespace fo4::avm1
