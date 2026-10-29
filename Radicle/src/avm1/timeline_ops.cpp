#include "timeline_ops.hpp"

#include "control_ops.hpp"
#include "text_ops.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace fo4::avm1::timeline_ops {

namespace {

constexpr std::uint8_t kCurrentFrame = 4;
constexpr std::uint8_t kTotalFrames = 5;

std::optional<std::uint8_t> standard_property(double number) {
  if (!(number > -1.0 && number < 14.0)) {
    return std::nullopt;
  }
  const int index = static_cast<int>(number);
  if (index == 11 || index == 12) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(index);
}

bool is_read_only(std::uint8_t index) { return index == kCurrentFrame || index == kTotalFrames; }

Avm1RunStatus next_frame(Avm1TimelineHost& host) {
  const std::uint32_t current = host.current_frame();
  if (current < host.total_frames()) {
    host.goto_frame(current + 1);
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus previous_frame(Avm1TimelineHost& host) {
  const std::uint32_t current = host.current_frame();
  if (current > 1) {
    host.goto_frame(current - 1);
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus go_to_label(Avm1TimelineHost& host, const std::string& label) {
  if (const auto frame = host.frame_for_label(label)) {
    host.goto_frame(*frame);
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus goto_frame_2(Avm1TimelineHost& host, Avm1State& state, const Avm1GotoFrame2& action,
                           std::string& message) {
  Avm1Data target;
  if (const Avm1RunStatus status = pop(state, target, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  std::string text = text_ops::to_text(target, state.swf_version);
  if (const std::size_t colon = text.find(':'); colon != std::string::npos) {
    text.erase(0, colon + 1);
  }
  const Avm1Data plain{.kind = Avm1DataKind::String, .text = text};
  const std::int64_t bias = action.scene_bias.value_or(0);
  std::int64_t frame = 0;
  if (!std::isnan(text_ops::to_number(plain, state.swf_version))) {
    frame = static_cast<std::int32_t>(text_ops::to_uint32(plain, state.swf_version)) + bias;
  } else if (const auto label = host.frame_for_label(text)) {
    frame = static_cast<std::int64_t>(*label) + bias;
  }
  if (frame <= 0) {
    return Avm1RunStatus::Ok;
  }
  if ((action.flags & 1u) != 0) {
    host.play();
  } else {
    host.stop();
  }
  host.goto_frame(static_cast<std::uint32_t>(frame));
  return Avm1RunStatus::Ok;
}

Avm1RunStatus get_property(Avm1TimelineHost& host, Avm1State& state, std::string& message) {
  Avm1Data index;
  Avm1Data target;
  if (const Avm1RunStatus status = pop(state, index, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, target, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const auto property = standard_property(text_ops::to_number(index, state.swf_version));
  state.stack.push_back(property ? host.get_property(*property) : Avm1Data{});
  return Avm1RunStatus::Ok;
}

Avm1RunStatus set_property(Avm1TimelineHost& host, Avm1State& state, std::string& message) {
  Avm1Data value;
  Avm1Data index;
  Avm1Data target;
  if (const Avm1RunStatus status = pop(state, value, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, index, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  if (const Avm1RunStatus status = pop(state, target, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  const auto property = standard_property(text_ops::to_number(index, state.swf_version));
  if (property && !is_read_only(*property)) {
    host.set_property(*property, value);
  }
  return Avm1RunStatus::Ok;
}

Avm1RunStatus set_target_2(Avm1TimelineHost& host, Avm1State& state, std::string& message) {
  Avm1Data target;
  if (const Avm1RunStatus status = pop(state, target, message); status != Avm1RunStatus::Ok) {
    return status;
  }
  host.set_target(text_ops::to_text(target, state.swf_version));
  return Avm1RunStatus::Ok;
}

}  // namespace

Avm1RunStatus execute(Avm1Run& run, const Avm1Block& block, const Avm1Action& action,
                      std::size_t& next, std::string& message) {
  Avm1TimelineHost* host = run.state.timeline;
  if (host == nullptr) {
    return control_ops::execute(run, block, action, next, message);
  }
  switch (action.code) {
    case 0x04:
      return next_frame(*host);
    case 0x05:
      return previous_frame(*host);
    case 0x06:
      host->play();
      return Avm1RunStatus::Ok;
    case 0x07:
      host->stop();
      return Avm1RunStatus::Ok;
    case 0x20:
      return set_target_2(*host, run.state, message);
    case 0x22:
      return get_property(*host, run.state, message);
    case 0x23:
      return set_property(*host, run.state, message);
    case 0x81:
      host->goto_frame(static_cast<std::uint32_t>(std::get<Avm1Frame>(action.payload).frame) + 1u);
      return Avm1RunStatus::Ok;
    case 0x8B:
      host->set_target(std::get<Avm1Text>(action.payload).text);
      return Avm1RunStatus::Ok;
    case 0x8C:
      return go_to_label(*host, std::get<Avm1Text>(action.payload).text);
    case 0x9F:
      return goto_frame_2(*host, run.state, std::get<Avm1GotoFrame2>(action.payload), message);
    default:
      return control_ops::execute(run, block, action, next, message);
  }
}

}  // namespace fo4::avm1::timeline_ops
