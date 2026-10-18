#include "fo4/plugin/vmad_writer.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace fo4;
namespace fixture = fo4::test::vmad;

void append_fragment(test::ByteWriter& tail, std::uint16_t stage, std::uint16_t unknown,
                     std::int32_t index, std::uint8_t unknown_byte, std::string_view function) {
  tail.u16(stage);
  tail.u16(unknown);
  tail.i32(index);
  tail.u8(unknown_byte);
  fixture::name(tail, "QF_Test");
  fixture::name(tail, function);
}

std::vector<std::byte> fragment_tail(bool duplicate_stage = false) {
  test::ByteWriter tail;
  tail.u8(3);
  tail.u16(2);
  fixture::name(tail, "QF_Test");
  tail.u8(0);
  tail.u16(0);
  append_fragment(tail, 10, 0, 0, 0, "Fragment_10");
  append_fragment(tail, duplicate_stage ? 10 : 20, 0xFFFF, 5, 1, "Fragment_20");
  tail.u16(0);
  return tail.bytes;
}

std::vector<std::byte> quest_bytes(const std::vector<std::byte>& tail) {
  auto data = fixture::payload();
  data.insert(data.end(), tail.begin(), tail.end());
  return data;
}

std::size_t extras_base(const std::vector<std::byte>& vmad) {
  return vmad.size() - read_vmad_scripts(vmad).trailing_bytes;
}

QuestVmadExtras extras_of(const std::vector<std::byte>& vmad) {
  const auto scripts = read_vmad_scripts(vmad);
  return read_quest_vmad_extras(
      std::span<const std::byte>(vmad).subspan(vmad.size() - scripts.trailing_bytes),
      scripts.object_format);
}

std::vector<std::byte> slice(const std::vector<std::byte>& data, std::size_t begin,
                             std::size_t end) {
  return {data.begin() + static_cast<std::ptrdiff_t>(begin),
          data.begin() + static_cast<std::ptrdiff_t>(end)};
}

void expect_fragment(const QuestVmadFragment& actual, std::uint16_t stage, std::uint16_t unknown,
                     std::int32_t index, std::uint8_t unknown_byte, std::string_view function) {
  EXPECT_EQ(actual.stage, stage);
  EXPECT_EQ(actual.unknown, unknown);
  EXPECT_EQ(actual.stage_index, index);
  EXPECT_EQ(actual.unknown_byte, unknown_byte);
  EXPECT_EQ(actual.script, "QF_Test");
  EXPECT_EQ(actual.function, function);
}

}  // namespace

TEST(VmadQuestFragments, NamesAreRewrittenInPlace) {
  const auto tail = fragment_tail();
  auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  ASSERT_TRUE(vmad_coverage(before, sig_qust).complete());
  const auto initial = extras_of(before);
  ASSERT_EQ(initial.fragments.size(), 2u);
  EXPECT_EQ(initial.script_name, "QF_Test");
  expect_fragment(initial.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(initial.fragments[1], 20, 0xFFFF, 5, 1, "Fragment_20");
  EXPECT_EQ(initial.fragments[1].offset, initial.fragments[0].end);
  EXPECT_EQ(initial.fragments[1].end, tail.size() - 2);
  EXPECT_EQ(initial.count_offset, 1u);

  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 20, "QF_Other", "Fragment_Stage_0020"),
            VmadStructureMutationStatus::updated);
  const auto saved = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(saved, sig_qust).complete());
  const auto edited = extras_of(saved);
  ASSERT_EQ(edited.fragments.size(), 2u);
  EXPECT_EQ(edited.script_name, "QF_Test");
  expect_fragment(edited.fragments[0], 10, 0, 0, 0, "Fragment_10");
  EXPECT_EQ(edited.fragments[0].offset, initial.fragments[0].offset);
  EXPECT_EQ(edited.fragments[0].end, initial.fragments[0].end);
  EXPECT_EQ(edited.fragments[1].stage, 20u);
  EXPECT_EQ(edited.fragments[1].unknown, 0xFFFF);
  EXPECT_EQ(edited.fragments[1].stage_index, 5);
  EXPECT_EQ(edited.fragments[1].unknown_byte, 1);
  EXPECT_EQ(edited.fragments[1].script, "QF_Other");
  EXPECT_EQ(edited.fragments[1].function, "Fragment_Stage_0020");

  const auto before_base = extras_base(before);
  const auto saved_base = extras_base(saved);
  EXPECT_EQ(slice(saved, 0, saved_base + edited.fragments[1].offset),
            slice(before, 0, before_base + initial.fragments[1].offset));
  EXPECT_EQ(slice(saved, saved_base + edited.fragments[1].end, saved.size()),
            slice(before, before_base + initial.fragments[1].end, before.size()));

  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 20, "QF_Other", "Fragment_Stage_0020"),
            VmadStructureMutationStatus::unchanged);
  EXPECT_EQ(fixture::subrecord(plugin).data, saved);
  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 10, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::unchanged);
  EXPECT_EQ(fixture::subrecord(plugin).data, saved);
}

