#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "fo4/c_api_papyrus.h"
#include "fo4/papyrus/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4::papyrus::Interpreter;
using fo4::papyrus::ObjectRef;
using fo4::papyrus::Status;
using fo4::papyrus::Struct;
using fo4::papyrus::Value;
using fo4::papyrus::ValueType;


namespace {

constexpr std::size_t kMaxValueArray = 1u << 20;

radicle_status make_value_impl(Value value, radicle_papyrus_value** out) {
  if (out == nullptr) return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_value is required");
  try {
    *out = new radicle_papyrus_value{std::move(value)};
  } catch (const std::bad_alloc&) {
    return radicle_c_detail::fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate a Papyrus value");
  }
  return RADICLE_STATUS_OK;
}

radicle_papyrus_status toCStatus(Status status) {
  switch (status) {
    case Status::Ok:
      return RADICLE_PAPYRUS_STATUS_OK;
    case Status::UnknownOpcode:
      return RADICLE_PAPYRUS_STATUS_UNKNOWN_OPCODE;
    case Status::UnsupportedOpcode:
      return RADICLE_PAPYRUS_STATUS_UNSUPPORTED_OPCODE;
    case Status::UnknownIdentifier:
      return RADICLE_PAPYRUS_STATUS_UNKNOWN_IDENTIFIER;
    case Status::TypeMismatch:
      return RADICLE_PAPYRUS_STATUS_TYPE_MISMATCH;
    case Status::DivisionByZero:
      return RADICLE_PAPYRUS_STATUS_DIVISION_BY_ZERO;
    case Status::IndexOutOfRange:
      return RADICLE_PAPYRUS_STATUS_INDEX_OUT_OF_RANGE;
    case Status::InvalidCount:
      return RADICLE_PAPYRUS_STATUS_INVALID_COUNT;
    case Status::ArrayTooLong:
      return RADICLE_PAPYRUS_STATUS_ARRAY_TOO_LONG;
    case Status::NullArray:
      return RADICLE_PAPYRUS_STATUS_NULL_ARRAY;
    case Status::NullObject:
      return RADICLE_PAPYRUS_STATUS_NULL_OBJECT;
    case Status::BadJumpTarget:
      return RADICLE_PAPYRUS_STATUS_BAD_JUMP_TARGET;
    case Status::StackUnderflow:
      return RADICLE_PAPYRUS_STATUS_STACK_UNDERFLOW;
    case Status::CallDepthExceeded:
      return RADICLE_PAPYRUS_STATUS_CALL_DEPTH_EXCEEDED;
    case Status::InstructionBudgetExceeded:
      return RADICLE_PAPYRUS_STATUS_INSTRUCTION_BUDGET_EXCEEDED;
    case Status::FunctionNotFound:
      return RADICLE_PAPYRUS_STATUS_FUNCTION_NOT_FOUND;
    case Status::ArgumentCountMismatch:
      return RADICLE_PAPYRUS_STATUS_ARGUMENT_COUNT_MISMATCH;
    case Status::HostError:
      return RADICLE_PAPYRUS_STATUS_HOST_ERROR;
    case Status::InternalError:
      return RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
    case Status::StringTooLong:
      return RADICLE_PAPYRUS_STATUS_STRING_TOO_LONG;
    case Status::GateRefused:
      return RADICLE_PAPYRUS_STATUS_GATE_REFUSED;
    case Status::Paused:
      return RADICLE_PAPYRUS_STATUS_PAUSED;
    case Status::Latent:
      return RADICLE_PAPYRUS_STATUS_LATENT;
  }
  return RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
}

using Getter = int32_t (*)(void*, const radicle_papyrus_object*, const char*, radicle_papyrus_value**);
using Setter = int32_t (*)(void*, const radicle_papyrus_object*, const char*, const radicle_papyrus_value*);
using Native = int32_t (*)(void*, const char*, const char*, const radicle_papyrus_value* const*, size_t,
                           radicle_papyrus_value**);

// Invariant: a missing callback is "not handled", so a standalone runtime still runs.
class CallbackHost final : public fo4::papyrus::Host {
 public:
  CallbackHost(const radicle_papyrus_callbacks& callbacks, const std::vector<const radicle_pex*>& files)
      : callbacks_(callbacks), files_(files) {}

