#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/error.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace en = fo4::test::envnode;

std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  std::vector<std::byte> out;
  for (const auto v : words) {
    out.push_back(static_cast<std::byte>(v & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
  }
  return out;
}

std::vector<std::byte> make_uvd_bytes(const std::string& banner, std::size_t size = 0x110) {
  // The banner lives at +0xBC; the image must stay 16-byte aligned.
  if (size < 0x100) size = 0x100;
  if (0xBC + banner.size() + 1 > size) size = 0xBC + banner.size() + 1;
  size = (size + 15) & ~std::size_t{15};
  std::vector<std::byte> out(size, std::byte{0});
  // Byte offset 0 must hold the LE u32 magic 0xD6000012.
  out[0] = static_cast<std::byte>(0x12);
  out[1] = static_cast<std::byte>(0x00);
  out[2] = static_cast<std::byte>(0x00);
  out[3] = static_cast<std::byte>(0xD6);
  std::memcpy(out.data() + 0xBC, banner.data(), banner.size() + 1);
  const std::span<const std::byte> body(out.data() + 8, out.size() - 8);
  const auto crc = fo4::previs::crc32c(body);
  out[4] = static_cast<std::byte>(crc & 0xFFu);
  out[5] = static_cast<std::byte>((crc >> 8) & 0xFFu);
  out[6] = static_cast<std::byte>((crc >> 16) & 0xFFu);
  out[7] = static_cast<std::byte>((crc >> 24) & 0xFFu);
  return out;
}

const fo4::previs::CellIssue* find_issue(const std::vector<fo4::previs::CellIssue>& issues,
                                         fo4::previs::PrevisIssue code) {
  for (const auto& issue : issues) {
    if (issue.code == code) return &issue;
  }
  return nullptr;
}

}  // namespace

// Sorted rows are an invariant: an unsorted rewrite would detach a cell's art.
TEST(PrevisCdx, RoundTripsAndFlagsUnsorted) {
  fo4::previs::CdxFile file;
  file.rows.push_back({0x00000080u, 0x000800u, 2, 3});
  file.rows.push_back({0x00000080u, 0x000801u, 4, 5});
  const auto bytes = fo4::previs::write_cdx(file);
  EXPECT_TRUE(fo4::previs::validate_cdx(bytes).empty());
  const auto back = fo4::previs::parse_cdx(bytes);
  ASSERT_EQ(back.rows.size(), 2u);
  EXPECT_EQ(back.rows[0].form, 0x000800u);
  EXPECT_EQ(fo4::previs::write_cdx(back), bytes);

  fo4::previs::CdxIndex index(back);
  EXPECT_TRUE(index.has_interior_row(0x000800u));
  EXPECT_TRUE(index.has_interior_row(0x000801u));
  EXPECT_FALSE(index.has_interior_row(0x000802u));

  fo4::previs::CdxFile flipped;
  flipped.rows.push_back({0x00000080u, 0x000800u, 4, 5});
  flipped.rows.push_back({0x00000000u, 0x000800u, 2, 3});
  EXPECT_FALSE(fo4::previs::validate_cdx(fo4::previs::write_cdx(flipped)).empty());

  auto truncated = bytes;
  truncated.pop_back();
  EXPECT_THROW(fo4::previs::parse_cdx(truncated), fo4::parse_error);
}

// Exterior grids must survive signed 12-bit encode and decode; an interior row
// must keep its grid zero or the measured layout is violated.
TEST(PrevisCdx, DecodesSignedExteriorGrid) {
  const fo4::previs::CdxGrid grids[] = {{-35, -39}, {30, 28}, {0, 0}, {-1, 1}};
  for (const auto& grid : grids) {
    EXPECT_EQ(fo4::previs::decode_cdx_grid(fo4::previs::encode_cdx_grid(grid)), grid);
  }
  fo4::previs::CdxFile file;
  const auto world = 0x0000003Cu;
  const auto key = (fo4::previs::encode_cdx_grid({-35, 28}) << 8u) | 0u;
  file.rows.push_back({key, world, 1, 2});
  const auto bytes = fo4::previs::write_cdx(file);
  EXPECT_TRUE(fo4::previs::validate_cdx(bytes).empty());
  fo4::previs::CdxIndex index(fo4::previs::parse_cdx(bytes));
  EXPECT_TRUE(index.has_exterior_row(world, {-35, 28}));
  EXPECT_FALSE(index.has_exterior_row(world, {-35, 27}));

  fo4::previs::CdxFile bad_interior;
  bad_interior.rows.push_back({0x00000180u, 0x000800u, 0, 0});
  EXPECT_FALSE(fo4::previs::validate_cdx(fo4::previs::write_cdx(bad_interior)).empty());
}

