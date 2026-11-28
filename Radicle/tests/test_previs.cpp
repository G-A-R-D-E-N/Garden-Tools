#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/previs_data.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/error.hpp"
#include "env_node_fixture.hpp"
#include "nif_build.hpp"
#include "plugin_bytes.hpp"
#include "previs_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace en = fo4::test::envnode;

using fo4::test::previs::bytes_of;
using fo4::test::previs::find_issue;
using fo4::test::previs::make_uvd_bytes;

}  // namespace

namespace {

std::vector<std::byte> make_previs_plugin() {
  const auto visi_cell = en::rec("CELL", 0x800,
                                 {{"DATA", {std::byte{0x01}, std::byte{0}}},
                                  {"VISI", {std::byte{0x34}, std::byte{0x12}}},
                                  {"RVIS", en::bytes_of({0x801})},
                                  {"XPRI", en::bytes_of({0x810})}});
  fo4::test::ByteWriter xcri;
  xcri.u32(1);
  xcri.u32(2);
  xcri.u32(0x55);
  xcri.u32(0x810);
  xcri.u32(0x56);
  const auto pcmb_cell = en::rec("CELL", 0x801,
                                 {{"DATA", {std::byte{0x01}, std::byte{0}}},
                                  {"PCMB", {std::byte{0x34}, std::byte{0x12}}},
                                  {"XCRI", xcri.bytes}});
  const auto bad_xcri = en::rec("CELL", 0x802,
                                {{"DATA", {std::byte{0x01}, std::byte{0}}},
                                 {"PCMB", {std::byte{0x34}, std::byte{0x12}}},
                                 {"XCRI", bytes_of({1, 1, 0x55, 0x01})}});
  return en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {visi_cell, pcmb_cell,
                                                                             bad_xcri})})})});
}

const fo4::previs::CellDiagnosis* find_cell(const std::vector<fo4::previs::CellDiagnosis>& report,
                                            std::uint32_t local) {
  for (const auto& entry : report) {
    if ((entry.cell.local().value() & 0xFFFFFFu) == local) return &entry;
  }
  return nullptr;
}

constexpr std::uint32_t k_foreign_vertices = 100;
constexpr std::uint32_t k_foreign_triangles = 20;
constexpr std::uint64_t k_foreign_desc = 0x0001B00000430205ull;

std::vector<std::byte> oc_nif_stamped_with(std::uint32_t file_hash) {
  nif_build::Writer extra;
  extra.i32(-1);
  extra.u64(k_foreign_desc);
  extra.u32(k_foreign_vertices);
  extra.u32(k_foreign_triangles);
  extra.u64(0u);
  extra.u32(1u);
  extra.u32(file_hash);
  extra.u32(0u);
  extra.u32(k_foreign_vertices);
  extra.u32(3u);
  extra.u32(k_foreign_triangles);
  extra.u32(0u);
  extra.u32(0u);
  extra.u32(3u * k_foreign_triangles);
  extra.u32(0u);
  extra.u32(3u * k_foreign_triangles);
  extra.u32(1u);
  extra.u32(0u);
  for (const float value : {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}) {
    extra.f32(value);
  }
  extra.u64(k_foreign_desc);
  nif_build::Writer root;
  root.i32(-1);
  root.u32(1u);
  root.i32(1);
  root.i32(-1);
  root.u32(0u);
  root.transform(fo4::nif::Transform{});
  root.i32(-1);
  root.u32(0u);
  return nif_build::build_nif(
      {}, {nif_build::Block{"NiNode", root.bytes()}, nif_build::Block{"BSPackedCombinedSharedGeomDataExtra", extra.bytes()}},
      {0});
}

std::vector<std::byte> csg_with_one_row_at(std::uint32_t row_offset) {
  fo4::previs::CsgFile file;
  fo4::previs::CsgRow row{};
  const std::uint32_t fields[5] = {row_offset, 0x00430205u, 0x0001B000u, k_foreign_vertices, 3u * k_foreign_triangles};
  for (std::size_t field = 0; field < 5; ++field) {
    for (std::size_t byte = 0; byte < 4; ++byte) {
      row.bytes[field * 4 + byte] = static_cast<std::byte>((fields[field] >> (8 * byte)) & 0xFFu);
    }
  }
  file.rows.push_back(row);
  const std::size_t stream = row_offset + k_foreign_vertices * 20u + 3u * k_foreign_triangles * 2u;
  for (std::size_t at = 0; at < stream; at += fo4::previs::k_csg_inflated_chunk_max) {
    const auto length = std::min(fo4::previs::k_csg_inflated_chunk_max, stream - at);
    file.chunks.push_back(fo4::test::zlib_compress(std::vector<std::byte>(length)));
  }
  return fo4::previs::write_csg(file);
}

class PrevisLoadOrderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_previs_" + std::to_string(id));
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


  std::optional<fo4::previs::CellCsgAnswer> folderless_oc_answer(std::uint32_t mod_row_offset) {
    const auto base_cell = en::rec("CELL", 0x800);
    const auto mod_cell = en::rec("CELL", 0x800, {{"DATA", {std::byte{0x01}, std::byte{0x00}}}});
    const auto base_plugin =
        en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {base_cell})})})});
    const auto mod_plugin = en::make_plugin(
        {"Fallout4.esm"}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {mod_cell})})})});
    const std::filesystem::path paths[] = {write("Fallout4.esm", base_plugin), write("Mod.esp", mod_plugin)};
    const auto env = fo4::env::Environment::load(paths);
    std::filesystem::create_directories(root_ / "Meshes" / "PreCombined" / "Fallout4.esm");
    write("Meshes/PreCombined/Fallout4.esm/00000800_00000050_OC.NIF",
          oc_nif_stamped_with(fo4::previs::csg_file_hash("Mod.esp")));
    write("Mod - Geometry.csg", csg_with_one_row_at(mod_row_offset));
    const std::filesystem::path roots[]{root_};
    const auto data = fo4::previs::PrevisData::load(roots, env);
    const auto base = env.find_plugin("Fallout4.esm");
    if (!base.has_value()) return std::nullopt;
    const auto cell = fo4::FormKey{env.plugins()[*base].key, fo4::LocalFormId{0x800}};
    const std::uint32_t ids[]{0x50u};
    return data.indexes().csg_rows(cell, ids);
  }

  std::filesystem::path root_;
};