TEST(VmadQuestFragments, RemovalDropsTheBindingAndItsCount) {
  const auto tail = fragment_tail();
  auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto initial = extras_of(before);
  ASSERT_EQ(initial.fragments.size(), 2u);
  const auto base = extras_base(before);
  const auto prefix = base + initial.count_offset;
  const auto first_entry = initial.fragments[0].end - initial.fragments[0].offset;

  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 10),
            VmadStructureMutationStatus::updated);
  const auto first = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(first, sig_qust).complete());
  const auto trimmed = extras_of(first);
  ASSERT_EQ(trimmed.fragments.size(), 1u);
  expect_fragment(trimmed.fragments[0], 20, 0xFFFF, 5, 1, "Fragment_20");
  EXPECT_EQ(trimmed.script_name, "QF_Test");
  EXPECT_EQ(first[prefix], std::byte{1});
  EXPECT_EQ(first[prefix + 1], std::byte{0});
  EXPECT_EQ(first.size(), before.size() - first_entry);
  EXPECT_EQ(slice(first, 0, base + initial.fragments[0].offset),
            slice(before, 0, base + initial.fragments[0].offset));
  EXPECT_EQ(slice(first, base + initial.fragments[0].offset, first.size()),
            slice(before, base + initial.fragments[1].offset, before.size()));

  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 20),
            VmadStructureMutationStatus::updated);
  const auto emptied = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(emptied, sig_qust).complete());
  const auto none = extras_of(emptied);
  EXPECT_TRUE(none.fragments.empty());
  EXPECT_EQ(none.script_name, "QF_Test");
  EXPECT_EQ(emptied[prefix], std::byte{0});
  EXPECT_EQ(emptied[prefix + 1], std::byte{0});
  EXPECT_EQ(emptied.size(),
            before.size() - first_entry -
                (initial.fragments[1].end - initial.fragments[1].offset));
  EXPECT_EQ(slice(emptied, 0, base + initial.fragments[0].offset),
            slice(before, 0, base + initial.fragments[0].offset));
  EXPECT_EQ(slice(emptied, emptied.size() - 2, emptied.size()),
            slice(before, before.size() - 2, before.size()));

  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 10),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 20, "QF_Test", "Fragment_20"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(fixture::subrecord(plugin).data, emptied);
}

TEST(VmadQuestFragments, EditsRefuseForeignRecordsAndBadPayloads) {
  auto refr = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto refr_before = write_plugin(refr);
  EXPECT_EQ(set_quest_vmad_fragment(refr, fixture::key(), 10, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(remove_quest_vmad_fragment(refr, fixture::key(), 10),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(write_plugin(refr), refr_before);

  auto bare = read_plugin(fixture::plugin_bytes(fixture::payload(), "QUST"), "Vmad.esp");
  EXPECT_EQ(set_quest_vmad_fragment(bare, fixture::key(), 10, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(remove_quest_vmad_fragment(bare, fixture::key(), 10),
            VmadStructureMutationStatus::binding_missing);

  auto quest =
      read_plugin(fixture::plugin_bytes(quest_bytes(fragment_tail()), "QUST"), "Vmad.esp");
  const auto quest_before = write_plugin(quest);
  EXPECT_EQ(set_quest_vmad_fragment(quest, fixture::key(0x999), 10, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(remove_quest_vmad_fragment(quest, fixture::key(0x999), 10),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(set_quest_vmad_fragment(quest, fixture::key(), 30, "QF_Test", "Fragment_30"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_THROW(set_quest_vmad_fragment(quest, fixture::key(), 10, "", "Fragment_10"),
               schema::ScalarError);
  EXPECT_THROW(set_quest_vmad_fragment(quest, fixture::key(), 10, "QF_Test", ""),
               schema::ScalarError);
  EXPECT_EQ(write_plugin(quest), quest_before);

  auto truncated = quest_bytes(fragment_tail());
  truncated.pop_back();
  auto damaged = read_plugin(fixture::plugin_bytes(truncated, "QUST"), "Vmad.esp");
  const auto damaged_before = write_plugin(damaged);
  EXPECT_THROW(set_quest_vmad_fragment(damaged, fixture::key(), 10, "QF_Test", "Fragment_10"),
               schema::ScalarError);
  EXPECT_THROW(remove_quest_vmad_fragment(damaged, fixture::key(), 10), schema::ScalarError);
  EXPECT_EQ(write_plugin(damaged), damaged_before);

  auto twice =
      read_plugin(fixture::plugin_bytes(quest_bytes(fragment_tail(true)), "QUST"), "Vmad.esp");
  const auto twice_before = write_plugin(twice);
  ASSERT_EQ(extras_of(fixture::subrecord(twice).data).fragments.size(), 2u);
  EXPECT_THROW(set_quest_vmad_fragment(twice, fixture::key(), 10, "QF_Test", "Fragment_Ten"),
               schema::ScalarError);
  EXPECT_THROW(remove_quest_vmad_fragment(twice, fixture::key(), 10), schema::ScalarError);
  EXPECT_EQ(write_plugin(twice), twice_before);
}
