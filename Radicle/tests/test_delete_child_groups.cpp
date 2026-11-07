#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::int32_t k_world_children = static_cast<std::int32_t>(fo4::GroupType::world_children);
constexpr std::int32_t k_cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
constexpr std::int32_t k_cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
constexpr std::int32_t k_topic_children = static_cast<std::int32_t>(fo4::GroupType::topic_children);
constexpr std::int32_t k_quest_children = static_cast<std::int32_t>(fo4::GroupType::quest_children);

fo4::FormKey local_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Delete.esp"), fo4::LocalFormId{local}};
}

void write_tes4(fo4::test::ByteWriter& out, std::uint32_t next_form_id) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

std::size_t begin_named(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id) {
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Edid\0", 5});
  return record;
}

void write_link(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t target, std::size_t size = 4) {
  fo4::test::ByteWriter data;
  data.u32(target);
  while (data.size() < size) data.u8(0);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

std::size_t begin_children(fo4::test::ByteWriter& out, std::string_view parent, std::int32_t type, std::uint32_t owner) {
  const auto group = fo4::test::begin_group(out, parent, type);
  out.patch_u32(group + 8, owner);
  return group;
}

void write_cell(fo4::test::ByteWriter& out, std::uint32_t cell, std::uint32_t reference, std::uint32_t teleport = 0) {
  fo4::test::end_record(out, begin_named(out, "CELL", cell));
  const auto children = begin_children(out, "CELL", k_cell_children, cell);
  const auto temporary = begin_children(out, "CELL", k_cell_temporary, cell);
  const auto ref = begin_named(out, "REFR", reference);
  if (teleport != 0) write_link(out, "XTEL", teleport, 32);
  fo4::test::end_record(out, ref);
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
}

void expect_child_groups_attached(const fo4::Group& group) {
  for (std::size_t i = 0; i < group.children.size(); ++i) {
    const auto* sub = std::get_if<fo4::Group>(&group.children[i]);
    if (sub == nullptr) continue;
    const auto type = sub->header.group_type;
    if (type == k_world_children || type == k_cell_children || type == k_topic_children) {
      const auto* owner = i == 0 ? nullptr : std::get_if<fo4::Record>(&group.children[i - 1]);
      if (owner == nullptr) {
        ADD_FAILURE() << "children group of type " << type << " is not preceded by a record";
      } else {
        EXPECT_EQ(owner->header.form_id, fo4::RawFormId{sub->header.label_as_u32()});
      }
    }
    expect_child_groups_attached(*sub);
  }
}

std::vector<std::byte> make_dial_plugin(bool info_links_inside) {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x806);
  const auto dials = fo4::test::begin_group(out, "DIAL", 0);
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x800));
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x801));
  const auto first = begin_children(out, "DIAL", k_topic_children, 0x801);
  fo4::test::end_record(out, begin_named(out, "INFO", 0x802));
  fo4::test::end_group(out, first);
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x803));
  const auto second = begin_children(out, "DIAL", k_topic_children, 0x803);
  const auto info = begin_named(out, "INFO", 0x804);
  if (info_links_inside) write_link(out, "PNAM", 0x802);
  fo4::test::end_record(out, info);
  fo4::test::end_group(out, second);
  fo4::test::end_group(out, dials);
  return out.bytes;
}

std::vector<std::byte> make_internal_link_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x804);
  const auto dials = fo4::test::begin_group(out, "DIAL", 0);
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x800));
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x801));
  const auto children = begin_children(out, "DIAL", k_topic_children, 0x801);
  fo4::test::end_record(out, begin_named(out, "INFO", 0x802));
  const auto linked = begin_named(out, "INFO", 0x803);
  write_link(out, "PNAM", 0x802);
  fo4::test::end_record(out, linked);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, dials);
  return out.bytes;
}

std::vector<std::byte> make_foreign_group_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x805);
  const auto dials = fo4::test::begin_group(out, "DIAL", 0);
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x800));
  const auto foreign = begin_children(out, "DIAL", k_topic_children, 0x803);
  fo4::test::end_record(out, begin_named(out, "INFO", 0x804));
  fo4::test::end_group(out, foreign);
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x803));
  fo4::test::end_group(out, dials);
  return out.bytes;
}

