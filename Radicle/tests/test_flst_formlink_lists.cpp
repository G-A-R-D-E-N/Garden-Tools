#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::uint32_t nth_raw(const fo4::Record& record,
                      std::string_view signature,
                      std::size_t wanted) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t ordinal = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig) continue;
    if (ordinal++ != wanted) continue;
    EXPECT_EQ(subrecord.data.size(), 4u);
    if (subrecord.data.size() != 4) return 0;
    std::uint32_t raw = 0;
    std::memcpy(&raw, subrecord.data.data(), sizeof(raw));
    return raw;
  }
  ADD_FAILURE() << "missing requested subrecord ordinal";
  return 0;
}

std::size_t count_subrecords(const fo4::Record& record,
                             std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig) ++count;
  }
  return count;
}

// The LNAM entry (index) that immediately precedes the first LNAM must follow
// the record's EDID, proving empty-list insertion appended after the header
// subrecord rather than before it.
std::size_t index_of(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  return record.subrecords.size();
}

// One MISC and one KYWD target (distinct record types, to prove the wildcard
// target admits either), an empty FLST (EDID only), and a populated FLST.
std::vector<std::byte> plugin_with_form_lists() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x812);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000800, 131);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);

  const auto kywd_group = fo4::test::begin_group(out, "KYWD", 0);
  const auto kywd = fo4::test::begin_record(out, "KYWD", 0, 0x00000801, 131);
  fo4::test::end_record(out, kywd);
  fo4::test::end_group(out, kywd_group);

  const auto flst_group = fo4::test::begin_group(out, "FLST", 0);

  auto empty = fo4::test::begin_record(out, "FLST", 0, 0x00000810, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("EmptyList"));
  fo4::test::end_record(out, empty);

  auto full = fo4::test::begin_record(out, "FLST", 0, 0x00000811, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("FullList"));
  fo4::test::ByteWriter first;
  first.u32(0x00000800);
  fo4::test::write_subrecord(out, "LNAM", first.bytes);
  fo4::test::ByteWriter second;
  second.u32(0x00000801);
  fo4::test::write_subrecord(out, "LNAM", second.bytes);
  fo4::test::end_record(out, full);

  fo4::test::end_group(out, flst_group);
  return out.bytes;
}

fo4::schema::FormLinkListDescriptor items_descriptor() {
  const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("FLST"), "Items");
  EXPECT_TRUE(descriptor.has_value());
  return descriptor.value_or(fo4::schema::FormLinkListDescriptor{});
}

}  // namespace

TEST(FlstFormLinkList, ResolvesItemsListByFieldAlias) {
  const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("FLST"), "Items");
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->record, fo4::Signature::from_string("FLST"));
  EXPECT_EQ(descriptor->subrecord, fo4::Signature::from_string("LNAM"));
  EXPECT_EQ(descriptor->path, "FLST\\Items");
  EXPECT_TRUE(descriptor->can_insert);
  EXPECT_TRUE(descriptor->wildcard_target);
  EXPECT_TRUE(descriptor->append_when_empty);
  EXPECT_FALSE(descriptor->nullable);
}

TEST(FlstFormLinkList, FieldAliasIsCaseInsensitive) {
  for (const auto* alias : {"items", "ITEMS", "iTeMs", "Items"}) {
    const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
        fo4::Signature::from_string("FLST"), alias);
    ASSERT_TRUE(descriptor.has_value()) << alias;
    EXPECT_EQ(descriptor->subrecord, fo4::Signature::from_string("LNAM")) << alias;
  }
}

TEST(FlstFormLinkList, EmptyAliasAndUnknownFieldDoNotResolve) {
  EXPECT_FALSE(fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("FLST"), "").has_value());
  EXPECT_FALSE(fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("FLST"), "Nope").has_value());
  EXPECT_FALSE(fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("RACE"), "Items").has_value());
}

TEST(FlstFormLinkList, WildcardTargetAdmitsAnyRecordType) {
  const auto descriptor = items_descriptor();
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(
      descriptor, fo4::Signature::from_string("MISC")));
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(
      descriptor, fo4::Signature::from_string("KYWD")));
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(
      descriptor, fo4::Signature::from_string("WEAP")));
  EXPECT_TRUE(fo4::schema::formlink_list_targets(descriptor).empty());
}