  const fo4::pex::PexObject* findObject(const std::string& name) const override {
    for (const radicle_pex* pex : files_) {
      if (pex == nullptr) continue;
      for (const fo4::pex::PexObject& object : pex->value.objects) {
        if (fo4::papyrus::sameName(object.name, name)) return &object;
      }
    }
    return nullptr;
  }

  bool getProperty(const ObjectRef& object, const std::string& name, Value& out, Status& status) override {
    return readThrough(callbacks_.get_property, object, name, out, status);
  }

  bool setProperty(const ObjectRef& object, const std::string& name, const Value& value,
                   Status& status) override {
    return writeThrough(callbacks_.set_property, object, name, value, status);
  }

  bool getVariable(const ObjectRef& object, const std::string& name, Value& out, Status& status) override {
    return readThrough(callbacks_.get_variable, object, name, out, status);
  }

  bool setVariable(const ObjectRef& object, const std::string& name, const Value& value,
                   Status& status) override {
    return writeThrough(callbacks_.set_variable, object, name, value, status);
  }

  bool callNative(const std::string& script, const std::string& function,
                  const std::vector<Value>& args, Value& out, Status& status) override {
    const Native native = callbacks_.call_native;
    if (native == nullptr) {
      status = Status::FunctionNotFound;
      return false;
    }
    std::vector<radicle_papyrus_value> values;
    values.reserve(args.size());
    for (const Value& argument : args) values.push_back(radicle_papyrus_value{argument});
    std::vector<const radicle_papyrus_value*> pointers;
    pointers.reserve(values.size());
    for (const radicle_papyrus_value& value : values) pointers.push_back(&value);
    radicle_papyrus_value* produced = nullptr;
    const int32_t handled =
        native(callbacks_.user, script.c_str(), function.c_str(), pointers.data(), pointers.size(), &produced);
    if (handled == 0) {
      status = Status::FunctionNotFound;
      return false;
    }
    out = produced == nullptr ? fo4::papyrus::noneValue() : produced->value;
    delete produced;
    return true;
  }

  bool gate(std::string_view key) override {
    if (callbacks_.gate == nullptr) return true;
    const std::string text(key);
    return callbacks_.gate(callbacks_.user, text.c_str()) != 0;
  }

 private:
  bool readThrough(Getter getter, const ObjectRef& object, const std::string& name, Value& out,
                   Status& status) {
    if (getter == nullptr) {
      status = Status::UnknownIdentifier;
      return false;
    }
    radicle_papyrus_object handle{object};
    radicle_papyrus_value* produced = nullptr;
    if (getter(callbacks_.user, &handle, name.c_str(), &produced) == 0) {
      status = Status::UnknownIdentifier;
      return false;
    }
    out = produced == nullptr ? fo4::papyrus::noneValue() : produced->value;
    delete produced;
    return true;
  }

  bool writeThrough(Setter setter, const ObjectRef& object, const std::string& name, const Value& value,
                    Status& status) {
    if (setter == nullptr) {
      status = Status::UnknownIdentifier;
      return false;
    }
    radicle_papyrus_object handle{object};
    radicle_papyrus_value stored{value};
    if (setter(callbacks_.user, &handle, name.c_str(), &stored) == 0) {
      status = Status::UnknownIdentifier;
      return false;
    }
    return true;
  }

  radicle_papyrus_callbacks callbacks_;
  const std::vector<const radicle_pex*>& files_;
};

}  // namespace

namespace radicle_c_papyrus_detail {

radicle_status make_value(Value value, radicle_papyrus_value** out) {
  return make_value_impl(std::move(value), out);
}

radicle_papyrus_status to_c_status(Status status) { return toCStatus(status); }

Status from_c_status(radicle_papyrus_status status) {
  if (static_cast<int>(status) < 0 || static_cast<int>(status) > static_cast<int>(RADICLE_PAPYRUS_STATUS_LATENT)) {
    return Status::InternalError;
  }
  return static_cast<Status>(static_cast<std::uint8_t>(status));
}

}  // namespace radicle_c_papyrus_detail

struct radicle_papyrus {
  radicle_papyrus_callbacks callbacks{};
  std::vector<const radicle_pex*> pex_files;
  CallbackHost host;
  Interpreter interpreter;