void write_tome_archive(const std::filesystem::path& path, const std::string& entry_path,
                        const std::vector<std::byte>& tome) {
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  fo4::archive::Ba2GeneralEntry entry;
  entry.path = entry_path;
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(tome));
  archive.entries = {entry};
  fo4::archive::write_ba2_general_file(archive, path);
}

}  // namespace

// The report must compute its inputs from an Environment plus index answers;
// a malformed CELL must be reported, never thrown.
TEST_F(PrevisLoadOrderTest, ComputesInputsFromRealIndexes) {
  const std::filesystem::path paths[] = {write("Previs.esp", make_previs_plugin())};
  const auto env = fo4::env::Environment::load(paths);

  fo4::previs::LoadOrderIndexes indexes;
  indexes.cdx_row = [](std::size_t, const fo4::previs::CdxCellTarget& target) {
    return std::optional<bool>{target.cell.local().value() == 0x801u};
  };
  indexes.missing_oc_nifs = [](const fo4::FormKey&,
                               std::span<const std::uint32_t>) {
    return std::optional<std::uint32_t>{0u};
  };
  indexes.uvd = [](const fo4::FormKey& cell) {
    fo4::previs::TomeLookup answer;
    answer.checked = true;
    answer.found = cell.local().value() == 0x801u;
    return answer;
  };

  const auto report = fo4::previs::diagnose_load_order(env, indexes);
  ASSERT_EQ(report.size(), 3u);

  const auto* visi = find_cell(report, 0x800u);
  ASSERT_NE(visi, nullptr);
  EXPECT_TRUE(visi->input.has_visi);
  EXPECT_TRUE(visi->input.has_rvis);
  EXPECT_TRUE(visi->input.has_xpri);
  EXPECT_TRUE(visi->input.uvd_checked);
  EXPECT_TRUE(visi->input.has_uvd);
  EXPECT_FALSE(visi->status.broken);
  EXPECT_FALSE(visi->status.skip_unsupported);

  const auto* pcmb = find_cell(report, 0x801u);
  ASSERT_NE(pcmb, nullptr);
  EXPECT_TRUE(pcmb->input.has_pcmb);
  EXPECT_TRUE(pcmb->input.has_xcri);
  EXPECT_TRUE(pcmb->input.has_cdx_row);
  EXPECT_FALSE(pcmb->status.broken);
  EXPECT_FALSE(pcmb->status.skip_unsupported);

  const auto* bad = find_cell(report, 0x802u);
  ASSERT_NE(bad, nullptr);
  EXPECT_TRUE(bad->status.broken);
  EXPECT_NE(find_issue(bad->status.issues, fo4::previs::PrevisIssue::xcri_malformed), nullptr);
}

// Constraint: a tome that exists but could not be read must be reported as unreadable, never as invalid.
TEST_F(PrevisLoadOrderTest, UnreadableTomeIsReportedAsUnreadableNotInvalid) {
  const std::filesystem::path paths[] = {write("Previs.esp", make_previs_plugin())};
  const auto env = fo4::env::Environment::load(paths);

  fo4::previs::LoadOrderIndexes indexes;
  indexes.uvd = [](const fo4::FormKey&) {
    fo4::previs::TomeLookup answer;
    answer.checked = true;
    answer.found = true;
    answer.read_failed = true;
    return answer;
  };

  const auto report = fo4::previs::diagnose_load_order(env, indexes);
  const auto* visi = find_cell(report, 0x800u);
  ASSERT_NE(visi, nullptr);
  EXPECT_TRUE(visi->input.uvd_unreadable);
  EXPECT_FALSE(visi->input.uvd_valid);
  EXPECT_TRUE(visi->status.broken);
  EXPECT_NE(find_issue(visi->status.issues, fo4::previs::PrevisIssue::tome_unreadable), nullptr);
  EXPECT_EQ(find_issue(visi->status.issues, fo4::previs::PrevisIssue::tome_invalid), nullptr);
}

// Missing index answers require skips: the same load order is not condemned.
TEST_F(PrevisLoadOrderTest, MissingIndexesBecomeSkips) {
  const std::filesystem::path paths[] = {write("Previs.esp", make_previs_plugin())};
  const auto env = fo4::env::Environment::load(paths);

  fo4::previs::LoadOrderIndexes indexes;  // all null: nothing checked
  const auto report = fo4::previs::diagnose_load_order(env, indexes);
  ASSERT_EQ(report.size(), 3u);

  const auto* visi = find_cell(report, 0x800u);
  ASSERT_NE(visi, nullptr);
  EXPECT_TRUE(visi->input.has_visi);
  EXPECT_FALSE(visi->input.uvd_checked);
  EXPECT_FALSE(visi->status.broken);
  EXPECT_TRUE(visi->status.skip_unsupported);

  const auto* pcmb = find_cell(report, 0x801u);
  ASSERT_NE(pcmb, nullptr);
  EXPECT_FALSE(pcmb->status.broken);
  EXPECT_TRUE(pcmb->status.skip_unsupported);
}

