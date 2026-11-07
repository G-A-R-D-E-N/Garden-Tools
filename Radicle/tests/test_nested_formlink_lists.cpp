#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> subgraph_plugin(bool malformed_terminator = false,
                                       bool malformed_second_actor = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x804);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto keyword_group = fo4::test::begin_group(out, "KYWD", 0);
  auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000800, 131);
  fo4::test::end_record(out, keyword);
  keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000801, 131);
  fo4::test::end_record(out, keyword);
  fo4::test::end_group(out, keyword_group);

  const auto color_group = fo4::test::begin_group(out, "CLFM", 0);
  const auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000802, 131);
  fo4::test::end_record(out, color);
  fo4::test::end_group(out, color_group);

  const auto race_group = fo4::test::begin_group(out, "RACE", 0);
  const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000803, 131);

  fo4::test::ByteWriter link;
  link.u32(0x00000800);
  fo4::test::write_subrecord(out, "SAKD", link.bytes);
  const std::vector<std::byte> graph0{
      std::byte{static_cast<unsigned char>('g')},
      std::byte{static_cast<unsigned char>('0')},
      std::byte{0}};
  fo4::test::write_subrecord(out, "SGNM", graph0);
  fo4::test::write_subrecord(out, "STKD", link.bytes);
  const std::vector<std::byte> sraf0 = malformed_terminator
      ? std::vector<std::byte>{std::byte{0}, std::byte{0}}
      : std::vector<std::byte>(4, std::byte{0});
  fo4::test::write_subrecord(out, "SRAF", sraf0);

  fo4::test::write_subrecord(out, "SAKD", link.bytes);
  if (malformed_second_actor) {
    const std::vector<std::byte> malformed{std::byte{1}, std::byte{2}};
    fo4::test::write_subrecord(out, "SAKD", malformed);
  } else {
    fo4::test::write_subrecord(out, "SAKD", link.bytes);
  }
  const std::vector<std::byte> graph1{
      std::byte{static_cast<unsigned char>('g')},
      std::byte{static_cast<unsigned char>('1')},
      std::byte{0}};
  fo4::test::write_subrecord(out, "SGNM", graph1);
  const std::vector<std::byte> sraf1(4, std::byte{0});
  fo4::test::write_subrecord(out, "SRAF", sraf1);

  fo4::test::end_record(out, race);
  fo4::test::end_group(out, race_group);
  return out.bytes;
}

std::uint32_t nth_raw_in_group(const fo4::Record& record,
                               std::string_view signature,
                               std::size_t group,
                               std::size_t wanted) {
  const auto sig = fo4::Signature::from_string(signature);
  const auto terminator = fo4::Signature::from_string("SRAF");
  std::size_t current_group = 0;
  std::size_t ordinal = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == terminator) {
      if (current_group == group) break;
      ++current_group;
      ordinal = 0;
      continue;
    }
    if (current_group != group || subrecord.signature != sig) continue;
    if (ordinal++ != wanted) continue;
    EXPECT_EQ(subrecord.data.size(), 4u);
    if (subrecord.data.size() != 4) return 0;
    std::uint32_t raw = 0;
    std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
    return raw;
  }
  ADD_FAILURE() << "missing requested nested subrecord ordinal";
  return 0;
}

std::size_t count_in_group(const fo4::Record& record,
                           std::string_view signature,
                           std::size_t group) {
  const auto sig = fo4::Signature::from_string(signature);
  const auto terminator = fo4::Signature::from_string("SRAF");
  std::size_t current_group = 0;
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == terminator) {
      if (current_group == group) break;
      ++current_group;
      continue;
    }
    if (current_group == group && subrecord.signature == sig) ++count;
  }
  return count;
}

bool has_issue(const std::vector<fo4::schema::PreflightIssue>& issues,
               std::string_view code) {
  return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
    return issue.code == code;
  });
}

}

