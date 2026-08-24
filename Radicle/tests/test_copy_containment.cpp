#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out,
                  std::uint32_t flags,
                  std::string_view master = {}) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (!master.empty()) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> nested_refr_source() {
  fo4::test::ByteWriter out;
  write_header(out, 0, "Base.esm");
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto children = fo4::test::begin_group(out, "CELL", 6);
  const auto temporary = fo4::test::begin_group(out, "CELL", 9);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NestedRef\0", 10});
  fo4::test::end_record(out, refr);
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> top_level_dial_source() {
  fo4::test::ByteWriter out;
  write_header(out, 0, "Base.esm");
  const auto group = fo4::test::begin_group(out, "DIAL", 0);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TopicOverride\0", 14});
  fo4::test::end_record(out, dial);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> empty_target(std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  write_header(out, flags);
  return out.bytes;
}

}  // namespace

TEST(CopyOverrideContainment, MissingNestedSourceRecordFailsWithoutMutation) {
  const auto source = fo4::read_plugin(nested_refr_source(), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto original = fo4::test::original_bytes(target);

  EXPECT_EQ(
      fo4::copy_record_as_override(
          target, source, fo4::FormKey::parse("000800:Base.esm"), false),
      fo4::RecordCopyStatus::source_containment_unsupported);
  EXPECT_EQ(fo4::test::original_bytes(target), original);
  EXPECT_TRUE(target.groups.empty());
  EXPECT_TRUE(target.header.masters.empty());
}

TEST(CopyOverrideContainment, TopLevelOverrideIntoMasterWritesOnamExactlyOnce) {
  const auto source = fo4::read_plugin(top_level_dial_source(), "Source.esp");
  auto target = fo4::read_plugin(
      empty_target(fo4::RecordFlags::master), "Patch.esm");
  const auto key = fo4::FormKey::parse("000800:Base.esm");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esm");
  ASSERT_EQ(reread.header.masters.size(), 1u);
  EXPECT_EQ(reread.header.masters[0].key.filename(), "Base.esm");
  ASSERT_EQ(reread.header.overrides.size(), 1u);
  EXPECT_EQ(reread.header.overrides[0], fo4::RawFormId{0x00000800u});
  EXPECT_NE(fo4::find_record(reread, key), nullptr);

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, true),
            fo4::RecordCopyStatus::replaced);
  reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esm");
  ASSERT_EQ(reread.header.overrides.size(), 1u);
  EXPECT_EQ(reread.header.overrides[0], fo4::RawFormId{0x00000800u});
}
