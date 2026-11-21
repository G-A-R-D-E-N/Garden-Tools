#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <variant>
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

std::size_t begin_group_raw(fo4::test::ByteWriter& out,
                            std::uint32_t label,
                            fo4::GroupType type) {
  const auto offset = out.size();
  out.sig("GRUP");
  out.u32(0);
  out.u32(label);
  out.i32(static_cast<std::int32_t>(type));
  out.u16(0);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  return offset;
}

const fo4::Group* child_group(const fo4::Group& parent, fo4::GroupType type) {
  for (const auto& child : parent.children) {
    const auto* group = std::get_if<fo4::Group>(&child);
    if (group != nullptr && group->header.group_type == static_cast<std::int32_t>(type)) {
      return group;
    }
  }
  return nullptr;
}

std::vector<std::byte> exterior_cell_source() {
  fo4::test::ByteWriter out;
  write_header(out, 0, "Base.esm");
  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto world = fo4::test::begin_record(out, "WRLD", 0, 0x00000700);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Wilderness\0", 11});
  fo4::test::end_record(out, world);
  const auto children = begin_group_raw(out, 0x00000700u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0u, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0u, fo4::GroupType::exterior_cell_subblock);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Wilderness\0", 11});
  fo4::test::end_record(out, cell);
  fo4::test::end_group(out, subblock);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
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

std::vector<std::byte> quest_child_dial_source() {
  fo4::test::ByteWriter out;
  write_header(out, 0, "Base.esm");
  const auto top = fo4::test::begin_group(out, "QUST", 0);
  const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Quest\0", 6});
  fo4::test::end_record(out, quest);
  const auto children = fo4::test::begin_group(out, std::string_view{"\0\x08\0\0", 4}, 10);
  const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Greeting\0", 9});
  fo4::test::end_record(out, dial);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

TEST(CopyOverrideContainment, QuestChildDialOverrideLandsInQuestChildrenGroup) {
  const auto source = fo4::read_plugin(quest_child_dial_source(), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto key = fo4::FormKey::parse("000801:Base.esm");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = fo4::read_plugin(fo4::write_plugin(target), "Target.esp");
  const auto* copied = fo4::find_record(reread, key);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->header.signature, fo4::Signature::from_string("DIAL"));
  bool in_quest_children = false;
  for (const auto& group : reread.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* nested = std::get_if<fo4::Group>(&child);
      if (nested == nullptr || nested->header.group_type != 10) continue;
      for (const auto& grand : nested->children) {
        const auto* record = std::get_if<fo4::Record>(&grand);
        if (record != nullptr && record->form_key == key) in_quest_children = true;
      }
    }
  }
  EXPECT_TRUE(in_quest_children);
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

// Regression: an exterior CELL override must keep its worldspace chain; the copy path used to refuse it.
TEST(CopyOverrideContainment, ExteriorCellOverrideKeepsWorldBlockChain) {
  const auto source = fo4::read_plugin(exterior_cell_source(), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto key = fo4::FormKey::parse("000800:Base.esm");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = fo4::read_plugin(fo4::write_plugin(target), "Target.esp");
  ASSERT_EQ(reread.header.masters.size(), 1u);
  EXPECT_EQ(reread.header.masters[0].key.filename(), "Base.esm");
  ASSERT_NE(fo4::find_record(reread, fo4::FormKey::parse("000700:Base.esm")), nullptr);

  const fo4::Group* top = nullptr;
  for (const auto& group : reread.groups) {
    if (group.header.group_type == 0 &&
        group.header.top_label() == fo4::Signature::from_string("WRLD")) {
      top = &group;
    }
  }
  ASSERT_NE(top, nullptr);
  const auto* children = child_group(*top, fo4::GroupType::world_children);
  ASSERT_NE(children, nullptr);
  const auto* block = child_group(*children, fo4::GroupType::exterior_cell_block);
  ASSERT_NE(block, nullptr);
  const auto* subblock = child_group(*block, fo4::GroupType::exterior_cell_subblock);
  ASSERT_NE(subblock, nullptr);

  bool carried = false;
  for (const auto& child : subblock->children) {
    const auto* record = std::get_if<fo4::Record>(&child);
    if (record != nullptr && record->form_key == key) carried = true;
  }
  EXPECT_TRUE(carried);
}
