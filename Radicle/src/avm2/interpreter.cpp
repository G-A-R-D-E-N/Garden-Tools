#include <fo4/avm2/interpreter.hpp>

#include "interpreter_internal.hpp"

#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/display.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

using namespace detail;

const char* interpreter_status_name(InterpreterStatus status) noexcept {
  switch (status) {
    case InterpreterStatus::Ok:
      return "ok";
    case InterpreterStatus::NotImplemented:
      return "not implemented";
    case InterpreterStatus::NotLinked:
      return "not linked";
    case InterpreterStatus::InvalidArgument:
      return "invalid argument";
    case InterpreterStatus::StackUnderflow:
      return "stack underflow";
    case InterpreterStatus::StackOverflow:
      return "stack overflow";
    case InterpreterStatus::LocalOutOfRange:
      return "local out of range";
    case InterpreterStatus::ScopeUnderflow:
      return "scope underflow";
    case InterpreterStatus::NullReference:
      return "null reference";
    case InterpreterStatus::NoSuchProperty:
      return "no such property";
    case InterpreterStatus::CannotCall:
      return "value is not callable";
    case InterpreterStatus::CannotConstruct:
      return "value is not constructable";
    case InterpreterStatus::TypeError:
      return "type error";
    case InterpreterStatus::UncaughtException:
      return "uncaught exception";
    case InterpreterStatus::InstructionBudget:
      return "instruction budget exceeded";
    case InterpreterStatus::CallDepthExceeded:
      return "call depth exceeded";
    case InterpreterStatus::InstructionIndexOutOfRange:
      return "instruction index out of range";
    case InterpreterStatus::MethodIndexOutOfRange:
      return "method index out of range";
    case InterpreterStatus::ClassIndexOutOfRange:
      return "class index out of range";
    case InterpreterStatus::ScopeDepthExceeded:
      return "scope depth exceeded";
    case InterpreterStatus::ResourceLimit:
      return "resource limit exceeded";
  }
  return "unknown";
}

Interpreter::Interpreter(Runtime& runtime, InterpreterLimits limits)
    : runtime_(runtime), limits_(limits) {}

ExecutionResult Interpreter::fail(InterpreterStatus status, std::string message) const {
  ExecutionResult result;
  result.status = status;
  result.message = std::move(message);
  result.instructions = instructions_;
  result.frames = frames_;
  result.opcode = current_opcode_;
  result.offset = current_offset_;
  return result;
}

const Multiname* Interpreter::resolve_multiname(const abc::AbcFile& file, std::uint32_t index) {
  const auto key = std::make_pair(&file, index);
  const auto found = multinames_.find(key);
  if (found != multinames_.end()) {
    return &found->second;
  }
  Multiname multiname;
  if (fo4::avm2::resolve_multiname(file, index, multiname) != RuntimeStatus::Ok) {
    return nullptr;
  }
  const auto inserted = multinames_.emplace(key, std::move(multiname));
  return &inserted.first->second;
}

const Class* Interpreter::class_at(const abc::AbcFile& file, std::uint32_t instance_index) {
  return runtime_.class_at(file, instance_index);
}

InterpreterStatus Interpreter::push(Frame& frame, Value value, std::string& message) {
  if (frame.stack.size() >= limits_.max_stack) {
    message = "operand stack limit " + std::to_string(limits_.max_stack) + " exceeded";
    return InterpreterStatus::StackOverflow;
  }
  frame.stack.push_back(std::move(value));
  return InterpreterStatus::Ok;
}

InterpreterStatus Interpreter::pop(Frame& frame, Value& value, std::string& message) {
  if (frame.stack.empty()) {
    message = "operand stack is empty";
    return InterpreterStatus::StackUnderflow;
  }
  value = std::move(frame.stack.back());
  frame.stack.pop_back();
  return InterpreterStatus::Ok;
}