// Regression: an override whose plugin ships no previs containers must skip, not condemn the cell.
TEST_F(PrevisLoadOrderTest, OverrideWithoutPrevisSkips) {
  fo4::test::ByteWriter xcri;
  xcri.u32(1);
  xcri.u32(2);
  xcri.u32(0x55);
  xcri.u32(0x810);
  xcri.u32(0x56);
  const auto make_cell = [&xcri](std::uint32_t local) {
    return en::rec("CELL", local,
                   {{"DATA", {std::byte{0x01}, std::byte{0}}},
                    {"PCMB", {std::byte{0x34}, std::byte{0x12}}},
                    {"XCRI", xcri.bytes}});
  };
  const auto base_plugin =
      en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {make_cell(0x800)})})})});
  const auto patch_plugin = en::make_plugin(
      {"Base.esm"},
      {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {make_cell(0x800), make_cell(0x801)})})})});
  const std::filesystem::path paths[] = {write("Base.esm", base_plugin),
                                         write("Patch.esp", patch_plugin)};
  const auto env = fo4::env::Environment::load(paths);

  fo4::previs::LoadOrderIndexes indexes;
  indexes.cdx_row = [](std::size_t, const fo4::previs::CdxCellTarget&) {
    return std::optional<bool>{false};
  };
  indexes.missing_oc_nifs = [](const fo4::FormKey&, std::span<const std::uint32_t>) {
    return std::optional<std::uint32_t>{0u};
  };
  indexes.plugin_ships_previs = [](std::size_t plugin) { return plugin == 0; };
  const auto report = fo4::previs::diagnose_load_order(env, indexes);
  ASSERT_EQ(report.size(), 2u);

  const auto* overridden = find_cell(report, 0x800u);
  ASSERT_NE(overridden, nullptr);
  EXPECT_TRUE(overridden->status.skip_unsupported);
  EXPECT_FALSE(overridden->status.broken);
  ASSERT_EQ(overridden->status.skips.size(), 1u);
  EXPECT_EQ(overridden->status.skips.front(),
            "override with no previs of its own: engine behaviour unverified");

  const auto* own = find_cell(report, 0x801u);
  ASSERT_NE(own, nullptr);
  EXPECT_TRUE(own->status.broken);
  EXPECT_FALSE(own->status.skip_unsupported);
  EXPECT_TRUE(overridden->status.issues.empty());
}

// Regression: the override skip must not hide a stale PCMB that the cell itself proves.
TEST_F(PrevisLoadOrderTest, OverrideWithoutPrevisKeepsStalePcmb) {
  fo4::test::ByteWriter xcri;
  xcri.u32(1);
  xcri.u32(2);
  xcri.u32(0x55);
  xcri.u32(0x810);
  xcri.u32(0x56);
  const auto base_cell = en::rec("CELL", 0x800);
  const auto patch_cell = en::rec("CELL", 0x800,
                                  {{"DATA", {std::byte{0x01}, std::byte{0x00}}},
                                   {"PCMB", {std::byte{0x6F}, std::byte{0x30}}},
                                   {"XCRI", xcri.bytes}});
  const auto child = en::rec("REFR", 0x01000801, {}, 0, 131, 0x3070);
  const auto base_plugin =
      en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {base_cell})})})});
  const auto patch_plugin = en::make_plugin(
      {"Base.esm"},
      {en::top("CELL",
               {en::grp(0, 2,
                        {en::grp(0, 3,
                                 {patch_cell,
                                  en::grp(0x00000800u, 6,
                                          {en::grp(0x00000800u, 9, {child})})})})})});
  const std::filesystem::path paths[]{write("Base.esm", base_plugin),
                                      write("Patch.esp", patch_plugin)};
  const auto env = fo4::env::Environment::load(paths);

  fo4::previs::LoadOrderIndexes indexes;
  indexes.cdx_row = [](std::size_t, const fo4::previs::CdxCellTarget&) {
    return std::optional<bool>{false};
  };
  indexes.missing_oc_nifs = [](const fo4::FormKey&, std::span<const std::uint32_t>) {
    return std::optional<std::uint32_t>{3u};
  };
  indexes.plugin_ships_previs = [](std::size_t plugin) { return plugin == 0; };
  const auto report = fo4::previs::diagnose_load_order(env, indexes);
  ASSERT_EQ(report.size(), 1u);
  const auto& status = report.front().status;
  EXPECT_TRUE(status.outdated);
  ASSERT_EQ(status.outdated_issues.size(), 1u);
  EXPECT_NE(find_issue(status.outdated_issues, fo4::previs::PrevisIssue::pcmb_stale), nullptr);
  EXPECT_TRUE(status.skip_unsupported);
  ASSERT_EQ(status.skips.size(), 1u);
  EXPECT_EQ(status.skips.front(),
            "override with no previs of its own: engine behaviour unverified");
  EXPECT_FALSE(status.broken);
  EXPECT_TRUE(status.issues.empty());
}

// Regression: a light plugin must skip its cells instead of receiving a full-plugin verdict.
TEST_F(PrevisLoadOrderTest, LightPluginIsSkipped) {
  const auto cell = en::rec("CELL", 0x800,
                            {{"DATA", {std::byte{0x01}, std::byte{0x00}}},
                             {"PCMB", {std::byte{0x6F}, std::byte{0x30}}}});
  const auto plugin_path =
      write("Light.esl", en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {cell})})})}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);
  ASSERT_EQ(env.plugins().size(), 1u);
  EXPECT_TRUE(env.plugins().front().kind == fo4::PluginKind::light);

  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  ASSERT_EQ(report.size(), 1u);
  EXPECT_TRUE(report.front().status.skip_unsupported);
  EXPECT_FALSE(report.front().status.broken);
  ASSERT_EQ(report.front().status.skips.size(), 1u);
  EXPECT_EQ(report.front().status.skips.front(), "light plugin layout not measured");
}

// Regression: a scan without Fallout4.esm must not attribute three-part OC NIFs to plugin 0.
TEST_F(PrevisLoadOrderTest, MissingBaseGameMarksNifIndexIncomplete) {
  const auto plugin_path = write("Patch.esp", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  bool named = false;
  for (const auto& problem : data.summary().problems) {
    if (problem.find("Fallout4.esm is absent") != std::string::npos) named = true;
  }
  EXPECT_TRUE(named);
  const auto indexes = data.indexes();
  const auto cell = fo4::FormKey{env.plugins().front().key, fo4::LocalFormId{0x800}};
  const std::uint32_t ids[]{0x50u};
  EXPECT_FALSE(indexes.missing_oc_nifs(cell, ids).has_value());
  const auto csg = indexes.csg_rows(cell, ids);
  ASSERT_TRUE(csg.has_value());
  EXPECT_FALSE(csg->checked);
  EXPECT_FALSE(csg->broken);
  EXPECT_FALSE(csg->not_run);
  EXPECT_TRUE(csg->issues.empty());
}

// Regression: a CELL that cannot be decoded must skip, never become a broken verdict.
TEST_F(PrevisLoadOrderTest, DecodeFailureBecomesSkip) {
  const auto cell = en::rec("CELL", 0x800,
                            {{"DATA", {std::byte{0x01}, std::byte{0x00}}},
                             {"VISI", {std::byte{0x34}, std::byte{0x12}}},
                             {"VISI", {std::byte{0x34}, std::byte{0x13}}}});
  const auto plugin_path =
      write("Broken.esp", en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {cell})})})}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);

  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  ASSERT_EQ(report.size(), 1u);
  EXPECT_TRUE(report.front().status.skip_unsupported);
  EXPECT_FALSE(report.front().status.broken);
  ASSERT_FALSE(report.front().status.skips.empty());
  EXPECT_NE(report.front().status.skips.front().find("CELL failed to decode"), std::string::npos);
}