std::vector<std::byte> make_last_dial_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x802);
  const auto dials = fo4::test::begin_group(out, "DIAL", 0);
  fo4::test::end_record(out, begin_named(out, "DIAL", 0x800));
  const auto children = begin_children(out, "DIAL", k_topic_children, 0x800);
  fo4::test::end_record(out, begin_named(out, "INFO", 0x801));
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, dials);
  return out.bytes;
}

std::vector<std::byte> make_quest_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x802);
  const auto quests = fo4::test::begin_group(out, "QUST", 0);
  fo4::test::end_record(out, begin_named(out, "QUST", 0x800));
  const auto children = begin_children(out, "QUST", k_quest_children, 0x800);
  fo4::test::end_record(out, begin_named(out, "SCEN", 0x801));
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, quests);
  return out.bytes;
}

std::vector<std::byte> make_cell_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x805);
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  fo4::test::end_record(out, begin_named(out, "CELL", 0x800));
  write_cell(out, 0x801, 0x802);
  write_cell(out, 0x803, 0x804);
  fo4::test::end_group(out, cells);
  return out.bytes;
}

std::vector<std::byte> make_cell_xtel_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x806);
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  fo4::test::end_record(out, begin_named(out, "CELL", 0x800));
  write_cell(out, 0x801, 0x802);
  write_cell(out, 0x803, 0x804, 0x802);
  fo4::test::end_group(out, cells);
  return out.bytes;
}

std::vector<std::byte> make_temporary_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x803);
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  fo4::test::end_record(out, begin_named(out, "CELL", 0x800));
  const auto children = begin_children(out, "CELL", k_cell_children, 0x800);
  const auto temporary = begin_children(out, "CELL", k_cell_temporary, 0x800);
  fo4::test::end_record(out, begin_named(out, "REFR", 0x801));
  fo4::test::end_record(out, begin_named(out, "REFR", 0x802));
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cells);
  return out.bytes;
}

std::vector<std::byte> make_world_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x805);
  const auto worlds = fo4::test::begin_group(out, "WRLD", 0);
  fo4::test::end_record(out, begin_named(out, "WRLD", 0x800));
  fo4::test::end_record(out, begin_named(out, "WRLD", 0x801));
  const auto world = begin_children(out, "WRLD", k_world_children, 0x801);
  fo4::test::end_record(out, begin_named(out, "CELL", 0x802));
  const auto cell = begin_children(out, "CELL", k_cell_children, 0x802);
  const auto temporary = begin_children(out, "CELL", k_cell_temporary, 0x802);
  fo4::test::end_record(out, begin_named(out, "REFR", 0x803));
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, cell);
  fo4::test::end_group(out, world);
  fo4::test::end_record(out, begin_named(out, "WRLD", 0x804));
  fo4::test::end_group(out, worlds);
  return out.bytes;
}

std::vector<std::byte> make_world_outside_link_plugin() {
  fo4::test::ByteWriter out;
  write_tes4(out, 0x807);
  const auto worlds = fo4::test::begin_group(out, "WRLD", 0);
  fo4::test::end_record(out, begin_named(out, "WRLD", 0x800));
  fo4::test::end_record(out, begin_named(out, "WRLD", 0x801));
  const auto world = begin_children(out, "WRLD", k_world_children, 0x801);
  fo4::test::end_record(out, begin_named(out, "CELL", 0x802));
  const auto cell = begin_children(out, "CELL", k_cell_children, 0x802);
  const auto temporary = begin_children(out, "CELL", k_cell_temporary, 0x802);
  fo4::test::end_record(out, begin_named(out, "REFR", 0x803));
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, cell);
  fo4::test::end_group(out, world);
  fo4::test::end_record(out, begin_named(out, "WRLD", 0x804));
  fo4::test::end_group(out, worlds);
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  write_cell(out, 0x805, 0x806, 0x803);
  fo4::test::end_group(out, cells);
  return out.bytes;
}

}  // namespace

TEST(DeleteChildGroups, DeleteDialRemovesItsTopicChildrenGroup) {
  auto plugin = fo4::read_plugin(make_dial_plugin(false), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x801)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x802)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x803)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x804)), nullptr);
  ASSERT_EQ(reloaded.groups.size(), 1u);
  expect_child_groups_attached(reloaded.groups[0]);
  EXPECT_EQ(fo4::collect_groups(reloaded).size(), 2u);
  EXPECT_EQ(reloaded.header.record_count, 5u);
}

