#include "fo4/anim/anim_text_data.hpp"

#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using fo4::anim::AnimEventInfo;
using fo4::anim::AnimEventRecord;
using fo4::anim::AnimFileDirList;
using fo4::anim::AnimFileList;
using fo4::anim::AnimIdleData;
using fo4::anim::parse_anim_event_info;
using fo4::anim::parse_anim_file_dirlist;
using fo4::anim::parse_anim_file_list;
using fo4::anim::parse_anim_idle_data;
using fo4::anim::write_anim_event_info;
using fo4::anim::write_anim_file_dirlist;
using fo4::anim::write_anim_file_list;
using fo4::anim::write_anim_idle_data;

namespace {

std::string numeric_list() {
  return "3\n1\n10067747564108604006\n2\n"
         "Actors\\Character\\Behaviors\\WeaponBehavior.hkx\n"
         "Actors\\Character\\Animations\\Weapon\\GripHeavy\\WPNChargeUp.hkx\n";
}

std::string named_list() {
  return "3\r\n0\r\nAnglerProject\r\n2\r\n"
         "Behaviors\\AnglerRootBehavior.hkx\r\n"
         "Animations\\Angler_GetUpFaceDown.hkx\r\n"
         "0\r\n";
}

}  // namespace

// Detects: a parser that must read a shipped numeric list, which is a four-line envelope plus one line per asset, but drops the count or the trailing newline on the way back out.
TEST(AnimTextData, RoundTripsAMeasuredNumericList) {
  const auto list = parse_anim_file_list(numeric_list());
  EXPECT_EQ(list.flavour, 1U);
  EXPECT_EQ(list.name, "10067747564108604006");
  ASSERT_EQ(list.files.size(), 2U);
  EXPECT_EQ(list.files[1], "Actors\\Character\\Animations\\Weapon\\GripHeavy\\WPNChargeUp.hkx");
  EXPECT_FALSE(list.crlf);
  EXPECT_EQ(write_anim_file_list(list), numeric_list());
}

// Detects: a parser that must recognise the second flavour, whose name is readable, whose lines are CRLF and whose list is followed by the measured zero line.
TEST(AnimTextData, RoundTripsAMeasuredNamedList) {
  const auto list = parse_anim_file_list(named_list());
  EXPECT_EQ(list.flavour, 0U);
  EXPECT_EQ(list.name, "AnglerProject");
  ASSERT_EQ(list.files.size(), 2U);
  EXPECT_TRUE(list.crlf);
  EXPECT_EQ(write_anim_file_list(list), named_list());
}

// Detects: a list with no entries, which 39 shipped files are, refused because the reader must accept a zero count without a body.
TEST(AnimTextData, RoundTripsAnEmptyNumericList) {
  const std::string text = "3\n1\n10008498266921984614\n0\n";
  const auto list = parse_anim_file_list(text);
  EXPECT_TRUE(list.files.empty());
  EXPECT_EQ(write_anim_file_list(list), text);
}

// Detects: a reader that must trust the count line, so a list whose body holds fewer lines than the count claims is refused instead of silently shortened.
TEST(AnimTextData, RefusesAListWhoseCountDisagreesWithItsLines) {
  const std::string text = "3\n1\n10067747564108604006\n3\nActors\\Character\\Behaviors\\WeaponBehavior.hkx\n";
  EXPECT_THROW(parse_anim_file_list(text), fo4::parse_error);
}

// Detects: a named list that must carry the measured trailing zero line, refused when that line is missing or not zero.
TEST(AnimTextData, RefusesANamedListWithoutItsZeroLine) {
  std::string missing = named_list();
  missing.resize(missing.size() - 3);
  EXPECT_THROW(parse_anim_file_list(missing), fo4::parse_error);
  std::string nonzero = named_list();
  nonzero.replace(nonzero.size() - 3, 1, "1");
  EXPECT_THROW(parse_anim_file_list(nonzero), fo4::parse_error);
}

// Detects: a flavour 1 list whose name must be a decimal id, refused when the top line says digits but the third line carries a readable name.
TEST(AnimTextData, RefusesAFlavourOneNameThatIsNotADecimalId) {
  const std::string text = "3\n1\nAnglerProject\n0\n";
  EXPECT_THROW(parse_anim_file_list(text), fo4::parse_error);
}

