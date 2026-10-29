#pragma once

#include <fo4/avm1/interpreter.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm1 {

inline constexpr const char* kUnderflow = "the operand stack has too few values";

struct Avm1State {
  std::vector<Avm1Data> stack;
  std::vector<std::string> constants;
  std::map<std::string, Avm1Data> variables;
  std::vector<std::string> trace;
  Avm1Data thrown;
  std::uint8_t swf_version{0};
  std::uint32_t random_state{0};
  std::uint32_t elapsed_ms{0};
};

struct Avm1Call {
  std::map<std::string, Avm1Data> locals;
  std::vector<Avm1Data> registers;
  Avm1Data return_value;
};

struct Avm1FunctionBody {
  Avm1Function header;
  std::vector<Avm1Action> actions;
  std::size_t end_offset{0};
};

struct Avm1Block {
  const std::vector<Avm1Action>& actions;
  std::size_t end_offset;
  Avm1Call* call;
  std::vector<Avm1Data>* scopes;
};

struct Avm1Run {
  Avm1State& state;
  Avm1Result& result;
  std::size_t max_steps;
  std::size_t depth{0};
  std::map<std::size_t, std::shared_ptr<const Avm1FunctionBody>> functions{};
  std::size_t regions{0};
  std::map<const Avm1FunctionBody*, std::shared_ptr<const Avm1FunctionBody>> superclasses{};
  std::map<const Avm1FunctionBody*, std::vector<std::shared_ptr<const Avm1FunctionBody>>>
      implements{};
};

Avm1RunStatus run_block(Avm1Run& run, const Avm1Block& block);
Avm1RunStatus run_range(Avm1Run& run, const Avm1Block& block, std::size_t first, std::size_t last,
                        std::size_t& resume);
Avm1RunStatus jump_index(const Avm1Block& block, std::size_t target, std::size_t& index,
                         std::string& message);

inline Avm1RunStatus pop(Avm1State& state, Avm1Data& out, std::string& message) {
  if (state.stack.empty()) {
    message = kUnderflow;
    return Avm1RunStatus::StackUnderflow;
  }
  out = std::move(state.stack.back());
  state.stack.pop_back();
  return Avm1RunStatus::Ok;
}

inline Avm1RunStatus pop_count(Avm1State& state, std::size_t slots, std::size_t& count,
                               std::string& message) {
  Avm1Data operand;
  if (const Avm1RunStatus status = pop(state, operand, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const double limit = static_cast<double>(state.stack.size() / slots);
  if (operand.kind != Avm1DataKind::Number || !(operand.number >= 0.0 && operand.number <= limit) ||
      std::floor(operand.number) != operand.number) {
    message = "the element count is not a whole number the operand stack holds";
    return Avm1RunStatus::StackUnderflow;
  }
  count = static_cast<std::size_t>(operand.number);
  return Avm1RunStatus::Ok;
}

}  // namespace fo4::avm1