// Detects a table edit that moves a chunk size or offset: parse must fail
// must fail closed instead of slicing out of bounds and corrupting geometry.
TEST(PrevisCsg, RoundTripsAndRejectsBadChunk) {
  fo4::previs::CsgFile file;
  fo4::previs::CsgRow row{};
  row.bytes[0] = std::byte{0x7A};
  file.rows.push_back(row);
  file.chunks.push_back(std::vector<std::byte>{std::byte{0x78}, std::byte{0x9C}, std::byte{0x03},
                                                std::byte{0x00}});
  file.chunks.push_back(std::vector<std::byte>{std::byte{0x78}, std::byte{0x9C}, std::byte{0x05},
                                                std::byte{0x00}});
  const auto bytes = fo4::previs::write_csg(file);
  EXPECT_TRUE(fo4::previs::validate_csg(bytes).empty());
  const auto back = fo4::previs::parse_csg(bytes);
  ASSERT_EQ(back.rows.size(), 1u);
  ASSERT_EQ(back.chunks.size(), 2u);
  EXPECT_EQ(fo4::previs::write_csg(back), bytes);

  // Entry order invariant: the size comes before the offset.
  EXPECT_EQ(std::to_integer<unsigned>(bytes[12]), 4u);

  auto big_size = bytes;
  // Corrupt the first entry's size; the chunk offset must then run past the file end.
  big_size[12] = std::byte{0xFF};
  big_size[13] = std::byte{0xFF};
  big_size[14] = std::byte{0xFF};
  big_size[15] = std::byte{0x7F};
  EXPECT_FALSE(fo4::previs::validate_csg(big_size).empty());

  auto moved_offset = bytes;
  // Second entry's offset no longer continues the first chunk.
  moved_offset[24] = std::byte{0x02};
  EXPECT_FALSE(fo4::previs::validate_csg(moved_offset).empty());

  auto magic = bytes;
  magic[0] = std::byte{'x'};
  EXPECT_FALSE(fo4::previs::validate_csg(magic).empty());
}

// A tiny chunk must stay tiny: a wrong cap of roughly 55 MB per chunk would
// must not allocate before the inflater proves the stream complete.
TEST(PrevisCsg, DecompressesOnlyCompleteChunks) {
  const auto good = std::vector<std::byte>{std::byte{0x78}, std::byte{0x9C}, std::byte{0x03},
                                           std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                           std::byte{0x00}, std::byte{0x01}};
  EXPECT_EQ(fo4::previs::decompress_csg_chunk(good).size(), 0u);
  auto truncated = good;
  truncated.pop_back();
  EXPECT_THROW(fo4::previs::decompress_csg_chunk(truncated), fo4::parse_error);
}

// A tome edit that breaks the CRC or alignment must be flagged: the game would
// ignore or reject the tome, so the validator must flag it before release.
TEST(PrevisUvd, ValidatesCrcAlignmentAndBanner) {
  const auto bytes = make_uvd_bytes("T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0");
  EXPECT_TRUE(fo4::previs::validate_uvd(bytes).empty());
  const auto file = fo4::previs::parse_uvd(bytes);
  EXPECT_NE(file.banner.find("3.3.17"), std::string::npos);
  EXPECT_EQ(file.body().size(), bytes.size() - 8);

  auto crc_bad = bytes;
  crc_bad[20] = static_cast<std::byte>(std::to_integer<int>(crc_bad[20]) ^ 0xFF);
  EXPECT_FALSE(fo4::previs::validate_uvd(crc_bad).empty());

  auto magic_bad = bytes;
  magic_bad[0] = std::byte{0x00};
  EXPECT_FALSE(fo4::previs::validate_uvd(magic_bad).empty());
}

