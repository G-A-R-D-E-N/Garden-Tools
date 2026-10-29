#include <fo4/avm1/avm1.hpp>
#include <fo4/avm1/interpreter.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using fo4::avm1::Avm1DataKind;
using fo4::avm1::Avm1RunStatus;

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

void append(Bytes& out, const Bytes& part) { out.insert(out.end(), part.begin(), part.end()); }

Bytes rec(std::uint8_t code, const Bytes& payload) {
  Bytes out{code};
  if (code >= 0x80) {
    out.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
  }
  append(out, payload);
  return out;
}

Bytes le16(std::size_t value) {
  return Bytes{static_cast<std::uint8_t>(value & 0xFFu),
               static_cast<std::uint8_t>((value >> 8) & 0xFFu)};
}

Bytes str(std::string_view text) {
  Bytes out(text.begin(), text.end());
  out.push_back(0);
  return out;
}

Bytes text_value(std::string_view text) {
  Bytes out{0x00};
  append(out, str(text));
  return out;
}

Bytes int_value(std::uint32_t value) {
  Bytes out{0x07};
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  return out;
}

Bytes push(std::initializer_list<Bytes> values) {
  Bytes payload;
  for (const Bytes& value : values) {
    append(payload, value);
  }
  return rec(0x96, payload);
}

fo4::avm1::Avm1Stream decode(const Bytes& input) {
  std::vector<std::byte> bytes;
  for (const std::uint8_t value : input) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return fo4::avm1::decode_actions(bytes);
}

struct RecordingTimeline : fo4::avm1::Avm1TimelineHost {
  std::vector<std::string> calls;
  std::uint32_t frame{1};
  std::uint32_t frames{3};
  std::map<std::string, std::uint32_t> labels;
  fo4::avm1::Avm1Data last_value;

  void goto_frame(std::uint32_t target) override {
    calls.push_back("goto " + std::to_string(target));
    frame = target;
  }
  void play() override { calls.push_back("play"); }
  void stop() override { calls.push_back("stop"); }
  std::uint32_t current_frame() override {
    calls.push_back("current");
    return frame;
  }
  std::uint32_t total_frames() override {
    calls.push_back("total");
    return frames;
  }
  std::optional<std::uint32_t> frame_for_label(const std::string& label) override {
    calls.push_back("label " + label);
    const auto found = labels.find(label);
    if (found == labels.end()) {
      return std::nullopt;
    }
    return found->second;
  }
  fo4::avm1::Avm1Data get_property(std::uint8_t index) override {
    calls.push_back("get " + std::to_string(index));
    return fo4::avm1::Avm1Data{.kind = fo4::avm1::Avm1DataKind::Number, .number = 10.0 * index};
  }
  void set_property(std::uint8_t index, const fo4::avm1::Avm1Data& value) override {
    calls.push_back("set " + std::to_string(index));
    last_value = value;
  }
};

fo4::avm1::Avm1Result execute_with_timeline(const Bytes& input, RecordingTimeline& timeline) {
  const auto stream = decode(input);
  EXPECT_TRUE(stream.ok()) << stream.message;
  return fo4::avm1::execute_actions(stream, 1000000, 0,
                                    fo4::avm1::Avm1Environment{.timeline = &timeline});
}

// Invariant: GotoFrame reads its wire frame as 0-based and Play and Stop leave the stack alone.
TEST(Avm1Timeline, GotoFramePlayAndStopCallTheHostOnceWithoutStackEffects) {
  RecordingTimeline timeline;
  const auto result = execute_with_timeline(
      cat({push({int_value(5)}), rec(0x81, le16(4)), rec(0x06, {}), rec(0x07, {}), rec(0x26, {}),
           Bytes{0x00}}),
      timeline);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"goto 5", "play", "stop"}));
  EXPECT_EQ(result.trace, (std::vector<std::string>{"5"}));
}

// Invariant: NextFrame and PreviousFrame make no goto at the last and first frames.
TEST(Avm1Timeline, NextAndPreviousFrameStayInsideTheTimeline) {
  RecordingTimeline timeline;
  const auto result = execute_with_timeline(
      cat({rec(0x04, {}), rec(0x04, {}), rec(0x04, {}), rec(0x05, {}), rec(0x05, {}), rec(0x05, {}),
           Bytes{0x00}}),
      timeline);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(timeline.calls,
            (std::vector<std::string>{"current", "total", "goto 2", "current", "total", "goto 3",
                                      "current", "total", "current", "goto 2", "current", "goto 1",
                                      "current"}));
}

