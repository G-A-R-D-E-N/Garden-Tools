#include "fo4/environment/environment.hpp"
#include "fo4/previs/detect.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "previs_test_support.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace en = fo4::test::envnode;

using fo4::previs::PrevisIssue;
using fo4::test::previs::find_issue;

constexpr std::uint32_t k_deleted_flag = 0x20;
constexpr std::uint32_t k_no_previs_flag = 0x80;
constexpr std::uint32_t k_partial_flag = 0x4000;
constexpr std::uint16_t k_pcmb_date = 0x3000;

en::Sub date_sub(const char* signature, std::uint16_t value) {
  return {signature, {static_cast<std::byte>(value & 0xFFu), static_cast<std::byte>(value >> 8)}};
}

en::Sub one_ref_xcri() {
  fo4::test::ByteWriter xcri;
  xcri.u32(1);
  xcri.u32(2);
  xcri.u32(0x55);
  xcri.u32(0x810);
  xcri.u32(0x56);
  return {"XCRI", xcri.bytes};
}

std::vector<en::Sub> cell_subs(bool exterior, std::vector<en::Sub> extra) {
  std::vector<en::Sub> subs{{"DATA", {std::byte{static_cast<unsigned char>(exterior ? 0 : 1)}, std::byte{0}}}};
  if (exterior) subs.push_back({"XCLC", en::bytes_of({0u, 0u, 0u})});
  for (auto& sub : extra) subs.push_back(std::move(sub));
  return subs;
}

en::Node placed_ref(float x, std::uint32_t flags, std::uint16_t timestamp) {
  return en::rec("REFR", 0x810,
                 {{"NAME", en::bytes_of({0x820})},
                  {"DATA", en::floats({x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                 flags, 131, timestamp);
}

en::Node land_record(std::uint16_t timestamp) {
  return en::rec("LAND", 0x811, {{"DATA", en::bytes_of({0u})}}, 0, 131, timestamp);
}

std::vector<en::Node> cell_with_temporary(en::Node cell, std::vector<en::Node> children) {
  std::vector<en::Node> nodes{std::move(cell)};
  if (!children.empty()) nodes.push_back(en::grp(0x800, 6, {en::grp(0x800, 9, std::move(children))}));
  return nodes;
}

std::vector<std::byte> cell_plugin(const std::vector<std::string>& masters, bool exterior,
                                   std::vector<en::Node> nodes) {
  if (!exterior) {
    return en::make_plugin(masters,
                           {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, std::move(nodes))})})});
  }
  std::vector<en::Node> world{en::grp(0x7F0, 1, {en::grp(0, 4, {en::grp(0, 5, std::move(nodes))})})};
  if (masters.empty()) world.insert(world.begin(), en::rec("WRLD", 0x7F0));
  return en::make_plugin(masters, {en::top("WRLD", std::move(world))});
}

en::Node precombined_cell(bool exterior) {
  return en::rec("CELL", 0x800,
                 cell_subs(exterior, {date_sub("PCMB", k_pcmb_date), one_ref_xcri()}));
}

std::vector<std::byte> conflict_base(bool exterior, std::uint16_t ref_stamp,
                                     std::uint16_t land_stamp = 0x2800) {
  std::vector<en::Node> children{placed_ref(1.0f, 0, ref_stamp)};
  if (exterior) children.push_back(land_record(land_stamp));
  return cell_plugin({}, exterior, cell_with_temporary(precombined_cell(exterior), std::move(children)));
}

en::Node partial_cell() { return en::rec("CELL", 0x800, {}, k_partial_flag); }

std::vector<std::byte> conflict_patch(bool exterior, en::Node cell, std::vector<en::Node> children) {
  return cell_plugin({"Base.esm"}, exterior,
                     cell_with_temporary(std::move(cell), std::move(children)));
}

bool outdated_with(const fo4::previs::CellPrevisStatus& status, PrevisIssue code) {
  return find_issue(status.outdated_issues, code) != nullptr;
}

class PrevisConflictTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_previs_conflict_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  fo4::previs::CellPrevisStatus scan_cell(const std::vector<std::byte>& base,
                                          const std::vector<std::byte>& patch) {
    const std::filesystem::path paths[]{write("Base.esm", base), write("Patch.esp", patch)};
    const auto env = fo4::env::Environment::load(paths);
    const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
    for (const auto& entry : report) {
      if ((entry.cell.local().value() & 0xFFFFFFu) == 0x800u) return entry.status;
    }
    ADD_FAILURE() << "cell 0x800 missing from the report";
    return {};
  }

  std::filesystem::path root_;
};

}  // namespace

// Detects: a later plugin's newer REFR override of an XCRI-listed reference, which the same-plugin
// version scan never sees, leaving a cell whose precombines the engine drops reported as clean.
TEST_F(PrevisConflictTest, NewerRefrOverrideInAnotherPluginIsOutdated) {
  const auto base = conflict_base(false, 0x2800);
  const auto patch_with = [](std::uint16_t stamp) {
    return conflict_patch(false, partial_cell(), {placed_ref(9.0f, 0, stamp)});
  };
  const auto newer = scan_cell(base, patch_with(0x3100));
  EXPECT_TRUE(newer.outdated);
  EXPECT_FALSE(newer.broken);
  EXPECT_TRUE(outdated_with(newer, PrevisIssue::pcmb_ref_newer));
  EXPECT_FALSE(outdated_with(newer, PrevisIssue::pcmb_stale));
  EXPECT_FALSE(scan_cell(base, patch_with(0x2900)).outdated);
  EXPECT_FALSE(scan_cell(base, patch_with(k_pcmb_date)).outdated);
}

// Detects: a no-op (ITM) cross-plugin REFR override with a fresh timestamp being reported, which
// would flag every cleaned patch; a master REFR itself newer than the PCMB must still report.
TEST_F(PrevisConflictTest, IdenticalRefrOverrideIsOnlyOutdatedWhenMasterIsNewer) {
  const auto itm = [](std::uint16_t stamp) {
    return conflict_patch(false, partial_cell(), {placed_ref(1.0f, 0, stamp)});
  };
  EXPECT_FALSE(scan_cell(conflict_base(false, 0x2800), itm(0x3100)).outdated);
  EXPECT_TRUE(
      outdated_with(scan_cell(conflict_base(false, 0x3200), itm(0x3100)), PrevisIssue::pcmb_ref_newer));
}

// Detects: a timestamp-0 REFR override that disables the winning cell's precombines going
// unreported, while a cross-plugin timestamp-0 override (unprovable) must stay quiet unless deleted.
TEST_F(PrevisConflictTest, UntimedRefrOverrideIsReportedOnlyWhereItDisablesPrecombines) {
  const auto base = conflict_base(false, 0x2800);
  const auto foreign = [&](std::uint32_t flags) {
    return scan_cell(base, conflict_patch(false, partial_cell(), {placed_ref(9.0f, flags, 0)}));
  };
  EXPECT_FALSE(foreign(0).outdated);
  EXPECT_TRUE(outdated_with(foreign(k_deleted_flag), PrevisIssue::pcmb_record_untimed));

  const auto own = [&](std::uint16_t stamp) {
    return scan_cell(base,
                     conflict_patch(false, precombined_cell(false), {placed_ref(9.0f, 0, stamp)}));
  };
  EXPECT_TRUE(outdated_with(own(0), PrevisIssue::pcmb_record_untimed));
  EXPECT_FALSE(own(0x2900).outdated);
}

// Detects: a LAND override in the winning cell with no timestamp, or newer than the PCMB, going
// unreported; a master LAND with timestamp 0 is not an override and must stay quiet.
TEST_F(PrevisConflictTest, LandOverrideInWinningCellIsReported) {
  const auto base = conflict_base(true, 0x2800);
  const auto with_land = [&](std::uint16_t stamp) {
    return scan_cell(base, conflict_patch(true, precombined_cell(true), {land_record(stamp)}));
  };
  EXPECT_TRUE(outdated_with(with_land(0), PrevisIssue::pcmb_record_untimed));
  const auto newer = with_land(0x3100);
  EXPECT_TRUE(outdated_with(newer, PrevisIssue::pcmb_stale));
  EXPECT_FALSE(outdated_with(newer, PrevisIssue::pcmb_record_untimed));
  EXPECT_FALSE(with_land(0x2900).outdated);

  EXPECT_FALSE(
      scan_cell(conflict_base(true, 0x2800, 0), conflict_patch(true, partial_cell(), {})).outdated);
}

