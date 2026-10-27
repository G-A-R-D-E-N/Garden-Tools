#include <fo4/avm2/replay.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::avm2 {

void apply_input(SwfRuntime& movie, Interpreter& interpreter, const ReplayInput& input) {
  switch (input.kind) {
    case ReplayInputKind::MouseMove:
      movie.mouse_move(interpreter, input.x, input.y);
      break;
    case ReplayInputKind::MouseDown:
      movie.mouse_event(interpreter, input.x, input.y, input.button, true);
      break;
    case ReplayInputKind::MouseUp:
      movie.mouse_event(interpreter, input.x, input.y, input.button, false);
      break;
    case ReplayInputKind::KeyDown:
      movie.key_event(interpreter, input.key_code, true);
      break;
    case ReplayInputKind::KeyUp:
      movie.key_event(interpreter, input.key_code, false);
      break;
    case ReplayInputKind::Char:
      movie.char_event(interpreter, input.key_code);
      break;
    case ReplayInputKind::Wheel:
      movie.mouse_wheel(interpreter, input.x, input.y, input.delta);
      break;
  }
}

ReplayResult replay(SwfRuntime& movie, Interpreter& interpreter,
                    const std::vector<ReplayInput>& inputs, double frame_dt_seconds,
                    std::size_t frame_count) {
  ReplayResult result;
  result.frame_hashes.reserve(frame_count);
  std::size_t cursor = 0;
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    const double now_ms =
        static_cast<double>(frame) * (frame_dt_seconds > 0.0 ? frame_dt_seconds : 0.0) * 1000.0;
    while (cursor < inputs.size() && inputs[cursor].time_ms <= now_ms) {
      apply_input(movie, interpreter, inputs[cursor]);
      ++cursor;
    }
    movie.advance(interpreter, frame_dt_seconds);
    const FrameSnapshot snapshot = movie.take_snapshot();
    result.frame_hashes.push_back(snapshot_hash(snapshot));
    ++result.frames;
  }
  return result;
}

}  // namespace fo4::avm2
