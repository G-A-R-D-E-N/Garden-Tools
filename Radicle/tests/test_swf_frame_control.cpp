#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfDisplayFrame;
using fo4::swf::SwfDisplayList;
using fo4::swf::SwfDisplayTimeline;
using fo4::swf::SwfFrameInstance;
using fo4::swf::SwfTimelineStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;
using Frame = std::vector<TagPayload>;

constexpr std::uint32_t kMissing = std::numeric_limits<std::uint32_t>::max();

std::vector<TagPayload> with_show_frames(const std::vector<Frame>& frames) {
  std::vector<TagPayload> out;
  for (const Frame& frame : frames) {
    out.insert(out.end(), frame.begin(), frame.end());
    out.push_back({1, {}});
  }
  return out;
}

std::vector<std::byte> movie_of(std::uint16_t frames, const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(frames);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> sprite_tag(std::uint16_t id, std::uint16_t frames,
                                  const std::vector<TagPayload>& children) {
  SwfBuilder body;
  body.u16(id);
  body.u16(frames);
  for (const auto& [code, payload] : children) {
    body.tag(code, payload, false);
  }
  body.tag(0);
  return body.body;
}

std::vector<std::byte> place_named(std::uint16_t depth, std::uint16_t character,
                                   const std::string& name) {
  SwfBuilder body;
  body.u8(0x22);
  body.u16(depth);
  body.u16(character);
  body.text(name);
  return body.body;
}

std::vector<std::byte> script(std::vector<std::uint8_t> records) {
  records.push_back(0x00);
  std::vector<std::byte> out;
  for (const std::uint8_t byte : records) {
    out.push_back(static_cast<std::byte>(byte));
  }
  return out;
}

std::vector<std::uint8_t> goto_frame2(std::uint8_t number, std::uint8_t flags) {
  return {0x96, 0x05, 0x00, 0x07, number, 0x00, 0x00, 0x00, 0x9F, 0x01, 0x00, flags};
}

std::vector<std::uint8_t> goto_label(const std::string& name) {
  std::vector<std::uint8_t> out = {0x8C, 0x00, 0x00};
  for (char c : name) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
  out.push_back(0x00);
  const auto size = static_cast<std::uint16_t>(out.size() - 3);
  out[1] = static_cast<std::uint8_t>(size & 0xFFu);
  out[2] = static_cast<std::uint8_t>(size >> 8);
  return out;
}

std::vector<std::uint8_t> push_frame(std::uint32_t number) {
  return {0x96, 0x05, 0x00, 0x07, static_cast<std::uint8_t>(number & 0xFFu),
          static_cast<std::uint8_t>((number >> 8) & 0xFFu),
          static_cast<std::uint8_t>((number >> 16) & 0xFFu),
          static_cast<std::uint8_t>((number >> 24) & 0xFFu)};
}

std::vector<std::uint8_t> push_label(const std::string& name) {
  std::vector<std::uint8_t> out = {0x96, 0x00, 0x00, 0x00};
  for (char c : name) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
  out.push_back(0x00);
  const auto size = static_cast<std::uint16_t>(out.size() - 3);
  out[1] = static_cast<std::uint8_t>(size & 0xFFu);
  out[2] = static_cast<std::uint8_t>(size >> 8);
  return out;
}

std::vector<std::uint8_t> call_frame(std::vector<std::uint8_t> push) {
  // ActionCall (0x9E) requires a u16 length even when it carries no operands.
  push.push_back(0x9E);
  push.push_back(0x00);
  push.push_back(0x00);
  return push;
}

std::vector<std::byte> label_payload(const std::string& name) {
  std::vector<std::byte> out;
  for (char c : name) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  }
  out.push_back(std::byte{0});
  return out;
}

fo4::swf::SwfDisplayOptions controlled_options() {
  fo4::swf::SwfDisplayOptions options;
  options.apply_frame_control = true;
  return options;
}

