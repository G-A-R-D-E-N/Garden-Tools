#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "c_api_papyrus_runtime_internal.hpp"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/papyrus/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using fo4::papyrus::EngineObjectHooks;
using fo4::papyrus::NativeCall;
using fo4::papyrus::NativeRegistration;
using fo4::papyrus::NativeResult;
using fo4::papyrus::NativeTargetKind;
using fo4::papyrus::ObjectRef;
using fo4::papyrus::OverrideResult;
using fo4::papyrus::Status;
using fo4::papyrus::Value;
using fo4::papyrus::ValueType;

namespace {

NativeTargetKind nativeKindFrom(radicle_papyrus_native_kind kind) {
  switch (kind) {
    case RADICLE_PAPYRUS_NATIVE_SCRIPT_INSTANCE:
      return NativeTargetKind::ScriptInstance;
    case RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT:
      return NativeTargetKind::EngineObject;
    case RADICLE_PAPYRUS_NATIVE_STATIC:
      break;
  }
  return NativeTargetKind::Static;
}

}  // namespace

extern "C" {

radicle_status radicle_papyrus_runtime_register_native(radicle_papyrus_runtime* runtime, const char* script,
                                                       const char* function,
                                                       radicle_papyrus_native_kind kind,
                                                       const char* required_script, int32_t latent) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || script == nullptr || function == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, script and function are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while registering a native", [&] {
    NativeRegistration registration;
    registration.kind = nativeKindFrom(kind);
    registration.requiredScript = required_script == nullptr ? std::string() : std::string(required_script);
    radicle_papyrus_runtime* raw = runtime;
    if (latent != 0) {
      registration.latent = true;
      registration.begin = [raw, script = std::string(script), function = std::string(function)](
                                const std::vector<Value>& args, Status& status) -> std::uint64_t {
        if (raw->callbacks.begin_latent == nullptr) {
          status = Status::FunctionNotFound;
          return 0;
        }
        std::vector<radicle_papyrus_value> values;
        values.reserve(args.size());
        for (const Value& argument : args) values.push_back(radicle_papyrus_value{argument});
        std::vector<const radicle_papyrus_value*> pointers;
        pointers.reserve(values.size());
        for (const radicle_papyrus_value& value : values) pointers.push_back(&value);
        const std::int64_t token = raw->callbacks.begin_latent(raw->callbacks.user, script.c_str(),
                                                              function.c_str(), pointers.data(),
                                                              pointers.size());
        return token > 0 ? static_cast<std::uint64_t>(token) : 0;
      };
    }
    registration.body = [raw, script = std::string(script), function = std::string(function)](
                            const Value&, const std::vector<Value>& args, Value& out, Status& status) {
      if (raw->callbacks.call_native == nullptr) {
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
      const int32_t handled = raw->callbacks.call_native(raw->callbacks.user, script.c_str(),
                                                         function.c_str(), pointers.data(),
                                                         pointers.size(), &produced);
      if (handled == 0) {
        status = Status::FunctionNotFound;
        return false;
      }
      out = produced == nullptr ? fo4::papyrus::noneValue() : produced->value;
      delete produced;
      return true;
    };
    raw->runtime.registerNative(script, function, std::move(registration));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_register_override(radicle_papyrus_runtime* runtime,
                                                         const char* script, const char* function,
                                                         int32_t parameter_count) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || script == nullptr || function == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, script and function are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while registering an override", [&] {
    radicle_papyrus_runtime* raw = runtime;
    raw->runtime.registerOverride(
        script, function, parameter_count,
        [raw](const Value& self, const std::string& name, const std::vector<Value>& args) {
          if (raw->callbacks.override_function == nullptr) return OverrideResult::notRegistered();
          std::vector<radicle_papyrus_value> values;
          values.reserve(args.size());
          for (const Value& argument : args) values.push_back(radicle_papyrus_value{argument});
          std::vector<const radicle_papyrus_value*> pointers;
          pointers.reserve(values.size());
          for (const radicle_papyrus_value& value : values) pointers.push_back(&value);
          int32_t outcome = RADICLE_PAPYRUS_OVERRIDE_NOT_REGISTERED;
          radicle_papyrus_value* produced = nullptr;
          const std::uint64_t instance = self.type == fo4::papyrus::ValueType::Object && self.object != nullptr
                                             ? self.object->handle
                                             : 0;
          raw->callbacks.override_function(raw->callbacks.user, instance, name.c_str(), pointers.data(),
                                           pointers.size(), &outcome, &produced);
          OverrideResult result;
          switch (outcome) {
            case RADICLE_PAPYRUS_OVERRIDE_HANDLED:
              result = OverrideResult::handled(produced == nullptr ? fo4::papyrus::noneValue()
                                                                   : produced->value);
              break;
            case RADICLE_PAPYRUS_OVERRIDE_DECLINED:
              result = OverrideResult::declined();
              break;
            case RADICLE_PAPYRUS_OVERRIDE_REFUSED:
              result = OverrideResult::refused("the override refused the call");
              break;
            default:
              result = OverrideResult::notRegistered();
              break;
          }
          delete produced;
          return result;
        });
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_set_engine_callbacks(radicle_papyrus_runtime* runtime,
                                                            const radicle_papyrus_engine_callbacks* callbacks) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime is required");
  }
  if (callbacks != nullptr && callbacks->struct_size < sizeof(radicle_papyrus_engine_callbacks)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the engine callbacks struct is too small");
  }
  return radicle_c_env_detail::guarded("unknown exception while installing engine callbacks", [&] {
    if (callbacks == nullptr) {
      runtime->engine = radicle_papyrus_engine_callbacks{};
      runtime->runtime.setEngineObjectHooks(fo4::papyrus::EngineObjectHooks{});
      return RADICLE_STATUS_OK;
    }
    runtime->engine = *callbacks;
    radicle_papyrus_runtime* raw = runtime;
    EngineObjectHooks hooks;
    hooks.get = [raw](const ObjectRef& object, const std::string& name, Value& out, Status& status) {
      if (raw->engine.get_property == nullptr) {
        status = Status::UnknownIdentifier;
        return false;
      }
      radicle_papyrus_object handle{object};
      radicle_papyrus_value* produced = nullptr;
      const int32_t handled = raw->engine.get_property(raw->engine.user, &handle, name.c_str(), &produced);
      if (handled == 0) {
        delete produced;
        status = Status::UnknownIdentifier;
        return false;
      }
      out = produced == nullptr ? fo4::papyrus::noneValue() : produced->value;
      delete produced;
      return true;
    };
    hooks.set = [raw](const ObjectRef& object, const std::string& name, const Value& value, Status& status) {
      if (raw->engine.set_property == nullptr) {
        status = Status::UnknownIdentifier;
        return false;
      }
      radicle_papyrus_object handle{object};
      radicle_papyrus_value stored{value};
      if (raw->engine.set_property(raw->engine.user, &handle, name.c_str(), &stored) == 0) {
        status = Status::UnknownIdentifier;
        return false;
      }
      return true;
    };
    raw->runtime.setEngineObjectHooks(std::move(hooks));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_register_native_ex(radicle_papyrus_runtime* runtime, const char* script,
                                                          const char* function,
                                                          radicle_papyrus_native_kind kind,
                                                          const char* required_script, uint32_t flags) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || script == nullptr || function == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, script and function are required");
  }
  if ((flags & ~static_cast<uint32_t>(RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT |
                                      RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE)) != 0) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the native flags are not valid");
  }
  const bool acceptScriptInstance = (flags & static_cast<uint32_t>(RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE)) != 0;
  if (acceptScriptInstance && kind != RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "ACCEPT_SCRIPT_INSTANCE needs an engine-object native");
  }
  return radicle_c_env_detail::guarded("unknown exception while registering a native", [&] {
    NativeRegistration registration;
    registration.kind = nativeKindFrom(kind);
    registration.requiredScript = required_script == nullptr ? std::string() : std::string(required_script);
    registration.acceptScriptInstance = acceptScriptInstance;
    const NativeTargetKind targetKind = registration.kind;
    radicle_papyrus_runtime* raw = runtime;
    registration.invoke = [raw, flags, targetKind](const NativeCall& call, NativeResult& result) -> bool {
      result = NativeResult{};
      if (raw->engine.call_native_ex == nullptr) {
        result.status = Status::FunctionNotFound;
        return false;
      }
      radicle_papyrus_native_call native;
      native.runtime = raw;
      native.flags = call.mayPark ? flags : (flags & ~static_cast<uint32_t>(RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT));
      native.script = call.script;
      native.function = call.function;
      native.arguments.reserve(call.args.size());
      for (const Value& argument : call.args) native.arguments.push_back(radicle_papyrus_value{argument});
      if (targetKind != NativeTargetKind::Static && call.self.type == ValueType::Object &&
          call.self.object != nullptr) {
        native.self = radicle_papyrus_object{*call.self.object};
      }
      const int32_t handled = raw->engine.call_native_ex(raw->engine.user, &native);
      if (handled == 0) {
        if (native.token.has_value()) raw->runtime.releaseWait(*native.token);
        result.status = native.failure == RADICLE_PAPYRUS_STATUS_OK
                            ? Status::FunctionNotFound
                            : radicle_c_papyrus_detail::from_c_status(native.failure);
        return false;
      }
      if (native.token.has_value()) {
        result.latentToken = *native.token;
      } else {
        result.value = native.result.has_value() ? native.result->value : fo4::papyrus::noneValue();
      }
      return true;
    };
    raw->runtime.registerNative(script, function, std::move(registration));
    return RADICLE_STATUS_OK;
  });
}

