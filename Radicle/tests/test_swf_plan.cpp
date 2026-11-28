#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_timeline.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfFile;
using fo4::swf::SwfStatus;
using fo4::swf::SwfTag;
using fo4::swf::SwfTagInsertion;
using fo4::swf::SwfTagPlan;
using fo4::swf::SwfTagPlanResult;
using fo4::swf::SwfTagReplacement;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

std::vector<std::byte> color_payload(std::uint8_t blue) {
  return {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{blue}};
}

std::vector<std::byte> plan_movie() {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  builder.tag(9, color_payload(0x01), false);
  builder.tag(9, color_payload(0x02), false);
  builder.tag(1);
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

SwfFile read(const std::vector<std::byte>& bytes) {
  auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::ok);
  return std::move(result.file);
}

SwfTag background(std::uint8_t blue) {
  SwfTag tag;
  tag.code = fo4::swf::tag::kSetBackgroundColor;
  tag.payload = color_payload(blue);
  tag.length = static_cast<std::uint32_t>(tag.payload.size());
  return tag;
}

}  // namespace

TEST(SwfTagPlan, InsertsAndReplacesAtOriginalIndices) {
  const auto bytes = plan_movie();
  auto file = read(bytes);
  ASSERT_EQ(file.tags.size(), 4u);

  SwfTagPlan plan;
  plan.replacements.push_back(SwfTagReplacement{0, color_payload(0x42)});
  plan.insertions.push_back(SwfTagInsertion{2, background(0x77)});
  SwfTagPlanResult result;
  ASSERT_TRUE(fo4::swf::apply_tag_plan(file, plan, result));

  ASSERT_EQ(file.tags.size(), 5u);
  EXPECT_EQ(file.tags[0].payload, color_payload(0x42));
  EXPECT_EQ(file.tags[1].payload, color_payload(0x02));
  EXPECT_EQ(file.tags[2].code, fo4::swf::tag::kSetBackgroundColor);
  EXPECT_EQ(file.tags[2].payload, color_payload(0x77));
  ASSERT_TRUE(file.tags[2].background_color.has_value());
  EXPECT_EQ(*file.tags[2].background_color, 0x77102030u);
  EXPECT_EQ(file.tags[3].code, fo4::swf::tag::kShowFrame);
  EXPECT_EQ(file.tags[4].code, fo4::swf::tag::kEnd);
  ASSERT_EQ(result.inserted_tag_indices.size(), 1u);
  EXPECT_EQ(result.inserted_tag_indices[0], 2u);

  const auto reparsed = fo4::swf::read_swf(fo4::swf::write_swf(file));
  ASSERT_EQ(reparsed.status, SwfStatus::ok);
  ASSERT_EQ(reparsed.file.tags.size(), 5u);
  EXPECT_EQ(reparsed.file.tags[2].payload, color_payload(0x77));
}

TEST(SwfTagPlan, KeepsInsertionOrderAcrossOriginalPositions) {
  auto file = read(plan_movie());
  SwfTagPlan plan;
  plan.insertions.push_back(SwfTagInsertion{1, background(0x55)});
  plan.insertions.push_back(SwfTagInsertion{2, background(0x66)});
  SwfTagPlanResult result;
  ASSERT_TRUE(fo4::swf::apply_tag_plan(file, plan, result));

  ASSERT_EQ(file.tags.size(), 6u);
  EXPECT_EQ(file.tags[0].payload, color_payload(0x01));
  EXPECT_EQ(file.tags[1].payload, color_payload(0x55));
  EXPECT_EQ(file.tags[2].payload, color_payload(0x02));
  EXPECT_EQ(file.tags[3].payload, color_payload(0x66));
  EXPECT_EQ(file.tags[4].code, fo4::swf::tag::kShowFrame);
  EXPECT_EQ(file.tags[5].code, fo4::swf::tag::kEnd);
  ASSERT_EQ(result.inserted_tag_indices.size(), 2u);
  EXPECT_EQ(result.inserted_tag_indices[0], 1u);
  EXPECT_EQ(result.inserted_tag_indices[1], 3u);
}

TEST(SwfTagPlan, RejectsAPlanThatBreaksTheTerminalEnd) {
  const auto bytes = plan_movie();
  auto file = read(bytes);
  const auto before = fo4::swf::write_swf(file);

  SwfTagPlan replace_end;
  replace_end.replacements.push_back(SwfTagReplacement{3, color_payload(0x09)});
  SwfTagPlanResult result;
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, replace_end, result));
  EXPECT_EQ(fo4::swf::write_swf(file), before);

  SwfTagPlan insert_after_end;
  insert_after_end.insertions.push_back(SwfTagInsertion{4, background(0x09)});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, insert_after_end, result));

  SwfTagPlan insert_end;
  SwfTag inserted_end;
  inserted_end.code = fo4::swf::tag::kEnd;
  insert_end.insertions.push_back(SwfTagInsertion{1, inserted_end});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, insert_end, result));
  EXPECT_EQ(file.tags.size(), 4u);
  EXPECT_EQ(fo4::swf::write_swf(file), before);
}

