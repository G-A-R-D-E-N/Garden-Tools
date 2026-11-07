#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_papyrus_internal.hpp"
#include "c_api_papyrus_runtime_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/papyrus/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using fo4::papyrus::CallRecord;
using fo4::papyrus::CallState;
using fo4::papyrus::ClockSource;
using fo4::papyrus::InstanceId;
using fo4::papyrus::Lifecycle;
using fo4::papyrus::EngineObjectHooks;
using fo4::papyrus::NativeCall;
using fo4::papyrus::NativeRegistration;
using fo4::papyrus::NativeResult;
using fo4::papyrus::ObjectRef;
using fo4::papyrus::ValueType;
using fo4::papyrus::NativeTargetKind;
using fo4::papyrus::OnInitArrivalPolicy;
using fo4::papyrus::OwnerId;
using fo4::papyrus::OwnerKind;
using fo4::papyrus::OverrideOutcome;
using fo4::papyrus::OverrideResult;
using fo4::papyrus::RegistrationKind;
using fo4::papyrus::Runtime;
using fo4::papyrus::ScriptRegistration;
using fo4::papyrus::Status;
using fo4::papyrus::Value;


namespace {

using radicle_c_papyrus_detail::make_value;
using radicle_c_papyrus_detail::to_c_status;

std::optional<OwnerId> ownerFrom(radicle_papyrus_owner_kind kind, const char* base, const char* discriminator) {
  if (base == nullptr || *base == '\0') return std::nullopt;
  const std::string baseKey(base);
  const std::string disc = discriminator == nullptr ? std::string() : std::string(discriminator);
  switch (kind) {
    case RADICLE_PAPYRUS_OWNER_FORM:
      return OwnerId::forForm(baseKey);
    case RADICLE_PAPYRUS_OWNER_PLACED_REFERENCE:
      if (disc.empty()) return std::nullopt;
      return OwnerId::forPlacedReference(baseKey, disc);
    case RADICLE_PAPYRUS_OWNER_DYNAMIC_REFERENCE:
      if (disc.empty()) return std::nullopt;
      return OwnerId::forDynamicReference(baseKey, disc);
    case RADICLE_PAPYRUS_OWNER_QUEST_ALIAS:
      return OwnerId::forQuestAlias(baseKey, static_cast<std::int32_t>(std::strtol(disc.c_str(), nullptr, 10)));
    case RADICLE_PAPYRUS_OWNER_RUNTIME_INSTANCE:
      return OwnerId::forRuntimeInstance(baseKey, std::strtoll(disc.c_str(), nullptr, 10));
  }
  return std::nullopt;
}

radicle_papyrus_call_state callStateFrom(CallState state) {
  switch (state) {
    case CallState::Pending:
      return RADICLE_PAPYRUS_CALL_PENDING;
    case CallState::Waiting:
      return RADICLE_PAPYRUS_CALL_WAITING;
    case CallState::Done:
      return RADICLE_PAPYRUS_CALL_DONE;
    case CallState::Failed:
      return RADICLE_PAPYRUS_CALL_FAILED;
    case CallState::Dropped:
      return RADICLE_PAPYRUS_CALL_DROPPED;
  }
  return RADICLE_PAPYRUS_CALL_FAILED;
}

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

radicle_status radicle_papyrus_runtime_create(const radicle_papyrus_runtime_callbacks* callbacks,
                                              radicle_papyrus_runtime** out_runtime) {
  radicle_c_detail::clear_error();
  if (out_runtime == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_runtime is required");
  }
  *out_runtime = nullptr;
  return radicle_c_env_detail::guarded("unknown exception while creating the Papyrus runtime", [&] {
    auto runtime = std::make_unique<radicle_papyrus_runtime>();
    if (callbacks != nullptr) runtime->callbacks = *callbacks;
    radicle_papyrus_runtime* raw = runtime.get();
    raw->runtime.setGate([raw](std::string_view key) {
      if (raw->callbacks.gate == nullptr) return true;
      const std::string text(key);
      return raw->callbacks.gate(raw->callbacks.user, text.c_str()) != 0;
    });
    *out_runtime = runtime.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_papyrus_runtime_destroy(radicle_papyrus_runtime* runtime) {
  radicle_c_detail::clear_error();
  delete runtime;
}

radicle_status radicle_papyrus_runtime_add_pex(radicle_papyrus_runtime* runtime, const radicle_pex* pex) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || pex == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and pex are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while adding a pex file", [&] {
    runtime->pex_files.push_back(pex);
    for (const fo4::pex::PexObject& object : pex->value.objects) runtime->runtime.addScript(&object);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_attach(radicle_papyrus_runtime* runtime, const char* script,
                                              radicle_papyrus_owner_kind owner_kind,
                                              const char* base_form_key, const char* discriminator,
                                              uint64_t* out_instance) {
  radicle_c_detail::clear_error();
  if (out_instance != nullptr) *out_instance = 0;
  if (runtime == nullptr || script == nullptr || out_instance == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, script and out_instance are required");
  }
  const std::optional<OwnerId> owner = ownerFrom(owner_kind, base_form_key, discriminator);
  if (!owner.has_value()) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the owner is not valid");
  }
  return radicle_c_env_detail::guarded("unknown exception while attaching a script", [&] {
    const auto instance = runtime->runtime.attach(script, *owner);
    if (!instance.has_value()) {
      return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "no loaded pex defines that script");
    }
    *out_instance = *instance;
    return RADICLE_STATUS_OK;
  });
}

int32_t radicle_papyrus_runtime_detach(radicle_papyrus_runtime* runtime, uint64_t instance) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) return 0;
  return runtime->runtime.detach(instance) ? 1 : 0;
}