InterpreterStatus Interpreter::pop_arguments(Frame& frame, std::size_t count,
                                             std::vector<Value>& out, std::string& message) {
  if (frame.stack.size() < count) {
    message = "needs " + std::to_string(count) + " arguments but the stack holds " +
              std::to_string(frame.stack.size());
    return InterpreterStatus::StackUnderflow;
  }
  out.resize(count);
  for (std::size_t index = count; index-- > 0;) {
    out[index] = std::move(frame.stack.back());
    frame.stack.pop_back();
  }
  return InterpreterStatus::Ok;
}

std::uint32_t Interpreter::instruction_index_at(const Frame& frame, std::int32_t offset) const {
  const auto found = std::lower_bound(frame.instruction_offsets.begin(),
                                      frame.instruction_offsets.end(),
                                      static_cast<std::uint32_t>(offset));
  if (found == frame.instruction_offsets.end() ||
      *found != static_cast<std::uint32_t>(offset)) {
    return static_cast<std::uint32_t>(frame.instruction_offsets.size());
  }
  return static_cast<std::uint32_t>(found - frame.instruction_offsets.begin());
}

InterpreterStatus Interpreter::jump_to_offset(Frame& frame, std::int32_t offset,
                                              std::string& message) {
  const std::uint32_t target = instruction_index_at(frame, offset);
  if (target >= frame.instructions.size()) {
    message = "jump target " + std::to_string(offset) + " is not an instruction boundary";
    return InterpreterStatus::InstructionIndexOutOfRange;
  }
  frame.ip = target;
  return InterpreterStatus::Ok;
}

ExecutionResult Interpreter::invoke(const FunctionObject& function, Value this_value,
                                    std::vector<Value> arguments,
                                    const std::vector<std::shared_ptr<Object>>& scopes,
                                    std::size_t depth) {
  if (!runtime_.linked()) {
    return fail(InterpreterStatus::NotLinked, "the runtime is not linked");
  }
  if (function.bound_receiver() != nullptr) {
    this_value = Value::object(function.bound_receiver());
  }
  const abc::AbcFile* file = function.file();
  const abc::AbcMethodBody* body = function.body();
  if (file == nullptr || body == nullptr) {
    if (function.native_id() != kNoNative) {
      return call_native_id(function.native_id(), this_value, arguments, depth);
    }
    if (limits_.lenient_builtins) {
      return ExecutionResult{};
    }
    return fail(InterpreterStatus::NotImplemented,
                "native function " + function.name() + " is not bound");
  }
  if (depth >= limits_.max_call_depth) {
    return fail(InterpreterStatus::CallDepthExceeded,
                "call depth " + std::to_string(depth) + " reached the limit " +
                    std::to_string(limits_.max_call_depth));
  }
  Frame frame;
  frame.file = file;
  frame.body = body;
  frame.method_index = function.method_index();
  frame.this_value = std::move(this_value);
  frame.owner_class = function.owner_class();
  frame.outer_scope = &function.captured_scope();
  if (abc::decode_code(body->code, frame.instructions, body->exceptions) != abc::AbcStatus::Ok) {
    return fail(InterpreterStatus::InvalidArgument,
                "method " + function.name() + " does not decode");
  }
  frame.instruction_offsets.reserve(frame.instructions.size());
  for (const abc::AbcInstruction& instruction : frame.instructions) {
    frame.instruction_offsets.push_back(static_cast<std::uint32_t>(instruction.offset));
  }
  const abc::AbcMethodInfo* info = function.method_info();
  const std::size_t local_count = std::max<std::size_t>(
      body->local_count, arguments.size() + 1);
  frame.locals.assign(local_count, Value::undefined());
  if (!frame.locals.empty()) {
    frame.locals[0] = frame.this_value;
  }
  if (info != nullptr) {
    std::string message;
    const InterpreterStatus bound =
        bind_arguments(frame, *info, function.name(), arguments, message);
    if (bound != InterpreterStatus::Ok) {
      return fail(bound, std::move(message));
    }
  }
  if (scopes.empty()) {
    if (!function.captured_scope().empty()) {
      frame.scopes = function.captured_scope();
    } else {
      const std::shared_ptr<Object>& global = runtime_.global_object_shared();
      if (global != nullptr) {
        frame.scopes.push_back(global);
      }
    }
  } else {
    frame.scopes = scopes;
  }
  if (frame.owner_class != nullptr && frame.owner_class->class_object != nullptr) {
    if (frame.scopes.empty() || frame.scopes.front().get() != runtime_.global_object()) {
      frame.scopes.insert(frame.scopes.begin(), runtime_.global_object_shared());
    }
    frame.scopes.push_back(frame.owner_class->class_object);
  }
  frame.entry_scope_count = frame.scopes.size();
  ++frames_;
  ExecutionResult result = execute_frame(frame, depth + 1);
  frames_ -= 1;
  return result;
}

