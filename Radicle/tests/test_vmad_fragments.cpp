#include "fo4/plugin/vmad_writer.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace fo4;
using fo4::test::ByteWriter;
namespace fixture = fo4::test::vmad;

struct Entry {
  std::uint8_t unknown{0};
  std::string function;
  std::string script{"PF_Test"};
};

struct IndexedEntry {
  std::uint32_t key{0};
  std::string function;
  std::string script{"PF_Test"};
  std::uint8_t unknown{0x2A};
};

struct PhaseEntry {
  std::uint8_t flag{1};
  std::uint32_t index{0};
  std::uint8_t unknown{0xFF};
  std::string function;
  std::string script{"PF_Test"};
};

void append_fragment_script(ByteWriter& out, std::string_view name, std::uint32_t properties) {
  fixture::name(out, name);
  out.u8(0);
  out.u16(static_cast<std::uint16_t>(properties));
  for (std::uint32_t i = 0; i < properties; ++i) {
    fixture::name(out, "StageCount");
    out.u8(3);
    out.u8(1);
    out.u32(i + 5);
  }
}

std::vector<std::byte> scene_tail(std::uint8_t flags, const std::vector<Entry>& entries,
                                  const std::vector<PhaseEntry>& phases) {
  ByteWriter tail;
  tail.u8(3);
  tail.u8(flags);
  append_fragment_script(tail, "PF_Test", 1);
  for (const auto& entry : entries) {
    tail.u8(entry.unknown);
    fixture::name(tail, entry.script);
    fixture::name(tail, entry.function);
  }
  tail.u16(static_cast<std::uint16_t>(phases.size()));
  for (const auto& phase : phases) {
    tail.u8(phase.flag);
    tail.u32(phase.index);
    tail.u8(phase.unknown);
    fixture::name(tail, phase.script);
    fixture::name(tail, phase.function);
  }
  return tail.bytes;
}

std::vector<std::byte> flagged_tail(std::uint8_t flags, const std::vector<Entry>& entries,
                                    bool scenes = false) {
  ByteWriter tail;
  tail.u8(3);
  tail.u8(flags);
  append_fragment_script(tail, "PF_Test", 1);
  for (const auto& entry : entries) {
    tail.u8(entry.unknown);
    fixture::name(tail, entry.script);
    fixture::name(tail, entry.function);
  }
  if (scenes) {
    tail.u16(1);
    tail.u8(1);
    tail.u32(2);
    tail.u8(0xFF);
    fixture::name(tail, "PF_Test");
    fixture::name(tail, "Phase_2");
  }
  return tail.bytes;
}

std::vector<std::byte> indexed_tail(const std::vector<IndexedEntry>& entries) {
  ByteWriter tail;
  tail.u8(3);
  append_fragment_script(tail, "PF_Test", 1);
  tail.u16(static_cast<std::uint16_t>(entries.size()));
  for (const auto& entry : entries) {
    tail.u32(entry.key);
    tail.u8(entry.unknown);
    fixture::name(tail, entry.script);
    fixture::name(tail, entry.function);
  }
  return tail.bytes;
}

std::vector<std::byte> vmad_bytes(const std::vector<std::byte>& tail) {
  auto data = fixture::payload();
  data.insert(data.end(), tail.begin(), tail.end());
  return data;
}

std::vector<std::byte> saved_vmad(Plugin& plugin) {
  return fixture::subrecord(read_plugin(write_plugin(plugin), "Vmad.esp")).data;
}

SceneVmadExtras read_scene_extras(const Plugin& plugin) {
  const auto data = fixture::subrecord(plugin).data;
  const auto scripts = read_vmad_scripts(data);
  return read_scene_vmad_extras(
      std::span<const std::byte>(data).subspan(data.size() - scripts.trailing_bytes),
      scripts.object_format);
}

}  // namespace