  explicit radicle_papyrus(const radicle_papyrus_callbacks& supplied)
      : callbacks(supplied), host(callbacks, pex_files), interpreter(host) {}
};

extern "C" {

radicle_status radicle_papyrus_create(const radicle_papyrus_callbacks* callbacks,
                                      radicle_papyrus** out_runtime) {
  radicle_c_detail::clear_error();
  if (out_runtime == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_runtime is required");
  }
  *out_runtime = nullptr;
  return radicle_c_env_detail::guarded("unknown exception while creating the Papyrus runtime", [&] {
    radicle_papyrus_callbacks supplied{};
    if (callbacks != nullptr) supplied = *callbacks;
    auto runtime = std::make_unique<radicle_papyrus>(supplied);
    *out_runtime = runtime.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_destroy(radicle_papyrus* runtime) {
  radicle_c_detail::clear_error();
  delete runtime;
}

radicle_status radicle_papyrus_add_pex(radicle_papyrus* runtime, const radicle_pex* pex) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || pex == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and pex are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while adding a pex file", [&] {
    runtime->pex_files.push_back(pex);
    return RADICLE_STATUS_OK;
  });
}

size_t radicle_papyrus_pex_count(const radicle_papyrus* runtime) {
  radicle_c_detail::clear_error();
  return runtime == nullptr ? 0 : runtime->pex_files.size();
}

radicle_status radicle_papyrus_run(radicle_papyrus* runtime, const char* script, const char* function,
                                   const radicle_papyrus_value* const* args, size_t arg_count,
                                   radicle_papyrus_value** out_result,
                                   radicle_papyrus_status* out_status, size_t* out_instructions) {
  radicle_c_detail::clear_error();
  if (out_result != nullptr) *out_result = nullptr;
  if (out_status != nullptr) *out_status = RADICLE_PAPYRUS_STATUS_OK;
  if (out_instructions != nullptr) *out_instructions = 0;
  if (runtime == nullptr || script == nullptr || function == nullptr || out_result == nullptr ||
      out_status == nullptr || (args == nullptr && arg_count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, script, function, out_result, out_status and args are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while running a Papyrus function", [&] {
    std::vector<Value> values;
    values.reserve(arg_count);
    for (size_t i = 0; i < arg_count; ++i) {
      if (args[i] == nullptr) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "argument handles must not be null");
      }
      values.push_back(args[i]->value);
    }
    const fo4::papyrus::Result result = runtime->interpreter.runFunction(script, function, values);
    *out_status = toCStatus(result.status);
    if (out_instructions != nullptr) *out_instructions = result.instructions;
    if (result.status != Status::Ok) return RADICLE_STATUS_OK;
    return radicle_c_papyrus_detail::make_value(result.value, out_result);
  });
}