InterpreterStatus Interpreter::coerce_parameter(const abc::AbcFile& file, std::uint32_t type_index,
                                                Value& value, std::string& message) {
  Multiname type;
  if (type_index == 0 ||
      fo4::avm2::resolve_multiname(file, type_index, type) != RuntimeStatus::Ok ||
      type.kind == MultinameKind::TypeName) {
    return InterpreterStatus::Ok;
  }
  const std::string name = property_text(type);
  const bool primitive = name == "int" || name == "uint" || name == "Number" ||
                         name == "double" || name == "Boolean" || name == "String";
  if (!primitive && name != "*" && name != "Object" && runtime_.find_class(type) == nullptr) {
    return InterpreterStatus::Ok;
  }
  return coerce_value(type, value, message);
}

InterpreterStatus Interpreter::bind_arguments(Frame& frame, const abc::AbcMethodInfo& info,
                                              const std::string& name,
                                              std::vector<Value>& arguments,
                                              std::string& message) {
  const std::size_t declared = info.param_count;
  const std::size_t optional = info.has_optional() ? std::min(info.options.size(), declared) : 0;
  const std::size_t required = declared - optional;
  if (arguments.size() < required) {
    message = "argument count mismatch on " + name + ": expected " + std::to_string(required) +
              ", got " + std::to_string(arguments.size());
    return InterpreterStatus::InvalidArgument;
  }
  std::vector<Value> all_arguments;
  if (info.needs_arguments()) {
    all_arguments = arguments;
  }
  for (std::size_t index = 0; index < declared && index + 1 < frame.locals.size(); ++index) {
    Value value = Value::undefined();
    if (index < arguments.size()) {
      value = std::move(arguments[index]);
    } else if (index - required < info.options.size()) {
      const abc::AbcOptionalValue& option = info.options[index - required];
      value = fo4::avm2::constant_value(*frame.file, option.kind, option.index);
    }
    const std::uint32_t type_index = index < info.param_types.size() ? info.param_types[index] : 0u;
    const InterpreterStatus coerced = coerce_parameter(*frame.file, type_index, value, message);
    if (coerced != InterpreterStatus::Ok) {
      return coerced;
    }
    frame.locals[index + 1] = std::move(value);
  }
  if (declared + 1 < frame.locals.size()) {
    if (info.needs_arguments()) {
      frame.locals[declared + 1] = Value::object(make_array(std::move(all_arguments)));
    } else if (info.needs_rest()) {
      std::vector<Value> rest;
      for (std::size_t index = declared; index < arguments.size(); ++index) {
        rest.push_back(std::move(arguments[index]));
      }
      frame.locals[declared + 1] = Value::object(make_array(std::move(rest)));
    }
  }
  return InterpreterStatus::Ok;
}