TEST(VmadFragments, FlaggedRecordsRenameOneBindingAndKeepEveryOtherByte) {
  for (const std::string_view signature : {"PACK", "INFO", "SCEN"}) {
    SCOPED_TRACE(signature);
    const bool scenes = signature == "SCEN";
    const auto tail = flagged_tail(0x03, {{7, "Fragment_0"}, {8, "Fragment_1"}}, scenes);
    auto plugin = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), signature), "Vmad.esp");
    const auto coverage = vmad_coverage(fixture::subrecord(plugin).data,
                                        Signature::from_string(signature));
    ASSERT_TRUE(coverage.complete());
    ASSERT_EQ(coverage.fragment_count, scenes ? 3u : 2u);

    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 1, "PF_Other", "Fragment_Renamed"),
              VmadStructureMutationStatus::updated);
    const auto saved = saved_vmad(plugin);
    EXPECT_TRUE(vmad_coverage(saved, Signature::from_string(signature)).complete());
    EXPECT_EQ(saved,
              vmad_bytes(flagged_tail(
                  0x03, {{7, "Fragment_0"}, {8, "Fragment_Renamed", "PF_Other"}}, scenes)));

    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 1, "PF_Other", "Fragment_Renamed"),
              VmadStructureMutationStatus::unchanged);
    EXPECT_EQ(fixture::subrecord(plugin).data, saved);
    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 0, "PF_Test", "Fragment_0"),
              VmadStructureMutationStatus::unchanged);
    EXPECT_EQ(fixture::subrecord(plugin).data, saved);
  }
}

TEST(VmadFragments, FlaggedRemovalClearsItsFlagAndKeepsTheOtherEntryByteForByte) {
  const auto tail = flagged_tail(0x03, {{7, "Fragment_0"}, {8, "Fragment_1"}});
  auto plugin = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), "PACK"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;

  ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 1), VmadStructureMutationStatus::updated);
  const auto first = saved_vmad(plugin);
  const auto coverage = vmad_coverage(first, sig_pack);
  EXPECT_TRUE(coverage.complete());
  EXPECT_EQ(coverage.fragment_count, 1u);
  EXPECT_EQ(first, vmad_bytes(flagged_tail(0x01, {{7, "Fragment_0"}})));
  EXPECT_LT(first.size(), before.size());

  ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 0), VmadStructureMutationStatus::updated);
  const auto emptied = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(emptied, sig_pack).complete());
  EXPECT_EQ(vmad_coverage(emptied, sig_pack).fragment_count, 0u);
  EXPECT_EQ(emptied, vmad_bytes(flagged_tail(0x00, {})));

  EXPECT_EQ(remove_vmad_fragment(plugin, fixture::key(), 0),
            VmadStructureMutationStatus::binding_missing);
}

TEST(VmadFragments, IndexedRecordsRenameAndRemoveByFragmentIndex) {
  for (const std::string_view signature : {"PERK", "TERM"}) {
    SCOPED_TRACE(signature);
    const auto tail = indexed_tail({{100, "Fragment_100"}, {200, "Fragment_200"}});
    auto plugin = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), signature), "Vmad.esp");
    const auto coverage = vmad_coverage(fixture::subrecord(plugin).data,
                                        Signature::from_string(signature));
    ASSERT_TRUE(coverage.complete());
    ASSERT_EQ(coverage.fragment_count, 2u);

    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 200, "PF_Other", "Fragment_Renamed"),
              VmadStructureMutationStatus::updated);
    auto saved = saved_vmad(plugin);
    EXPECT_TRUE(vmad_coverage(saved, Signature::from_string(signature)).complete());
    EXPECT_EQ(saved, vmad_bytes(indexed_tail(
                        {{100, "Fragment_100"}, {200, "Fragment_Renamed", "PF_Other"}})));

    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 200, "PF_Other", "Fragment_Renamed"),
              VmadStructureMutationStatus::unchanged);
    EXPECT_EQ(fixture::subrecord(plugin).data, saved);

    ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 100),
              VmadStructureMutationStatus::updated);
    saved = saved_vmad(plugin);
    const auto trimmed = vmad_coverage(saved, Signature::from_string(signature));
    EXPECT_TRUE(trimmed.complete());
    EXPECT_EQ(trimmed.fragment_count, 1u);
    EXPECT_EQ(saved, vmad_bytes(indexed_tail({{200, "Fragment_Renamed", "PF_Other"}})));

    EXPECT_EQ(remove_vmad_fragment(plugin, fixture::key(), 100),
              VmadStructureMutationStatus::binding_missing);
  }
}