TEST(SwfTagPlan, RejectsDuplicateTargetsAndWideCodesWithoutChangingBytes) {
  const auto bytes = plan_movie();
  auto file = read(bytes);
  const auto before = fo4::swf::write_swf(file);
  SwfTagPlanResult result;

  SwfTagPlan duplicate;
  duplicate.replacements.push_back(SwfTagReplacement{1, color_payload(0x31)});
  duplicate.replacements.push_back(SwfTagReplacement{1, color_payload(0x32)});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, duplicate, result));

  SwfTag wide = background(0x31);
  wide.code = 0x0400;
  SwfTagPlan wide_code;
  wide_code.insertions.push_back(SwfTagInsertion{2, wide});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, wide_code, result));

  SwfTagPlan out_of_range;
  out_of_range.replacements.push_back(SwfTagReplacement{4, color_payload(0x31)});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, out_of_range, result));

  EXPECT_EQ(file.tags.size(), 4u);
  EXPECT_EQ(fo4::swf::write_swf(file), before);
}

TEST(SwfTagPlan, AcceptsANoOpReplacementByteExactly) {
  const auto bytes = plan_movie();
  auto file = read(bytes);
  SwfTagPlan plan;
  plan.replacements.push_back(SwfTagReplacement{0, color_payload(0x01)});
  SwfTagPlanResult result;
  ASSERT_TRUE(fo4::swf::apply_tag_plan(file, plan, result));
  EXPECT_TRUE(result.inserted_tag_indices.empty());
  EXPECT_EQ(fo4::swf::write_swf(file), bytes);
}

TEST(SwfTagPlan, InsertsAtOnePositionInPlanOrderAndReportsPlanOrder) {
  auto file = read(plan_movie());
  SwfTagPlan plan;
  plan.insertions.push_back(SwfTagInsertion{2, background(0x31)});
  plan.insertions.push_back(SwfTagInsertion{2, background(0x32)});
  SwfTagPlanResult result;
  ASSERT_TRUE(fo4::swf::apply_tag_plan(file, plan, result));
  ASSERT_EQ(file.tags.size(), 6u);
  EXPECT_EQ(file.tags[2].payload, color_payload(0x31));
  EXPECT_EQ(file.tags[3].payload, color_payload(0x32));
  ASSERT_EQ(result.inserted_tag_indices.size(), 2u);
  EXPECT_EQ(result.inserted_tag_indices[0], 2u);
  EXPECT_EQ(result.inserted_tag_indices[1], 3u);

  auto second = read(plan_movie());
  SwfTagPlan reversed;
  reversed.insertions.push_back(SwfTagInsertion{2, background(0x41)});
  reversed.insertions.push_back(SwfTagInsertion{1, background(0x42)});
  SwfTagPlanResult reversed_result;
  ASSERT_TRUE(fo4::swf::apply_tag_plan(second, reversed, reversed_result));
  ASSERT_EQ(second.tags.size(), 6u);
  EXPECT_EQ(second.tags[1].payload, color_payload(0x42));
  EXPECT_EQ(second.tags[3].payload, color_payload(0x41));
  ASSERT_EQ(reversed_result.inserted_tag_indices.size(), 2u);
  EXPECT_EQ(reversed_result.inserted_tag_indices[0], 3u);
  EXPECT_EQ(reversed_result.inserted_tag_indices[1], 1u);
}

TEST(SwfTagPlan, RejectsAPlanWhosePayloadDoesNotDecode) {
  auto file = read(plan_movie());
  const auto before = fo4::swf::write_swf(file);
  SwfTagPlanResult result;

  SwfTag truncated;
  truncated.code = fo4::swf::tag::kPlaceObject2;
  truncated.payload = {std::byte{0x02}};
  SwfTagPlan inserted;
  inserted.insertions.push_back(SwfTagInsertion{2, truncated});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, inserted, result));
  EXPECT_EQ(file.tags.size(), 4u);
  EXPECT_EQ(fo4::swf::write_swf(file), before);

  SwfTagPlan replaced;
  replaced.replacements.push_back(SwfTagReplacement{0, {std::byte{0x01}}});
  EXPECT_FALSE(fo4::swf::apply_tag_plan(file, replaced, result));
  EXPECT_EQ(file.tags.size(), 4u);
  EXPECT_EQ(fo4::swf::write_swf(file), before);
}

TEST(SwfTagPlan, ReplacingATagClearsItsDecodedImports) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  SwfBuilder imports;
  imports.text("SharedFonts.swf");
  imports.u8(0);
  imports.u8(0);
  imports.u16(1);
  imports.u16(7);
  imports.text("Bold");
  builder.tag(71, imports.body, false);
  builder.tag(1);
  builder.tag(0);
  const auto bytes = wrap_body(builder.body, 10, false, false);

  auto result = fo4::swf::read_swf(bytes);
  ASSERT_EQ(result.status, SwfStatus::ok);
  ASSERT_TRUE(result.file.tags[0].imports.has_value());
  EXPECT_NE(fo4::swf::inspect_swf(result.file, "imports.swf").find("Imports: SharedFonts.swf (1 symbols)"),
            std::string::npos);

  const std::vector<std::byte> malformed{std::byte{0x01}};
  ASSERT_TRUE(fo4::swf::replace_tag_payload(result.file, 0, malformed));
  EXPECT_FALSE(result.file.tags[0].imports.has_value());
  EXPECT_EQ(fo4::swf::inspect_swf(result.file, "imports.swf").find("Imports:"), std::string::npos);
}
