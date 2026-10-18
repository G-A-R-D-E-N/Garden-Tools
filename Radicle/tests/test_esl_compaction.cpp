#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct RecordSpec {
  std::string_view signature;
  std::uint32_t form_id;
  std::string_view editor_id;
  std::optional<std::uint32_t> name_link;
};

std::vector<std::byte> make_flat_plugin(std::initializer_list<RecordSpec> records,
                                        std::uint32_t next_form_id,
                                        std::string_view master = {}) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (!master.empty()) {
    std::string terminated{master};
    terminated.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", std::string_view{terminated});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  std::optional<std::size_t> group;
  std::string_view open;
  for (const auto& spec : records) {
    if (!group.has_value() || open != spec.signature) {
      if (group.has_value()) fo4::test::end_group(out, *group);
      group = fo4::test::begin_group(out, spec.signature, 0);
      open = spec.signature;
    }
    const auto record = fo4::test::begin_record(out, spec.signature, 0, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", spec.editor_id);
    if (spec.name_link.has_value()) {
      fo4::test::ByteWriter link;
      link.u32(*spec.name_link);
      fo4::test::write_subrecord(out, "NAME", link.bytes);
    }
    fo4::test::end_record(out, record);
  }
  if (group.has_value()) fo4::test::end_group(out, *group);
  return out.bytes;
}

std::vector<std::byte> make_cell_plugin(std::uint32_t cell_id,
                                        std::uint32_t reference_id,
                                        std::uint32_t next_form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, cell_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Cell\0", 5});
  fo4::test::end_record(out, cell);
  const auto children = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 6);
  out.patch_u32(children + 8, cell_id);
  const auto reference = fo4::test::begin_record(out, "REFR", 0, reference_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Ref\0", 4});
  fo4::test::end_record(out, reference);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

const fo4::Group* group_of_type(const fo4::Plugin& plugin, std::int32_t type) {
  const fo4::Group* found = nullptr;
  fo4::visit_groups(plugin, [&](const fo4::Group& group) {
    if (found == nullptr && group.header.group_type == type) found = &group;
  });
  return found;
}

}  // namespace

TEST(EslCompaction, MovesOutOfRangeRecordsAndKeepsInRangeSlots) {
  const auto in_range = fo4::FormKey::parse("000800:Compact.esp");
  const auto out_of_range = fo4::FormKey::parse("009000:Compact.esp");
  const auto moved = fo4::FormKey::parse("000801:Compact.esp");
  auto plugin = fo4::read_plugin(
      make_flat_plugin({{"MISC", 0x800, std::string_view{"Target\0", 7}, std::nullopt},
                        {"MISC", 0x9000, std::string_view{"Mover\0", 6}, 0x800}},
                       0x9001),
      "Compact.esp");

  const auto result = fo4::compact_to_esl(plugin);
  EXPECT_EQ(result.status, fo4::EslCompactionStatus::compacted);
  EXPECT_EQ(result.remapped, 1u);
  EXPECT_EQ(result.next_form_id, 0x802u);
  EXPECT_TRUE(result.blockers.empty());

  EXPECT_NE(fo4::find_record(plugin, in_range), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, out_of_range), nullptr);
  ASSERT_NE(fo4::find_record(plugin, moved), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, moved)->header.form_id, fo4::RawFormId{0x00000801});
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
  EXPECT_FALSE(plugin.header.tes4_flags.is_small_file());

  const auto links = fo4::collect_form_links(plugin);
  const auto link = std::find_if(links.begin(), links.end(), [&](const auto& hit) {
    return hit.record == moved && hit.subrecord == fo4::sig_name;
  });
  ASSERT_NE(link, links.end());
  EXPECT_EQ(link->key, in_range);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Compact.esp");
  EXPECT_NE(fo4::find_record(reloaded, moved), nullptr);
  EXPECT_EQ(reloaded.header.next_form_id, 0x802u);
}

TEST(EslCompaction, RelabelsTheMovedRecordsChildGroup) {
  auto plugin =
      fo4::read_plugin(make_cell_plugin(0x9000, 0x800, 0x9001), "Compact.esp");
  const auto moved = fo4::FormKey::parse("000801:Compact.esp");

  const auto result = fo4::compact_to_esl(plugin);
  EXPECT_EQ(result.status, fo4::EslCompactionStatus::compacted);
  EXPECT_EQ(result.remapped, 1u);

  ASSERT_NE(fo4::find_record(plugin, moved), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, fo4::FormKey::parse("009000:Compact.esp")), nullptr);
  const auto* children = group_of_type(plugin, 6);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->header.label_as_u32(), 0x00000801u);
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000800:Compact.esp")), nullptr);

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Compact.esp");
  const auto* reloaded_children = group_of_type(reloaded, 6);
  ASSERT_NE(reloaded_children, nullptr);
  EXPECT_EQ(reloaded_children->header.label_as_u32(), 0x00000801u);
}

TEST(EslCompaction, RefusesNestedOutOfRangeRecordWithoutMutation) {
  auto plugin =
      fo4::read_plugin(make_cell_plugin(0x800, 0x9000, 0x9001), "Compact.esp");
  const auto original = fo4::test::original_bytes(plugin);

  const auto result = fo4::compact_to_esl(plugin);
  EXPECT_EQ(result.status, fo4::EslCompactionStatus::blocked_nested_records);
  EXPECT_EQ(result.remapped, 0u);
  ASSERT_EQ(result.blockers.size(), 1u);
  EXPECT_EQ(result.blockers[0].key, fo4::FormKey::parse("009000:Compact.esp"));
  EXPECT_TRUE(result.blockers[0].nested);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(EslCompaction, LowersNextFormIdWhenEveryRecordIsAlreadyInRange) {
  auto plugin = fo4::read_plugin(
      make_flat_plugin({{"MISC", 0x800, std::string_view{"Only\0", 5}, std::nullopt}}, 0x9001),
      "Compact.esp");

  const auto result = fo4::compact_to_esl(plugin);
  EXPECT_EQ(result.status, fo4::EslCompactionStatus::already_compatible);
  EXPECT_EQ(result.remapped, 0u);
  EXPECT_EQ(result.next_form_id, 0x801u);
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  EXPECT_FALSE(plugin.header.tes4_flags.is_small_file());
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000800:Compact.esp")), nullptr);
}

TEST(EslCompaction, SlotRangeFollowsTheHardcodedRangeRule) {
  const auto without_masters = fo4::read_plugin(
      make_flat_plugin({{"MISC", 0x800, std::string_view{"Only\0", 5}, std::nullopt}}, 0x801),
      "Compact.esp");
  EXPECT_EQ(fo4::esl_slot_range(without_masters).first, 0x800u);
  EXPECT_EQ(fo4::esl_slot_range(without_masters).last, 0xFFFu);
  EXPECT_EQ(fo4::esl_slot_range(without_masters).capacity, 0x800u);

  const auto with_master = fo4::read_plugin(
      make_flat_plugin({{"MISC", 0x800, std::string_view{"Only\0", 5}, std::nullopt}}, 0x801,
                       "Master.esm"),
      "Compact.esp");
  EXPECT_EQ(fo4::esl_slot_range(with_master).first, 0x001u);
  EXPECT_EQ(fo4::esl_slot_range(with_master).last, 0xFFFu);
  EXPECT_EQ(fo4::esl_slot_range(with_master).capacity, 0xFFFu);
}