TEST(VmadFragments, ScenePhaseFragmentsSurviveMainFragmentEdits) {
  const auto tail = flagged_tail(0x03, {{7, "Fragment_0"}, {8, "Fragment_1"}}, true);
  auto plugin = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), "SCEN"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(fixture::subrecord(plugin).data, sig_scen).complete());

  ASSERT_EQ(set_vmad_fragment(plugin, fixture::key(), 1, "PF_Other", "Fragment_Renamed"),
            VmadStructureMutationStatus::updated);
  const auto renamed = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(renamed, sig_scen).complete());
  EXPECT_EQ(renamed,
            vmad_bytes(flagged_tail(
                0x03, {{7, "Fragment_0"}, {8, "Fragment_Renamed", "PF_Other"}}, true)));

  ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 0), VmadStructureMutationStatus::updated);
  const auto trimmed = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(trimmed, sig_scen).complete());
  EXPECT_EQ(trimmed, vmad_bytes(flagged_tail(0x02, {{8, "Fragment_Renamed", "PF_Other"}}, true)));
  EXPECT_LT(trimmed.size(), renamed.size());

  const auto one = flagged_tail(0x01, {{7, "Fragment_0"}}, true);
  auto single = read_plugin(fixture::plugin_bytes(vmad_bytes(one), "SCEN"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(fixture::subrecord(single).data, sig_scen).complete());
  ASSERT_EQ(add_vmad_fragment(single, fixture::key(), 1, "PF_Test", "Fragment_1"),
            VmadStructureMutationStatus::updated);
  const auto appended = saved_vmad(single);
  EXPECT_TRUE(vmad_coverage(appended, sig_scen).complete());
  EXPECT_EQ(vmad_coverage(appended, sig_scen).fragment_count, 3u);
  EXPECT_EQ(appended,
            vmad_bytes(flagged_tail(0x03, {{7, "Fragment_0"}, {0, "Fragment_1"}}, true)));
}