// Word pairs are an invariant: 8-byte pairs would split REFR ids from OC ids.
TEST(PrevisXcri, ParsesMeasuredWordPairs) {
  const auto bytes = bytes_of({1, 2, 0x55, 0x00013002u, 0x56});
  const auto raw = fo4::previs::parse_xcri_bytes(bytes);
  ASSERT_EQ(raw.mesh_ids.size(), 1u);
  EXPECT_EQ(raw.mesh_ids[0], 0x55u);
  ASSERT_EQ(raw.refs.size(), 1u);
  EXPECT_EQ(raw.refs[0].first, 0x00013002u);
  EXPECT_EQ(raw.refs[0].second, 0x56u);

  EXPECT_THROW(fo4::previs::parse_xcri_bytes(bytes_of({1, 1, 0x55, 0x01})), fo4::parse_error);
  EXPECT_THROW(fo4::previs::parse_xcri_bytes(bytes_of({1, 2, 0x55})), fo4::parse_error);
}

// Mesh/reference order is an invariant: the round trip pins the exact byte layout.
TEST(PrevisXcri, WriteRoundTrips) {
  const auto bytes = bytes_of({1, 2, 0x55, 0x00013002u, 0x56});
  const auto raw = fo4::previs::parse_xcri_bytes(bytes);
  EXPECT_EQ(fo4::previs::write_xcri_bytes(raw), bytes);
  EXPECT_EQ(fo4::previs::write_xcri_bytes(fo4::previs::XcriRaw{}), bytes_of({0, 0}));
}

TEST(PrevisDate, DecodesPackedStampAndStaleness) {
  const auto date = fo4::previs::decode_packed_date(0x306Fu);
  EXPECT_EQ(date.year, 2024u);
  EXPECT_EQ(date.month, 3u);
  EXPECT_EQ(date.day, 15u);
  EXPECT_TRUE(fo4::previs::is_precalc_stale(0x306Fu, 0x3070u));
  EXPECT_FALSE(fo4::previs::is_precalc_stale(0x3070u, 0x306Fu));
  EXPECT_FALSE(fo4::previs::is_precalc_stale(0, 0x3070u));
}

// A repair patch that stamps VISI without a tome must be broken: it falls back
// to individual references in game; staleness must be outdated, never broken.
TEST(PrevisDetect, ClassifiesBrokenAndOutdated) {
  fo4::previs::CellPrevisInput clean{};
  EXPECT_FALSE(fo4::previs::diagnose_cell(clean).broken);

  fo4::previs::CellPrevisInput tome_missing{};
  tome_missing.has_visi = true;
  tome_missing.has_rvis = true;
  tome_missing.uvd_checked = true;
  const auto missing = fo4::previs::diagnose_cell(tome_missing);
  EXPECT_TRUE(missing.broken);
  EXPECT_NE(find_issue(missing.issues, fo4::previs::PrevisIssue::tome_missing), nullptr);

  // VISI without RVIS requires the cell-named tome, which this input supplies.
  fo4::previs::CellPrevisInput visi_no_rvis{};
  visi_no_rvis.has_visi = true;
  visi_no_rvis.uvd_checked = true;
  visi_no_rvis.has_uvd = true;
  visi_no_rvis.uvd_valid = true;
  EXPECT_FALSE(fo4::previs::diagnose_cell(visi_no_rvis).broken);

  // Without RVIS a missing cell-named tome must stay unsupported, never proven broken.
  fo4::previs::CellPrevisInput visi_no_rvis_missing{};
  visi_no_rvis_missing.has_visi = true;
  visi_no_rvis_missing.uvd_checked = true;
  const auto unsupported = fo4::previs::diagnose_cell(visi_no_rvis_missing);
  EXPECT_FALSE(unsupported.broken);
  EXPECT_TRUE(unsupported.skip_unsupported);

  fo4::previs::CellPrevisInput rvis_no_visi{};
  rvis_no_visi.has_rvis = true;
  const auto no_visi = fo4::previs::diagnose_cell(rvis_no_visi);
  EXPECT_TRUE(no_visi.broken);
  EXPECT_NE(find_issue(no_visi.issues, fo4::previs::PrevisIssue::rvis_without_visi), nullptr);

  fo4::previs::CellPrevisInput stale{};
  stale.has_pcmb = true;
  stale.pcmb_stale = true;
  stale.visi_stale = true;
  const auto outdated = fo4::previs::diagnose_cell(stale);
  EXPECT_FALSE(outdated.broken);
  EXPECT_TRUE(outdated.outdated);
  EXPECT_EQ(outdated.outdated_issues.size(), 2u);
}

