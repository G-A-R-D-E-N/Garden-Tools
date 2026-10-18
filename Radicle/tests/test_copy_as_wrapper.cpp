#include "fo4/forms/copy_as_wrapper.hpp"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Rec {
  std::string_view signature;
  std::uint32_t form_id;
};

std::vector<std::byte> make_plugin(std::initializer_list<Rec> records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x800u + static_cast<std::uint32_t>(records.size()));
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  for (const auto& rec : records) {
    const auto group = fo4::test::begin_group(out, rec.signature, 0);
    const auto record = fo4::test::begin_record(out, rec.signature, 0, rec.form_id);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

fo4::forms::LeveledListEntry entry_for(std::string_view form_key) {
  fo4::forms::LeveledListEntry entry;
  entry.item = fo4::FormKey::parse(form_key);
  return entry;
}

std::string edid_of(const fo4::Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != fo4::sig_edid) continue;
    const std::string_view view(reinterpret_cast<const char*>(subrecord.data.data()),
                                subrecord.data.size());
    return std::string(view.substr(0, view.find('\0')));
  }
  return {};
}

std::vector<std::byte> lvlo_of(const fo4::Record& record) {
  const auto lvlo = fo4::Signature::from_string("LVLO");
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == lvlo) return subrecord.data;
  }
  return {};
}

struct SourceFixture {
  fo4::Plugin plugin;
  fo4::FormKey key;
};

SourceFixture list_source(std::string_view signature) {
  auto plugin = fo4::read_plugin(make_plugin({{"WEAP", 0x800}, {"MISC", 0x801}}), "Source.esp");
  const auto key = fo4::forms::create_leveled_list(
      plugin, fo4::Signature::from_string(signature), std::string_view{"SourceList"}, 0, 0, 0);
  return SourceFixture{std::move(plugin), key};
}

void add_two_entries(SourceFixture& source) {
  const auto first = entry_for("000800:Source.esp");
  const auto second = entry_for("000801:Source.esp");
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(source.plugin, source.key, 0, first));
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(source.plugin, source.key, 1, second));
}

}  // namespace

TEST(CopyAsWrapper, MovesTheEntriesToACopyAndLeavesOneEntryBehind) {
  auto source = list_source("LVLI");
  add_two_entries(source);
  auto target = fo4::read_plugin(make_plugin({{"MISC", 0x800}}), "Patch.esp");

  const auto result =
      fo4::forms::copy_record_as_wrapper(target, source.plugin, source.key, "WrappedList");
  ASSERT_EQ(result.status, fo4::forms::WrapperCopyStatus::wrapped);
  EXPECT_EQ(result.wrapper, source.key);
  EXPECT_NE(result.copy, source.key);

  const auto* copy = fo4::find_record(target, result.copy);
  ASSERT_NE(copy, nullptr);
  EXPECT_EQ(copy->header.signature, fo4::Signature::from_string("LVLI"));
  EXPECT_EQ(edid_of(*copy), "WrappedList");
  const auto copied_list = fo4::forms::decode_leveled_list(target, result.copy);
  ASSERT_EQ(copied_list.entries.size(), 2u);
  EXPECT_EQ(copied_list.entries[0].item, fo4::FormKey::parse("000800:Source.esp"));
  EXPECT_EQ(copied_list.entries[1].item, fo4::FormKey::parse("000801:Source.esp"));

  const auto* wrapper = fo4::find_record(target, source.key);
  ASSERT_NE(wrapper, nullptr);
  EXPECT_EQ(edid_of(*wrapper), "SourceList");
  const auto wrapped_list = fo4::forms::decode_leveled_list(target, source.key);
  ASSERT_EQ(wrapped_list.entries.size(), 1u);
  EXPECT_EQ(wrapped_list.entries[0].item, result.copy);
  EXPECT_EQ(wrapped_list.entries[0].level, 1);
  EXPECT_EQ(wrapped_list.entries[0].count, 1);

  const auto lvlo = lvlo_of(*wrapper);
  ASSERT_EQ(lvlo.size(), 12u);
  EXPECT_EQ(lvlo[0], std::byte{1});
  EXPECT_EQ(lvlo[1], std::byte{0});
  EXPECT_EQ(lvlo[8], std::byte{1});
  EXPECT_EQ(lvlo[9], std::byte{0});
  EXPECT_EQ(lvlo[10], std::byte{0});

  ASSERT_EQ(target.header.masters.size(), 1u);
  EXPECT_EQ(target.header.masters[0].key.filename(), "Source.esp");

  const auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  ASSERT_EQ(fo4::forms::decode_leveled_list(reread, source.key).entries.size(), 1u);
  ASSERT_EQ(fo4::forms::decode_leveled_list(reread, result.copy).entries.size(), 2u);
  EXPECT_EQ(fo4::forms::decode_leveled_list(reread, source.key).entries[0].item, result.copy);
}