TEST(VmadFragments, ScenePhaseFragmentsDecodeAndRoundTripByPhaseIndexAndFlag) {
  const std::vector<Entry> entries{{7, "Fragment_0"}, {8, "Fragment_1"}};
  const std::vector<PhaseEntry> phases{{1, 2, 0, "Phase_2"}, {2, 5, 0xFF, "Phase_5"}};
  auto plugin = read_plugin(
      fixture::plugin_bytes(vmad_bytes(scene_tail(0x03, entries, phases)), "SCEN"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  ASSERT_TRUE(vmad_coverage(before, sig_scen).complete());
  ASSERT_EQ(vmad_coverage(before, sig_scen).fragment_count, 4u);

  const auto decoded = read_scene_extras(plugin);
  EXPECT_TRUE(decoded.fully_consumed);
  EXPECT_FALSE(decoded.unsupported);
  EXPECT_TRUE(decoded.unsupported_reason.empty());
  EXPECT_EQ(decoded.flags, 0x03);
  ASSERT_EQ(decoded.phases.size(), 2u);
  EXPECT_EQ(decoded.phases[0].flag, 1);
  EXPECT_EQ(decoded.phases[0].index, 2u);
  EXPECT_EQ(decoded.phases[0].unknown, 0);
  EXPECT_EQ(decoded.phases[0].script, "PF_Test");
  EXPECT_EQ(decoded.phases[0].function, "Phase_2");
  EXPECT_EQ(decoded.phases[0].end - decoded.phases[0].offset, 24u);
  EXPECT_EQ(decoded.phases[1].flag, 2);
  EXPECT_EQ(decoded.phases[1].index, 5u);
  EXPECT_EQ(decoded.phases[1].unknown, 0xFF);
  EXPECT_EQ(decoded.phases[1].function, "Phase_5");

  EXPECT_EQ(set_scene_vmad_phase_fragment(plugin, fixture::key(), 5, 1, "PF_Other", "Phase_X"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(set_scene_vmad_phase_fragment(plugin, fixture::key(), 9, 2, "PF_Other", "Phase_X"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(fixture::subrecord(plugin).data, before);

  ASSERT_EQ(set_scene_vmad_phase_fragment(plugin, fixture::key(), 5, 2, "PF_Other", "Phase_5b"),
            VmadStructureMutationStatus::updated);
  const auto renamed = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(renamed, sig_scen).complete());
  EXPECT_EQ(renamed,
            vmad_bytes(scene_tail(0x03, entries, {{1, 2, 0, "Phase_2"},
                                                 {2, 5, 0xFF, "Phase_5b", "PF_Other"}})));
  EXPECT_EQ(set_scene_vmad_phase_fragment(plugin, fixture::key(), 5, 2, "PF_Other", "Phase_5b"),
            VmadStructureMutationStatus::unchanged);
  EXPECT_EQ(fixture::subrecord(plugin).data, renamed);

  ASSERT_EQ(set_scene_vmad_phase_fragment(plugin, fixture::key(), 5, 2, "PF_Test", "Phase_5"),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(saved_vmad(plugin), before);

  ASSERT_EQ(remove_scene_vmad_phase_fragment(plugin, fixture::key(), 2, 1),
            VmadStructureMutationStatus::updated);
  const auto trimmed = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(trimmed, sig_scen).complete());
  EXPECT_EQ(vmad_coverage(trimmed, sig_scen).fragment_count, 3u);
  EXPECT_EQ(trimmed, vmad_bytes(scene_tail(0x03, entries, {{2, 5, 0xFF, "Phase_5"}})));
  EXPECT_EQ(remove_scene_vmad_phase_fragment(plugin, fixture::key(), 2, 1),
            VmadStructureMutationStatus::binding_missing);

  ASSERT_EQ(add_scene_vmad_phase_fragment(plugin, fixture::key(), 2, 1, "PF_Test", "Phase_2"),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(saved_vmad(plugin), before);
}

TEST(VmadFragments, SceneUnsupportedFragmentPropertyReportsInsteadOfThrowing) {
  ByteWriter tail;
  tail.u8(3);
  tail.u8(0x00);
  fixture::name(tail, "PF_Test");
  tail.u8(0);
  tail.u16(1);
  fixture::name(tail, "Unsupported");
  tail.u8(6);
  tail.u8(0);
  auto plugin = read_plugin(fixture::plugin_bytes(vmad_bytes(tail.bytes), "SCEN"), "Vmad.esp");
  const auto extras = read_scene_extras(plugin);
  EXPECT_TRUE(extras.unsupported);
  EXPECT_FALSE(extras.unsupported_reason.empty());
  EXPECT_FALSE(extras.fully_consumed);
}

TEST(VmadFragments, CreatedScenePhaseFragmentLandsInPhaseOrderAndRefusalsHold) {
  const std::vector<PhaseEntry> phases{{1, 2, 0xFF, "Phase_2"}, {2, 5, 0xFF, "Phase_5"}};
  auto plugin = read_plugin(
      fixture::plugin_bytes(vmad_bytes(scene_tail(0x00, {}, phases)), "SCEN"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(fixture::subrecord(plugin).data, sig_scen).complete());

  ASSERT_EQ(add_scene_vmad_phase_fragment(plugin, fixture::key(), 3, 2, "PF_Test", "Phase_3"),
            VmadStructureMutationStatus::updated);
  const auto inserted = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(inserted, sig_scen).complete());
  EXPECT_EQ(vmad_coverage(inserted, sig_scen).fragment_count, 3u);
  EXPECT_EQ(inserted, vmad_bytes(scene_tail(0x00, {},
                                            {{1, 2, 0xFF, "Phase_2"},
                                             {2, 3, 0, "Phase_3", "PF_Test"},
                                             {2, 5, 0xFF, "Phase_5"}})));

  EXPECT_EQ(add_scene_vmad_phase_fragment(plugin, fixture::key(), 5, 2, "PF_New", "Phase_5b"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(add_scene_vmad_phase_fragment(plugin, fixture::key(), 4, 0, "PF_New", "Phase_4"),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(add_scene_vmad_phase_fragment(plugin, fixture::key(), 4, 4, "PF_New", "Phase_4"),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(add_scene_vmad_phase_fragment(plugin, fixture::key(), 4, 0x80, "PF_New", "Phase_4"),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_THROW(add_scene_vmad_phase_fragment(plugin, fixture::key(), 4, 1, "", "Phase_4"),
               schema::ScalarError);
  EXPECT_THROW(add_scene_vmad_phase_fragment(plugin, fixture::key(), 4, 1, "PF_New", ""),
               schema::ScalarError);
  EXPECT_THROW(set_scene_vmad_phase_fragment(plugin, fixture::key(), 5, 2, "", "Phase_5"),
               schema::ScalarError);
  EXPECT_EQ(fixture::subrecord(plugin).data, inserted);

  ASSERT_EQ(remove_scene_vmad_phase_fragment(plugin, fixture::key(), 3, 2),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(saved_vmad(plugin), vmad_bytes(scene_tail(0x00, {}, phases)));

  auto packed = read_plugin(
      fixture::plugin_bytes(vmad_bytes(flagged_tail(0x03, {{7, "F0"}, {8, "F1"}})), "PACK"),
      "Vmad.esp");
  const auto packed_before = write_plugin(packed);
  EXPECT_EQ(add_scene_vmad_phase_fragment(packed, fixture::key(), 0, 1, "PF_New", "Phase_0"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(set_scene_vmad_phase_fragment(packed, fixture::key(), 0, 1, "PF_New", "Phase_0"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(remove_scene_vmad_phase_fragment(packed, fixture::key(), 0, 1),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(write_plugin(packed), packed_before);

  auto bare = read_plugin(fixture::plugin_bytes(fixture::payload(), "SCEN"), "Vmad.esp");
  const auto bare_before = write_plugin(bare);
  EXPECT_EQ(add_scene_vmad_phase_fragment(bare, fixture::key(), 0, 1, "PF_New", "Phase_0"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(set_scene_vmad_phase_fragment(bare, fixture::key(), 0, 1, "PF_New", "Phase_0"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(remove_scene_vmad_phase_fragment(bare, fixture::key(), 0, 1),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(write_plugin(bare), bare_before);

  auto countless = read_plugin(
      fixture::plugin_bytes(vmad_bytes(flagged_tail(0x01, {{7, "F0"}})), "SCEN"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(fixture::subrecord(countless).data, sig_scen).complete());
  const auto countless_before = write_plugin(countless);
  EXPECT_THROW(add_scene_vmad_phase_fragment(countless, fixture::key(), 0, 1, "PF_New", "Phase_0"),
               schema::ScalarError);
  EXPECT_THROW(set_scene_vmad_phase_fragment(countless, fixture::key(), 0, 1, "PF_New", "Phase_0"),
               schema::ScalarError);
  EXPECT_THROW(remove_scene_vmad_phase_fragment(countless, fixture::key(), 0, 1),
               schema::ScalarError);
  EXPECT_EQ(write_plugin(countless), countless_before);
}

TEST(VmadFragments, CreatedBindingLandsInKeyOrderAndRemovalRestoresThePayload) {
  const auto tail = flagged_tail(0x05, {{7, "Fragment_0"}, {8, "Fragment_2"}});
  auto plugin = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), "PACK"), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  ASSERT_EQ(vmad_coverage(before, sig_pack).fragment_count, 2u);

  ASSERT_EQ(add_vmad_fragment(plugin, fixture::key(), 1, "PF_Test", "Fragment_1"),
            VmadStructureMutationStatus::updated);
  const auto inserted = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(inserted, sig_pack).complete());
  EXPECT_EQ(vmad_coverage(inserted, sig_pack).fragment_count, 3u);
  EXPECT_EQ(inserted, vmad_bytes(flagged_tail(
                          0x07, {{7, "Fragment_0"}, {0, "Fragment_1"}, {8, "Fragment_2"}})));
  EXPECT_GT(inserted.size(), before.size());

  EXPECT_EQ(add_vmad_fragment(plugin, fixture::key(), 1, "PF_New", "Fragment_1"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(add_vmad_fragment(plugin, fixture::key(), 3, "PF_New", "Fragment_3"),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_THROW(add_vmad_fragment(plugin, fixture::key(), 1, "", "Fragment_1"),
               schema::ScalarError);
  EXPECT_THROW(add_vmad_fragment(plugin, fixture::key(), 1, "PF_New", ""), schema::ScalarError);
  EXPECT_EQ(fixture::subrecord(plugin).data, inserted);

  ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 1), VmadStructureMutationStatus::updated);
  EXPECT_EQ(saved_vmad(plugin), before);
}

TEST(VmadFragments, CreatedBindingWorksOnIndexedAndTwoBitRecords) {
  const auto perk_tail = indexed_tail({{100, "Fragment_100"}, {200, "Fragment_200"}});
  auto perk = read_plugin(fixture::plugin_bytes(vmad_bytes(perk_tail), "PERK"), "Vmad.esp");
  ASSERT_EQ(add_vmad_fragment(perk, fixture::key(), 150, "PF_Test", "Fragment_150"),
            VmadStructureMutationStatus::updated);
  const auto perk_saved = saved_vmad(perk);
  EXPECT_TRUE(vmad_coverage(perk_saved, sig_perk).complete());
  EXPECT_EQ(vmad_coverage(perk_saved, sig_perk).fragment_count, 3u);
  EXPECT_EQ(perk_saved, vmad_bytes(indexed_tail({{100, "Fragment_100"},
                                                {150, "Fragment_150", "PF_Test", 0},
                                                {200, "Fragment_200"}})));
  EXPECT_EQ(add_vmad_fragment(perk, fixture::key(), 150, "PF_New", "Fragment_150"),
            VmadStructureMutationStatus::already_present);
  EXPECT_EQ(fixture::subrecord(perk).data, perk_saved);

  const auto term_tail = indexed_tail({{4, "Fragment_4"}});
  auto term = read_plugin(fixture::plugin_bytes(vmad_bytes(term_tail), "TERM"), "Vmad.esp");
  ASSERT_EQ(add_vmad_fragment(term, fixture::key(), 9, "PF_Test", "Fragment_9"),
            VmadStructureMutationStatus::updated);
  const auto term_saved = saved_vmad(term);
  EXPECT_TRUE(vmad_coverage(term_saved, sig_term).complete());
  EXPECT_EQ(vmad_coverage(term_saved, sig_term).fragment_count, 2u);
  EXPECT_EQ(term_saved,
            vmad_bytes(indexed_tail({{4, "Fragment_4"}, {9, "Fragment_9", "PF_Test", 0}})));

  const auto info_tail = flagged_tail(0x01, {{7, "Fragment_0"}});
  auto info = read_plugin(fixture::plugin_bytes(vmad_bytes(info_tail), "INFO"), "Vmad.esp");
  ASSERT_EQ(add_vmad_fragment(info, fixture::key(), 1, "PF_Test", "Fragment_1"),
            VmadStructureMutationStatus::updated);
  const auto info_saved = saved_vmad(info);
  EXPECT_TRUE(vmad_coverage(info_saved, sig_info).complete());
  EXPECT_EQ(vmad_coverage(info_saved, sig_info).fragment_count, 2u);
  EXPECT_EQ(info_saved, vmad_bytes(flagged_tail(0x03, {{7, "Fragment_0"}, {0, "Fragment_1"}})));
  EXPECT_EQ(add_vmad_fragment(info, fixture::key(), 2, "PF_New", "Fragment_2"),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(fixture::subrecord(info).data, info_saved);
}

TEST(VmadFragments, EditsRefuseForeignRecordsAndBadKeys) {
  auto refr = read_plugin(
      fixture::plugin_bytes(vmad_bytes(flagged_tail(0x03, {{7, "F0"}, {8, "F1"}}))), "Vmad.esp");
  const auto refr_before = write_plugin(refr);
  EXPECT_EQ(set_vmad_fragment(refr, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(remove_vmad_fragment(refr, fixture::key(), 0),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(add_vmad_fragment(refr, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(write_plugin(refr), refr_before);

  auto bare = read_plugin(fixture::plugin_bytes(fixture::payload(), "PACK"), "Vmad.esp");
  const auto bare_before = write_plugin(bare);
  EXPECT_EQ(set_vmad_fragment(bare, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(remove_vmad_fragment(bare, fixture::key(), 0),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(add_vmad_fragment(bare, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(write_plugin(bare), bare_before);

  auto packed = read_plugin(
      fixture::plugin_bytes(vmad_bytes(flagged_tail(0x03, {{7, "F0"}, {8, "F1"}})), "PACK"),
      "Vmad.esp");
  const auto packed_before = write_plugin(packed);
  EXPECT_EQ(set_vmad_fragment(packed, fixture::key(0x999), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(remove_vmad_fragment(packed, fixture::key(0x999), 0),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(set_vmad_fragment(packed, fixture::key(), 5, "PF_Other", "F1"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(remove_vmad_fragment(packed, fixture::key(), 5),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_THROW(set_vmad_fragment(packed, fixture::key(), 0, "", "F1"), schema::ScalarError);
  EXPECT_THROW(set_vmad_fragment(packed, fixture::key(), 0, "PF_Other", ""), schema::ScalarError);
  EXPECT_EQ(write_plugin(packed), packed_before);
}

TEST(VmadFragments, ScriptlessAndMalformedSectionsAreRefused) {
  ByteWriter without_name;
  without_name.u8(3);
  without_name.u8(0);
  append_fragment_script(without_name, "", 0);
  auto scriptless = read_plugin(
      fixture::plugin_bytes(vmad_bytes(without_name.bytes), "PACK"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(fixture::subrecord(scriptless).data, sig_pack).complete());
  const auto scriptless_before = write_plugin(scriptless);
  EXPECT_EQ(set_vmad_fragment(scriptless, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(remove_vmad_fragment(scriptless, fixture::key(), 0),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(add_vmad_fragment(scriptless, fixture::key(), 1, "PF_Other", "F1"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(write_plugin(scriptless), scriptless_before);

  auto tail = flagged_tail(0x03, {{7, "F0"}, {8, "F1"}});
  tail.push_back(std::byte{0xAB});
  auto damaged = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), "PACK"), "Vmad.esp");
  const auto damaged_before = write_plugin(damaged);
  EXPECT_THROW(set_vmad_fragment(damaged, fixture::key(), 0, "PF_Other", "F1"),
               schema::ScalarError);
  EXPECT_THROW(remove_vmad_fragment(damaged, fixture::key(), 0), schema::ScalarError);
  EXPECT_THROW(add_vmad_fragment(damaged, fixture::key(), 0, "PF_Other", "F1"),
               schema::ScalarError);
  EXPECT_EQ(write_plugin(damaged), damaged_before);
}

TEST(VmadFragments, ScenePhaseIndicesFollowTheGivenOrderOrRefuse) {
  const std::vector<Entry> entries{{7, "Fragment_0"}, {8, "Fragment_1"}};
  const std::vector<PhaseEntry> phases{{1, 2, 0, "Phase_2"}, {2, 5, 0xFF, "Phase_5"}};
  auto plugin = read_plugin(
      fixture::plugin_bytes(vmad_bytes(scene_tail(0x03, entries, phases)), "SCEN"), "Vmad.esp");
  EXPECT_EQ(remap_scene_phase_indices(plugin, fixture::key(),
                                      std::vector<std::uint32_t>{3, 4, 0, 1, 2, 5}),
            VmadStructureMutationStatus::updated);
  const auto remapped = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(remapped, sig_scen).complete());
  EXPECT_EQ(remapped,
            vmad_bytes(scene_tail(0x03, entries, {{1, 0, 0, "Phase_2"}, {2, 1, 0xFF, "Phase_5"}})));
  EXPECT_EQ(remap_scene_phase_indices(plugin, fixture::key(),
                                      std::vector<std::uint32_t>{3, 4, 0, 1, 2, 5}),
            VmadStructureMutationStatus::unchanged);
  EXPECT_EQ(fixture::subrecord(plugin).data, remapped);

  const std::vector<std::uint32_t> removes_bound{0, k_scene_phase_removed, 2, 3, 4, 5};
  EXPECT_THROW(static_cast<void>(remap_scene_phase_indices(plugin, fixture::key(), removes_bound)),
               schema::ScalarError);
  const std::vector<std::uint32_t> too_short{0};
  EXPECT_THROW(static_cast<void>(remap_scene_phase_indices(plugin, fixture::key(), too_short)),
               schema::ScalarError);
  EXPECT_EQ(fixture::subrecord(plugin).data, remapped);

  auto packed = read_plugin(fixture::plugin_bytes(vmad_bytes(flagged_tail(0x03, {{7, "F0"}})), "PACK"),
                            "Vmad.esp");
  EXPECT_EQ(remap_scene_phase_indices(packed, fixture::key(),
                                      std::vector<std::uint32_t>{1, 0}),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(remap_scene_phase_indices(packed, fixture::key(0x999),
                                      std::vector<std::uint32_t>{1, 0}),
            VmadStructureMutationStatus::record_missing);
}