radicle_papyrus_lifecycle radicle_papyrus_runtime_lifecycle(const radicle_papyrus_runtime* runtime,
                                                            uint64_t instance) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) return RADICLE_PAPYRUS_LIFECYCLE_FAILED;
  switch (runtime->runtime.lifecycle(instance)) {
    case Lifecycle::NotInitialized:
      return RADICLE_PAPYRUS_LIFECYCLE_NOT_INITIALIZED;
    case Lifecycle::Initializing:
      return RADICLE_PAPYRUS_LIFECYCLE_INITIALIZING;
    case Lifecycle::Ready:
      return RADICLE_PAPYRUS_LIFECYCLE_READY;
    case Lifecycle::Failed:
      return RADICLE_PAPYRUS_LIFECYCLE_FAILED;
  }
  return RADICLE_PAPYRUS_LIFECYCLE_FAILED;
}

const char* radicle_papyrus_runtime_failure(const radicle_papyrus_runtime* runtime, uint64_t instance) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) return nullptr;
  return runtime->runtime.failure(instance).c_str();
}

const char* radicle_papyrus_runtime_instance_state(const radicle_papyrus_runtime* runtime,
                                                   uint64_t instance) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) return nullptr;
  return runtime->runtime.instanceState(instance).c_str();
}

radicle_status radicle_papyrus_runtime_initialize(radicle_papyrus_runtime* runtime, uint64_t instance,
                                                  radicle_papyrus_status* out_status) {
  radicle_c_detail::clear_error();
  if (out_status != nullptr) *out_status = RADICLE_PAPYRUS_STATUS_OK;
  if (runtime == nullptr || out_status == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_status are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while initializing a script", [&] {
    *out_status = to_c_status(runtime->runtime.initialize(instance));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_send_event(radicle_papyrus_runtime* runtime, uint64_t instance,
                                                  const char* function,
                                                  const radicle_papyrus_value* const* args,
                                                  size_t arg_count,
                                                  radicle_papyrus_status* out_status) {
  radicle_c_detail::clear_error();
  if (out_status != nullptr) *out_status = RADICLE_PAPYRUS_STATUS_OK;
  if (runtime == nullptr || function == nullptr || out_status == nullptr ||
      (args == nullptr && arg_count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, function, out_status and args are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while sending a script event", [&] {
    std::vector<Value> values;
    values.reserve(arg_count);
    for (size_t i = 0; i < arg_count; ++i) {
      if (args[i] == nullptr) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "argument handles must not be null");
      }
      values.push_back(args[i]->value);
    }
    *out_status = to_c_status(runtime->runtime.sendEvent(instance, function, std::move(values)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_call_function(radicle_papyrus_runtime* runtime, uint64_t instance,
                                                     const char* function,
                                                     const radicle_papyrus_value* const* args,
                                                     size_t arg_count, uint64_t* out_ticket,
                                                     radicle_papyrus_status* out_status) {
  radicle_c_detail::clear_error();
  if (out_ticket != nullptr) *out_ticket = 0;
  if (out_status != nullptr) *out_status = RADICLE_PAPYRUS_STATUS_OK;
  if (runtime == nullptr || function == nullptr || out_ticket == nullptr || out_status == nullptr ||
      (args == nullptr && arg_count != 0)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, function, out_ticket, out_status and args are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while calling a script function", [&] {
    std::vector<Value> values;
    values.reserve(arg_count);
    for (size_t i = 0; i < arg_count; ++i) {
      if (args[i] == nullptr) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "argument handles must not be null");
      }
      values.push_back(args[i]->value);
    }
    fo4::papyrus::Status status = fo4::papyrus::Status::Ok;
    *out_ticket = runtime->runtime.submitCall(instance, function, std::move(values), status);
    *out_status = to_c_status(status);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_call_result(radicle_papyrus_runtime* runtime, uint64_t ticket,
                                                   int32_t consume, radicle_papyrus_call_info* out_info,
                                                   radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (out_value != nullptr) *out_value = nullptr;
  if (runtime == nullptr || out_info == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_info are required");
  }
  if (out_info->struct_size < sizeof(radicle_papyrus_call_info)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the call info struct is too small");
  }
  return radicle_c_env_detail::guarded("unknown exception while reading a call result", [&] {
    CallRecord record;
    if (!runtime->runtime.takeResult(ticket, false, record)) {
      return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the call ticket is unknown, consumed or evicted");
    }
    if (record.state == CallState::Done && out_value != nullptr) {
      const radicle_status made = make_value(std::move(record.value), out_value);
      if (made != RADICLE_STATUS_OK) return made;
    }
    out_info->state = callStateFrom(record.state);
    out_info->status = to_c_status(record.status);
    out_info->instructions = static_cast<uint64_t>(record.instructions);
    if (consume != 0) {
      CallRecord released;
      runtime->runtime.takeResult(ticket, true, released);
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_step(radicle_papyrus_runtime* runtime, int32_t budget,
                                            radicle_papyrus_step_result* out_result) {
  radicle_c_detail::clear_error();
  if (out_result != nullptr) *out_result = {};
  if (runtime == nullptr || out_result == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_result are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while stepping the scheduler", [&] {
    const fo4::papyrus::SchedulerStepResult result = runtime->runtime.runStep(budget);
    out_result->spent = result.spent;
    out_result->completed = result.completed;
    out_result->parked = result.parked;
    out_result->paused = result.paused;
    out_result->refused = result.refused;
    out_result->dropped = result.dropped;
    out_result->waiting = result.waiting;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_complete_latent(radicle_papyrus_runtime* runtime, uint64_t token,
                                                       const radicle_papyrus_value* value,
                                                       int32_t* out_accepted) {
  radicle_c_detail::clear_error();
  if (out_accepted != nullptr) *out_accepted = 0;
  if (runtime == nullptr || value == nullptr || out_accepted == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, value and out_accepted are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while completing a latent call", [&] {
    *out_accepted = runtime->runtime.completeLatent(token, value->value) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

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

radicle_status radicle_papyrus_runtime_get_variable(const radicle_papyrus_runtime* runtime,
                                                    uint64_t instance, const char* name,
                                                    radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (out_value != nullptr) *out_value = nullptr;
  if (runtime == nullptr || name == nullptr || out_value == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, name and out_value are required");
  }
  const std::optional<Value> value = runtime->runtime.instanceVariable(instance, name);
  if (!value.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the variable is not set");
  return make_value(*value, out_value);
}

radicle_status radicle_papyrus_runtime_set_variable(radicle_papyrus_runtime* runtime, uint64_t instance,
                                                    const char* name,
                                                    const radicle_papyrus_value* value) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || name == nullptr || value == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime, name and value are required");
  }
  if (!runtime->runtime.setInstanceVariable(instance, name, value->value)) {
    return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the instance does not exist");
  }
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_runtime_register_custom_event(radicle_papyrus_runtime* runtime,
                                                             uint64_t instance, const char* event_name,
                                                             int64_t* out_handle) {
  radicle_c_detail::clear_error();
  if (out_handle != nullptr) *out_handle = 0;
  if (runtime == nullptr || event_name == nullptr || out_handle == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, event_name and out_handle are required");
  }
  const std::optional<OwnerId> owner = runtime->runtime.instanceOwner(instance);
  if (!owner.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the instance does not exist");
  return radicle_c_env_detail::guarded("unknown exception while registering a custom event", [&] {
    const auto* entry = runtime->runtime.registrations().registerRegistration(
        *owner, ScriptRegistration::forCustomEvent(event_name));
    *out_handle = entry->handle;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_register_timer(radicle_papyrus_runtime* runtime, uint64_t instance,
                                                      double interval_seconds, int32_t repeats,
                                                      radicle_papyrus_clock clock, int64_t* out_handle) {
  radicle_c_detail::clear_error();
  if (out_handle != nullptr) *out_handle = 0;
  if (runtime == nullptr || out_handle == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_handle are required");
  }
  const std::optional<OwnerId> owner = runtime->runtime.instanceOwner(instance);
  if (!owner.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the instance does not exist");
  return radicle_c_env_detail::guarded("unknown exception while registering a timer", [&] {
    const auto* entry = runtime->runtime.registrations().registerRegistration(
        *owner, ScriptRegistration::forTimer(interval_seconds, repeats != 0,
                                             clock == RADICLE_PAPYRUS_CLOCK_GAME_TIME ? ClockSource::GameTime
                                                                                     : ClockSource::RealTime));
    *out_handle = entry->handle;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_unregister(radicle_papyrus_runtime* runtime, int64_t handle,
                                                  int32_t* out_removed) {
  radicle_c_detail::clear_error();
  if (out_removed != nullptr) *out_removed = 0;
  if (runtime == nullptr || out_removed == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_removed are required");
  }
  *out_removed = runtime->runtime.registrations().unregister(handle) ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_papyrus_runtime_advance_clock(radicle_papyrus_runtime* runtime, const char* owner,
                                                     double delta_seconds, radicle_papyrus_clock clock,
                                                     int32_t* out_fired) {
  radicle_c_detail::clear_error();
  if (out_fired != nullptr) *out_fired = 0;
  if (runtime == nullptr || owner == nullptr || out_fired == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, owner and out_fired are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while advancing the clock", [&] {
    *out_fired = runtime->runtime.registrations().advance(
        owner, delta_seconds,
        clock == RADICLE_PAPYRUS_CLOCK_GAME_TIME ? ClockSource::GameTime : ClockSource::RealTime);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_attach_with_properties(
    radicle_papyrus_runtime* runtime, const char* script, radicle_papyrus_owner_kind owner_kind,
    const char* base_form_key, const char* discriminator, const char* const* names,
    const radicle_papyrus_value* const* values, size_t count, uint64_t* out_instance) {
  radicle_c_detail::clear_error();
  if (out_instance != nullptr) *out_instance = 0;
  if (runtime == nullptr || script == nullptr || out_instance == nullptr ||
      (count != 0 && (names == nullptr || values == nullptr))) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, script, out_instance and the property arrays are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while attaching a script", [&] {
    const std::optional<OwnerId> owner = ownerFrom(owner_kind, base_form_key, discriminator);
    if (!owner.has_value()) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the owner is not valid");
    }
    std::vector<std::pair<std::string, Value>> properties;
    properties.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      if (names[i] == nullptr || values[i] == nullptr) {
        return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                      "property names and values must not be null");
      }
      properties.emplace_back(names[i], values[i]->value);
    }
    const fo4::papyrus::AttachResult result =
        runtime->runtime.attachWithProperties(script, *owner, properties);
    if (result.refusal == fo4::papyrus::AttachRefusal::UnknownScript) {
      return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, result.reason.c_str());
    }
    if (result.refusal != fo4::papyrus::AttachRefusal::None || !result.instance.has_value()) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, result.reason.c_str());
    }
    *out_instance = *result.instance;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_get_property(const radicle_papyrus_runtime* runtime, uint64_t instance,
                                                    const char* name, radicle_papyrus_value** out_value) {
  radicle_c_detail::clear_error();
  if (out_value != nullptr) *out_value = nullptr;
  if (runtime == nullptr || name == nullptr || out_value == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, name and out_value are required");
  }
  const std::optional<Value> value = runtime->runtime.instanceProperty(instance, name);
  if (!value.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the property is not set");
  return make_value(*value, out_value);
}

radicle_status radicle_papyrus_runtime_register_remote_event(
    radicle_papyrus_runtime* runtime, uint64_t instance, radicle_papyrus_owner_kind source_kind,
    const char* source_base_form_key, const char* source_discriminator, const char* event_name,
    int64_t* out_handle) {
  radicle_c_detail::clear_error();
  if (out_handle != nullptr) *out_handle = 0;
  if (runtime == nullptr || event_name == nullptr || out_handle == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, event_name and out_handle are required");
  }
  const std::optional<OwnerId> owner = runtime->runtime.instanceOwner(instance);
  if (!owner.has_value()) return radicle_c_detail::fail(RADICLE_STATUS_NOT_FOUND, "the instance does not exist");
  return radicle_c_env_detail::guarded("unknown exception while registering a remote event", [&] {
    const std::optional<OwnerId> source = ownerFrom(source_kind, source_base_form_key, source_discriminator);
    if (!source.has_value()) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the source owner is not valid");
    }
    try {
      const auto* entry = runtime->runtime.registrations().registerRegistration(
          *owner, ScriptRegistration::forRemoteEvent(*source, event_name));
      *out_handle = entry->handle;
    } catch (const std::invalid_argument& error) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, error.what());
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_match_remote_event(
    const radicle_papyrus_runtime* runtime, radicle_papyrus_owner_kind source_kind,
    const char* source_base_form_key, const char* source_discriminator, const char* event_name,
    int64_t* out_handles, uint64_t* out_instances, size_t capacity, size_t* out_count) {
  radicle_c_detail::clear_error();
  if (out_count != nullptr) *out_count = 0;
  if (runtime == nullptr || event_name == nullptr || out_count == nullptr ||
      (capacity != 0 && (out_handles == nullptr || out_instances == nullptr))) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "runtime, event_name, out_count and the output arrays are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while matching a remote event", [&] {
    const std::optional<OwnerId> source = ownerFrom(source_kind, source_base_form_key, source_discriminator);
    if (!source.has_value()) {
      return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the source owner is not valid");
    }
    const auto matches = runtime->runtime.registrations().matchRemoteEvent(*source, event_name);
    for (size_t i = 0; i < matches.size() && i < capacity; ++i) {
      out_handles[i] = matches[i]->handle;
      out_instances[i] = runtime->runtime.instanceForOwner(matches[i]->owner).value_or(0);
    }
    *out_count = matches.size();
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

radicle_status radicle_papyrus_runtime_set_oninit_policy(radicle_papyrus_runtime* runtime,
                                                         radicle_papyrus_oninit_policy policy) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime is required");
  }
  return radicle_c_env_detail::guarded("unknown exception while setting the OnInit policy", [&] {
    switch (policy) {
      case RADICLE_PAPYRUS_ONINIT_REFUSE:
        runtime->runtime.setOnInitArrivalPolicy(OnInitArrivalPolicy::Refuse);
        return RADICLE_STATUS_OK;
      case RADICLE_PAPYRUS_ONINIT_QUEUE:
        runtime->runtime.setOnInitArrivalPolicy(OnInitArrivalPolicy::QueueAsApproximation);
        return RADICLE_STATUS_OK;
    }
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the OnInit policy is not valid");
  });
}

radicle_status radicle_papyrus_runtime_set_max_parks(radicle_papyrus_runtime* runtime, int32_t parks) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime is required");
  }
  if (parks < 1) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the park limit must be at least 1");
  }
  return radicle_c_env_detail::guarded("unknown exception while setting the park limit", [&] {
    runtime->runtime.setMaxParksPerEvent(parks);
    return RADICLE_STATUS_OK;
  });
}

int32_t radicle_papyrus_runtime_has_script(const radicle_papyrus_runtime* runtime, const char* name) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || name == nullptr) return 0;
  try {
    return runtime->runtime.hasScript(name) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int32_t radicle_papyrus_runtime_instance_declares(const radicle_papyrus_runtime* runtime, uint64_t instance,
                                                  const char* function) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || function == nullptr) return 0;
  try {
    return runtime->runtime.instanceDeclares(instance, function) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

const char* radicle_papyrus_runtime_instance_script(const radicle_papyrus_runtime* runtime, uint64_t instance) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr) return nullptr;
  try {
    if (!runtime->runtime.instanceOwner(instance).has_value()) return nullptr;
    return runtime->runtime.instanceScript(instance).c_str();
  } catch (...) {
    return nullptr;
  }
}

radicle_status radicle_papyrus_runtime_list_instances(const radicle_papyrus_runtime* runtime, uint64_t* out_ids,
                                                      size_t capacity, size_t* out_count) {
  radicle_c_detail::clear_error();
  if (out_count != nullptr) *out_count = 0;
  if (runtime == nullptr || out_count == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out_count are required");
  }
  return radicle_c_env_detail::guarded("unknown exception while listing instances", [&] {
    const std::vector<InstanceId> ids = runtime->runtime.instanceIds();
    *out_count = ids.size();
    if (out_ids != nullptr) {
      for (size_t i = 0; i < ids.size() && i < capacity; ++i) out_ids[i] = ids[i];
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_papyrus_runtime_get_stats(const radicle_papyrus_runtime* runtime,
                                                 radicle_papyrus_runtime_stats* out) {
  radicle_c_detail::clear_error();
  if (runtime == nullptr || out == nullptr) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "runtime and out are required");
  }
  if (out->struct_size < sizeof(radicle_papyrus_runtime_stats)) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "the stats struct is too small");
  }
  return radicle_c_env_detail::guarded("unknown exception while reading the runtime statistics", [&] {
    const Runtime& source = runtime->runtime;
    out->ready = static_cast<int32_t>(source.readyCount());
    out->waiting = static_cast<int32_t>(source.waitingCount());
    out->completed = source.completedEvents();
    out->parked = source.parkedEvents();
    out->paused = source.pausedEvents();
    out->refused = source.refusedEvents();
    out->dropped = source.droppedEvents();
    out->waiting_events = source.waitingEvents();
    out->refused_completions = source.refusedCompletions();
    out->queued_during_oninit = source.queuedDuringOnInit();
    out->native_target_refusals = source.nativeTargetRefusals();
    out->overrides_handled = source.overridesHandled();
    out->overrides_declined = source.overridesDeclined();
    out->overrides_refused = source.overridesRefused();
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