ExecutionResult Interpreter::ensure_class_initialized(Class& klass, std::size_t depth) {
  if (klass.initialization != ClassInitializationState::Uninitialized) {
    return ExecutionResult{};
  }
  if (klass.external || klass.file == nullptr || klass.cinit == kNoMethod) {
    klass.initialization = ClassInitializationState::Initialized;
    return ExecutionResult{};
  }
  klass.initialization = ClassInitializationState::Initializing;
  auto initializer = std::make_shared<FunctionObject>(klass.file->method_name(klass.cinit),
                                                      klass.file, klass.cinit);
  initializer->set_owner_class(&klass);
  ExecutionResult result = invoke(*initializer, Value::object(klass.class_object), {}, {}, depth);
  klass.initialization = ClassInitializationState::Initialized;
  return result;
}

ExecutionResult Interpreter::initialize_class_value(const Value& value, std::size_t depth) {
  if (!value.is_object() || value.as_object()->kind() != ObjectKind::Class) {
    return ExecutionResult{};
  }
  Class* klass = static_cast<const ClassObject&>(*value.as_object()).represented_class();
  if (klass == nullptr) {
    return ExecutionResult{};
  }
  return ensure_class_initialized(*klass, depth);
}

ExecutionResult Interpreter::execute_frame(Frame& frame, std::size_t depth) {
  while (frame.ip < frame.instructions.size()) {
    const abc::AbcInstruction& instruction = frame.instructions[frame.ip];
    current_opcode_ = instruction.opcode;
    current_offset_ = static_cast<std::uint32_t>(instruction.offset);
    ++instructions_;
    if (instructions_ > limits_.max_instructions) {
      return fail(InterpreterStatus::InstructionBudget,
                  "instruction budget " + std::to_string(limits_.max_instructions) + " exceeded");
    }
    frame.ip += 1;
    std::string message;
    InterpreterStatus status = InterpreterStatus::Ok;
    const auto push_value = [&](Value value) {
      status = push(frame, std::move(value), message);
    };
    const auto pop_value = [&](Value& value) {
      status = pop(frame, value, message);
    };
    const auto finish = [&](Value value) {
      ExecutionResult result;
      result.value = std::move(value);
      result.instructions = instructions_;
      result.frames = frames_;
      return result;
    };
    switch (instruction.opcode) {
#include "interpreter_dispatch_a.inc"
#include "interpreter_dispatch_b.inc"
#include "interpreter_dispatch_c.inc"
#include "interpreter_dispatch_d.inc"
#include "interpreter_dispatch_e.inc"
      default:
        return fail(InterpreterStatus::NotImplemented,
                    std::string{"opcode "} + instruction.mnemonic + " is not implemented yet");
    }
    if (status != InterpreterStatus::Ok) {
      return fail(status, std::move(message));
    }
  }
  return fail(InterpreterStatus::TypeError, "method ended without a return");
}

ExecutionResult Interpreter::run_script(std::size_t script_index) {
  if (!runtime_.linked()) {
    return fail(InterpreterStatus::NotLinked, "the runtime is not linked");
  }
  if (script_index >= runtime_.scripts().size()) {
    return fail(InterpreterStatus::MethodIndexOutOfRange,
                "script index " + std::to_string(script_index) + " is out of range");
  }
  const ScriptEntry& script = runtime_.scripts()[script_index];
  if (script.file == nullptr) {
    return fail(InterpreterStatus::InvalidArgument, "script has no ABC file");
  }
  FunctionObject initializer(script.file->method_name(script.init_method), script.file,
                             script.init_method);
  Object* global = runtime_.global_object();
  const Value this_value =
      global == nullptr ? Value::undefined() : Value::object(runtime_.global_object_shared());
  return invoke(initializer, this_value, {}, {}, 0);
}

ExecutionResult Interpreter::run_function(const FunctionObject& function, Value this_value,
                                          std::vector<Value> arguments) {
  return invoke(function, std::move(this_value), std::move(arguments), {}, 0);
}

}  // namespace fo4::avm2