// A precombined cell requires XCRI, CDX rows, CSG rows and every OC NIF.
TEST(PrevisDetect, ClassifiesPrecombineLinkage) {
  fo4::previs::CellPrevisInput complete{};
  complete.has_pcmb = true;
  complete.has_xcri = true;
  complete.cdx_checked = true;
  complete.has_cdx_row = true;
  complete.csg_checked = true;
  complete.has_csg_rows = true;
  complete.oc_nif_checked = true;
  EXPECT_FALSE(fo4::previs::diagnose_cell(complete).broken);

  auto no_cdx = complete;
  no_cdx.has_cdx_row = false;
  EXPECT_TRUE(fo4::previs::diagnose_cell(no_cdx).broken);

  auto no_csg = complete;
  no_csg.has_csg_rows = false;
  EXPECT_TRUE(fo4::previs::diagnose_cell(no_csg).broken);

  auto no_nif = complete;
  no_nif.missing_oc_nifs = 3;
  const auto missing = fo4::previs::diagnose_cell(no_nif);
  EXPECT_TRUE(missing.broken);
  EXPECT_NE(find_issue(missing.issues, fo4::previs::PrevisIssue::oc_nif_missing), nullptr);

  // PCMB without XCRI is legal in vanilla and must not be broken.
  fo4::previs::CellPrevisInput pcmb_only{};
  pcmb_only.has_pcmb = true;
  pcmb_only.cdx_checked = true;
  pcmb_only.csg_checked = true;
  pcmb_only.uvd_checked = true;
  EXPECT_FALSE(fo4::previs::diagnose_cell(pcmb_only).broken);
}

// Unchecked indexes require skips: no false missing-row or missing-tome verdicts.
TEST(PrevisDetect, UncheckedIndexesBecomeSkipsNotBroken) {
  fo4::previs::CellPrevisInput visi{};
  visi.has_visi = true;
  visi.has_rvis = true;
  const auto unvis = fo4::previs::diagnose_cell(visi);
  EXPECT_FALSE(unvis.broken);
  EXPECT_TRUE(unvis.skip_unsupported);
  ASSERT_FALSE(unvis.skips.empty());

  fo4::previs::CellPrevisInput xcri{};
  xcri.has_pcmb = true;
  xcri.has_xcri = true;
  const auto unx = fo4::previs::diagnose_cell(xcri);
  EXPECT_FALSE(unx.broken);
  EXPECT_TRUE(unx.skip_unsupported);
  EXPECT_EQ(unx.skips.size(), 3u);  // CDX + CSG + OC NIF
}

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

  std::filesystem::path root_;
};

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
  indexes.csg_rows = [](std::size_t, const fo4::previs::FormKey& cell) {
    return std::optional<bool>{cell.local().value() == 0x801u};
  };
  indexes.missing_oc_nifs = [](const fo4::previs::FormKey&,
                               std::span<const std::uint32_t>) {
    return std::optional<std::uint32_t>{0u};
  };
  indexes.uvd = [](const fo4::previs::FormKey& cell) {
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
  EXPECT_TRUE(pcmb->input.has_csg_rows);
  EXPECT_FALSE(pcmb->status.broken);
  EXPECT_FALSE(pcmb->status.skip_unsupported);

  const auto* bad = find_cell(report, 0x802u);
  ASSERT_NE(bad, nullptr);
  EXPECT_TRUE(bad->status.broken);
  EXPECT_NE(find_issue(bad->status.issues, fo4::previs::PrevisIssue::xcri_malformed), nullptr);
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