// Detects: a list that names the same file twice, which must be refused because no shipped list does and a rebuilt one would load one clip twice.
TEST(AnimTextData, RefusesARepeatedEntry) {
  const std::string text =
      "3\n1\n10067747564108604006\n2\nActors\\Character\\Behaviors\\WeaponBehavior.hkx\n"
      "Actors\\Character\\Behaviors\\WeaponBehavior.hkx\n";
  EXPECT_THROW(parse_anim_file_list(text), fo4::parse_error);
}

// Detects: a file whose last line is not terminated, or whose lines mix CRLF with LF, refused because the split then reads the next line at the wrong offset.
TEST(AnimTextData, RefusesAMissingOrMixedLineEnding) {
  std::string unterminated = numeric_list();
  unterminated.pop_back();
  EXPECT_THROW(parse_anim_file_list(unterminated), fo4::parse_error);
  const std::string mixed = "3\r\n1\n10067747564108604006\n0\n";
  EXPECT_THROW(parse_anim_file_list(mixed), fo4::parse_error);
}

// Detects: a version line other than the measured 3, which must be refused rather than read as the current layout.
TEST(AnimTextData, RefusesAnUnknownVersionLine) {
  const std::string text = "4\n1\n10067747564108604006\n0\n";
  EXPECT_THROW(parse_anim_file_list(text), fo4::parse_error);
}

// Detects: a writer that must refuse a list a reader could not load back, such as one without a name or with a repeated entry, instead of writing it.
TEST(AnimTextData, RefusesToWriteAListItCouldNotReadBack) {
  AnimFileList nameless;
  nameless.name.clear();
  EXPECT_THROW(write_anim_file_list(nameless), std::invalid_argument);
  AnimFileList repeated;
  repeated.name = "10067747564108604006";
  repeated.files = {"Actors\\Character\\Behaviors\\WeaponBehavior.hkx",
                    "Actors\\Character\\Behaviors\\WeaponBehavior.hkx"};
  EXPECT_THROW(write_anim_file_list(repeated), std::invalid_argument);
}

// Detects: a directory index that must round-trip its CRLF lines and final ending, and must refuse an entry that is not a shipped .txt name.
TEST(AnimTextData, RoundTripsTheMeasuredDirList) {
  const std::string text = "10008498266921984614.txt\r\nYaoGuai.txt\r\n_SharedProject.txt\r\n";
  const auto list = parse_anim_file_dirlist(text);
  ASSERT_EQ(list.names.size(), 3U);
  EXPECT_EQ(write_anim_file_dirlist(list), text);
  EXPECT_THROW(parse_anim_file_dirlist("YaoGuai.hkx\r\n"), fo4::parse_error);
  EXPECT_THROW(parse_anim_file_dirlist("YaoGuai.txt\r\nYaoGuai.txt\r\n"), fo4::parse_error);
  AnimFileDirList empty;
  EXPECT_THROW(write_anim_file_dirlist(empty), std::invalid_argument);
}

// Detects: a reader that must keep the version line and every pose of a shipped idle list, whose poses are extensionless paths.
TEST(AnimTextData, RoundTripsAMeasuredIdleList) {
  const std::string text =
      "Version 1\n"
      "Actors\\DLC03\\Character\\Animations\\Furniture\\KidSittingCatatonic\\PoseA_IdleFlavor1\n";
  const auto data = parse_anim_idle_data(text);
  ASSERT_EQ(data.poses.size(), 1U);
  EXPECT_EQ(data.poses[0], "Actors\\DLC03\\Character\\Animations\\Furniture\\KidSittingCatatonic\\PoseA_IdleFlavor1");
  EXPECT_FALSE(data.crlf);
  EXPECT_EQ(write_anim_idle_data(data), text);
}

// Detects: an idle list with no poses, which 548 shipped files are, which must round-trip as the version line alone rather than as an empty file.
TEST(AnimTextData, RoundTripsAnEmptyIdleList) {
  const std::string text = "Version 1\n";
  const auto data = parse_anim_idle_data(text);
  EXPECT_TRUE(data.poses.empty());
  EXPECT_EQ(write_anim_idle_data(data), text);
}

