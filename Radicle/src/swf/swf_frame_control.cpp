#include "swf_frame_control.hpp"

#include <fo4/avm1/avm1.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace fo4::swf::detail {

namespace {

constexpr std::uint8_t kNextFrame = 0x04;
constexpr std::uint8_t kPreviousFrame = 0x05;
constexpr std::uint8_t kPlay = 0x06;
constexpr std::uint8_t kStop = 0x07;
constexpr std::uint8_t kGotoFrame = 0x81;
constexpr std::uint8_t kGotoFrame2 = 0x9F;

constexpr std::array<std::uint8_t, 10> kUnmodelledFlow{0x20, 0x8A, 0x8B, 0x8D, 0x8E,
                                                       0x8F, 0x94, 0x99, 0x9B, 0x9D};

constexpr std::size_t kMaxHops = 64;
constexpr std::size_t kEntriesPerTick = 4;
constexpr std::size_t kEntrySlack = 1024;

struct Cursor {
  std::uint32_t frame{0};
  bool playing{true};
  std::size_t recorded{0};
  std::size_t budget{0};
};

bool has_unmodelled_flow(const std::vector<avm1::Avm1Action>& actions) {
  return std::any_of(actions.begin(), actions.end(), [](const avm1::Avm1Action& action) {
    return std::find(kUnmodelledFlow.begin(), kUnmodelledFlow.end(), action.code) !=
           kUnmodelledFlow.end();
  });
}

std::optional<double> number_of(const avm1::Avm1Value& value) {
  switch (value.type) {
    case avm1::Avm1ValueType::Float:
      return std::bit_cast<float>(value.float_bits);
    case avm1::Avm1ValueType::Double:
      return std::bit_cast<double>(value.double_bits);
    case avm1::Avm1ValueType::Integer:
      return static_cast<double>(value.integer);
    default:
      return std::nullopt;
  }
}

std::optional<std::uint32_t> frame_number(double value) {
  if (!(value >= 1.0 && value <= 65535.0 && std::floor(value) == value)) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

void append_controls(const std::vector<avm1::Avm1Action>& actions, std::vector<FrameControl>& out) {
  const avm1::Avm1Action* previous = nullptr;
  for (const avm1::Avm1Action& action : actions) {
    switch (action.code) {
      case kStop:
        out.push_back({FrameControlKind::stop, 0});
        break;
      case kPlay:
        out.push_back({FrameControlKind::play, 0});
        break;
      case kNextFrame:
        out.push_back({FrameControlKind::next, 0});
        break;
      case kPreviousFrame:
        out.push_back({FrameControlKind::previous, 0});
        break;
      case kGotoFrame:
        if (const auto* jump = std::get_if<avm1::Avm1Frame>(&action.payload)) {
          out.push_back({FrameControlKind::go_to, jump->frame});
          out.push_back({FrameControlKind::stop, 0});
        }
        break;
      case kGotoFrame2: {
        const auto* jump = std::get_if<avm1::Avm1GotoFrame2>(&action.payload);
        const auto* push =
            previous == nullptr ? nullptr : std::get_if<avm1::Avm1Push>(&previous->payload);
        if (jump == nullptr || jump->scene_bias.has_value() || push == nullptr ||
            push->values.empty()) {
          break;
        }
        const auto number = number_of(push->values.back());
        const auto frame = number ? frame_number(*number) : std::nullopt;
        if (!frame) {
          break;
        }
        out.push_back({FrameControlKind::go_to, *frame - 1});
        out.push_back({(jump->flags & 1) != 0 ? FrameControlKind::play : FrameControlKind::stop, 0});
        break;
      }
      default:
        break;
    }
    previous = &action;
  }
}

void enter(Playhead& head, Cursor& cursor, std::uint32_t to, std::uint32_t tick) {
  const std::uint32_t first = to > cursor.frame ? cursor.frame + 1 : to;
  for (std::uint32_t frame = first; frame <= to; ++frame) {
    std::vector<std::uint32_t>& ticks = head.entries[frame];
    if (ticks.empty() || ticks.back() != tick) {
      ticks.push_back(tick);
      ++cursor.recorded;
    }
  }
}

bool land(const FrameControls& controls, std::uint32_t tick, std::uint32_t target, Cursor& cursor,
          Playhead& head) {
  const auto count = static_cast<std::uint32_t>(controls.size());
  const std::size_t max_hops = std::min(controls.size() + 1, kMaxHops);
  bool moved = false;
  for (std::size_t hop = 0; hop < max_hops; ++hop) {
    if (target != cursor.frame) {
      if (cursor.recorded >= cursor.budget) {
        return moved;
      }
      enter(head, cursor, target, tick);
      cursor.frame = target;
      moved = true;
    }
    std::optional<std::uint32_t> jump;
    for (const FrameControl& control : controls[cursor.frame]) {
      switch (control.kind) {
        case FrameControlKind::stop:
          cursor.playing = false;
          break;
        case FrameControlKind::play:
          cursor.playing = true;
          break;
        case FrameControlKind::go_to:
          jump = std::min(control.frame, count - 1);
          break;
        case FrameControlKind::next:
          if (cursor.frame + 1 < count) {
            jump = cursor.frame + 1;
            cursor.playing = false;
          }
          break;
        case FrameControlKind::previous:
          if (cursor.frame > 0) {
            jump = cursor.frame - 1;
            cursor.playing = false;
          }
          break;
      }
    }
    if (!jump) {
      return moved;
    }
    target = *jump;
  }
  return moved;
}

}  // namespace

FrameControls collect_frame_controls(const std::vector<SwfTag>& tags) {
  FrameControls frames;
  std::vector<FrameControl> current;
  for (const SwfTag& tag : tags) {
    if (tag.code == tag::kShowFrame) {
      frames.push_back(std::move(current));
      current.clear();
      continue;
    }
    if (tag.code != tag::kDoAction || !tag.action) {
      continue;
    }
    const auto bytes =
        std::span<const std::byte>(tag.payload).subspan(tag.action->offset, tag.action->size);
    const avm1::Avm1Stream stream = avm1::decode_actions(bytes);
    if (!stream.ok() || has_unmodelled_flow(stream.actions)) {
      continue;
    }
    append_controls(stream.actions, current);
  }
  if (frames.empty()) {
    frames.emplace_back();
  }
  return frames;
}

Playhead simulate_playhead(const FrameControls& controls, std::uint32_t ticks) {
  if (std::all_of(controls.begin(), controls.end(),
                  [](const std::vector<FrameControl>& frame) { return frame.empty(); })) {
    return {};
  }
  const auto count = static_cast<std::uint32_t>(controls.size());
  Playhead head;
  head.entries.resize(count);
  head.frame_at.reserve(ticks);
  head.entries[0].push_back(0);
  Cursor cursor;
  cursor.recorded = 1;
  cursor.budget = kEntriesPerTick * ticks + kEntrySlack;
  land(controls, 0, 0, cursor, head);
  head.frame_at.push_back(cursor.frame);
  for (std::uint32_t tick = 1; tick < ticks; ++tick) {
    const bool was_playing = cursor.playing;
    const bool moved = was_playing && land(controls, tick, (cursor.frame + 1) % count, cursor, head);
    head.frame_at.push_back(cursor.frame);
    if (!moved && cursor.playing == was_playing) {
      head.frame_at.resize(ticks, cursor.frame);
      break;
    }
  }
  return head;
}

}  // namespace fo4::swf::detail
