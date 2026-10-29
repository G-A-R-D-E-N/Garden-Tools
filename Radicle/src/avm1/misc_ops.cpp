#include "misc_ops.hpp"

#include "control_ops.hpp"
#include "text_ops.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::avm1::misc_ops {

namespace {

constexpr double kMaxRandomBound = 2147483647.0;
constexpr std::uint32_t kRandomMask = 0x7FFFFFFFu;

void push_number(Avm1State& state, double number) {
  state.stack.push_back(Avm1Data{.kind = Avm1DataKind::Number, .number = number});
}

std::uint32_t next_random(Avm1State& state) {
  std::uint32_t value = state.random_state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  state.random_state = value;
  return value & kRandomMask;
}

Avm1RunStatus random_number(Avm1State& state, std::string& message) {
  Avm1Data bound;
  if (const Avm1RunStatus status = pop(state, bound, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const double limit = text_ops::to_number(bound, state.swf_version);
  if (!(limit >= 1.0)) {
    push_number(state, 0.0);
    return Avm1RunStatus::Ok;
  }
  const auto modulus = static_cast<std::uint32_t>(std::min(std::trunc(limit), kMaxRandomBound));
  push_number(state, static_cast<double>(next_random(state) % modulus));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus get_time(Avm1State& state) {
  push_number(state, static_cast<double>(state.elapsed_ms));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus target_path(Avm1State& state, std::string& message) {
  Avm1Data target;
  if (const Avm1RunStatus status = pop(state, target, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  state.stack.push_back(Avm1Data{});
  return Avm1RunStatus::Ok;
}

}  // namespace

Avm1RunStatus execute(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                      std::size_t& next, std::string& message) {
  switch (action.code) {
    case 0x30:
      return random_number(run.state, message);
    case 0x34:
      return get_time(run.state);
    case 0x45:
      return target_path(run.state, message);
    case 0x89:
      return Avm1RunStatus::Ok;
    default:
      return control_ops::execute(run, block, action, next, message);
  }
}

}  // namespace fo4::avm1::misc_ops
