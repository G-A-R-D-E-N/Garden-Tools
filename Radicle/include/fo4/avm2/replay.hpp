#pragma once

#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/swf_runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::avm2 {

enum class ReplayInputKind : std::uint8_t {
  MouseMove = 0,
  MouseDown,
  MouseUp,
  KeyDown,
  KeyUp,
  Char,
  Wheel,
};

struct ReplayInput {
  double time_ms{0.0};
  ReplayInputKind kind{ReplayInputKind::MouseMove};
  double x{0.0};
  double y{0.0};
  std::int32_t button{0};
  std::uint32_t key_code{0};
  std::int32_t delta{0};
};

/* The host must feed inputs in nondecreasing time order; a replay of the same log and dt sequence
   must produce the same hash sequence. */
struct ReplayResult {
  std::vector<std::uint64_t> frame_hashes;
  std::size_t frames{0};
};

void apply_input(SwfRuntime& movie, Interpreter& interpreter, const ReplayInput& input);

ReplayResult replay(SwfRuntime& movie, Interpreter& interpreter,
                    const std::vector<ReplayInput>& inputs, double frame_dt_seconds,
                    std::size_t frame_count);

}  // namespace fo4::avm2