// Regression: an invalid CDX must still count as a shipped container for the override rule.
TEST_F(PrevisLoadOrderTest, UnreadableCdxStillCountsAsShippedContainer) {
  const auto plugin_path = write("Patch.esp", en::make_plugin({}, {}));
  write("Patch.cdx", std::vector<std::byte>{std::byte{'x'}, std::byte{'x'}, std::byte{'x'},
                                            std::byte{'x'}, std::byte{0}, std::byte{0},
                                            std::byte{0}, std::byte{0}});
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto plugin = env.find_plugin("Patch.esp");
  ASSERT_TRUE(plugin.has_value());

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto indexes = data.indexes();
  EXPECT_TRUE(indexes.plugin_ships_previs(*plugin));
  bool named = false;
  for (const auto& problem : data.summary().problems) {
    if (problem.find("CDX invalid for Patch.esp") != std::string::npos) named = true;
  }
  EXPECT_TRUE(named);
  const auto cell = fo4::FormKey{env.plugins().front().key, fo4::LocalFormId{0x800}};
  EXPECT_FALSE(
      indexes.cdx_row(*plugin, fo4::previs::CdxCellTarget{std::nullopt, std::nullopt, cell})
          .has_value());
}

// Regression: a higher-priority asset root must win, including loose files over archives in one root.
TEST_F(PrevisLoadOrderTest, HigherAssetRootWins) {
  const auto low = root_ / "low";
  const auto high = root_ / "high";
  std::filesystem::create_directories(low);
  std::filesystem::create_directories(high);
  const auto put = [](const std::filesystem::path& dir, const std::string& name,
                      const std::vector<std::byte>& bytes) {
    const auto path = dir / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  };
  const auto plugin_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const auto patch_path = write("Patch.esp", en::make_plugin({"Fallout4.esm"}, {}));
  const std::filesystem::path paths[]{plugin_path, patch_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto plugin = env.find_plugin("Patch.esp");
  ASSERT_TRUE(plugin.has_value());
  const auto cell = fo4::FormKey{env.plugins()[*plugin].key, fo4::LocalFormId{0x800}};

  fo4::previs::CdxFile low_cdx;
  fo4::previs::CdxFile high_cdx;
  high_cdx.rows.push_back({0x80u, 0x01000800u, 0u, 0u});
  put(low, "Patch.cdx", fo4::previs::write_cdx(low_cdx));
  put(high, "Patch.cdx", fo4::previs::write_cdx(high_cdx));

  fo4::previs::CsgFile good_csg;
  good_csg.rows.push_back(fo4::previs::CsgRow{});
  good_csg.chunks.push_back({std::byte{0x78}, std::byte{0x9C}, std::byte{0x03}, std::byte{0x00},
                             std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}});
  auto bad_csg = fo4::previs::write_csg(good_csg);
  bad_csg[0] = std::byte{'x'};
  put(low, "Patch - Geometry.csg", bad_csg);
  put(high, "Patch - Geometry.csg", fo4::previs::write_csg(good_csg));

  auto bad_tome = make_uvd_bytes("T 512.0 SO 128.0");
  bad_tome[4] = static_cast<std::byte>(std::to_integer<int>(bad_tome[4]) ^ 0xFF);
  put(low, "Vis/Patch.esp/00000800.uvd", bad_tome);
  put(high, "Vis/Patch.esp/00000800.uvd", make_uvd_bytes("T 512.0 SO 128.0"));
  put(high, "Meshes/PreCombined/Patch.esp/00000800_00000050_OC.nif",
      std::vector<std::byte>{std::byte{1}});
  put(high, "Meshes/PreCombined/Fallout4.esm/00000800_00000050_OC.nif",
      std::vector<std::byte>{std::byte{1}});

  const std::filesystem::path roots[]{low, high};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto indexes = data.indexes();
  const auto cdx = indexes.cdx_row(*plugin, fo4::previs::CdxCellTarget{std::nullopt,
                                                                       std::nullopt, cell});
  ASSERT_TRUE(cdx.has_value());
  EXPECT_TRUE(*cdx);
  const auto tome = indexes.uvd(cell);
  EXPECT_TRUE(tome.checked);
  EXPECT_TRUE(tome.found);
  ASSERT_TRUE(tome.issues.empty()) << tome.issues.front();
  const std::uint32_t ids[]{0x50u};
  const auto missing = indexes.missing_oc_nifs(cell, ids);
  ASSERT_TRUE(missing.has_value());
  EXPECT_EQ(*missing, 0u);
  // Detects: a plugin whose geometry check cannot run must not answer silence.
  const auto no_container =
      indexes.csg_rows(fo4::FormKey{env.plugins()[0].key, fo4::LocalFormId{0x800}}, ids);
  ASSERT_TRUE(no_container.has_value());
  EXPECT_TRUE(no_container->not_run);
  EXPECT_FALSE(no_container->checked);
  ASSERT_EQ(no_container->issues.size(), 1u);
  EXPECT_NE(no_container->issues.front().find("ships no Geometry.csg"), std::string::npos);
  const auto unreadable_oc = indexes.csg_rows(cell, ids);
  ASSERT_TRUE(unreadable_oc.has_value());
  EXPECT_TRUE(unreadable_oc->not_run);
  EXPECT_FALSE(unreadable_oc->broken);
  ASSERT_EQ(unreadable_oc->issues.size(), 1u);
  EXPECT_NE(unreadable_oc->issues.front().find("could not be read"), std::string::npos);
  EXPECT_TRUE(indexes.plugin_ships_previs(*plugin));
  ASSERT_TRUE(data.summary().problems.empty()) << data.summary().problems.front();
}