const radicle_papyrus_object* radicle_papyrus_native_call_self(const radicle_papyrus_native_call* call) {
  radicle_c_detail::clear_error();
  return call == nullptr || !call->self.has_value() ? nullptr : &*call->self;
}

const char* radicle_papyrus_native_call_script(const radicle_papyrus_native_call* call) {
  radicle_c_detail::clear_error();
  return call == nullptr ? nullptr : call->script.c_str();
}

const char* radicle_papyrus_native_call_function(const radicle_papyrus_native_call* call) {
  radicle_c_detail::clear_error();
  return call == nullptr ? nullptr : call->function.c_str();
}

size_t radicle_papyrus_native_call_arg_count(const radicle_papyrus_native_call* call) {
  radicle_c_detail::clear_error();
  return call == nullptr ? 0 : call->arguments.size();
}

const radicle_papyrus_value* radicle_papyrus_native_call_arg(const radicle_papyrus_native_call* call,
                                                             size_t index) {
  radicle_c_detail::clear_error();
  if (call == nullptr || index >= call->arguments.size()) return nullptr;
  return &call->arguments[index];
}

radicle_status radicle_papyrus_native_call_set_result(radicle_papyrus_native_call* call,
                                                      const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (call == nullptr || value == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "call and value are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while setting a native result", [&] {
    call->result = radicle_papyrus_value{value->value};
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_native_call_begin_wait(radicle_papyrus_native_call* call, uint64_t* out_token) {
  radicle_c_detail::clear_error();
  if (out_token != nullptr) *out_token = 0;
  if (call == nullptr || out_token == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "call and out_token are required");
  }
  if ((call->flags & static_cast<uint32_t>(RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT)) == 0) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the native was not registered with MAY_WAIT");
  }
  if (call->token.has_value()) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the call already began a wait");
  }
  return radicle_c_env_detail::guarded("unknown exception while beginning a native wait", [&] {
    call->token = call->runtime->runtime.beginWait();
    *out_token = *call->token;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_native_call_fail(radicle_papyrus_native_call* call, radicle_papyrus_status status) {
  radicle_c_detail::clear_error();
  if (call == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "call is required");
  }
  call->failure = status;
  return RADICLE_STATUS_OK;
}

}  // extern "C"
