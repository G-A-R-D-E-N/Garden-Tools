#include "fo4/c_api_vmad.h"
#include "fo4/plugin/vmad_writer.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

enum class FragmentTail { distinct, duplicate_stage, duplicate_pair };

std::vector<std::byte> fragment_tail(FragmentTail kind = FragmentTail::distinct) {
  test::ByteWriter tail;
  tail.u8(3);
  tail.u16(2);
  fixture::name(tail, "QF_Test");
  tail.u8(0);
  tail.u16(0);
  append_fragment(tail, 10, 0, 0, 0, "Fragment_10");
  append_fragment(tail, kind == FragmentTail::distinct ? 20 : 10, 0xFFFF,
                  kind == FragmentTail::duplicate_pair ? 0 : 5, 1, "Fragment_20");
  tail.u16(0);
  return tail.bytes;
}

std::vector<std::byte> scriptless_fragment_tail() {
  test::ByteWriter tail;
  tail.u8(3);
  tail.u16(1);
  fixture::name(tail, "");
  append_fragment(tail, 10, 0, 0, 0, "Fragment_10");
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

struct CQuestPlugin {
  std::filesystem::path root;
  radicle_plugin* handle{nullptr};

  explicit CQuestPlugin(const std::vector<std::byte>& bytes)
      : root(std::filesystem::temp_directory_path() /
             ("radicle_quest_fragments_" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(root);
    const auto input = root / "Vmad.esp";
    std::ofstream stream(input, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    EXPECT_EQ(radicle_plugin_read_file(input.string().c_str(), &handle), RADICLE_STATUS_OK);
  }
  ~CQuestPlugin() {
    radicle_plugin_destroy(handle);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};

}  // namespace

// Detects: the first quest fragment must preserve existing scripts and survive a save and reload.
TEST(VmadQuestFragments, FirstBindingPreservesLeadingScripts) {
  constexpr std::string_view script = "IntegratedAddons:Fragments:Quests:QF_SimpleRadioFailsafes";
  constexpr std::string_view function = "Fragment_Stage_0000_Item_00";
  auto plugin = read_plugin(fixture::plugin_bytes(fixture::payload(), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  ASSERT_EQ(bootstrap_quest_vmad_fragment(plugin, fixture::key(), 0, 0, script, function),
            VmadStructureMutationStatus::updated);
  const auto& vmad = fixture::subrecord(plugin).data;
  ASSERT_GE(vmad.size(), before.size());
  EXPECT_TRUE(std::equal(before.begin(), before.end(), vmad.begin()));
  auto expected = before;
  test::ByteWriter tail;
  tail.u8(3); tail.u16(1); fixture::name(tail, script); tail.u8(0); tail.u16(0);
  tail.u16(0); tail.u16(0); tail.i32(0); tail.u8(1);
  fixture::name(tail, script); fixture::name(tail, function);
  tail.u16(0);
  expected.insert(expected.end(), tail.bytes.begin(), tail.bytes.end());
  EXPECT_EQ(vmad, expected);
  const auto extras = extras_of(vmad);
  ASSERT_TRUE(extras.fully_consumed);
  EXPECT_EQ(extras.script_name, script);
  ASSERT_EQ(extras.fragments.size(), 1u);
  EXPECT_EQ(extras.fragments[0].stage, 0);
  EXPECT_EQ(extras.fragments[0].function, function);
  const auto reread = read_plugin(write_plugin(plugin), "Vmad.esp");
  EXPECT_EQ(fixture::subrecord(reread).data, vmad);
}

// Detects: bootstrapping must create a missing VMAD and refuse existing fragment data unchanged.
TEST(VmadQuestFragments, FirstBindingCreatesVmadAndRefusesExistingExtras) {
  auto bare = read_plugin(fixture::plugin_bytes_without_vmad("QUST"), "Vmad.esp");
  ASSERT_EQ(bootstrap_quest_vmad_fragment(bare, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(extras_of(fixture::subrecord(bare).data).fragments.size(), 1u);
  auto existing = read_plugin(fixture::plugin_bytes(quest_bytes(fragment_tail()), "QUST"), "Vmad.esp");
  const auto before = write_plugin(existing);
  EXPECT_EQ(bootstrap_quest_vmad_fragment(existing, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(write_plugin(existing), before);
}

// Detects: a first fragment must preserve existing alias bindings in the quest VMAD.
TEST(VmadQuestFragments, FirstBindingPreservesExistingQuestExtras) {
  for (bool with_alias : {false, true}) {
    test::ByteWriter tail;
    tail.u8(3); tail.u16(0); tail.u16(0); tail.u16(with_alias ? 1 : 0);
    if (with_alias) {
      tail.u16(0); tail.u16(5); tail.u32(0x800); tail.u16(6); tail.u16(2); tail.u16(0);
    }
    auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail.bytes), "QUST"), "Vmad.esp");
    const auto before = fixture::subrecord(plugin).data;
    EXPECT_EQ(bootstrap_quest_vmad_fragment(plugin, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
              VmadStructureMutationStatus::updated);
    const auto& vmad = fixture::subrecord(plugin).data;
    const auto extras = extras_of(vmad);
    ASSERT_TRUE(extras.fully_consumed);
    ASSERT_EQ(extras.fragments.size(), 1u);
    EXPECT_EQ(extras.aliases.size(), with_alias ? 1u : 0u);
    const auto alias_bytes = with_alias ? 16u : 2u;
    EXPECT_TRUE(std::equal(before.end() - alias_bytes, before.end(), vmad.end() - alias_bytes));
  }
}

// Detects: invalid names and damaged or duplicate VMADs must not alter the quest.
TEST(VmadQuestFragments, FirstBindingRejectsInvalidInputsWithoutChanges) {
  auto bad_tail = quest_bytes(fragment_tail());
  bad_tail.pop_back();
  for (const auto& bytes : {fixture::plugin_bytes(bad_tail, "QUST"),
                            fixture::plugin_bytes(fixture::payload(), "QUST", true)}) {
    auto plugin = read_plugin(bytes, "Vmad.esp");
    const auto before = write_plugin(plugin);
    EXPECT_THROW(bootstrap_quest_vmad_fragment(plugin, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
                 schema::ScalarError);
    EXPECT_EQ(write_plugin(plugin), before);
  }
  auto plugin = read_plugin(fixture::plugin_bytes(fixture::payload(), "QUST"), "Vmad.esp");
  const auto before = write_plugin(plugin);
  EXPECT_THROW(bootstrap_quest_vmad_fragment(plugin, fixture::key(), 10, 0, std::string(65536, 'x'), "F"),
               schema::ScalarError);
  EXPECT_THROW(bootstrap_quest_vmad_fragment(plugin, fixture::key(), 10, 0, "QF_Test", std::string(65536, 'x')),
               schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
}

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
  EXPECT_EQ(slice(first, 0, prefix), slice(before, 0, prefix));
  EXPECT_EQ(slice(first, prefix + 2, base + initial.fragments[0].offset),
            slice(before, prefix + 2, base + initial.fragments[0].offset));
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
  EXPECT_EQ(slice(emptied, 0, prefix), slice(before, 0, prefix));
  EXPECT_EQ(slice(emptied, prefix + 2, base + initial.fragments[0].offset),
            slice(before, prefix + 2, base + initial.fragments[0].offset));
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
      read_plugin(fixture::plugin_bytes(quest_bytes(fragment_tail(FragmentTail::duplicate_stage)),
                                        "QUST"),
                  "Vmad.esp");
  const auto twice_before = write_plugin(twice);
  ASSERT_EQ(extras_of(fixture::subrecord(twice).data).fragments.size(), 2u);
  EXPECT_THROW(set_quest_vmad_fragment(twice, fixture::key(), 10, "QF_Test", "Fragment_Ten"),
               schema::ScalarError);
  EXPECT_THROW(remove_quest_vmad_fragment(twice, fixture::key(), 10), schema::ScalarError);
  EXPECT_EQ(write_plugin(twice), twice_before);
}

TEST(VmadQuestFragments, AddedBindingLandsInStageOrderAndRemovalRestoresThePayload) {
  const auto tail = fragment_tail();
  auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto initial = extras_of(before);
  ASSERT_EQ(initial.fragments.size(), 2u);
  const auto base = extras_base(before);
  const auto count_at = base + initial.count_offset;

  EXPECT_EQ(add_quest_vmad_fragment(plugin, fixture::key(), 15, 7, "QF_Test", "Fragment_15"),
            VmadStructureMutationStatus::updated);
  const auto middle = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(middle, sig_qust).complete());
  const auto three = extras_of(middle);
  ASSERT_EQ(three.fragments.size(), 3u);
  expect_fragment(three.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(three.fragments[1], 15, 0, 7, 1, "Fragment_15");
  expect_fragment(three.fragments[2], 20, 0xFFFF, 5, 1, "Fragment_20");
  EXPECT_EQ(three.fragments[0].offset, initial.fragments[0].offset);
  EXPECT_EQ(three.fragments[0].end, initial.fragments[0].end);
  EXPECT_EQ(three.fragments[1].offset, initial.fragments[1].offset);
  test::ByteWriter created;
  append_fragment(created, 15, 0, 7, 1, "Fragment_15");
  EXPECT_EQ(slice(middle, base + three.fragments[1].offset, base + three.fragments[1].end),
            created.bytes);
  EXPECT_EQ(middle[count_at], std::byte{3});
  EXPECT_EQ(middle[count_at + 1], std::byte{0});
  EXPECT_EQ(slice(middle, 0, count_at), slice(before, 0, count_at));
  EXPECT_EQ(slice(middle, count_at + 2, base + three.fragments[1].offset),
            slice(before, count_at + 2, base + initial.fragments[1].offset));
  EXPECT_EQ(slice(middle, base + three.fragments[2].offset, middle.size()),
            slice(before, base + initial.fragments[1].offset, before.size()));

  EXPECT_EQ(add_quest_vmad_fragment(plugin, fixture::key(), 15, 7, "QF_Test", "Fragment_15"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(fixture::subrecord(plugin).data, middle);

  EXPECT_EQ(add_quest_vmad_fragment(plugin, fixture::key(), 5, 0, "QF_Test", "Fragment_05"),
            VmadStructureMutationStatus::updated);
  const auto fronted = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  const auto four = extras_of(fronted);
  ASSERT_EQ(four.fragments.size(), 4u);
  expect_fragment(four.fragments[0], 5, 0, 0, 1, "Fragment_05");
  expect_fragment(four.fragments[1], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(four.fragments[2], 15, 0, 7, 1, "Fragment_15");
  expect_fragment(four.fragments[3], 20, 0xFFFF, 5, 1, "Fragment_20");
  EXPECT_EQ(four.fragments[0].offset, three.entries_begin);
  EXPECT_EQ(four.fragments[1].offset,
            three.entries_begin + (four.fragments[0].end - four.fragments[0].offset));
  EXPECT_EQ(fronted[count_at], std::byte{4});
  EXPECT_EQ(slice(fronted, base + four.fragments[1].offset, fronted.size()),
            slice(middle, base + three.entries_begin, middle.size()));

  EXPECT_EQ(add_quest_vmad_fragment(plugin, fixture::key(), 30, 9, "QF_Test", "Fragment_30"),
            VmadStructureMutationStatus::updated);
  const auto appended = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  const auto five = extras_of(appended);
  ASSERT_EQ(five.fragments.size(), 5u);
  expect_fragment(five.fragments[4], 30, 0, 9, 1, "Fragment_30");
  EXPECT_EQ(appended[count_at], std::byte{5});
  EXPECT_EQ(five.fragments[4].offset, five.fragments[3].end);
  EXPECT_EQ(five.fragments[4].end, appended.size() - base - 2);
  EXPECT_EQ(slice(appended, 0, count_at), slice(fronted, 0, count_at));
  EXPECT_EQ(slice(appended, count_at + 2, base + five.fragments[4].offset),
            slice(fronted, count_at + 2, base + four.fragments[3].end));

  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 5),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 15),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 30),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(fixture::subrecord(plugin).data, before);

  auto repeated =
      read_plugin(fixture::plugin_bytes(quest_bytes(fragment_tail(FragmentTail::duplicate_stage)),
                                        "QUST"),
                  "Vmad.esp");
  const auto repeated_base = extras_base(fixture::subrecord(repeated).data);
  EXPECT_EQ(add_quest_vmad_fragment(repeated, fixture::key(), 10, 9, "QF_Test", "Fragment_10b"),
            VmadStructureMutationStatus::updated);
  const auto longer = fixture::subrecord(read_plugin(write_plugin(repeated), "Vmad.esp")).data;
  const auto three_of_stage = extras_of(longer);
  ASSERT_EQ(three_of_stage.fragments.size(), 3u);
  expect_fragment(three_of_stage.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(three_of_stage.fragments[1], 10, 0xFFFF, 5, 1, "Fragment_20");
  expect_fragment(three_of_stage.fragments[2], 10, 0, 9, 1, "Fragment_10b");
  EXPECT_EQ(longer[repeated_base + three_of_stage.count_offset], std::byte{3});

  EXPECT_EQ(add_quest_vmad_fragment(repeated, fixture::key(), 10, 9, "QF_Test", "Fragment_10b"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(add_quest_vmad_fragment(repeated, fixture::key(), 10, 3, "QF_Test", "Fragment_10a"),
            VmadStructureMutationStatus::updated);
  const auto between = fixture::subrecord(read_plugin(write_plugin(repeated), "Vmad.esp")).data;
  const auto ordered = extras_of(between);
  ASSERT_EQ(ordered.fragments.size(), 4u);
  expect_fragment(ordered.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(ordered.fragments[1], 10, 0, 3, 1, "Fragment_10a");
  expect_fragment(ordered.fragments[2], 10, 0xFFFF, 5, 1, "Fragment_20");
  expect_fragment(ordered.fragments[3], 10, 0, 9, 1, "Fragment_10b");
  EXPECT_THROW(remove_quest_vmad_fragment(repeated, fixture::key(), 10), schema::ScalarError);
  EXPECT_EQ(fixture::subrecord(repeated).data, between);
}

TEST(VmadQuestFragments, HighStageIndexLandsAfterSmallerIndexesAtTheSameStage) {
  const auto tail = fragment_tail(FragmentTail::duplicate_stage);
  auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto initial = extras_of(before);
  ASSERT_EQ(initial.fragments.size(), 2u);
  ASSERT_EQ(initial.fragments[0].stage_index, 0);
  ASSERT_EQ(initial.fragments[1].stage_index, 5);
  const auto base = extras_base(before);
  const auto count_at = base + initial.count_offset;
  constexpr auto high = static_cast<std::int32_t>(0x80000000u);

  EXPECT_EQ(add_quest_vmad_fragment(plugin, fixture::key(), 10, high, "QF_Test", "Fragment_High"),
            VmadStructureMutationStatus::updated);
  const auto appended = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  const auto three = extras_of(appended);
  ASSERT_EQ(three.fragments.size(), 3u);
  expect_fragment(three.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(three.fragments[1], 10, 0xFFFF, 5, 1, "Fragment_20");
  expect_fragment(three.fragments[2], 10, 0, high, 1, "Fragment_High");
  EXPECT_EQ(three.fragments[2].offset, three.fragments[1].end);
  EXPECT_EQ(appended[count_at], std::byte{3});
  EXPECT_EQ(appended[count_at + 1], std::byte{0});
  EXPECT_EQ(slice(appended, 0, count_at), slice(before, 0, count_at));
  EXPECT_EQ(slice(appended, count_at + 2, base + three.fragments[2].offset),
            slice(before, count_at + 2, base + initial.fragments[1].end));

  EXPECT_EQ(add_quest_vmad_fragment(plugin, fixture::key(), 10, high, "QF_Test", "Fragment_High"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(fixture::subrecord(plugin).data, appended);
}

TEST(VmadQuestFragments, AddedBindingRefusesBoundStagesAndMissingFragmentSections) {
  auto refr = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto refr_before = write_plugin(refr);
  EXPECT_EQ(add_quest_vmad_fragment(refr, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(write_plugin(refr), refr_before);

  auto bare = read_plugin(fixture::plugin_bytes(fixture::payload(), "QUST"), "Vmad.esp");
  const auto bare_before = write_plugin(bare);
  EXPECT_EQ(add_quest_vmad_fragment(bare, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(write_plugin(bare), bare_before);

  auto scriptless =
      read_plugin(fixture::plugin_bytes(quest_bytes(scriptless_fragment_tail()), "QUST"),
                  "Vmad.esp");
  const auto scriptless_before = write_plugin(scriptless);
  ASSERT_EQ(extras_of(fixture::subrecord(scriptless).data).fragments.size(), 1u);
  EXPECT_EQ(add_quest_vmad_fragment(scriptless, fixture::key(), 20, 0, "QF_Test", "Fragment_20"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(write_plugin(scriptless), scriptless_before);

  auto quest =
      read_plugin(fixture::plugin_bytes(quest_bytes(fragment_tail()), "QUST"), "Vmad.esp");
  const auto quest_before = write_plugin(quest);
  EXPECT_EQ(add_quest_vmad_fragment(quest, fixture::key(), 10, 0, "QF_Other", "Fragment_Ten"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(add_quest_vmad_fragment(quest, fixture::key(0x999), 15, 0, "QF_Test", "Fragment_15"),
            VmadStructureMutationStatus::record_missing);
  EXPECT_THROW(add_quest_vmad_fragment(quest, fixture::key(), 15, 0, "", "Fragment_15"),
               schema::ScalarError);
  EXPECT_THROW(add_quest_vmad_fragment(quest, fixture::key(), 15, 0, "QF_Test", ""),
               schema::ScalarError);
  EXPECT_EQ(write_plugin(quest), quest_before);

  auto truncated = quest_bytes(fragment_tail());
  truncated.pop_back();
  auto damaged = read_plugin(fixture::plugin_bytes(truncated, "QUST"), "Vmad.esp");
  const auto damaged_before = write_plugin(damaged);
  EXPECT_THROW(add_quest_vmad_fragment(damaged, fixture::key(), 15, 0, "QF_Test", "Fragment_15"),
               schema::ScalarError);
  EXPECT_EQ(write_plugin(damaged), damaged_before);
}

TEST(VmadQuestFragments, StageBoundTwiceIsAddressedByStageIndex) {
  const auto tail = fragment_tail(FragmentTail::duplicate_stage);
  auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto initial = extras_of(before);
  ASSERT_EQ(initial.fragments.size(), 2u);
  expect_fragment(initial.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(initial.fragments[1], 10, 0xFFFF, 5, 1, "Fragment_20");
  const auto base = extras_base(before);

  const auto before_bytes = write_plugin(plugin);
  EXPECT_THROW(set_quest_vmad_fragment(plugin, fixture::key(), 10, "QF_Other", "Fragment_Ten"),
               schema::ScalarError);
  try {
    (void)remove_quest_vmad_fragment(plugin, fixture::key(), 10);
    ADD_FAILURE() << "a stage bound twice was not refused by the stage-only form";
  } catch (const schema::ScalarError& error) {
    EXPECT_NE(std::string(error.what()).find("stage index"), std::string::npos);
  }
  EXPECT_EQ(write_plugin(plugin), before_bytes);

  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::unchanged);
  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 10, 5, "QF_Other",
                                    "Fragment_Stage_0010"),
            VmadStructureMutationStatus::updated);
  const auto saved = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(saved, sig_qust).complete());
  const auto edited = extras_of(saved);
  ASSERT_EQ(edited.fragments.size(), 2u);
  expect_fragment(edited.fragments[0], 10, 0, 0, 0, "Fragment_10");
  EXPECT_EQ(edited.fragments[0].offset, initial.fragments[0].offset);
  EXPECT_EQ(edited.fragments[0].end, initial.fragments[0].end);
  EXPECT_EQ(edited.fragments[1].stage, 10u);
  EXPECT_EQ(edited.fragments[1].unknown, 0xFFFF);
  EXPECT_EQ(edited.fragments[1].stage_index, 5);
  EXPECT_EQ(edited.fragments[1].unknown_byte, 1);
  EXPECT_EQ(edited.fragments[1].script, "QF_Other");
  EXPECT_EQ(edited.fragments[1].function, "Fragment_Stage_0010");
  const auto saved_base = extras_base(saved);
  EXPECT_EQ(slice(saved, 0, saved_base + edited.fragments[1].offset),
            slice(before, 0, base + initial.fragments[1].offset));
  EXPECT_EQ(slice(saved, saved_base + edited.fragments[1].end, saved.size()),
            slice(before, base + initial.fragments[1].end, before.size()));
  EXPECT_EQ(fixture::subrecord(plugin).data, saved);

  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 10, 7),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(fixture::subrecord(plugin).data, saved);
  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 10, 5),
            VmadStructureMutationStatus::updated);
  const auto single = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(single, sig_qust).complete());
  const auto one = extras_of(single);
  ASSERT_EQ(one.fragments.size(), 1u);
  expect_fragment(one.fragments[0], 10, 0, 0, 0, "Fragment_10");
  EXPECT_EQ(single[base + one.count_offset], std::byte{1});
  EXPECT_EQ(single.size(),
            before.size() - (initial.fragments[1].end - initial.fragments[1].offset));
  EXPECT_EQ(slice(single, base + one.fragments[0].offset, base + one.fragments[0].end),
            slice(before, base + initial.fragments[0].offset,
                  base + initial.fragments[1].offset));
  EXPECT_EQ(slice(single, single.size() - 2, single.size()),
            slice(before, before.size() - 2, before.size()));

  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 10, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::unchanged);
  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 10, "QF_Solo", "Fragment_Solo"),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 10),
            VmadStructureMutationStatus::updated);
  const auto emptied = fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
  EXPECT_TRUE(vmad_coverage(emptied, sig_qust).complete());
  const auto none = extras_of(emptied);
  EXPECT_TRUE(none.fragments.empty());
  EXPECT_EQ(emptied[base + none.count_offset], std::byte{0});
  EXPECT_EQ(remove_quest_vmad_fragment(plugin, fixture::key(), 10),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(set_quest_vmad_fragment(plugin, fixture::key(), 10, 0, "QF_Test", "Fragment_10"),
            VmadStructureMutationStatus::binding_missing);
}

TEST(VmadQuestFragments, DuplicateStageIndexIsRefused) {
  const auto tail = fragment_tail(FragmentTail::duplicate_pair);
  auto plugin = read_plugin(fixture::plugin_bytes(quest_bytes(tail), "QUST"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto initial = extras_of(before);
  ASSERT_EQ(initial.fragments.size(), 2u);
  expect_fragment(initial.fragments[0], 10, 0, 0, 0, "Fragment_10");
  expect_fragment(initial.fragments[1], 10, 0xFFFF, 0, 1, "Fragment_20");

  const auto before_bytes = write_plugin(plugin);
  EXPECT_THROW(set_quest_vmad_fragment(plugin, fixture::key(), 10, 0, "QF_Other", "Fragment_Ten"),
               schema::ScalarError);
  try {
    (void)remove_quest_vmad_fragment(plugin, fixture::key(), 10, 0);
    ADD_FAILURE() << "a stage index bound twice was not refused by the pair form";
  } catch (const schema::ScalarError& error) {
    EXPECT_NE(std::string(error.what()).find("stage index twice"), std::string::npos);
  }
  EXPECT_EQ(write_plugin(plugin), before_bytes);
}

TEST(VmadQuestFragments, CApiPairFormEditsOneOfTwoBindingsAndRefusesBadInput) {
  CQuestPlugin plugin(
      fixture::plugin_bytes(quest_bytes(fragment_tail(FragmentTail::duplicate_stage)), "QUST"));
  ASSERT_NE(plugin.handle, nullptr);
  const radicle_form_key key{"Vmad.esp", 0x800};
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(nullptr, key, 10, 5, "QF_Other", "Fragment_Ten"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, key, 10, 5, nullptr, "Fragment_Ten"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, key, 10, 5, "QF_Other", "\xFF"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, key, 10, 7, "QF_Other", "Fragment_Ten"),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_quest_vmad_fragment(plugin.handle, key, 10, 7),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, {"Vmad.esp", 0x801}, 10, 5,
                                                   "QF_Other", "Fragment_Ten"),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, key, 10, 0, "QF_Test", "Fragment_10"),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, key, 10, 5, "QF_Other",
                                                   "Fragment_Stage_0010"),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_quest_vmad_fragment(plugin.handle, key, 10, 5), RADICLE_STATUS_OK);
  const auto output = plugin.root / "Saved.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin.handle, output.string().c_str()), RADICLE_STATUS_OK);
  const auto remaining = extras_of(fixture::subrecord(fo4::read_plugin_file(output), "Saved.esp").data);
  ASSERT_EQ(remaining.fragments.size(), 1u);
  expect_fragment(remaining.fragments[0], 10, 0, 0, 0, "Fragment_10");
}

TEST(VmadQuestFragments, CApiPairFormRefusesDuplicatePairAndLeavesBytesUnchanged) {
  const auto bytes =
      fixture::plugin_bytes(quest_bytes(fragment_tail(FragmentTail::duplicate_pair)), "QUST");
  CQuestPlugin plugin(bytes);
  ASSERT_NE(plugin.handle, nullptr);
  const radicle_form_key key{"Vmad.esp", 0x800};
  EXPECT_EQ(radicle_plugin_set_quest_vmad_fragment(plugin.handle, key, 10, 0, "QF_Other", "Fragment_Ten"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("stage index twice"), std::string::npos);
  EXPECT_EQ(radicle_plugin_remove_quest_vmad_fragment(plugin.handle, key, 10, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("stage index twice"), std::string::npos);
  const auto output = plugin.root / "Refused.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin.handle, output.string().c_str()), RADICLE_STATUS_OK);
  EXPECT_EQ(fixture::subrecord(fo4::read_plugin_file(output), "Refused.esp").data,
            fixture::subrecord(read_plugin(bytes, "Vmad.esp")).data);
}

// Detects: localized record creation must write the supplied required string ID and refuse missing IDs.
TEST(QuestVmad, LocalizedCreationUsesSuppliedStringId) {
  auto plugin = fo4::read_plugin(fo4::test::make_tes4_plugin(0x80, 1.0f, {}), "Localized.esp");
  EXPECT_EQ(fo4::create_record(plugin, fo4::Signature::from_string("BOOK"), "NoDescription").status,
            fo4::RecordCreateStatus::no_default);
  const std::vector<fo4::LocalizedRowId> ids{{fo4::Signature::from_string("DESC"), 77}};
  const auto created = fo4::create_record_with_localized_rows(plugin, fo4::Signature::from_string("BOOK"), "HasDescription", ids);
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key); ASSERT_NE(record, nullptr);
  const auto found = std::find_if(record->subrecords.begin(), record->subrecords.end(), [](const auto& sub) { return sub.signature == fo4::Signature::from_string("DESC"); });
  ASSERT_NE(found, record->subrecords.end());
  EXPECT_EQ(found->data, (std::vector<std::byte>{std::byte{77}, std::byte{0}, std::byte{0}, std::byte{0}}));
}