TEST(CopyAsWrapper, KeepsTheSourceEditorIdWhenNoWrapperEditorIdIsGiven) {
  auto source = list_source("LVLI");
  add_two_entries(source);
  auto target = fo4::read_plugin(make_plugin({{"MISC", 0x800}}), "Patch.esp");

  const auto result = fo4::forms::copy_record_as_wrapper(target, source.plugin, source.key);
  ASSERT_EQ(result.status, fo4::forms::WrapperCopyStatus::wrapped);
  EXPECT_EQ(edid_of(*fo4::find_record(target, result.copy)), "SourceList");
  EXPECT_EQ(edid_of(*fo4::find_record(target, source.key)), "SourceList");
  EXPECT_EQ(fo4::forms::decode_leveled_list(target, source.key).entries.size(), 1u);
}

TEST(CopyAsWrapper, RefusesASignatureThatIsNotALeveledList) {
  const auto source = fo4::read_plugin(make_plugin({{"MISC", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(make_plugin({{"MISC", 0x800}}), "Patch.esp");
  const auto original = fo4::test::original_bytes(target);

  const auto result = fo4::forms::copy_record_as_wrapper(
      target, source, fo4::FormKey::parse("000800:Source.esp"), "Copy");
  EXPECT_EQ(result.status, fo4::forms::WrapperCopyStatus::unsupported_signature);
  EXPECT_EQ(fo4::test::original_bytes(target), original);
  EXPECT_TRUE(target.header.masters.empty());
}

TEST(CopyAsWrapper, MissingSourceRecordDoesNotMutateTheDestination) {
  auto source = list_source("LVLI");
  add_two_entries(source);
  auto target = fo4::read_plugin(make_plugin({{"MISC", 0x800}}), "Patch.esp");
  const auto original = fo4::test::original_bytes(target);

  const auto result = fo4::forms::copy_record_as_wrapper(
      target, source.plugin, fo4::FormKey::parse("000900:Source.esp"), "Copy");
  EXPECT_EQ(result.status, fo4::forms::WrapperCopyStatus::source_missing);
  EXPECT_EQ(fo4::test::original_bytes(target), original);
  EXPECT_TRUE(target.header.masters.empty());
}

TEST(CopyAsWrapper, RefusesWhenTheDestinationAlreadyHoldsTheRecord) {
  auto source = list_source("LVLI");
  add_two_entries(source);
  auto target = fo4::read_plugin(make_plugin({{"MISC", 0x800}}), "Patch.esp");

  const auto first =
      fo4::forms::copy_record_as_wrapper(target, source.plugin, source.key, "WrappedList");
  ASSERT_EQ(first.status, fo4::forms::WrapperCopyStatus::wrapped);
  ASSERT_EQ(fo4::forms::decode_leveled_list(target, source.key).entries.size(), 1u);

  const auto second =
      fo4::forms::copy_record_as_wrapper(target, source.plugin, source.key, "SecondWrapper");
  EXPECT_EQ(second.status, fo4::forms::WrapperCopyStatus::override_failed);

  const auto wrapper = fo4::forms::decode_leveled_list(target, source.key);
  ASSERT_EQ(wrapper.entries.size(), 1u);
  EXPECT_EQ(wrapper.entries[0].item, first.copy);
  EXPECT_EQ(edid_of(*fo4::find_record(target, source.key)), "SourceList");
  std::size_t lists = 0;
  for (const auto* record : fo4::collect_records(target)) {
    if (record->header.signature == fo4::Signature::from_string("LVLI")) ++lists;
  }
  EXPECT_EQ(lists, 2u);
}
