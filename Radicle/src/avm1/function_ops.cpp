#include "function_ops.hpp"

#include "object_ops.hpp"
#include "text_ops.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::avm1::function_ops {

namespace {

using text_ops::to_text;

constexpr std::size_t kMaxCallDepth = 64;
constexpr std::uint16_t kPreloadThis = 0x01;
constexpr std::uint16_t kPreloadArguments = 0x04;
constexpr std::uint16_t kPreloadSuper = 0x10;
constexpr std::uint16_t kPreloadRoot = 0x40;
constexpr std::uint16_t kPreloadParent = 0x80;
constexpr std::uint16_t kPreloadGlobal = 0x100;
constexpr std::uint16_t kUnmodelledPreloads =
    kPreloadSuper | kPreloadRoot | kPreloadParent | kPreloadGlobal;

bool register_in_range(const Avm1Call& call, std::size_t index, std::string& message) {
  if (index < call.registers.size()) {
    return true;
  }
  message = "register " + std::to_string(index) + " is past the " +
            std::to_string(call.registers.size()) + " registers the function declares";
  return false;
}

Avm1RunStatus place_register(Avm1Call& call, std::size_t index, Avm1Data value,
                             std::string& message) {
  if (!register_in_range(call, index, message)) {
    return Avm1RunStatus::InvalidStream;
  }
  call.registers[index] = std::move(value);
  return Avm1RunStatus::Ok;
}

Avm1Data* scope_slot(const Avm1Block& block, const std::string& name) {
  for (auto scope = block.scopes->rbegin(); scope != block.scopes->rend(); ++scope) {
    const auto found = scope->object->properties.find(name);
    if (found != scope->object->properties.end()) {
      return &found->second;
    }
  }
  return nullptr;
}

Avm1Data& declare_slot(const Avm1Block& block, Avm1State& state, const std::string& name) {
  return block.call != nullptr ? block.call->locals[name] : state.variables[name];
}

Avm1Data& assign_slot(const Avm1Block& block, Avm1State& state, const std::string& name) {
  if (Avm1Data* scoped = scope_slot(block, name)) {
    return *scoped;
  }
  if (block.call != nullptr) {
    const auto local = block.call->locals.find(name);
    if (local != block.call->locals.end()) {
      return local->second;
    }
  }
  return state.variables[name];
}

const Avm1Data* find_slot(const Avm1Block& block, const Avm1State& state,
                          const std::string& name) {
  if (const Avm1Data* scoped = scope_slot(block, name)) {
    return scoped;
  }
  if (block.call != nullptr) {
    const auto local = block.call->locals.find(name);
    if (local != block.call->locals.end()) {
      return &local->second;
    }
  }
  const auto global = state.variables.find(name);
  return global == state.variables.end() ? nullptr : &global->second;
}

Avm1RunStatus pop_arguments(Avm1State& state, std::vector<Avm1Data>& args, std::string& message) {
  std::size_t count = 0;
  if (const Avm1RunStatus status = pop_count(state, 1, count, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  args.resize(count);
  for (Avm1Data& arg : args) {
    if (const Avm1RunStatus status = pop(state, arg, message); status != Avm1RunStatus::Ok) {
      return status;
    }
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus invoke(Avm1Run& run, const Avm1FunctionBody& body, Avm1Data this_value,
                     const std::vector<Avm1Data>& args, Avm1Data& result, std::string& message) {
  const Avm1Function& fn = body.header;
  if ((fn.flags & kUnmodelledPreloads) != 0) {
    message = "preloading super, _root, _parent or _global is not in this slice";
    return Avm1RunStatus::Unsupported;
  }
  if (run.depth == kMaxCallDepth) {
    message = "the call depth of " + std::to_string(kMaxCallDepth) + " functions was reached";
    return Avm1RunStatus::ResourceLimit;
  }
  Avm1Call call;
  call.registers.resize(fn.register_count);
  std::size_t preload = 1;
  if ((fn.flags & kPreloadThis) != 0) {
    if (const Avm1RunStatus status = place_register(call, preload++, this_value, message);
        status != Avm1RunStatus::Ok) {
      return status;
    }
  }
  if ((fn.flags & kPreloadArguments) != 0) {
    if (const Avm1RunStatus status =
            place_register(call, preload++, object_ops::arguments_object(args), message);
        status != Avm1RunStatus::Ok) {
      return status;
    }
  }
  for (std::size_t index = 0; index < fn.parameters.size(); ++index) {
    const Avm1Parameter& parameter = fn.parameters[index];
    Avm1Data value = index < args.size() ? args[index] : Avm1Data{};
    if (parameter.register_index == 0) {
      call.locals[parameter.name] = std::move(value);
    } else if (const Avm1RunStatus status =
                   place_register(call, parameter.register_index, std::move(value), message);
               status != Avm1RunStatus::Ok) {
      return status;
    }
  }
  run.depth += 1;
  std::vector<Avm1Data> scopes;
  const Avm1Block block{body.actions, body.end_offset, &call, &scopes};
  const Avm1RunStatus status = run_block(run, block);
  run.depth -= 1;
  if (status != Avm1RunStatus::Ok) {
    return status;
  }
  result = std::move(call.return_value);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus invoke_method(Avm1Run& run, std::string& message, bool construct) {
  Avm1State& state = run.state;
  Avm1Data method;
  Avm1Data base;
  if (const Avm1RunStatus status = pop(state, method, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, base, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  std::vector<Avm1Data> args;
  if (const Avm1RunStatus status = pop_arguments(state, args, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  if (base.kind != Avm1DataKind::Object) {
    message = "a method of a value that is not an object is not in this slice";
    return Avm1RunStatus::Unsupported;
  }
  const std::string name = to_text(method, state.swf_version);
  const auto found = base.object->properties.find(name);
  if (found == base.object->properties.end() || found->second.kind != Avm1DataKind::Function) {
    message = "method '" + name + "' is not a script function in this slice";
    return Avm1RunStatus::Unsupported;
  }
  const Avm1Data target = found->second;
  Avm1Data result;
  if (!construct) {
    if (const Avm1RunStatus status =
            invoke(run, *target.function, base, args, result, message);
        status != Avm1RunStatus::Ok) {
      return status;
    }
    state.stack.push_back(std::move(result));
    return Avm1RunStatus::Ok;
  }
  Avm1Data instance = object_ops::plain_object();
  instance.object->constructed_by = target.function;
  if (const Avm1RunStatus status = invoke(run, *target.function, instance, args, result, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  state.stack.push_back(result.kind == Avm1DataKind::Object ? std::move(result)
                                                            : std::move(instance));
  return Avm1RunStatus::Ok;
}

}  // namespace

Avm1RunStatus get_variable(const Avm1Block& block, Avm1State& state, std::string& message) {
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const Avm1Data* found = find_slot(block, state, to_text(name, state.swf_version));
  state.stack.push_back(found == nullptr ? Avm1Data{} : *found);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus set_variable(const Avm1Block& block, Avm1State& state, std::string& message) {
  Avm1Data value;
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  assign_slot(block, state, to_text(name, state.swf_version)) = std::move(value);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus define_local(const Avm1Block& block, Avm1State& state, std::string& message) {
  Avm1Data value;
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  declare_slot(block, state, to_text(name, state.swf_version)) = std::move(value);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus define_local2(const Avm1Block& block, Avm1State& state, std::string& message) {
  Avm1Data name;
  if (const Avm1RunStatus status = pop(state, name, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  declare_slot(block, state, to_text(name, state.swf_version)) = Avm1Data{};
  return Avm1RunStatus::Ok;
}

Avm1RunStatus define_function(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                              std::size_t& next) {
  const auto& header = std::get<Avm1Function>(action.payload);
  const std::size_t end_offset = action.offset + action.length + header.body_size;
  const auto first = block.actions.begin() + static_cast<std::ptrdiff_t>(next);
  const auto last = std::lower_bound(
      first, block.actions.end(), end_offset,
      [](const Avm1Action& record, std::size_t offset) { return record.offset < offset; });
  next = static_cast<std::size_t>(last - block.actions.begin());
  auto& body = run.functions[action.offset];
  if (body == nullptr) {
    auto built = std::make_shared<Avm1FunctionBody>();
    built->header = header;
    built->end_offset = end_offset;
    built->actions.assign(first, last);
    body = std::move(built);
  }
  Avm1Data value{.kind = Avm1DataKind::Function, .function = body};
  if (header.name.empty()) {
    run.state.stack.push_back(std::move(value));
  } else {
    declare_slot(block, run.state, header.name) = std::move(value);
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus call_function(Avm1Run& run, const Avm1Block& block, std::string& message) {
  Avm1State& state = run.state;
  Avm1Data callee;
  if (const Avm1RunStatus status = pop(state, callee, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  std::vector<Avm1Data> args;
  if (const Avm1RunStatus status = pop_arguments(state, args, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  const std::string name = to_text(callee, state.swf_version);
  Avm1Data target = callee;
  if (target.kind != Avm1DataKind::Function) {
    const Avm1Data* found = find_slot(block, state, name);
    target = found == nullptr ? Avm1Data{} : *found;
  }
  if (target.kind != Avm1DataKind::Function) {
    message = "CallFunction of '" + name + "' is not a script function in this slice";
    return Avm1RunStatus::Unsupported;
  }
  Avm1Data result;
  if (const Avm1RunStatus status = invoke(run, *target.function, Avm1Data{}, args, result, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  state.stack.push_back(std::move(result));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus call_method(Avm1Run& run, std::string& message) {
  return invoke_method(run, message, false);
}

Avm1RunStatus new_method(Avm1Run& run, std::string& message) {
  return invoke_method(run, message, true);
}

Avm1RunStatus store_register(const Avm1Block& block, Avm1State& state, std::uint8_t index,
                             std::string& message) {
  if (block.call == nullptr) {
    message = "StoreRegister outside a function is not in this slice";
    return Avm1RunStatus::Unsupported;
  }
  if (state.stack.empty()) {
    message = kUnderflow;
    return Avm1RunStatus::StackUnderflow;
  }
  return place_register(*block.call, index, state.stack.back(), message);
}

Avm1RunStatus push_register(const Avm1Block& block, Avm1State& state, std::uint16_t index,
                            std::string& message) {
  if (block.call == nullptr) {
    message = "a register outside a function is not in this slice";
    return Avm1RunStatus::Unsupported;
  }
  if (!register_in_range(*block.call, index, message)) {
    return Avm1RunStatus::InvalidStream;
  }
  state.stack.push_back(block.call->registers[index]);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus return_value(const Avm1Block& block, Avm1State& state, std::size_t& next,
                           std::string& message) {
  next = block.actions.size();
  if (block.call == nullptr) {
    return Avm1RunStatus::Ok;
  }
  return pop(state, block.call->return_value, message);
}

}  // namespace fo4::avm1::function_ops