// Invariant: GoToLabel with a label the host does not know is a no-op and the run continues.
TEST(Avm1Timeline, GoToLabelJumpsToAKnownLabelAndIgnoresAnUnknownOne) {
  RecordingTimeline timeline;
  timeline.labels["intro"] = 3;
  const auto result = execute_with_timeline(
      cat({rec(0x8C, str("intro")), rec(0x8C, str("missing")), Bytes{0x00}}), timeline);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(timeline.calls,
            (std::vector<std::string>{"label intro", "goto 3", "label missing"}));
}

void run_goto_frame_2(RecordingTimeline& timeline, const Bytes& target, const Bytes& flags) {
  const auto result = execute_with_timeline(
      cat({push({int_value(7), target}), rec(0x9F, flags), rec(0x26, {}), Bytes{0x00}}), timeline);
  EXPECT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(result.trace, (std::vector<std::string>{"7"}));
}

// Invariant: GotoFrame2 with the play bit pops a numeric frame and calls play before the goto.
TEST(Avm1Timeline, GotoFrame2NumericWithPlayPlaysBeforeTheGoto) {
  RecordingTimeline timeline;
  run_goto_frame_2(timeline, int_value(5), {0x01});
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"play", "goto 5"}));
}

// Invariant: GotoFrame2 without the play bit stops, and its scene bias adds to a numeric frame.
TEST(Avm1Timeline, GotoFrame2NumericWithoutPlayStopsAndAddsSceneBias) {
  RecordingTimeline timeline;
  run_goto_frame_2(timeline, int_value(5), {0x02, 0x02, 0x00});
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"stop", "goto 7"}));
}

// Invariant: GotoFrame2 jumps to a label the host knows, calling play before the goto.
TEST(Avm1Timeline, GotoFrame2LabelJumpsToItsFrame) {
  RecordingTimeline timeline;
  timeline.labels["intro"] = 3;
  run_goto_frame_2(timeline, text_value("intro"), {0x01});
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"label intro", "play", "goto 3"}));
}

// Invariant: GotoFrame2 with a label the host does not know calls no play, stop or goto.
TEST(Avm1Timeline, GotoFrame2UnknownLabelIsANoOp) {
  RecordingTimeline timeline;
  run_goto_frame_2(timeline, text_value("missing"), {0x01});
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"label missing"}));
}

// Invariant: GotoFrame2 drops a target path before its colon and resolves only the label after it.
TEST(Avm1Timeline, GotoFrame2ResolvesOnlyTheLabelAfterATargetPath) {
  RecordingTimeline timeline;
  timeline.labels["intro"] = 3;
  run_goto_frame_2(timeline, text_value("clip:intro"), {0x01});
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"label intro", "play", "goto 3"}));
}

// Invariant: GotoFrame2 with a numeric frame that resolves to 0 makes no host call, even with the play bit.
TEST(Avm1Timeline, GotoFrame2NumericZeroIsANoOp) {
  RecordingTimeline timeline;
  run_goto_frame_2(timeline, int_value(0), {0x01});
  EXPECT_TRUE(timeline.calls.empty());
}

// Invariant: GotoFrame2 with the play bit clear stops, then goes to the label frame plus the scene bias.
TEST(Avm1Timeline, GotoFrame2LabelWithoutPlayStopsAndAddsSceneBias) {
  RecordingTimeline timeline;
  timeline.labels["intro"] = 3;
  run_goto_frame_2(timeline, text_value("intro"), {0x02, 0x02, 0x00});
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"label intro", "stop", "goto 5"}));
}

struct PropertyRow {
  const char* name;
  Bytes index;
  std::vector<std::string> calls;
  std::string value;
};