TEST(DeleteChildGroups, DeleteQuestRemovesItsQuestChildrenGroup) {
  auto plugin = fo4::read_plugin(make_quest_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x800)), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x801)), nullptr);
  EXPECT_TRUE(reloaded.groups.empty());
}

TEST(DeleteChildGroups, DeleteCellRemovesItsCellChildrenGroup) {
  auto plugin = fo4::read_plugin(make_cell_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x801)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x802)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x803)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x804)), nullptr);
  ASSERT_EQ(reloaded.groups.size(), 1u);
  expect_child_groups_attached(reloaded.groups[0]);
  EXPECT_EQ(fo4::collect_groups(reloaded).size(), 3u);
  EXPECT_EQ(reloaded.header.record_count, 6u);
}

TEST(DeleteChildGroups, DeleteWorldRemovesItsWorldChildrenGroup) {
  auto plugin = fo4::read_plugin(make_world_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x801)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x802)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x803)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x804)), nullptr);
  ASSERT_EQ(reloaded.groups.size(), 1u);
  expect_child_groups_attached(reloaded.groups[0]);
  EXPECT_EQ(fo4::collect_groups(reloaded).size(), 1u);
  EXPECT_EQ(reloaded.header.record_count, 3u);
}

TEST(DeleteChildGroups, DeletingLastDialWithChildrenRemovesItsEmptiedTopGroup) {
  auto plugin = fo4::read_plugin(make_last_dial_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x800)), fo4::RecordDeleteStatus::deleted);
  EXPECT_TRUE(plugin.groups.empty());

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_TRUE(reloaded.groups.empty());
  EXPECT_EQ(reloaded.header.record_count, 0u);
}

TEST(DeleteChildGroups, DeletingLastReferenceInTemporaryGroupLeavesNoEmptyGroup) {
  auto plugin = fo4::read_plugin(make_temporary_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::deleted);
  EXPECT_EQ(fo4::collect_groups(plugin).size(), 3u);

  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x802)), fo4::RecordDeleteStatus::deleted);
  ASSERT_EQ(plugin.groups.size(), 1u);
  ASSERT_EQ(plugin.groups[0].children.size(), 1u);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x801)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x802)), nullptr);
  EXPECT_EQ(fo4::collect_groups(reloaded).size(), 1u);
  EXPECT_EQ(reloaded.header.record_count, 2u);
}

TEST(DeleteChildGroups, DeleteRefusesWhenOutsideInfoReferencesChildWithoutMutation) {
  auto plugin = fo4::read_plugin(make_dial_plugin(true), "Delete.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::referenced);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_NE(fo4::find_record(plugin, local_key(0x802)), nullptr);
}

TEST(DeleteChildGroups, DeleteRefusesWhenOutsideReferenceTargetsCellChildWithoutMutation) {
  auto plugin = fo4::read_plugin(make_cell_xtel_plugin(), "Delete.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::referenced);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_NE(fo4::find_record(plugin, local_key(0x802)), nullptr);
}

TEST(DeleteChildGroups, DeleteRefusesWorldWhenOutsideReferenceTargetsNestedRefWithoutMutation) {
  auto plugin = fo4::read_plugin(make_world_outside_link_plugin(), "Delete.esp");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::referenced);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_NE(fo4::find_record(plugin, local_key(0x803)), nullptr);
}

TEST(DeleteChildGroups, DeleteAllowsReferencesThatStayInsideTheDeletedSubtree) {
  auto plugin = fo4::read_plugin(make_internal_link_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x801)), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x801)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x802)), nullptr);
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x803)), nullptr);
  EXPECT_EQ(reloaded.header.record_count, 2u);
}

TEST(DeleteChildGroups, DeleteKeepsFollowingGroupThatNamesAnotherRecord) {
  auto plugin = fo4::read_plugin(make_foreign_group_plugin(), "Delete.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, local_key(0x800)), fo4::RecordDeleteStatus::deleted);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Delete.esp");
  EXPECT_EQ(fo4::find_record(reloaded, local_key(0x800)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x803)), nullptr);
  EXPECT_NE(fo4::find_record(reloaded, local_key(0x804)), nullptr);
  EXPECT_EQ(fo4::collect_groups(reloaded).size(), 2u);
}