TEST(FlstFormLinkList, AppendsFirstElementAfterEditorId) {
  auto plugin = fo4::read_plugin(plugin_with_form_lists(), "Lists.esp");
  const auto empty_key = fo4::FormKey::parse("000810:Lists.esp");
  const auto target = fo4::FormKey::parse("000800:Lists.esp");
  const auto descriptor = items_descriptor();

  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, empty_key, descriptor),
            0u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, empty_key, descriptor, 0, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Lists.esp");
  const auto* flst = fo4::find_record(reread, empty_key);
  ASSERT_NE(flst, nullptr);
  ASSERT_EQ(count_subrecords(*flst, "LNAM"), 1u);
  EXPECT_EQ(nth_raw(*flst, "LNAM", 0), 0x00000800u);
  EXPECT_GT(index_of(*flst, "LNAM"), index_of(*flst, "EDID"));
}

TEST(FlstFormLinkList, AppendsAtEndOfPopulatedList) {
  auto plugin = fo4::read_plugin(plugin_with_form_lists(), "Lists.esp");
  const auto full_key = fo4::FormKey::parse("000811:Lists.esp");
  const auto target = fo4::FormKey::parse("000801:Lists.esp");
  const auto descriptor = items_descriptor();

  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, full_key, descriptor),
            2u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, full_key, descriptor, 2, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Lists.esp");
  const auto* flst = fo4::find_record(reread, full_key);
  ASSERT_NE(flst, nullptr);
  ASSERT_EQ(count_subrecords(*flst, "LNAM"), 3u);
  EXPECT_EQ(nth_raw(*flst, "LNAM", 0), 0x00000800u);
  EXPECT_EQ(nth_raw(*flst, "LNAM", 1), 0x00000801u);
  EXPECT_EQ(nth_raw(*flst, "LNAM", 2), 0x00000801u);
}

TEST(FlstFormLinkList, RemovesRequestedElement) {
  auto plugin = fo4::read_plugin(plugin_with_form_lists(), "Lists.esp");
  const auto full_key = fo4::FormKey::parse("000811:Lists.esp");
  const auto descriptor = items_descriptor();

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(
                plugin, full_key, descriptor, 0),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Lists.esp");
  const auto* flst = fo4::find_record(reread, full_key);
  ASSERT_NE(flst, nullptr);
  ASSERT_EQ(count_subrecords(*flst, "LNAM"), 1u);
  EXPECT_EQ(nth_raw(*flst, "LNAM", 0), 0x00000801u);
}

// An FLST whose Items list holds the same target twice, to prove value-based
// removal drops every matching entry the way WriteService.RemoveListItem does.
std::vector<std::byte> plugin_with_duplicate_items() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x812);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000800, 131);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);

  const auto flst_group = fo4::test::begin_group(out, "FLST", 0);
  auto full = fo4::test::begin_record(out, "FLST", 0, 0x00000811, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Dupes"));
  for (const std::uint32_t value : {0x00000800u, 0x00000801u, 0x00000800u}) {
    fo4::test::ByteWriter link;
    link.u32(value);
    fo4::test::write_subrecord(out, "LNAM", link.bytes);
  }
  fo4::test::end_record(out, full);
  fo4::test::end_group(out, flst_group);
  return out.bytes;
}

TEST(FlstFormLinkList, RemovesEveryElementMatchingValue) {
  auto plugin = fo4::read_plugin(plugin_with_duplicate_items(), "Lists.esp");
  const auto full_key = fo4::FormKey::parse("000811:Lists.esp");
  const auto target = fo4::FormKey::parse("000800:Lists.esp");
  const auto descriptor = items_descriptor();

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_elements_matching(
                plugin, full_key, descriptor, target),
            2u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Lists.esp");
  const auto* flst = fo4::find_record(reread, full_key);
  ASSERT_NE(flst, nullptr);
  ASSERT_EQ(count_subrecords(*flst, "LNAM"), 1u);
  EXPECT_EQ(nth_raw(*flst, "LNAM", 0), 0x00000801u);
}

TEST(FlstFormLinkList, RemoveMatchingLeavesListUnchangedWhenAbsent) {
  auto plugin = fo4::read_plugin(plugin_with_duplicate_items(), "Lists.esp");
  const auto full_key = fo4::FormKey::parse("000811:Lists.esp");
  const auto absent = fo4::FormKey::parse("000809:Lists.esp");
  const auto descriptor = items_descriptor();

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_elements_matching(
                plugin, full_key, descriptor, absent),
            0u);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, full_key, descriptor),
            3u);
}