const char* radicle_papyrus_status_name(radicle_papyrus_status status) {
  radicle_c_detail::clear_error();
  switch (status) {
    case RADICLE_PAPYRUS_STATUS_OK:
      return "Ok";
    case RADICLE_PAPYRUS_STATUS_UNKNOWN_OPCODE:
      return "UnknownOpcode";
    case RADICLE_PAPYRUS_STATUS_UNSUPPORTED_OPCODE:
      return "UnsupportedOpcode";
    case RADICLE_PAPYRUS_STATUS_UNKNOWN_IDENTIFIER:
      return "UnknownIdentifier";
    case RADICLE_PAPYRUS_STATUS_TYPE_MISMATCH:
      return "TypeMismatch";
    case RADICLE_PAPYRUS_STATUS_DIVISION_BY_ZERO:
      return "DivisionByZero";
    case RADICLE_PAPYRUS_STATUS_INDEX_OUT_OF_RANGE:
      return "IndexOutOfRange";
    case RADICLE_PAPYRUS_STATUS_INVALID_COUNT:
      return "InvalidCount";
    case RADICLE_PAPYRUS_STATUS_ARRAY_TOO_LONG:
      return "ArrayTooLong";
    case RADICLE_PAPYRUS_STATUS_NULL_ARRAY:
      return "NullArray";
    case RADICLE_PAPYRUS_STATUS_NULL_OBJECT:
      return "NullObject";
    case RADICLE_PAPYRUS_STATUS_BAD_JUMP_TARGET:
      return "BadJumpTarget";
    case RADICLE_PAPYRUS_STATUS_STACK_UNDERFLOW:
      return "StackUnderflow";
    case RADICLE_PAPYRUS_STATUS_CALL_DEPTH_EXCEEDED:
      return "CallDepthExceeded";
    case RADICLE_PAPYRUS_STATUS_INSTRUCTION_BUDGET_EXCEEDED:
      return "InstructionBudgetExceeded";
    case RADICLE_PAPYRUS_STATUS_FUNCTION_NOT_FOUND:
      return "FunctionNotFound";
    case RADICLE_PAPYRUS_STATUS_ARGUMENT_COUNT_MISMATCH:
      return "ArgumentCountMismatch";
    case RADICLE_PAPYRUS_STATUS_HOST_ERROR:
      return "HostError";
    case RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR:
      return "InternalError";
    case RADICLE_PAPYRUS_STATUS_STRING_TOO_LONG:
      return "StringTooLong";
    case RADICLE_PAPYRUS_STATUS_GATE_REFUSED:
      return "GateRefused";
    case RADICLE_PAPYRUS_STATUS_PAUSED:
      return "Paused";
    case RADICLE_PAPYRUS_STATUS_LATENT:
      return "Latent";
  }
  return "Unknown";
}

uint64_t radicle_papyrus_object_handle(const radicle_papyrus_object* object) {
  radicle_c_detail::clear_error();
  return object == nullptr ? 0 : object->ref.handle;
}

const char* radicle_papyrus_object_type(const radicle_papyrus_object* object) {
  radicle_c_detail::clear_error();
  return object == nullptr ? nullptr : object->ref.type.c_str();
}

const char* radicle_papyrus_object_state(const radicle_papyrus_object* object) {
  radicle_c_detail::clear_error();
  return object == nullptr ? nullptr : object->ref.state.c_str();
}

void radicle_papyrus_value_destroy(radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  delete value;
}

radicle_papyrus_value_kind radicle_papyrus_value_kind_of(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr) return RADICLE_PAPYRUS_VALUE_NONE;
  switch (value->value.type) {
    case ValueType::None:
      return RADICLE_PAPYRUS_VALUE_NONE;
    case ValueType::String:
      return RADICLE_PAPYRUS_VALUE_STRING;
    case ValueType::Integer:
      return RADICLE_PAPYRUS_VALUE_INT;
    case ValueType::Float:
      return RADICLE_PAPYRUS_VALUE_FLOAT;
    case ValueType::Bool:
      return RADICLE_PAPYRUS_VALUE_BOOL;
    case ValueType::Object:
      return RADICLE_PAPYRUS_VALUE_OBJECT;
    case ValueType::Array:
      return RADICLE_PAPYRUS_VALUE_ARRAY;
    case ValueType::Struct:
      return RADICLE_PAPYRUS_VALUE_STRUCT;
  }
  return RADICLE_PAPYRUS_VALUE_NONE;
}

radicle_status radicle_papyrus_value_none(radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  return radicle_c_papyrus_detail::make_value(fo4::papyrus::noneValue(), out_value);
}

radicle_status radicle_papyrus_value_string(const char* text, size_t size, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (text == nullptr && size != 0) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is required when size is not zero");
  }
  return radicle_c_papyrus_detail::make_value(fo4::papyrus::stringValue(size == 0 ? std::string() : std::string(text, size)), out_value);
}

radicle_status radicle_papyrus_value_int(int32_t number, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  return radicle_c_papyrus_detail::make_value(fo4::papyrus::integerValue(number), out_value);
}

radicle_status radicle_papyrus_value_float(float number, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  return radicle_c_papyrus_detail::make_value(fo4::papyrus::floatValue(number), out_value);
}

radicle_status radicle_papyrus_value_bool(int32_t flag, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  return radicle_c_papyrus_detail::make_value(fo4::papyrus::boolValue(flag != 0), out_value);
}