// Detects: a container that validates but whose last chunk does not inflate must skip the geometry check, not answer silence.
TEST_F(PrevisLoadOrderTest, UnindexableCsgSkipsTheGeometryCheck) {
  const auto plugin_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const auto patch_path = write("Patch.esp", en::make_plugin({"Fallout4.esm"}, {}));
  const std::filesystem::path paths[]{plugin_path, patch_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto plugin = env.find_plugin("Patch.esp");
  ASSERT_TRUE(plugin.has_value());
  const auto cell = fo4::FormKey{env.plugins()[*plugin].key, fo4::LocalFormId{0x800}};

  fo4::previs::CsgFile truncated;
  truncated.rows.push_back(fo4::previs::CsgRow{});
  truncated.chunks.push_back({std::byte{0x78}, std::byte{0x9C}, std::byte{0x03}, std::byte{0x00}});
  write("Patch - Geometry.csg", fo4::previs::write_csg(truncated));
  std::filesystem::create_directories(root_ / "Meshes/PreCombined/Patch.esp");
  write("Meshes/PreCombined/Patch.esp/00000800_00000050_OC.nif", std::vector<std::byte>{std::byte{1}});

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto indexes = data.indexes();
  const std::uint32_t ids[]{0x50u};
  const auto answer = indexes.csg_rows(cell, ids);
  ASSERT_TRUE(answer.has_value());
  EXPECT_TRUE(answer->not_run);
  EXPECT_FALSE(answer->checked);
  EXPECT_FALSE(answer->broken);
  ASSERT_EQ(answer->issues.size(), 1u);
  EXPECT_NE(answer->issues.front().find("cannot be indexed"), std::string::npos);
}

// Regression: a clean-mode .psg must be reported unsupported and never counted as CSG geometry.
TEST_F(PrevisLoadOrderTest, PsgIsReportedUnsupportedAndNotCountedAsCsg) {
  const auto plugin_path = write("Patch.esp", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);
  write("Patch.psg", std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}});

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  bool psg_problem = false;
  for (const auto& problem : data.summary().problems) {
    if (problem.find(".psg") != std::string::npos) psg_problem = true;
  }
  EXPECT_TRUE(psg_problem);
  EXPECT_EQ(data.summary().csg_files, 0u);
}

// Regression: a loose tome must win over the archive copy, and archive paths must parse folded.
TEST_F(PrevisLoadOrderTest, ArchiveEntryLosesToLooseFile) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const auto patch_path = write("Patch.esp", en::make_plugin({"Fallout4.esm"}, {}));
  const std::filesystem::path paths[]{base_path, patch_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto base = env.find_plugin("Fallout4.esm");
  const auto patch = env.find_plugin("Patch.esp");
  ASSERT_TRUE(base.has_value());
  ASSERT_TRUE(patch.has_value());

  auto bad_tome = make_uvd_bytes("T 512.0 SO 128.0");
  bad_tome[4] = static_cast<std::byte>(std::to_integer<int>(bad_tome[4]) ^ 0xFF);
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = true;
  fo4::archive::Ba2GeneralEntry tome_entry;
  tome_entry.path = "Vis/Patch.ESP/00000802.uvd";
  tome_entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bad_tome));
  fo4::archive::Ba2GeneralEntry nif_entry;
  nif_entry.path = "Meshes/PreCombined/00000802_00000060_OC.NIF";
  nif_entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(
      std::vector<std::byte>{std::byte{1}}));
  fo4::archive::Ba2GeneralEntry broken_entry;
  broken_entry.path = "Meshes/PreCombined/not-a-cell.nif";
  broken_entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(
      std::vector<std::byte>{std::byte{2}}));
  archive.entries = {tome_entry, nif_entry, broken_entry};
  fo4::archive::write_ba2_general_file(archive, root_ / "Patch.ba2");

  const auto loose = root_ / "Vis" / "Patch.esp";
  std::filesystem::create_directories(loose);
  std::ofstream stream(loose / "00000802.uvd", std::ios::binary | std::ios::trunc);
  const auto good_tome = make_uvd_bytes("T 512.0 SO 128.0");
  stream.write(reinterpret_cast<const char*>(good_tome.data()),
               static_cast<std::streamsize>(good_tome.size()));
  stream.close();

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto indexes = data.indexes();
  const auto patch_cell = fo4::FormKey{env.plugins()[*patch].key, fo4::LocalFormId{0x802}};
  const auto tome = indexes.uvd(patch_cell);
  EXPECT_TRUE(tome.checked);
  EXPECT_TRUE(tome.found);
  ASSERT_TRUE(tome.issues.empty()) << tome.issues.front();

  const auto base_cell = fo4::FormKey{env.plugins()[*base].key, fo4::LocalFormId{0x802}};
  const std::uint32_t ids[]{0x60u};
  const auto missing = indexes.missing_oc_nifs(base_cell, ids);
  ASSERT_TRUE(missing.has_value());
  EXPECT_EQ(*missing, 0u);
  EXPECT_EQ(data.summary().oc_nif_files, 1u);
}

// Detects: a file-name-sorted archive winning over a later game-loaded archive that holds the same entry; the later archive must win.
TEST_F(PrevisLoadOrderTest, LaterLoadedArchiveWinsOverSortedFirstArchive) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const auto mod_path = write("ZMod.esp", en::make_plugin({"Fallout4.esm"}, {}));
  const std::filesystem::path paths[]{base_path, mod_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto mod = env.find_plugin("ZMod.esp");
  ASSERT_TRUE(mod.has_value());

  auto bad_tome = make_uvd_bytes("T 512.0 SO 128.0");
  bad_tome[4] = static_cast<std::byte>(std::to_integer<int>(bad_tome[4]) ^ 0xFF);
  write_tome_archive(root_ / "Fallout4 - Main.ba2", "Vis/ZMod.esp/00000803.uvd", bad_tome);
  write_tome_archive(root_ / "ZMod.ba2", "Vis/ZMod.esp/00000803.uvd",
                     make_uvd_bytes("T 512.0 SO 128.0"));

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto tome =
      data.indexes().uvd(fo4::FormKey{env.plugins()[*mod].key, fo4::LocalFormId{0x803}});
  EXPECT_TRUE(tome.found);
  ASSERT_TRUE(tome.issues.empty()) << tome.issues.front();
}