// Detects: an idle list that must be refused when its version line is unknown, a pose repeats or the last line is unterminated, and a writer that must refuse what a reader could not load back.
TEST(AnimTextData, RefusesAnIdleListItCouldNotLoadBack) {
  EXPECT_THROW(parse_anim_idle_data("Version 2\n"), fo4::parse_error);
  EXPECT_THROW(parse_anim_idle_data("Version 1\nA\\b\nA\\b\n"), fo4::parse_error);
  EXPECT_THROW(parse_anim_idle_data("Version 1\nA\\b"), fo4::parse_error);
  AnimIdleData repeated;
  repeated.poses = {"A\\b", "A\\b"};
  EXPECT_THROW(write_anim_idle_data(repeated), std::invalid_argument);
  AnimIdleData empty_pose;
  empty_pose.poses = {""};
  EXPECT_THROW(write_anim_idle_data(empty_pose), std::invalid_argument);
}

// Detects: a reader that must keep an event record's event name, flag and every clip, which the shipped files write as three lines plus one line per clip.
TEST(AnimTextData, RoundTripsAMeasuredEventInfo) {
  const std::string text =
      "V2\n"
      "Actors\\DLC03\\Hermit\\Behaviors\\HermitCoreBehavior.hkx\n"
      "\n"
      "2\n"
      "DynamicAnim\n0\n1\nDynamicAnimA\n"
      "meleeStart_1\n0\n2\nAttack1\nAttack2\n";
  const auto info = parse_anim_event_info(text);
  ASSERT_EQ(info.records.size(), 2U);
  EXPECT_TRUE(info.parent_behavior.empty());
  EXPECT_EQ(info.records[0].event, "DynamicAnim");
  EXPECT_EQ(info.records[0].flag, 0U);
  ASSERT_EQ(info.records[0].clips.size(), 1U);
  EXPECT_EQ(info.records[1].clips[1], "Attack2");
  EXPECT_EQ(write_anim_event_info(info), text);
}

// Detects: the second header variant, which must keep the parent behavior path on line three rather than the blank line the other 84 shipped files carry.
TEST(AnimTextData, RoundTripsAnEventInfoWithAParentBehavior) {
  const std::string text =
      "V2\n"
      "Actors\\DLC01\\HandyCreateABot\\Behaviors\\HandyLocomotionInjuredWrappingBehavior.hkx\n"
      "Actors\\DLC01\\HandyCreateABot\\Behaviors\\HandyBehavior.hkx\n"
      "1\n"
      "meleeStart_1\n0\n1\nAttack_1\n";
  const auto info = parse_anim_event_info(text);
  EXPECT_EQ(info.parent_behavior, "Actors\\DLC01\\HandyCreateABot\\Behaviors\\HandyBehavior.hkx");
  ASSERT_EQ(info.records.size(), 1U);
  EXPECT_EQ(write_anim_event_info(info), text);
}

// Detects: an event info that must be refused when its declared record count disagrees, a clip count runs past the body, or a flag is not 0 or 1.
TEST(AnimTextData, RefusesAnEventInfoItCouldNotLoadBack) {
  const std::string wrong_count =
      "V2\nA\\b.hkx\n\n2\nDynamicAnim\n0\n1\nDynamicAnimA\n";
  EXPECT_THROW(parse_anim_event_info(wrong_count), fo4::parse_error);
  const std::string past_body =
      "V2\nA\\b.hkx\n\n1\nDynamicAnim\n0\n3\nDynamicAnimA\n";
  EXPECT_THROW(parse_anim_event_info(past_body), fo4::parse_error);
  const std::string bad_flag =
      "V2\nA\\b.hkx\n\n1\nDynamicAnim\n2\n1\nDynamicAnimA\n";
  EXPECT_THROW(parse_anim_event_info(bad_flag), fo4::parse_error);
  AnimEventInfo no_behavior;
  no_behavior.behavior.clear();
  EXPECT_THROW(write_anim_event_info(no_behavior), std::invalid_argument);
  AnimEventInfo no_clips;
  no_clips.behavior = "A\\b.hkx";
  no_clips.records = {{"DynamicAnim", 0, {}}};
  EXPECT_THROW(write_anim_event_info(no_clips), std::invalid_argument);
  AnimEventInfo bad_flag_write;
  bad_flag_write.behavior = "A\\b.hkx";
  bad_flag_write.records = {{"DynamicAnim", 2, {"DynamicAnimA"}}};
  EXPECT_THROW(write_anim_event_info(bad_flag_write), std::invalid_argument);
}