radicle_status radicle_papyrus_value_object(uint64_t handle, const char* type, const char* state,
                                            radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (type == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "type is required");
  }
  return radicle_c_papyrus_detail::make_value(fo4::papyrus::objectValue(handle, type, state == nullptr ? std::string() : std::string(state)),
                   out_value);
}

radicle_status radicle_papyrus_value_array(size_t count, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (count > kMaxValueArray) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "array count is too large");
  }
  return radicle_c_env_detail::guarded("unknown exception while creating an array value", [&] {
    auto array = std::make_shared<fo4::papyrus::Array>();
    array->elements.reserve(count);
    for (size_t i = 0; i < count; ++i) array->elements.push_back(fo4::papyrus::noneValue());
    return radicle_c_papyrus_detail::make_value(fo4::papyrus::arrayValue(std::move(array)), out_value);
  });
}

size_t radicle_papyrus_value_array_count(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Array || value->value.array == nullptr) return 0;
  return value->value.array->elements.size();
}

radicle_status radicle_papyrus_value_array_set(radicle_papyrus_value* value, size_t index,
                                               const radicle_papyrus_value* element) {
  radicle_c_detail::clear_error();
  if (value == nullptr || element == nullptr || value->value.type != ValueType::Array ||
      value->value.array == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "an array value and element are required");
  }
  if (index >= value->value.array->elements.size()) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "array index is out of range");
  }
  value->value.array->elements[index] = element->value;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_value_array_get(const radicle_papyrus_value* value, size_t index,
                                               radicle_papyrus_value** out_element) {
  radicle_c_detail::clear_error();
  if (out_element != nullptr) *out_element = nullptr;
  if (value == nullptr || out_element == nullptr || value->value.type != ValueType::Array ||
      value->value.array == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "an array value and out_element are required");
  }
  if (index >= value->value.array->elements.size()) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "array index is out of range");
  }
  return radicle_c_papyrus_detail::make_value(value->value.array->elements[index], out_element);
}

radicle_status radicle_papyrus_value_struct(const char* type_name, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (type_name == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "type_name is required");
  }
  return radicle_c_env_detail::guarded("unknown exception while creating a struct value", [&] {
    auto structure = std::make_shared<Struct>();
    structure->typeName = type_name;
    return radicle_c_papyrus_detail::make_value(fo4::papyrus::structValue(std::move(structure)), out_value);
  });
}

radicle_status radicle_papyrus_value_struct_set(radicle_papyrus_value* value, const char* member,
                                                const radicle_papyrus_value* member_value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || member == nullptr || member_value == nullptr ||
      value->value.type != ValueType::Struct || value->value.structure == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "a struct value, member name and member value are required");
  }
  value->value.structure->members[fo4::papyrus::lowerAscii(member)] = member_value->value;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_value_struct_get(const radicle_papyrus_value* value, const char* member,
                                                radicle_papyrus_value** out_member) {
  radicle_c_detail::clear_error();
  if (out_member != nullptr) *out_member = nullptr;
  if (value == nullptr || member == nullptr || out_member == nullptr ||
      value->value.type != ValueType::Struct || value->value.structure == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "a struct value, member name and out_member are required");
  }
  const auto found = value->value.structure->members.find(fo4::papyrus::lowerAscii(member));
  if (found == value->value.structure->members.end()) {
    return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the struct has no such member");
  }
  return radicle_c_papyrus_detail::make_value(found->second, out_member);
}

int32_t radicle_papyrus_value_int_of(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Integer) return 0;
  return value->value.integer;
}

float radicle_papyrus_value_float_of(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Float) return 0.0F;
  return value->value.real;
}

int32_t radicle_papyrus_value_bool_of(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Bool) return 0;
  return value->value.boolean ? 1 : 0;
}

const char* radicle_papyrus_value_text(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::String) return nullptr;
  return value->value.str.c_str();
}

uint64_t radicle_papyrus_value_object_handle(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Object || value->value.object == nullptr) return 0;
  return value->value.object->handle;
}

const char* radicle_papyrus_value_object_type(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Object || value->value.object == nullptr) return nullptr;
  return value->value.object->type.c_str();
}

const char* radicle_papyrus_value_object_state(const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (value == nullptr || value->value.type != ValueType::Object || value->value.object == nullptr) return nullptr;
  return value->value.object->state.c_str();
}

}  // extern "C"