// Detects: a loose tome losing to an archive copy in its own root when several archives hold the entry; the loose file must win.
TEST_F(PrevisLoadOrderTest, LooseTomeBeatsEveryArchiveCopyInItsRoot) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const auto mod_path = write("ZMod.esp", en::make_plugin({"Fallout4.esm"}, {}));
  const std::filesystem::path paths[]{base_path, mod_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto mod = env.find_plugin("ZMod.esp");
  ASSERT_TRUE(mod.has_value());

  auto bad_tome = make_uvd_bytes("T 512.0 SO 128.0");
  bad_tome[4] = static_cast<std::byte>(std::to_integer<int>(bad_tome[4]) ^ 0xFF);
  write_tome_archive(root_ / "Fallout4 - Main.ba2", "Vis/ZMod.esp/00000803.uvd", bad_tome);
  write_tome_archive(root_ / "ZMod.ba2", "Vis/ZMod.esp/00000803.uvd", bad_tome);
  std::filesystem::create_directories(root_ / "Vis" / "ZMod.esp");
  write("Vis/ZMod.esp/00000803.uvd", make_uvd_bytes("T 512.0 SO 128.0"));

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto tome =
      data.indexes().uvd(fo4::FormKey{env.plugins()[*mod].key, fo4::LocalFormId{0x803}});
  EXPECT_TRUE(tome.found);
  ASSERT_TRUE(tome.issues.empty()) << tome.issues.front();
}

// Winner invariant: a mod override of a base cell must not be reported as missing its _OC NIFs, which sit under the winning plugin's folder.
TEST_F(PrevisLoadOrderTest, OcNifLookupFollowsTheWinningPlugin) {
  const auto base_cell = en::rec("CELL", 0x800);
  const auto mod_cell = en::rec("CELL", 0x800, {{"DATA", {std::byte{0x01}, std::byte{0x00}}}});
  const auto base_plugin =
      en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {base_cell})})})});
  const auto mod_plugin = en::make_plugin(
      {"Fallout4.esm"}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {mod_cell})})})});
  const std::filesystem::path paths[]{write("Fallout4.esm", base_plugin), write("Mod.esp", mod_plugin)};
  const auto env = fo4::env::Environment::load(paths);
  std::filesystem::create_directories(root_ / "Meshes" / "PreCombined" / "Mod.esp");
  write("Meshes/PreCombined/Mod.esp/00000800_00000050_OC.NIF", std::vector<std::byte>{std::byte{1}});
  std::filesystem::create_directories(root_ / "Meshes" / "PreCombined" / "Fallout4.esm");
  write("Meshes/PreCombined/Fallout4.esm/00000800_00000051_OC.NIF", std::vector<std::byte>{std::byte{1}});

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto base = env.find_plugin("Fallout4.esm");
  ASSERT_TRUE(base.has_value());
  const auto cell = fo4::FormKey{env.plugins()[*base].key, fo4::LocalFormId{0x800}};
  const std::uint32_t ids[]{0x50u, 0x51u};
  const auto missing = data.indexes().missing_oc_nifs(cell, ids);
  ASSERT_TRUE(missing.has_value());
  EXPECT_EQ(*missing, 0u);
  const std::uint32_t with_absent[]{0x50u, 0x51u, 0x52u};
  EXPECT_EQ(data.indexes().missing_oc_nifs(cell, with_absent).value_or(0u), 1u);
}

// Winner invariant: the geometry check must read the winning plugin's container when it and the Form-ID owner both hold the _OC NIF.
TEST_F(PrevisLoadOrderTest, CsgCheckFollowsTheWinningPlugin) {
  const auto base_cell = en::rec("CELL", 0x800);
  const auto mod_cell = en::rec("CELL", 0x800, {{"DATA", {std::byte{0x01}, std::byte{0x00}}}});
  const auto base_plugin =
      en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {base_cell})})})});
  const auto mod_plugin = en::make_plugin(
      {"Fallout4.esm"}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {mod_cell})})})});
  const std::filesystem::path paths[]{write("Fallout4.esm", base_plugin), write("Mod.esp", mod_plugin)};
  const auto env = fo4::env::Environment::load(paths);
  std::filesystem::create_directories(root_ / "Meshes" / "PreCombined" / "Mod.esp");
  write("Meshes/PreCombined/Mod.esp/00000800_00000050_OC.NIF", std::vector<std::byte>{std::byte{1}});
  std::filesystem::create_directories(root_ / "Meshes" / "PreCombined" / "Fallout4.esm");
  write("Meshes/PreCombined/Fallout4.esm/00000800_00000050_OC.NIF", std::vector<std::byte>{std::byte{1}});
  fo4::previs::CsgFile truncated;
  truncated.rows.push_back(fo4::previs::CsgRow{});
  truncated.chunks.push_back({std::byte{0x78}, std::byte{0x9C}, std::byte{0x03}, std::byte{0x00}});
  write("Mod - Geometry.csg", fo4::previs::write_csg(truncated));

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto base = env.find_plugin("Fallout4.esm");
  ASSERT_TRUE(base.has_value());
  const auto cell = fo4::FormKey{env.plugins()[*base].key, fo4::LocalFormId{0x800}};
  const std::uint32_t ids[]{0x50u};
  const auto answer = data.indexes().csg_rows(cell, ids);
  ASSERT_TRUE(answer.has_value());
  EXPECT_TRUE(answer->not_run);
  ASSERT_EQ(answer->issues.size(), 1u);
  EXPECT_NE(answer->issues.front().find("cannot be indexed"), std::string::npos);
}

// Invariant: an object stamped with a loaded plugin's file hash is judged against that plugin's container, wherever its _OC.NIF is stored.
TEST_F(PrevisLoadOrderTest, FolderlessOcNifIsCheckedAgainstTheContainerItsHashNames) {
  const auto answer = folderless_oc_answer(0u);
  ASSERT_TRUE(answer.has_value());
  EXPECT_TRUE(answer->checked);
  EXPECT_FALSE(answer->broken);
  EXPECT_FALSE(answer->not_run);
  EXPECT_TRUE(answer->issues.empty()) << answer->issues.front();
}