TEST(NestedFormLinkListSchema, ParsesScopedAndIndexedSubgraphPaths) {
  const auto scoped = fo4::schema::scoped_nested_formlink_list_path(
      "RACE\\Subgraphs[1]\\TargetKeywords");
  ASSERT_TRUE(scoped.has_value());
  EXPECT_EQ(scoped->container_index, 1u);
  EXPECT_EQ(scoped->descriptor.subrecord, fo4::Signature::from_string("STKD"));
  EXPECT_EQ(scoped->descriptor.terminator, fo4::Signature::from_string("SRAF"));
  EXPECT_TRUE(scoped->descriptor.can_insert);

  const auto indexed = fo4::schema::indexed_nested_formlink_list_path(
      "RACE\\Subgraphs[1]\\ActorKeywords[1]");
  ASSERT_TRUE(indexed.has_value());
  EXPECT_EQ(indexed->container_index, 1u);
  EXPECT_EQ(indexed->index, 1u);
  ASSERT_EQ(fo4::schema::nested_formlink_list_targets(indexed->descriptor).size(), 1u);
  EXPECT_EQ(fo4::schema::nested_formlink_list_targets(indexed->descriptor)[0],
            fo4::Signature::from_string("KYWD"));
}

TEST(NestedFormLinkListMutation, ReplacesOnlyRequestedElementInRequestedSubgraph) {
  auto plugin = fo4::read_plugin(subgraph_plugin(), "Subgraphs.esp");
  const auto race_key = fo4::FormKey::parse("000803:Subgraphs.esp");
  const auto keyword = fo4::FormKey::parse("000801:Subgraphs.esp");
  const auto parsed = fo4::schema::indexed_nested_formlink_list_path(
      "RACE\\Subgraphs[1]\\ActorKeywords[1]");
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(
      fo4::schema::set_record_nested_formlink_list_element(
          plugin, race_key, parsed->descriptor, parsed->container_index,
          parsed->index, keyword),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "Subgraphs.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  EXPECT_EQ(nth_raw_in_group(*race, "SAKD", 0, 0), 0x00000800u);
  EXPECT_EQ(nth_raw_in_group(*race, "SAKD", 1, 0), 0x00000800u);
  EXPECT_EQ(nth_raw_in_group(*race, "SAKD", 1, 1), 0x00000801u);
  EXPECT_EQ(nth_raw_in_group(*race, "STKD", 0, 0), 0x00000800u);
}

TEST(NestedFormLinkListMutation, InsertsIntoEmptyListBeforeRequestedGroupTerminator) {
  auto plugin = fo4::read_plugin(subgraph_plugin(), "Subgraphs.esp");
  const auto race_key = fo4::FormKey::parse("000803:Subgraphs.esp");
  const auto keyword = fo4::FormKey::parse("000801:Subgraphs.esp");
  const auto scoped = fo4::schema::scoped_nested_formlink_list_path(
      "RACE\\Subgraphs[1]\\TargetKeywords");
  ASSERT_TRUE(scoped.has_value());

  EXPECT_EQ(
      fo4::schema::insert_record_nested_formlink_list_element(
          plugin, race_key, scoped->descriptor, scoped->container_index, 0,
          keyword),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "Subgraphs.esp");
  const auto* race = fo4::find_record(reread, race_key);
  ASSERT_NE(race, nullptr);
  EXPECT_EQ(count_in_group(*race, "STKD", 0), 1u);
  EXPECT_EQ(count_in_group(*race, "STKD", 1), 1u);
  EXPECT_EQ(nth_raw_in_group(*race, "STKD", 1, 0), 0x00000801u);
}

TEST(NestedFormLinkListMutation, RemovesOnlyRequestedNestedElement) {
  auto plugin = fo4::read_plugin(subgraph_plugin(), "Subgraphs.esp");
  const auto race_key = fo4::FormKey::parse("000803:Subgraphs.esp");
  const auto scoped = fo4::schema::scoped_nested_formlink_list_path(
      "RACE\\Subgraphs[1]\\ActorKeywords");
  ASSERT_TRUE(scoped.has_value());

  EXPECT_EQ(
      fo4::schema::remove_record_nested_formlink_list_element(
          plugin, race_key, scoped->descriptor, scoped->container_index, 0),
      fo4::schema::FormLinkListMutationStatus::updated);

  const auto* race = fo4::find_record(plugin, race_key);
  ASSERT_NE(race, nullptr);
  EXPECT_EQ(count_in_group(*race, "SAKD", 0), 1u);
  EXPECT_EQ(count_in_group(*race, "SAKD", 1), 1u);
}

TEST(NestedFormLinkListMutation, RejectsWrongTargetTypeAndOutOfRangeGroup) {
  auto plugin = fo4::read_plugin(subgraph_plugin(), "Subgraphs.esp");
  const auto race_key = fo4::FormKey::parse("000803:Subgraphs.esp");
  const auto color = fo4::FormKey::parse("000802:Subgraphs.esp");
  const auto keyword = fo4::FormKey::parse("000801:Subgraphs.esp");
  const auto scoped = fo4::schema::scoped_nested_formlink_list_path(
      "RACE\\Subgraphs[0]\\ActorKeywords");
  ASSERT_TRUE(scoped.has_value());

  EXPECT_EQ(
      fo4::schema::insert_record_nested_formlink_list_element(
          plugin, race_key, scoped->descriptor, 0, 0, color),
      fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(
      fo4::schema::insert_record_nested_formlink_list_element(
          plugin, race_key, scoped->descriptor, 2, 0, keyword),
      fo4::schema::FormLinkListMutationStatus::container_index_out_of_range);
}

TEST(NestedFormLinkListMutation, MalformedTerminatorOrElementFailsClosed) {
  const auto race_key = fo4::FormKey::parse("000803:Subgraphs.esp");
  const auto keyword = fo4::FormKey::parse("000801:Subgraphs.esp");
  const auto scoped = fo4::schema::scoped_nested_formlink_list_path(
      "RACE\\Subgraphs[0]\\ActorKeywords");
  ASSERT_TRUE(scoped.has_value());

  auto bad_terminator =
      fo4::read_plugin(subgraph_plugin(true, false), "Subgraphs.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::schema::set_record_nested_formlink_list_element(
          bad_terminator, race_key, scoped->descriptor, 0, 0, keyword)),
      fo4::schema::FormLinkListError);

  auto bad_element =
      fo4::read_plugin(subgraph_plugin(false, true), "Subgraphs.esp");
  EXPECT_THROW(
      static_cast<void>(fo4::schema::set_record_nested_formlink_list_element(
          bad_element, race_key, scoped->descriptor, 1, 1, keyword)),
      fo4::schema::FormLinkListError);
}

TEST(NestedFormLinkListPreflight, ReportsMalformedTerminatorAndElementStorage) {
  const auto race_key = fo4::FormKey::parse("000803:Subgraphs.esp");

  auto bad_terminator =
      fo4::read_plugin(subgraph_plugin(true, false), "Subgraphs.esp");
  const auto* bad_terminator_race = fo4::find_record(bad_terminator, race_key);
  ASSERT_NE(bad_terminator_race, nullptr);
  const auto terminator_issues = fo4::schema::preflight_record(*bad_terminator_race);
  EXPECT_TRUE(has_issue(
      terminator_issues, "invalid_nested_formlink_terminator_storage"));

  auto bad_element =
      fo4::read_plugin(subgraph_plugin(false, true), "Subgraphs.esp");
  const auto* bad_element_race = fo4::find_record(bad_element, race_key);
  ASSERT_NE(bad_element_race, nullptr);
  const auto element_issues = fo4::schema::preflight_record(*bad_element_race);
  EXPECT_TRUE(has_issue(
      element_issues, "invalid_nested_formlink_list_storage"));
}
