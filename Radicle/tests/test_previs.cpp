#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/archive/ba2.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/error.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
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

// A table edit that moves a chunk size or offset must fail closed instead of slicing past the file.
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

// A tiny chunk must stay tiny: the inflater must not allocate before it proves the stream complete.
TEST(PrevisCsg, DecompressesOnlyCompleteChunks) {
  const auto good = std::vector<std::byte>{std::byte{0x78}, std::byte{0x9C}, std::byte{0x03},
                                           std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                           std::byte{0x00}, std::byte{0x01}};
  EXPECT_EQ(fo4::previs::decompress_csg_chunk(good).size(), 0u);
  auto truncated = good;
  truncated.pop_back();
  EXPECT_THROW(fo4::previs::decompress_csg_chunk(truncated), fo4::parse_error);
}

// Regression: a hostile chunk count must fail on the table bound, not reserve gigabytes.
TEST(PrevisCsg, RejectsHugeChunkCountBeforeAllocating) {
  std::vector<std::byte> bytes(12, std::byte{0});
  bytes[0] = std::byte{'b'};
  bytes[1] = std::byte{'c'};
  bytes[2] = std::byte{'s'};
  bytes[3] = std::byte{'g'};
  bytes[8] = std::byte{0xFF};
  bytes[9] = std::byte{0xFF};
  bytes[10] = std::byte{0xFF};
  bytes[11] = std::byte{0xFF};
  EXPECT_THROW(fo4::previs::parse_csg(bytes), fo4::parse_error);
  EXPECT_FALSE(fo4::previs::validate_csg(bytes).empty());
}

// Regression: a stream that inflates past the 64 KiB chunk cap must throw, not truncate.
TEST(PrevisCsg, RejectsInflateOverTheChunkCap) {
  const std::vector<std::byte> oversized(65537, std::byte{0});
  const auto chunk = fo4::test::zlib_compress(oversized);
  EXPECT_THROW(fo4::previs::decompress_csg_chunk(chunk), fo4::parse_error);
}

// Regression: a huge XCRI count must fail on the size comparison, before any reserve.
TEST(PrevisXcri, RejectsHugeCountsBeforeAllocating) {
  const auto bytes = bytes_of({0xFFFFFFFFu, 0xFFFFFFFEu});
  EXPECT_THROW(fo4::previs::parse_xcri_bytes(bytes), fo4::parse_error);
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

// Regression: a banner with no NUL must be rejected instead of reading past the image.
TEST(PrevisUvd, RejectsUnterminatedBanner) {
  auto bytes = make_uvd_bytes("T 512.0");
  std::fill(bytes.begin() + 0xBC, bytes.end(), std::byte{'A'});
  const std::span<const std::byte> body(bytes.data() + 8, bytes.size() - 8);
  const auto crc = fo4::previs::crc32c(body);
  bytes[4] = static_cast<std::byte>(crc & 0xFFu);
  bytes[5] = static_cast<std::byte>((crc >> 8) & 0xFFu);
  bytes[6] = static_cast<std::byte>((crc >> 16) & 0xFFu);
  bytes[7] = static_cast<std::byte>((crc >> 24) & 0xFFu);
  EXPECT_THROW(fo4::previs::parse_uvd(bytes), fo4::parse_error);
  EXPECT_FALSE(fo4::previs::validate_uvd(bytes).empty());
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

// A precombined cell requires XCRI, a CDX row and every OC NIF; CSG is a per-plugin check only.
TEST(PrevisDetect, ClassifiesPrecombineLinkage) {
  fo4::previs::CellPrevisInput complete{};
  complete.has_pcmb = true;
  complete.has_xcri = true;
  complete.cdx_checked = true;
  complete.has_cdx_row = true;
  complete.oc_nif_checked = true;
  EXPECT_FALSE(fo4::previs::diagnose_cell(complete).broken);

  auto no_cdx = complete;
  no_cdx.has_cdx_row = false;
  EXPECT_TRUE(fo4::previs::diagnose_cell(no_cdx).broken);

  auto no_nif = complete;
  no_nif.missing_oc_nifs = 3;
  const auto missing = fo4::previs::diagnose_cell(no_nif);
  EXPECT_TRUE(missing.broken);
  EXPECT_NE(find_issue(missing.issues, fo4::previs::PrevisIssue::oc_nif_missing), nullptr);

  // PCMB without XCRI is legal in vanilla and must not be broken.
  fo4::previs::CellPrevisInput pcmb_only{};
  pcmb_only.has_pcmb = true;
  pcmb_only.cdx_checked = true;
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
  EXPECT_EQ(unx.skips.size(), 2u);  // CDX + OC NIF
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
  const auto plugin_path = write("Patch.esp", en::make_plugin({}, {}));
  const std::filesystem::path paths[]{plugin_path};
  const auto env = fo4::env::Environment::load(paths);
  const auto plugin = env.find_plugin("Patch.esp");
  ASSERT_TRUE(plugin.has_value());
  const auto cell = fo4::FormKey{env.plugins()[*plugin].key, fo4::LocalFormId{0x800}};

  fo4::previs::CdxFile low_cdx;
  fo4::previs::CdxFile high_cdx;
  high_cdx.rows.push_back({0x80u, 0x00000800u, 0u, 0u});
  put(low, "Patch.cdx", fo4::previs::write_cdx(low_cdx));
  put(high, "Patch.cdx", fo4::previs::write_cdx(high_cdx));

  fo4::previs::CsgFile good_csg;
  good_csg.rows.push_back(fo4::previs::CsgRow{});
  good_csg.chunks.push_back({std::byte{0x78}, std::byte{0x9C}, std::byte{0x03}, std::byte{0x00}});
  auto bad_csg = fo4::previs::write_csg(good_csg);
  bad_csg[0] = std::byte{'x'};
  put(low, "Patch - Geometry.csg", bad_csg);
  put(high, "Patch - Geometry.csg", fo4::previs::write_csg(good_csg));

  auto bad_tome = make_uvd_bytes("T 512.0 SO 128.0");
  bad_tome[4] = static_cast<std::byte>(std::to_integer<int>(bad_tome[4]) ^ 0xFF);
  put(low, "Vis\\Patch.esp\\00000800.uvd", bad_tome);
  put(high, "Vis\\Patch.esp\\00000800.uvd", make_uvd_bytes("T 512.0 SO 128.0"));
  put(high, "Meshes\\PreCombined\\Patch.esp\\00000800_00000050_OC.nif",
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
  EXPECT_TRUE(indexes.plugin_ships_previs(*plugin));
  ASSERT_TRUE(data.summary().problems.empty()) << data.summary().problems.front();
}

// Regression: a clean-mode .psg must fail the CSG container check closed, never pass as geometry.
TEST_F(PrevisLoadOrderTest, PsgFailsClosed) {
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
    EXPECT_FALSE(entry->status.broken);
  }
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