// Detects: a folderless _OC.NIF whose hashed container lacks the row must be broken, never clean and never a skip.
TEST_F(PrevisLoadOrderTest, FolderlessOcNifWithoutARowInItsHashedContainerIsBroken) {
  const auto answer = folderless_oc_answer(0x2000u);
  ASSERT_TRUE(answer.has_value());
  EXPECT_TRUE(answer->checked);
  EXPECT_TRUE(answer->broken);
  EXPECT_FALSE(answer->not_run);
  ASSERT_EQ(answer->issues.size(), 1u);
  EXPECT_NE(answer->issues.front().find("CSG has no row at stream offset 0 for OC id 80"), std::string::npos);
}

// Constraint: a tome that exists but cannot be read must not be reported as invalid, since the file may be fine.
TEST_F(PrevisLoadOrderTest, UnreadableTomeIsNotReportedInvalid) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{base_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto base = env.find_plugin("Fallout4.esm");
  ASSERT_TRUE(base.has_value());
  std::filesystem::create_directories(root_ / "Vis" / "Fallout4.esm");
  write("Vis/Fallout4.esm/00000802.uvd", make_uvd_bytes("T 512.0 SO 128.0"));

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  std::filesystem::remove(root_ / "Vis" / "Fallout4.esm" / "00000802.uvd");
  const auto tome =
      data.indexes().uvd(fo4::FormKey{env.plugins()[*base].key, fo4::LocalFormId{0x802}});
  EXPECT_TRUE(tome.found);
  EXPECT_TRUE(tome.read_failed);

  fo4::previs::CellPrevisInput input;
  input.has_visi = true;
  input.has_rvis = true;
  input.uvd_checked = true;
  input.has_uvd = true;
  input.uvd_unreadable = true;
  const auto status = fo4::previs::diagnose_cell(input);
  EXPECT_TRUE(status.broken);
  EXPECT_NE(find_issue(status.issues, fo4::previs::PrevisIssue::tome_unreadable), nullptr);
  EXPECT_EQ(find_issue(status.issues, fo4::previs::PrevisIssue::tome_invalid), nullptr);
}

TEST_F(PrevisLoadOrderTest, CaseInsensitiveLooseVisAndPrecombinedFolders) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const auto patch_path = write("Patch.esp", en::make_plugin({"Fallout4.esm"}, {}));
  const std::filesystem::path paths[]{base_path, patch_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto patch = env.find_plugin("Patch.esp");
  ASSERT_TRUE(patch.has_value());

  const auto tome = make_uvd_bytes("T 512.0 SO 128.0");
  std::filesystem::create_directories(root_ / "vis" / "Patch.esp");
  write("vis/Patch.esp/00000802.uvd", tome);
  std::filesystem::create_directories(root_ / "MESHeS" / "precombined" / "Patch.esp");
  write("MESHeS/precombined/Patch.esp/00000802_00000060_OC.NIF", oc_nif_stamped_with(0u));

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto indexes = data.indexes();
  const auto patch_cell = fo4::FormKey{env.plugins()[*patch].key, fo4::LocalFormId{0x802}};
  const auto tome_answer = indexes.uvd(patch_cell);
  EXPECT_TRUE(tome_answer.checked);
  EXPECT_TRUE(tome_answer.found);
  ASSERT_TRUE(tome_answer.issues.empty()) << tome_answer.issues.front();

  const std::uint32_t ids[]{0x60u};
  const auto missing = indexes.missing_oc_nifs(patch_cell, ids);
  ASSERT_TRUE(missing.has_value());
  EXPECT_EQ(*missing, 0u);
}

TEST_F(PrevisLoadOrderTest, NamelessArchiveMakesAssetCoverageIncomplete) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{base_path};
  const auto env = fo4::env::Environment::load(paths);
  fo4::archive::Ba2GeneralArchive archive;
  archive.has_string_table = false;
  fo4::archive::Ba2GeneralEntry entry;
  entry.path = "Vis/Fallout4.esm/00000800.uvd";
  entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(make_uvd_bytes("T 512.0 SO 128.0")));
  archive.entries = {entry};
  fo4::archive::write_ba2_general_file(archive, root_ / "Fallout4 - Main.ba2");
  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto cell = fo4::FormKey{env.plugins().front().key, fo4::LocalFormId{0x800}};
  EXPECT_FALSE(data.indexes().uvd(cell).checked);
  const std::uint32_t ids[]{0x50u};
  EXPECT_FALSE(data.indexes().missing_oc_nifs(cell, ids).has_value());
}

TEST_F(PrevisLoadOrderTest, UnreadableGeneralArchiveMakesAssetCoverageIncomplete) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{base_path};
  const auto env = fo4::env::Environment::load(paths);
  const std::string header = std::string("BTDX\x01\x00\x00\x00", 8) + "GNRL";
  std::ofstream stream(root_ / "Fallout4 - Main.ba2", std::ios::binary);
  stream.write(header.data(), static_cast<std::streamsize>(header.size()));
  stream.close();
  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto cell = fo4::FormKey{env.plugins().front().key, fo4::LocalFormId{0x800}};
  EXPECT_FALSE(data.summary().problems.empty());
  EXPECT_FALSE(data.indexes().uvd(cell).checked);
  const std::uint32_t ids[]{0x50u};
  EXPECT_FALSE(data.indexes().missing_oc_nifs(cell, ids).has_value());
}

TEST_F(PrevisLoadOrderTest, SkipsNonGeneralArchiveEvenWhenItsContentsAreUnreadable) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{base_path};
  const auto env = fo4::env::Environment::load(paths);
  const std::string texture_header = std::string("BTDX\x01\x00\x00\x00", 8) + "DX10";
  std::ofstream stream(root_ / "Fallout4 - Textures.ba2", std::ios::binary);
  stream.write(texture_header.data(), static_cast<std::streamsize>(texture_header.size()));
  stream.close();

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  EXPECT_TRUE(data.summary().problems.empty());
}