SwfDisplayList display_of(const std::vector<std::byte>& bytes,
                          const fo4::swf::SwfDisplayOptions& options = controlled_options()) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  return fo4::swf::build_display_list(result.file, options);
}

std::vector<std::uint32_t> frames_of_row(const SwfDisplayTimeline& timeline, std::string_view path) {
  std::vector<std::uint32_t> out;
  for (const SwfDisplayFrame& frame : timeline.frames) {
    const SwfFrameInstance* row = nullptr;
    for (const SwfFrameInstance& instance : frame.instances) {
      if (std::string_view(instance.path) == path) {
        row = &instance;
      }
    }
    out.push_back(row == nullptr ? kMissing : row->timeline_frame);
  }
  return out;
}

std::vector<std::byte> clip_movie(const std::vector<Frame>& clip_frames) {
  const auto sprite = sprite_tag(9, static_cast<std::uint16_t>(clip_frames.size()),
                                 with_show_frames(clip_frames));
  return movie_of(5, with_show_frames({
                         {{39, sprite}, {26, place_named(1, 9, "Art")}},
                         {},
                         {},
                         {},
                         {},
                     }));
}

TEST(SwfFrameControl, StopOnTheSecondFrameHoldsTheClipThere) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x07})}},
      {},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
}

TEST(SwfFrameControl, GotoFrameJumpsBackAndKeepsPlaying) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {},
      {{12, script({0x81, 0x02, 0x00, 0x00, 0x00, 0x06})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 0, 1, 0}));
}

TEST(SwfFrameControl, BareGotoFrameStopsAtItsTarget) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {},
      {{12, script({0x81, 0x02, 0x00, 0x00, 0x00})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 0, 0, 0}));
}

TEST(SwfFrameControl, GoToLabelJumpsToTheNamedFrameAndStops) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{43, label_payload("mid")}},
      {{12, script(goto_label("mid"))}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
}

TEST(SwfFrameControl, CallRunsTheTargetFrameInline) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(call_frame(push_frame(3)))}},
      {{12, script({0x81, 0x02, 0x00, 0x00, 0x00})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 0, 0, 0, 0}));
}

TEST(SwfFrameControl, CallRunsTheTargetLabelInline) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(call_frame(push_label("mid")))}},
      {{43, label_payload("mid")}, {12, script({0x07})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
}

TEST(SwfFrameControl, CallToItsOwnFrameExpandsOnceAndKeepsPlaying) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(call_frame(push_frame(2)))}},
      {},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));
}

TEST(SwfFrameControl, DoInitActionStopsTheSpriteBeforeItsFirstFrame) {
  const auto sprite = sprite_tag(9, 3,
                                 with_show_frames({
                                     {{26, place_named(1, 5, "Cell")}},
                                     {},
                                     {},
                                 }));
  std::vector<std::byte> init = {std::byte{9}, std::byte{0}};
  const std::vector<std::byte> stop = script({0x07});
  init.insert(init.end(), stop.begin(), stop.end());
  const auto movie = movie_of(5, with_show_frames({
                                      {{{39, sprite}, {59, init}, {26, place_named(1, 9, "Art")}}},
                                      {},
                                      {},
                                      {},
                                      {},
                                  }));
  const auto display = display_of(movie);
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 0, 0, 0, 0}));
}

TEST(SwfFrameControl, GotoLoopEndsAtTheHopCap) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x81, 0x02, 0x00, 0x02, 0x00, 0x06})}},
      {{12, script({0x81, 0x02, 0x00, 0x01, 0x00, 0x06})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 2, 0, 2, 0}));
}

TEST(SwfFrameControl, StopThenPlayOnTheSameFrameResumesThePlayhead) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x07, 0x06})}},
      {},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));
}

