#include "control_ops.hpp"

#include "function_ops.hpp"
#include "object_ops.hpp"
#include "text_ops.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::avm1::control_ops {

namespace {

// Invariant: Try and With share one nesting budget per run, so recursion through them stays bounded.
constexpr std::size_t kMaxNesting = 64;

Avm1RunStatus nesting_limit(std::string& message) {
  message = "Try and With nesting reached the limit of " + std::to_string(kMaxNesting) + " levels";
  return Avm1RunStatus::ResourceLimit;
}

bool reaches(const Avm1Run& run, const Avm1FunctionBody* from, const Avm1FunctionBody* target) {
  std::vector<const Avm1FunctionBody*> pending{from};
  std::set<const Avm1FunctionBody*> seen;
  while (!pending.empty()) {
    const Avm1FunctionBody* current = pending.back();
    pending.pop_back();
    if (current == nullptr) {
      continue;
    }
    if (current == target) {
      return true;
    }
    if (!seen.insert(current).second) {
      continue;
    }
    const auto parent = run.superclasses.find(current);
    if (parent != run.superclasses.end()) {
      pending.push_back(parent->second.get());
    }
    const auto listed = run.implements.find(current);
    if (listed != run.implements.end()) {
      for (const auto& implemented : listed->second) {
        pending.push_back(implemented.get());
      }
    }
  }
  return false;
}

bool instance_of(const Avm1Run& run, const Avm1Data& value, const Avm1Data& constructor) {
  return value.kind == Avm1DataKind::Object && constructor.kind == Avm1DataKind::Function &&
         reaches(run, value.object->constructed_by.get(), constructor.function.get());
}

Avm1RunStatus pop_pair(Avm1State& state, Avm1Data& below, Avm1Data& top, std::string& message) {
  if (const Avm1RunStatus status = pop(state, top, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  return pop(state, below, message);
}

void push_names(Avm1State& state, const Avm1Data& object) {
  state.stack.push_back(Avm1Data{.kind = Avm1DataKind::Null});
  if (object.kind != Avm1DataKind::Object) {
    return;
  }
  for (auto key = object.object->order.begin(); key != object.object->order.end(); ++key) {
    state.stack.push_back(Avm1Data{.kind = Avm1DataKind::String, .text = *key});
  }
}

Avm1RunStatus enumerate_object(Avm1State& state, std::string& message) {
  Avm1Data object;
  if (const Avm1RunStatus status = pop(state, object, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  push_names(state, object);
  return Avm1RunStatus::Ok;
}

Avm1RunStatus enumerate_variable(const Avm1Block& block, Avm1State& state, std::string& message) {
  if (const Avm1RunStatus status = function_ops::get_variable(block, state, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  return enumerate_object(state, message);
}

Avm1RunStatus throw_value(Avm1State& state, std::string& message) {
  if (const Avm1RunStatus status = pop(state, state.thrown, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  return Avm1RunStatus::Thrown;
}

Avm1RunStatus instance_of_op(Avm1Run& run, std::string& message) {
  Avm1Data value;
  Avm1Data constructor;
  if (const Avm1RunStatus status = pop_pair(run.state, value, constructor, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  run.state.stack.push_back(Avm1Data{.kind = Avm1DataKind::Boolean,
                                     .boolean = instance_of(run, value, constructor)});
  return Avm1RunStatus::Ok;
}

Avm1RunStatus cast_op(Avm1Run& run, std::string& message) {
  Avm1Data value;
  Avm1Data constructor;
  if (const Avm1RunStatus status = pop_pair(run.state, constructor, value, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  const bool keeps = instance_of(run, value, constructor);
  run.state.stack.push_back(keeps ? std::move(value) : Avm1Data{.kind = Avm1DataKind::Null});
  return Avm1RunStatus::Ok;
}

Avm1RunStatus implements_op(Avm1Run& run, std::string& message) {
  Avm1State& state = run.state;
  Avm1Data constructor;
  if (const Avm1RunStatus status = pop(state, constructor, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  Avm1Data count;
  if (const Avm1RunStatus status = pop(state, count, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const auto wanted = static_cast<std::size_t>(
      static_cast<std::int32_t>(text_ops::to_uint32(count, state.swf_version)));
  const std::size_t listed = std::min(wanted, state.stack.size());
  std::vector<std::shared_ptr<const Avm1FunctionBody>> interfaces;
  for (std::size_t index = 0; index < listed; ++index) {
    Avm1Data value;
    if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
      return status;
    }
    if (value.kind == Avm1DataKind::Function) {
      interfaces.push_back(value.function);
    }
  }
  const bool versioned = state.swf_version == 0 || state.swf_version >= 7;
  if (listed > 0 && versioned && constructor.kind == Avm1DataKind::Function) {
    // Invariant: the first ImplementsOp on a class wins, as Ruffle's set_interfaces does.
    run.implements.try_emplace(constructor.function.get(), std::move(interfaces));
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus extends(Avm1Run& run, std::string& message) {
  Avm1Data sub;
  Avm1Data base;
  if (const Avm1RunStatus status = pop_pair(run.state, sub, base, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  if (sub.kind != Avm1DataKind::Function || base.kind != Avm1DataKind::Function) {
    message = "Extends of a value that is not a function is not in this slice";
    return Avm1RunStatus::Unsupported;
  }
  if (reaches(run, base.function.get(), sub.function.get())) {
    message = "a class cannot extend itself, directly or through its superclasses";
    return Avm1RunStatus::InvalidStream;
  }
  run.superclasses[sub.function.get()] = base.function;
  return Avm1RunStatus::Ok;
}

Avm1RunStatus try_regions(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                          std::size_t& next, std::string& message) {
  const auto& header = std::get<Avm1Try>(action.payload);
  const bool has_catch = (header.flags & 1) != 0;
  const bool has_finally = (header.flags & 2) != 0;
  const bool has_register = (header.flags & 4) != 0;
  const std::size_t catch_size = has_catch ? header.catch_size : 0u;
  const std::size_t finally_size = has_finally ? header.finally_size : 0u;
  const std::size_t body = action.offset + action.length;
  const std::size_t catch_at = body + header.try_size;
  const std::size_t finally_at = catch_at + catch_size;
  const std::size_t after_at = finally_at + finally_size;
  const std::size_t offsets[4] = {body, catch_at, finally_at, after_at};
  std::size_t at[4] = {};
  for (std::size_t slot = 0; slot < 4; ++slot) {
    if (const Avm1RunStatus status = jump_index(block, offsets[slot], at[slot], message);
        status != Avm1RunStatus::Ok) {
      return status;
    }
  }
  Avm1State& state = run.state;
  const std::size_t depth = state.stack.size();
  std::size_t resume = 0;
  std::size_t leave = at[3];
  Avm1RunStatus status = run_range(run, block, at[0], at[1], resume);
  if (status == Avm1RunStatus::Ok && resume != at[1]) {
    leave = resume;
  }
  if (status == Avm1RunStatus::Thrown) {
    if (state.stack.size() > depth) {
      state.stack.resize(depth);
    }
    if (has_catch) {
      Avm1Data caught = std::move(state.thrown);
      if (has_register) {
        state.stack.push_back(caught);
        status = function_ops::store_register(block, state, header.catch_register, message);
        state.stack.pop_back();
      } else {
        (block.call != nullptr ? block.call->locals[header.catch_name]
                               : state.variables[header.catch_name]) = std::move(caught);
        // Must mark the throw handled: status is still Thrown here, and the catch body runs only on Ok.
        status = Avm1RunStatus::Ok;
      }
      if (status == Avm1RunStatus::Ok) {
        status = run_range(run, block, at[1], at[2], resume);
        if (status == Avm1RunStatus::Ok && resume != at[2]) {
          leave = resume;
        }
      }
    }
  }
  if (status != Avm1RunStatus::Ok && status != Avm1RunStatus::Thrown) {
    return status;
  }
  if (has_finally) {
    const bool thrown = status == Avm1RunStatus::Thrown;
    Avm1Data pending = std::move(state.thrown);
    const Avm1RunStatus finally_status = run_range(run, block, at[2], at[3], resume);
    if (finally_status != Avm1RunStatus::Ok) {
      return finally_status;
    }
    if (resume != at[3]) {
      next = resume;
      return Avm1RunStatus::Ok;
    }
    if (thrown) {
      state.thrown = std::move(pending);
      return Avm1RunStatus::Thrown;
    }
  }
  if (status == Avm1RunStatus::Thrown) {
    return status;
  }
  next = leave;
  return Avm1RunStatus::Ok;
}

Avm1RunStatus run_try(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                      std::size_t& next, std::string& message) {
  if (run.regions == kMaxNesting) {
    return nesting_limit(message);
  }
  run.regions += 1;
  const Avm1RunStatus status = try_regions(run, block, action, next, message);
  run.regions -= 1;
  return status;
}

Avm1RunStatus run_with(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                       std::size_t& next, std::string& message) {
  Avm1Data object;
  if (const Avm1RunStatus status = pop(run.state, object, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const std::size_t body = action.offset + action.length;
  const std::size_t body_end = body + std::get<Avm1With>(action.payload).body_size;
  std::size_t first = 0;
  std::size_t last = 0;
  if (const Avm1RunStatus status = jump_index(block, body, first, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = jump_index(block, body_end, last, message);
      status != Avm1RunStatus::Ok) {
    return status;
  }
  if (object.kind != Avm1DataKind::Object) {
    next = last;
    return Avm1RunStatus::Ok;
  }
  if (run.regions == kMaxNesting) {
    return nesting_limit(message);
  }
  run.regions += 1;
  block.scopes->push_back(std::move(object));
  std::size_t resume = last;
  const Avm1RunStatus status = run_range(run, block, first, last, resume);
  block.scopes->pop_back();
  run.regions -= 1;
  if (status == Avm1RunStatus::Ok) {
    next = resume;
  }
  return status;
}

}  // namespace

Avm1RunStatus execute(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                      std::size_t& next, std::string& message) {
  switch (action.code) {
    case 0x2A:
      return throw_value(run.state, message);
    case 0x2B:
      return cast_op(run, message);
    case 0x2C:
      return implements_op(run, message);
    case 0x46:
      return enumerate_variable(block, run.state, message);
    case 0x54:
      return instance_of_op(run, message);
    case 0x55:
      return enumerate_object(run.state, message);
    case 0x69:
      return extends(run, message);
    case 0x8F:
      return run_try(run, block, action, next, message);
    case 0x94:
      return run_with(run, block, action, next, message);
    default:
      message = "this opcode is not in the AVM1 interpreter slice";
      return Avm1RunStatus::Unsupported;
  }
}

}  // namespace fo4::avm1::control_ops