// Constraint: an archive of a plugin that is not in the load order must not be mounted, or its entries make a cell look covered.
TEST_F(PrevisLoadOrderTest, ArchiveOfAnUnloadedPluginIsNotScanned) {
  const auto base_path = write("Fallout4.esm", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{base_path};
  const auto env = fo4::env::Environment::load(paths);
  write_tome_archive(root_ / "Unloaded.ba2", "Vis/Unloaded.esp/00000803.uvd",
                     make_uvd_bytes("T 512.0 SO 128.0"));
  write_tome_archive(root_ / "Fallout4 - Main.ba2", "Vis/Fallout4.esm/00000804.uvd",
                     make_uvd_bytes("T 512.0 SO 128.0"));

  const std::filesystem::path roots[]{root_};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  EXPECT_EQ(data.summary().tome_files, 1u);
}

// Regression: a malformed or duplicate RVIS must not make decode_cell throw for the water caller.
TEST_F(PrevisLoadOrderTest, MalformedRvisStaysTolerantForWater) {
  const auto odd_size = en::rec(
      "CELL", 0x800,
      {{"DATA", {std::byte{0x01}, std::byte{0x00}}},
       {"VISI", {std::byte{0x34}, std::byte{0x12}}},
       {"RVIS", {std::byte{0x01}}},
       {"RVIS", en::bytes_of({0x801})}});
  const auto duplicate = en::rec(
      "CELL", 0x801,
      {{"DATA", {std::byte{0x01}, std::byte{0x00}}},
       {"VISI", {std::byte{0x34}, std::byte{0x12}}},
       {"RVIS", en::bytes_of({0x802})},
       {"RVIS", en::bytes_of({0x803})}});
  const auto plugin_path = write(
      "Tolerant.esp",
      en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {odd_size, duplicate})})})}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto key = [&env](std::uint32_t local) {
    return fo4::FormKey{env.plugins()[0].key, fo4::LocalFormId{local}};
  };
  EXPECT_NO_THROW(fo4::env::resolve_water(env, *env.winner(key(0x800))));
  EXPECT_NO_THROW(fo4::env::resolve_water(env, *env.winner(key(0x801))));

  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  for (const auto local : {0x800u, 0x801u}) {
    const auto* entry = find_cell(report, local);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->input.has_rvis);
    EXPECT_TRUE(entry->status.broken);
    EXPECT_NE(find_issue(entry->status.issues, fo4::previs::PrevisIssue::rvis_malformed), nullptr);
  }
}

// Detects: a malformed RVIS decoding as an absent root, which hid a broken cell; malformed must report broken and absent must not.
TEST_F(PrevisLoadOrderTest, MalformedRvisIsBrokenWhileMissingRvisIsNot) {
  const auto malformed = en::rec("CELL", 0x800,
                                 {{"DATA", {std::byte{0x01}, std::byte{0x00}}},
                                  {"RVIS", {std::byte{0x01}}}});
  const auto missing = en::rec("CELL", 0x801, {{"DATA", {std::byte{0x01}, std::byte{0x00}}}});
  const auto plugin_path = write(
      "Rvis.esp",
      en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {malformed, missing})})})}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);

  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  ASSERT_EQ(report.size(), 2u);
  const auto* bad = find_cell(report, 0x800u);
  ASSERT_NE(bad, nullptr);
  EXPECT_TRUE(bad->status.broken);
  EXPECT_NE(find_issue(bad->status.issues, fo4::previs::PrevisIssue::rvis_malformed), nullptr);
  const auto* absent = find_cell(report, 0x801u);
  ASSERT_NE(absent, nullptr);
  EXPECT_FALSE(absent->status.broken);
  EXPECT_EQ(find_issue(absent->status.issues, fo4::previs::PrevisIssue::rvis_malformed), nullptr);
}

namespace {

std::vector<std::byte> make_xcri_version_plugin() {
  fo4::test::ByteWriter xcri;
  xcri.u32(1);
  xcri.u32(2);
  xcri.u32(0x55);
  xcri.u32(0x810);
  xcri.u32(0x56);
  const auto ok = en::rec("CELL", 0x900,
                          {{"DATA", {std::byte{0x01}, std::byte{0}}},
                           {"PCMB", {std::byte{0x34}, std::byte{0x12}}},
                           {"XCRI", xcri.bytes}});
  const auto malformed = en::rec("CELL", 0x901,
                                 {{"DATA", {std::byte{0x01}, std::byte{0}}},
                                  {"PCMB", {std::byte{0x34}, std::byte{0x12}}},
                                  {"XCRI", bytes_of({1, 1, 0x55, 0x01})}});
  const auto unmeasured = en::rec("CELL", 0x902,
                                  {{"DATA", {std::byte{0x01}, std::byte{0}}},
                                   {"PCMB", {std::byte{0x34}, std::byte{0x12}}},
                                   {"XCRI", xcri.bytes}},
                                  0, 0x71);
  return en::make_plugin({}, {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {ok, malformed, unmeasured})})})});
}

}  // namespace

// A malformed XCRI in a mod must not fail the whole cell read and must be reported.
TEST_F(PrevisLoadOrderTest, MalformedXcriReportedBroken) {
  const std::filesystem::path paths[] = {write("PrevisXcriVer.esp", make_xcri_version_plugin())};
  const auto env = fo4::env::Environment::load(paths);
  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  const auto* bad = find_cell(report, 0x901u);
  ASSERT_NE(bad, nullptr);
  EXPECT_TRUE(bad->status.broken);
  EXPECT_NE(find_issue(bad->status.issues, fo4::previs::PrevisIssue::xcri_malformed), nullptr);
  const auto* ok = find_cell(report, 0x900u);
  ASSERT_NE(ok, nullptr);
  EXPECT_FALSE(ok->status.broken);
}

// An XCRI below the measured form version must skip, never parse or condemn.
TEST_F(PrevisLoadOrderTest, UnmeasuredXcriVersionSkips) {
  const std::filesystem::path paths[] = {write("PrevisXcriVer.esp", make_xcri_version_plugin())};
  const auto env = fo4::env::Environment::load(paths);
  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  const auto* entry = find_cell(report, 0x902u);
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->input.has_xcri);
  EXPECT_TRUE(entry->input.xcri_unmeasured_version);
  EXPECT_FALSE(entry->status.broken);
  EXPECT_TRUE(entry->status.skip_unsupported);
  ASSERT_FALSE(entry->status.skips.empty());
  EXPECT_EQ(entry->status.skips.front(), "XCRI form version not measured");
}