// Invariant: GetProperty pops its index and target path, pushes one value, and reads undefined for an index outside the standard set.
TEST(Avm1Timeline, GetPropertyMapsItsIndexAndPushesOneValue) {
  const std::vector<PropertyRow> rows = {
      {"_xscale as the integer 2", int_value(2), {"get 2"}, "20"},
      {"_name as the string \"13\"", text_value("13"), {"get 13"}, "130"},
      {"_yscale as the double 3.9, truncated to 3",
       Bytes{0x06, 0x33, 0x33, 0x0F, 0x40, 0x33, 0x33, 0x33, 0x33},
       {"get 3"},
       "30"},
      {"_target (11) is outside the slice", int_value(11), {}, "undefined"},
      {"a NaN index reads undefined", text_value("abc"), {}, "undefined"},
      {"a negative index reads undefined", int_value(0xFFFFFFFFu), {}, "undefined"},
  };
  for (const PropertyRow& row : rows) {
    SCOPED_TRACE(row.name);
    RecordingTimeline timeline;
    const auto result = execute_with_timeline(
        cat({push({int_value(7), text_value("clip"), row.index}), rec(0x22, {}), rec(0x26, {}),
             rec(0x26, {}), Bytes{0x00}}),
        timeline);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(timeline.calls, row.calls);
    EXPECT_EQ(result.trace, (std::vector<std::string>{row.value, "7"}));
  }
}

// Invariant: SetProperty pops three operands and never forwards a read-only or unsupported index to the host.
TEST(Avm1Timeline, SetPropertyPassesWritableIndicesAndSkipsReadOnlyOnes) {
  RecordingTimeline timeline;
  const auto result = execute_with_timeline(
      cat({push({int_value(7)}), push({text_value("clip"), int_value(0), int_value(42)}),
           rec(0x23, {}), push({text_value("clip"), int_value(4), int_value(9)}), rec(0x23, {}),
           push({text_value("clip"), int_value(5), int_value(9)}), rec(0x23, {}),
           push({text_value("clip"), int_value(11), int_value(9)}), rec(0x23, {}),
           push({text_value("clip"), int_value(13), text_value("hero")}), rec(0x23, {}),
           rec(0x26, {}), Bytes{0x00}}),
      timeline);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(timeline.calls, (std::vector<std::string>{"set 0", "set 13"}));
  EXPECT_EQ(timeline.last_value.kind, Avm1DataKind::String);
  EXPECT_EQ(timeline.last_value.text, "hero");
  EXPECT_EQ(result.trace, (std::vector<std::string>{"7"}));

  RecordingTimeline numeric;
  const auto stored = execute_with_timeline(
      cat({push({text_value("clip"), int_value(0), int_value(42)}), rec(0x23, {}), Bytes{0x00}}),
      numeric);
  ASSERT_TRUE(stored.ok()) << stored.message;
  EXPECT_EQ(numeric.calls, (std::vector<std::string>{"set 0"}));
  EXPECT_EQ(numeric.last_value.kind, Avm1DataKind::Number);
  EXPECT_EQ(numeric.last_value.number, 42.0);
}

struct Refusal {
  Bytes input;
  std::string name;
};

// Invariant: a timeline opcode with no host refuses by name at its own offset and runs nothing.
TEST(Avm1Timeline, TimelineOpcodesAreRefusedByNameWithoutAHost) {
  const std::vector<Refusal> rows = {
      {rec(0x04, {}), "NextFrame (0x04)"},
      {rec(0x05, {}), "PreviousFrame (0x05)"},
      {rec(0x06, {}), "Play (0x06)"},
      {rec(0x07, {}), "Stop (0x07)"},
      {rec(0x81, le16(1)), "GotoFrame (0x81)"},
      {rec(0x8C, str("intro")), "GoToLabel (0x8C)"},
      {rec(0x9F, {0x01}), "GotoFrame2 (0x9F)"},
      {rec(0x22, {}), "GetProperty (0x22)"},
      {rec(0x23, {}), "SetProperty (0x23)"},
  };
  for (const Refusal& row : rows) {
    SCOPED_TRACE(row.name);
    const auto stream = decode(cat({push({int_value(7)}), row.input, rec(0x26, {}), Bytes{0x00}}));
    ASSERT_TRUE(stream.ok()) << stream.message;
    const auto result = fo4::avm1::execute_actions(stream);
    EXPECT_EQ(result.status, Avm1RunStatus::Unsupported) << result.message;
    EXPECT_EQ(result.opcode, row.input[0]);
    EXPECT_NE(result.message.find(row.name), std::string::npos) << result.message;
    EXPECT_TRUE(result.trace.empty());
  }
}
}  // namespace
