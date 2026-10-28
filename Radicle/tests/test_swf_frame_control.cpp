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

SwfDisplayList display_of(const std::vector<std::byte>& bytes) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  return fo4::swf::build_display_list(result.file);
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
      {{12, script({0x81, 0x02, 0x00, 0x00, 0x00})}},
  }));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 1, 0, 1, 0}));
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
  const std::vector<std::uint8_t> push_two{0x96, 0x05, 0x00, 0x07, 0x02, 0x00, 0x00, 0x00};
  auto stop_records = push_two;
  stop_records.insert(stop_records.end(), {0x9F, 0x01, 0x00, 0x00});
  auto play_records = push_two;
  play_records.insert(play_records.end(), {0x9F, 0x01, 0x00, 0x01});

  const auto stopped = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(stop_records)}},
      {},
  }));
  ASSERT_EQ(stopped.status, SwfTimelineStatus::ok) << stopped.message;
  EXPECT_EQ(frames_of_row(stopped.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 2, 2, 2, 2}));

  const auto playing = display_of(clip_movie({
      {{26, place_named(1, 5, "Cell")}},
      {{12, script(play_records)}},
      {},
  }));
  ASSERT_EQ(playing.status, SwfTimelineStatus::ok) << playing.message;
  EXPECT_EQ(frames_of_row(playing.timelines[0], "Art/Cell"),
            (std::vector<std::uint32_t>{0, 2, 0, 2, 0}));
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

TEST(SwfFrameControl, StopOnTheRootTimelineHoldsTheRootFrame) {
  const auto display = display_of(movie_of(5, with_show_frames({
                                                  {{26, place_named(1, 5, "Marker")}},
                                                  {{12, script({0x07})}},
                                                  {},
                                                  {},
                                                  {},
                                              })));
  ASSERT_EQ(display.status, SwfTimelineStatus::ok) << display.message;
  EXPECT_EQ(frames_of_row(display.timelines[0], "Marker"),
            (std::vector<std::uint32_t>{0, 1, 1, 1, 1}));
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

}  // namespace
