#include "fo4/plugin/vmad_writer.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace fo4;
namespace fixture = fo4::test::vmad;

using Entry = std::pair<std::uint8_t, std::string>;
using IndexedEntry = std::pair<std::uint32_t, std::string>;

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

std::vector<std::byte> flagged_tail(std::uint8_t flags, const std::vector<Entry>& entries,
                                    bool scenes = false) {
  ByteWriter tail;
  tail.u8(3);
  tail.u8(flags);
  append_fragment_script(tail, "PF_Test", 1);
  for (const auto& [unknown, function] : entries) {
    tail.u8(unknown);
    fixture::name(tail, "PF_Test");
    fixture::name(tail, function);
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
  for (const auto& [key, function] : entries) {
    tail.u32(key);
    tail.u8(0x2A);
    fixture::name(tail, "PF_Test");
    fixture::name(tail, function);
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
    ASSERT_EQ(coverage.fragment_count, 2u);

    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 1, "PF_Other", "Fragment_Renamed"),
              VmadStructureMutationStatus::updated);
    const auto saved = saved_vmad(plugin);
    EXPECT_TRUE(vmad_coverage(saved, Signature::from_string(signature)).complete());
    EXPECT_EQ(saved, vmad_bytes(flagged_tail(0x03, {{7, "Fragment_0"}, {8, "Fragment_Renamed"}},
                                             scenes)));

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
    EXPECT_EQ(saved, vmad_bytes(indexed_tail({{100, "Fragment_100"}, {200, "Fragment_Renamed"}})));

    EXPECT_EQ(set_vmad_fragment(plugin, fixture::key(), 200, "PF_Other", "Fragment_Renamed"),
              VmadStructureMutationStatus::unchanged);
    EXPECT_EQ(fixture::subrecord(plugin).data, saved);

    ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 100),
              VmadStructureMutationStatus::updated);
    saved = saved_vmad(plugin);
    const auto trimmed = vmad_coverage(saved, Signature::from_string(signature));
    EXPECT_TRUE(trimmed.complete());
    EXPECT_EQ(trimmed.fragment_count, 1u);
    EXPECT_EQ(saved, vmad_bytes(indexed_tail({{200, "Fragment_Renamed"}})));

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
  EXPECT_EQ(renamed, vmad_bytes(flagged_tail(0x03, {{7, "Fragment_0"}, {8, "Fragment_Renamed"}},
                                             true)));

  ASSERT_EQ(remove_vmad_fragment(plugin, fixture::key(), 0), VmadStructureMutationStatus::updated);
  const auto trimmed = saved_vmad(plugin);
  EXPECT_TRUE(vmad_coverage(trimmed, sig_scen).complete());
  EXPECT_EQ(trimmed, vmad_bytes(flagged_tail(0x01, {{8, "Fragment_Renamed"}}, true)));
  EXPECT_LT(trimmed.size(), renamed.size());
}

TEST(VmadFragments, EditsRefuseForeignRecordsAndBadKeys) {
  auto refr = read_plugin(
      fixture::plugin_bytes(vmad_bytes(flagged_tail(0x03, {{7, "F0"}, {8, "F1"}}))), "Vmad.esp");
  const auto refr_before = write_plugin(refr);
  EXPECT_EQ(set_vmad_fragment(refr, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(remove_vmad_fragment(refr, fixture::key(), 0),
            VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(write_plugin(refr), refr_before);

  auto bare = read_plugin(fixture::plugin_bytes(fixture::payload(), "PACK"), "Vmad.esp");
  const auto bare_before = write_plugin(bare);
  EXPECT_EQ(set_vmad_fragment(bare, fixture::key(), 0, "PF_Other", "F1"),
            VmadStructureMutationStatus::binding_missing);
  EXPECT_EQ(remove_vmad_fragment(bare, fixture::key(), 0),
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
  EXPECT_EQ(write_plugin(scriptless), scriptless_before);

  auto tail = flagged_tail(0x03, {{7, "F0"}, {8, "F1"}});
  tail.push_back(std::byte{0xAB});
  auto damaged = read_plugin(fixture::plugin_bytes(vmad_bytes(tail), "PACK"), "Vmad.esp");
  const auto damaged_before = write_plugin(damaged);
  EXPECT_THROW(set_vmad_fragment(damaged, fixture::key(), 0, "PF_Other", "F1"),
               schema::ScalarError);
  EXPECT_THROW(remove_vmad_fragment(damaged, fixture::key(), 0), schema::ScalarError);
  EXPECT_EQ(write_plugin(damaged), damaged_before);
}
