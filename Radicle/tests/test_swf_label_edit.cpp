#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_text_edit.hpp"
#include "swf_character_fixtures.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using swf_character_test::movie_of;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

std::vector<std::byte> frame_label(const char* name) {
  std::vector<std::byte> payload;
  for (const char* ch = name; *ch != '\0'; ++ch) {
    payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(*ch)));
  }
  payload.push_back(std::byte{0});
  return payload;
}

// Constraint: the sprite carries one label and two frames so a sprite timeline has a label to edit.
std::vector<std::byte> sprite_with_label() {
  SwfBuilder body;
  body.u16(14);
  body.u16(2);
  body.tag(fo4::swf::tag::kFrameLabel, frame_label("inner"), false);
  body.tag(fo4::swf::tag::kShowFrame);
  body.tag(fo4::swf::tag::kShowFrame);
  body.tag(fo4::swf::tag::kEnd);
  return body.body;
}

// Constraint: root labels sit before the ShowFrame of their frame, so "loop" is frame 2 here.
std::vector<std::byte> label_movie() {
  return movie_of({
      {fo4::swf::tag::kFrameLabel, frame_label("start")},
      {fo4::swf::tag::kShowFrame, {}},
      {fo4::swf::tag::kShowFrame, {}},
      {fo4::swf::tag::kFrameLabel, frame_label("loop")},
      {fo4::swf::tag::kShowFrame, {}},
      {fo4::swf::tag::kDefineSprite, sprite_with_label()},
      {fo4::swf::tag::kShowFrame, {}},
  });
}

std::vector<std::string> label_names(const std::vector<fo4::swf::SwfFrameLabel>& labels) {
  std::vector<std::string> names;
  names.reserve(labels.size());
  for (const auto& label : labels) {
    names.push_back(label.name);
  }
  return names;
}

}  // namespace

TEST(SwfFrameLabels, ReadsRootAndSpriteTimelines) {
  const auto result = fo4::swf::read_swf(label_movie());
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);

  std::vector<fo4::swf::SwfFrameLabel> root;
  std::string message;
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 0, root, message)) << message;
  ASSERT_EQ(root.size(), 2u);
  EXPECT_EQ(root[0].name, "start");
  EXPECT_EQ(root[0].frame, 0u);
  EXPECT_EQ(root[1].name, "loop");
  EXPECT_EQ(root[1].frame, 2u);

  std::vector<fo4::swf::SwfFrameLabel> sprite;
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 14, sprite, message)) << message;
  ASSERT_EQ(sprite.size(), 1u);
  EXPECT_EQ(sprite[0].name, "inner");
  EXPECT_EQ(sprite[0].timeline_id, 14);
}

TEST(SwfFrameLabels, RenamesOnBothTimelines) {
  auto result = fo4::swf::read_swf(label_movie());
  ASSERT_TRUE(result.ok());
  std::string message;
  ASSERT_TRUE(fo4::swf::rename_frame_label(result.file, 0, 1, "menu", message)) << message;
  ASSERT_TRUE(fo4::swf::rename_frame_label(result.file, 14, 0, "inside", message)) << message;

  std::vector<fo4::swf::SwfFrameLabel> labels;
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 0, labels, message));
  ASSERT_EQ(labels.size(), 2u);
  EXPECT_EQ(labels[1].name, "menu");
  EXPECT_EQ(labels[1].frame, 2u);
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 14, labels, message));
  ASSERT_EQ(labels.size(), 1u);
  EXPECT_EQ(labels[0].name, "inside");
}

TEST(SwfFrameLabels, AddsALabelOnARequestedFrame) {
  auto result = fo4::swf::read_swf(label_movie());
  ASSERT_TRUE(result.ok());
  std::string message;
  ASSERT_TRUE(fo4::swf::add_frame_label(result.file, 0, 1, "mid", message)) << message;
  ASSERT_TRUE(fo4::swf::add_frame_label(result.file, 14, 1, "later", message)) << message;

  std::vector<fo4::swf::SwfFrameLabel> labels;
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 0, labels, message));
  EXPECT_EQ(label_names(labels), (std::vector<std::string>{"start", "mid", "loop"}));
  ASSERT_EQ(labels.size(), 3u);
  EXPECT_EQ(labels[1].frame, 1u);
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 14, labels, message));
  EXPECT_EQ(label_names(labels), (std::vector<std::string>{"inner", "later"}));
}

TEST(SwfFrameLabels, RemovesALabelFromTheTimeline) {
  auto result = fo4::swf::read_swf(label_movie());
  ASSERT_TRUE(result.ok());
  std::string message;
  ASSERT_TRUE(fo4::swf::remove_frame_label(result.file, 0, 0, message)) << message;
  ASSERT_TRUE(fo4::swf::remove_frame_label(result.file, 14, 0, message)) << message;

  std::vector<fo4::swf::SwfFrameLabel> labels;
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 0, labels, message));
  ASSERT_EQ(labels.size(), 1u);
  EXPECT_EQ(labels[0].name, "loop");
  EXPECT_EQ(labels[0].frame, 2u);
  ASSERT_TRUE(fo4::swf::list_frame_labels(result.file, 14, labels, message));
  EXPECT_TRUE(labels.empty());

  const auto written = fo4::swf::write_swf(result.file);
  const auto reread = fo4::swf::read_swf(written);
  ASSERT_TRUE(reread.ok()) << fo4::swf::status_name(reread.status);
  ASSERT_TRUE(fo4::swf::list_frame_labels(reread.file, 0, labels, message));
  EXPECT_EQ(label_names(labels), (std::vector<std::string>{"loop"}));
}

TEST(SwfFrameLabels, RefusesADuplicateNameOnTheSameTimeline) {
  auto result = fo4::swf::read_swf(label_movie());
  ASSERT_TRUE(result.ok());
  const auto before = fo4::swf::write_swf(result.file);
  std::string message;
  EXPECT_FALSE(fo4::swf::rename_frame_label(result.file, 0, 1, "start", message));
  EXPECT_NE(message.find("already exists"), std::string::npos) << message;
  EXPECT_FALSE(fo4::swf::add_frame_label(result.file, 0, 1, "loop", message));
  EXPECT_NE(message.find("already exists"), std::string::npos) << message;
  EXPECT_EQ(fo4::swf::write_swf(result.file), before);
}

TEST(SwfFrameLabels, RefusesAnUnknownTimelineAndAnUnreachedFrame) {
  auto result = fo4::swf::read_swf(label_movie());
  ASSERT_TRUE(result.ok());
  std::string message;
  std::vector<fo4::swf::SwfFrameLabel> labels;
  EXPECT_FALSE(fo4::swf::list_frame_labels(result.file, 99, labels, message));
  EXPECT_NE(message.find("not a DefineSprite"), std::string::npos) << message;
  EXPECT_FALSE(fo4::swf::add_frame_label(result.file, 0, 99, "late", message));
  EXPECT_NE(message.find("does not reach"), std::string::npos) << message;
  EXPECT_FALSE(fo4::swf::remove_frame_label(result.file, 0, 5, message));
  EXPECT_NE(message.find("out of range"), std::string::npos) << message;
}