TEST(SwfFrameControl, GotoFrame2TakesItsFrameFromThePrecedingPush) {
  const auto stopped = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(goto_frame2(2, 0))}},
      {},
  }));
  ASSERT_EQ(stopped.status, SwfTimelineStatus::ok) << stopped.message;
  EXPECT_EQ(frames_of_row(stopped.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));

  const auto playing = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(goto_frame2(3, 1))}},
      {},
  }));
  ASSERT_EQ(playing.status, SwfTimelineStatus::ok) << playing.message;
  EXPECT_EQ(frames_of_row(playing.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 2, 0, 2, 0}));
}

TEST(SwfFrameControl, GotoFrame2ZeroIsNoOpAndAboveTheLastFrameClamps) {
  const auto zero = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(goto_frame2(0, 0))}},
      {},
  }));
  ASSERT_EQ(zero.status, SwfTimelineStatus::ok) << zero.message;
  EXPECT_EQ(frames_of_row(zero.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));

  const auto clamped = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(goto_frame2(9, 0))}},
      {},
  }));
  ASSERT_EQ(clamped.status, SwfTimelineStatus::ok) << clamped.message;
  EXPECT_EQ(frames_of_row(clamped.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 2, 2, 2, 2}));
}

TEST(SwfFrameControl, NextAndPreviousFrameStepOnceAndStop) {
  const auto next = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x04})}},
      {},
  }));
  ASSERT_EQ(next.status, SwfTimelineStatus::ok) << next.message;
  EXPECT_EQ(frames_of_row(next.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 2, 2, 2, 2}));

  const auto previous = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {},
      {{12, script({0x05})}},
  }));
  ASSERT_EQ(previous.status, SwfTimelineStatus::ok) << previous.message;
  EXPECT_EQ(frames_of_row(previous.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
}

TEST(SwfFrameControl, NextAndPreviousAtTheEndsAreNoOps) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}, {12, script({0x05})}},
      {},
      {{12, script({0x04})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));
}

TEST(SwfFrameControl, AStopAfterSetTarget2IsNotAppliedToTheClip) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x20, 0x07})}},
      {},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));
}

TEST(SwfFrameControl, ChildCreatedAfterAStopAgesFromItsFrameEntry) {
  const auto leaf = sprite_tag(8, 2, with_show_frames({
                                         {{26, place_named(1, 5, "Leaf")}},
                                         {},
                                     }));
  const auto clip = sprite_tag(9, 3, with_show_frames({
                                         {},
                                         {{26, place_named(1, 8, "Cell")}, {12, script({0x07})}},
                                         {},
                                     }));
  const auto display = display_of(movie_of(5, with_show_frames({
                                                  {{39, leaf}, {39, clip},
                                                   {26, place_named(1, 9, "Art")}},
                                                  {},
                                                  {},
                                                  {},
                                                  {},
                                              })));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{kMissing, 1, 1, 1, 1}));
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell/Leaf"),
            (std::vector<std::uint32_t>{kMissing, 0, 1, 0, 1}));
}

TEST(SwfFrameControl, SkippedFrameChildAgesFromTheLandingTick) {
  const auto leaf = sprite_tag(8, 2, with_show_frames({
                                         {{26, place_named(1, 5, "Leaf")}},
                                         {},
                                     }));
  const auto clip = sprite_tag(9, 4, with_show_frames({
                                         {},
                                         {{12, script({0x81, 0x02, 0x00, 0x03, 0x00, 0x07})}},
                                         {{26, place_named(1, 8, "Cell")}},
                                         {},
                                     }));
  const auto display = display_of(movie_of(5, with_show_frames({
                                                  {{39, leaf}, {39, clip},
                                                   {26, place_named(1, 9, "Art")}},
                                                  {},
                                                  {},
                                                  {},
                                                  {},
                                              })));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{kMissing, 3, 3, 3, 3}));
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell/Leaf"),
            (std::vector<std::uint32_t>{kMissing, 0, 1, 0, 1}));
}

std::vector<std::byte> root_stop_movie() {
  return movie_of(5, with_show_frames({
                         {{26, place_named(1, 5, "Marker")}},
                         {{12, script({0x07})}},
                         {},
                         {{26, place_named(2, 6, "Late")}},
                         {},
                     }));
}

