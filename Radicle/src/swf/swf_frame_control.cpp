#include "swf_frame_control.hpp"

#include <fo4/avm1/avm1.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
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
constexpr std::uint8_t kGoToLabel = 0x8C;
constexpr std::uint8_t kCall = 0x9E;

// A call must inline one level only, so a frame calling itself still terminates.
constexpr std::size_t kMaxCallDepth = 1;

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
  bool init_done{false};
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

using LabelMap = std::map<std::string, std::uint32_t>;

struct CollectedTimeline {
  std::vector<std::vector<avm1::Avm1Action>> frame_actions;
  LabelMap labels;
};

std::string frame_label_of(const SwfTag& tag) {
  std::string label;
  for (const std::byte byte : tag.payload) {
    if (byte == std::byte{0}) {
      break;
    }
    label.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
  }
  return label;
}

CollectedTimeline collect_actions(const std::vector<SwfTag>& tags) {
  CollectedTimeline out;
  std::vector<avm1::Avm1Action> current;
  for (const SwfTag& tag : tags) {
    if (tag.code == tag::kShowFrame) {
      out.frame_actions.push_back(std::move(current));
      current.clear();
      continue;
    }
    if (tag.code == tag::kFrameLabel) {
      // A label must address the frame it opens, so it indexes the frames collected so far.
      out.labels.emplace(frame_label_of(tag),
                         static_cast<std::uint32_t>(out.frame_actions.size()));
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
    current.insert(current.end(), stream.actions.begin(), stream.actions.end());
  }
  if (out.frame_actions.empty()) {
    out.frame_actions.push_back(std::move(current));
  }
  return out;
}

std::optional<std::uint32_t> call_target_frame(const avm1::Avm1Value& value,
                                               const LabelMap& labels) {
  if (value.type == avm1::Avm1ValueType::String) {
    const auto found = labels.find(value.text);
    if (found == labels.end()) {
      return std::nullopt;
    }
    return found->second;
  }
  const auto number = number_of(value);
  const auto frame = number ? frame_number(*number) : std::nullopt;
  if (!frame) {
    return std::nullopt;
  }
  return *frame - 1;
}

void append_controls(const std::vector<avm1::Avm1Action>& actions,
                     const CollectedTimeline& timeline, std::size_t depth,
                     std::vector<FrameControl>& out, std::size_t& budget) {
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
      case kGoToLabel: {
        // A label jump must land where the display list lands, so it resolves through the same map.
        const auto* text = std::get_if<avm1::Avm1Text>(&action.payload);
        const auto found =
            text == nullptr ? timeline.labels.end() : timeline.labels.find(text->text);
        if (found != timeline.labels.end()) {
          out.push_back({FrameControlKind::go_to, found->second});
          out.push_back({FrameControlKind::stop, 0});
        }
        break;
      }
      case kCall: {
        // A call must run the target frame inline, so a shared routine takes effect.
        const auto* push =
            previous == nullptr ? nullptr : std::get_if<avm1::Avm1Push>(&previous->payload);
        if (push == nullptr || push->values.empty() || depth >= kMaxCallDepth) {
          break;
        }
        const auto target = call_target_frame(push->values.back(), timeline.labels);
        if (!target || *target >= timeline.frame_actions.size()) {
          break;
        }
        const std::vector<avm1::Avm1Action>& body = timeline.frame_actions[*target];
        // A call must walk its whole body or do nothing, so the walk stays inside the budget.
        if (body.size() > budget) {
          break;
        }
        budget -= body.size();
        append_controls(body, timeline, depth + 1, out, budget);
        break;
      }
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
        out.push_back(
            {(jump->flags & 1) != 0 ? FrameControlKind::play : FrameControlKind::stop, 0});
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
    // Init controls must run once per simulation, so later landings on frame 0 skip to the marker.
    bool skip = cursor.init_done && cursor.frame == 0;
    for (const FrameControl& control : controls[cursor.frame]) {
      if (control.kind == FrameControlKind::init_barrier) {
        cursor.init_done = true;
        skip = false;
        continue;
      }
      if (skip) {
        continue;
      }
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
        case FrameControlKind::init_barrier:
          // The guard above must consume the barrier, so this case never runs.
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

FrameControls collect_frame_controls(const std::vector<SwfTag>& tags,
                                     const InitActionStreams& init, std::size_t& budget) {
  CollectedTimeline collected = collect_actions(tags);
  // Init actions must run once at instantiation, before the sprite's first frame.
  std::vector<avm1::Avm1Action> init_actions;
  for (const auto& stream : init) {
    if (!has_unmodelled_flow(stream)) {
      init_actions.insert(init_actions.end(), stream.begin(), stream.end());
    }
  }
  FrameControls frames;
  bool first = true;
  for (const auto& actions : collected.frame_actions) {
    std::vector<FrameControl> current;
    if (first && !init_actions.empty()) {
      append_controls(init_actions, collected, 0, current, budget);
      current.push_back({FrameControlKind::init_barrier, 0});
    }
    first = false;
    append_controls(actions, collected, 0, current, budget);
    frames.push_back(std::move(current));
  }
  return frames;
}

std::map<std::uint16_t, InitActionStreams> collect_init_actions(
    const std::vector<SwfTag>& tags) {
  std::map<std::uint16_t, InitActionStreams> out;
  for (const SwfTag& tag : tags) {
    // By invariant the sprite id lives on the action; only DefineSprite sets the tag-level id.
    if (tag.code != tag::kDoInitAction || !tag.action) {
      continue;
    }
    const auto bytes =
        std::span<const std::byte>(tag.payload).subspan(tag.action->offset, tag.action->size);
    const avm1::Avm1Stream stream = avm1::decode_actions(bytes);
    if (!stream.ok()) {
      continue;
    }
    out[tag.action->sprite_id].push_back(stream.actions);
  }
  return out;
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
      break;
    }
  }
  return head;
}

}  // namespace fo4::swf::detail
