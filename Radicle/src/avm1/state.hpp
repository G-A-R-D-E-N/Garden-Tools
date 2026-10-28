#pragma once

#include <fo4/avm1/interpreter.hpp>

#include <cstdint>
#include <map>
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
  std::uint8_t swf_version{0};
};

inline Avm1RunStatus pop(Avm1State& state, Avm1Data& out, std::string& message) {
  if (state.stack.empty()) {
    message = kUnderflow;
    return Avm1RunStatus::StackUnderflow;
  }
  out = std::move(state.stack.back());
  state.stack.pop_back();
  return Avm1RunStatus::Ok;
}

}  // namespace fo4::avm1