TEST(SwfFrameControl, StopOnTheRootTimelineHoldsTheRootFrame) {
  const auto display = display_of(root_stop_movie());
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Marker"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
  EXPECT_EQ(frames_of_row(display.timelines[0], "Late"),
            (std::vector<std::uint32_t>{kMissing, kMissing, kMissing, kMissing, kMissing}));
}

TEST(SwfFrameControl, ByDefaultTheRootTimelineKeepsItsAuthoredFrames) {
  const auto display = display_of(root_stop_movie(), fo4::swf::SwfDisplayOptions{});
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Marker"),
            (std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
  EXPECT_EQ(frames_of_row(display.timelines[0], "Late"),
            (std::vector<std::uint32_t>{kMissing, kMissing, kMissing, 3, 4}));
}

TEST(SwfFrameControl, StoppedClipKeepsItsNestedClipPlaying) {
  const auto child = sprite_tag(8, 2, with_show_frames({
                                          {{26, place_named(1, 5, "Leaf")}},
                                          {},
                                      }));
  const auto parent = sprite_tag(9, 3, with_show_frames({
                                           {{26, place_named(1, 8, "Cell")}},
                                           {{12, script({0x07})}},
                                           {},
                                       }));
  const auto display = display_of(movie_of(5, with_show_frames({
                                                  {{39, child}, {39, parent},
                                                   {26, place_named(1, 9, "Art")}},
                                                  {},
                                                  {},
                                                  {},
                                                  {},
                                              })));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell/Leaf"),
            (std::vector<std::uint32_t>{0, 1, 0, 1, 0}));
}

TEST(SwfFrameControl, ASpriteWithOnlyNonControlActionsStillLoops) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x96, 0x05, 0x00, 0x07, 0x02, 0x00, 0x00, 0x00, 0x17})}},
      {},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));
}

TEST(SwfFrameControl, AStopBehindABranchIsNotAppliedToTheClip) {
  const auto display = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script({0x99, 0x02, 0x00, 0x01, 0x00, 0x07})}},
      {},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 2, 0, 1}));
}

std::vector<std::uint8_t> goto_then_play(std::uint16_t frame) {
  return {0x81, 0x02, 0x00, static_cast<std::uint8_t>(frame & 0xFF),
          static_cast<std::uint8_t>(frame >> 8), 0x06};
}

std::vector<std::byte> hundred_frame_cycle() {
  std::vector<Frame> frames(100);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    frames[i] = {{12, script(goto_then_play(static_cast<std::uint16_t>((i + 1) % 100)))}};
  }
  frames[0].push_back({26, place_named(1, 5, "Marker")});
  return sprite_tag(9, 100, with_show_frames(frames));
}

TEST(SwfFrameControl, GotoChainStopsAtTheHopCapWithinOneTick) {
  std::vector<Frame> root(100);
  root[0] = {{39, hundred_frame_cycle()}, {26, place_named(1, 9, "Art")}};
  const auto display = display_of(movie_of(100, with_show_frames(root)));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  const auto row = frames_of_row(display.timelines[0], "Art/Marker");
  EXPECT_EQ(std::vector<std::uint32_t>(row.begin(), row.begin() + 4),
            (std::vector<std::uint32_t>{63, 27, 91, 55}));
}

TEST(SwfFrameControl, EntryBudgetFreezesALongChainInsteadOfGrowing) {
  std::vector<Frame> root(1200);
  root[0] = {{39, hundred_frame_cycle()}, {26, place_named(1, 9, "Art")}};
  const auto display = display_of(movie_of(1200, with_show_frames(root)));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  std::vector<std::uint32_t> expected;
  for (std::uint32_t tick = 0; tick < 1200; ++tick) {
    expected.push_back(tick <= 90 ? (63 + 64 * tick) % 100 : 23);
  }
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Marker"), expected);
}

}  // namespace