// Detects: a later plugin that sets NoPreVis on a cell whose earlier version had working VISI
// (previs silently off); a flag on a partial-form override, or on a cell that never had VISI, is not it.
TEST_F(PrevisConflictTest, NoPreVisOverrideOfWorkingPrevisIsOutdated) {
  const auto visi_cell = [](std::uint32_t flags) {
    return en::rec("CELL", 0x800, cell_subs(false, {date_sub("VISI", 0x2000)}), flags);
  };
  const auto patch = [&](std::uint32_t flags) {
    return cell_plugin({"Base.esm"}, false, {visi_cell(flags)});
  };
  const auto disabled_by = [](const fo4::previs::CellPrevisStatus& status) {
    return outdated_with(status, PrevisIssue::previs_disabled_by_override);
  };
  const auto base = cell_plugin({}, false, {visi_cell(0)});
  const auto disabled = scan_cell(base, patch(k_no_previs_flag));
  EXPECT_TRUE(disabled.outdated);
  EXPECT_FALSE(disabled.broken);
  EXPECT_TRUE(disabled_by(disabled));
  EXPECT_FALSE(disabled_by(scan_cell(base, patch(0))));
  EXPECT_FALSE(disabled_by(scan_cell(base, patch(k_no_previs_flag | k_partial_flag))));
  EXPECT_FALSE(disabled_by(scan_cell(cell_plugin({}, false, {visi_cell(k_no_previs_flag)}),
                                     patch(k_no_previs_flag))));
}

// Detects: an override that zeroes PCMB or drops XCRI while an earlier version had working
// precombines going unreported; an override that keeps both, or a PCMB-only chain, is fine.
TEST_F(PrevisConflictTest, OverrideDroppingPrecombinesIsOutdated) {
  const auto base = conflict_base(false, 0x2800);
  const auto dropped = [&](std::vector<en::Sub> extra, const std::vector<std::byte>& from) {
    const auto patch = cell_plugin({"Base.esm"}, false,
                                   {en::rec("CELL", 0x800, cell_subs(false, std::move(extra)))});
    return outdated_with(scan_cell(from, patch), PrevisIssue::pcmb_dropped_by_override);
  };
  EXPECT_TRUE(dropped({one_ref_xcri()}, base));
  EXPECT_TRUE(dropped({date_sub("PCMB", k_pcmb_date)}, base));
  EXPECT_TRUE(dropped({}, base));
  EXPECT_FALSE(dropped({date_sub("PCMB", k_pcmb_date), one_ref_xcri()}, base));

  const auto pcmb_only = cell_plugin(
      {}, false, {en::rec("CELL", 0x800, cell_subs(false, {date_sub("PCMB", k_pcmb_date)}))});
  EXPECT_FALSE(dropped({date_sub("PCMB", k_pcmb_date)}, pcmb_only));
}

// Detects: a unit-level classification slip that would turn an override finding into a broken
// cell; the pinned vanilla broken list requires these to stay in the outdated class.
TEST(PrevisConflictDetect, OverrideFindingsAreOutdatedNeverBroken) {
  fo4::previs::CellPrevisInput input{};
  input.has_pcmb = true;
  input.override_refs_newer = 2;
  input.override_records_untimed = 1;
  input.previs_disabled_by_override = true;
  input.pcmb_zeroed_by_override = true;
  input.xcri_removed_by_override = true;
  const auto status = fo4::previs::diagnose_cell(input);
  EXPECT_FALSE(status.broken);
  EXPECT_TRUE(status.outdated);
  EXPECT_TRUE(status.issues.empty());
  for (const auto code : {PrevisIssue::pcmb_ref_newer, PrevisIssue::pcmb_record_untimed,
                          PrevisIssue::previs_disabled_by_override,
                          PrevisIssue::pcmb_dropped_by_override}) {
    EXPECT_TRUE(outdated_with(status, code)) << fo4::previs::to_string(code);
  }
}
