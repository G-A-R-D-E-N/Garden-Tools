#include "fo4/error.hpp"
#include "fo4/previs/bake_oc.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/stamp.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/plugin/reader.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "previs_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

using fo4::test::previs::bytes_of;
using fo4::test::previs::find_issue;
using fo4::test::previs::make_uvd_bytes;

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

// Detects: an issue code added to PrevisIssue without a name, which the panel would show as unknown; every code must have a name.
TEST(PrevisDetect, EveryIssueCodeHasAName) {
  for (std::int32_t code = 0; code < fo4::previs::k_previs_issue_count; ++code) {
    const auto name = fo4::previs::to_string(static_cast<fo4::previs::PrevisIssue>(code));
    EXPECT_FALSE(name.empty()) << code;
    EXPECT_NE(name, "unknown") << code;
  }
}

// Regression: native UVD writer must preserve magic, alignment, banner and CRC-32C.
TEST(PrevisUvd, WriteRoundTripsThroughValidate) {
  const auto raw = make_uvd_bytes("TUMBRA umbrella compiler 3.3.17", 0x120);
  fo4::previs::UvdWriteRequest req{.banner = "TUMBRA umbrella compiler 3.3.17",
                                   .payload = std::span<const std::byte>(raw).subspan(8)};
  const auto out = fo4::previs::write_uvd(req);
  ASSERT_GE(out.size(), fo4::previs::k_uvd_header_min);
  EXPECT_TRUE(fo4::previs::validate_uvd(out).empty());
  EXPECT_EQ(fo4::previs::parse_uvd(out).banner, "TUMBRA umbrella compiler 3.3.17");
}

// Regression: stamping PCMB/VISI/XCRI must round-trip through the CELL parser.
TEST(PrevisStamp, WritesPrevisSubrecords) {
  namespace en = fo4::test::envnode;
  const auto bytes = en::make_plugin(
      {},
      {en::top("CELL", {en::grp(0, 2, {en::grp(0, 3, {en::rec(
                               "CELL", 0x801,
                               {{"EDID", en::zstring("TestCell")},
                                {"DATA", {std::byte{0x01}, std::byte{0}}}})})})})});
  auto plugin = fo4::read_plugin(bytes);
  const fo4::FormKey cell{plugin.key, fo4::LocalFormId{0x801u}};
  fo4::previs::StampRequest req{.pcmb_stamp = 0x5678,
                                .visi_stamp = 0x1234,
                                .xcri = fo4::previs::XcriRaw{.mesh_ids = {1}, .refs = {{0x55, 1}}}};
  ASSERT_EQ(fo4::previs::stamp_cell_previs(plugin, cell, req), fo4::previs::StampStatus::ok);
  const auto* record = fo4::find_record(plugin, cell);
  ASSERT_NE(record, nullptr);
  const auto sig_pcmb = fo4::Signature::from_string("PCMB");
  const auto sig_visi = fo4::Signature::from_string("VISI");
  const auto sig_xcri = fo4::Signature::from_string("XCRI");
  const fo4::Subrecord* pcmb = nullptr;
  const fo4::Subrecord* visi = nullptr;
  const fo4::Subrecord* xcri = nullptr;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == sig_pcmb) pcmb = &sub;
    if (sub.signature == sig_visi) visi = &sub;
    if (sub.signature == sig_xcri) xcri = &sub;
  }
  ASSERT_NE(pcmb, nullptr);
  ASSERT_NE(visi, nullptr);
  ASSERT_NE(xcri, nullptr);
  ASSERT_EQ(pcmb->data.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(pcmb->data[0]), 0x78);
  EXPECT_EQ(std::to_integer<std::uint8_t>(pcmb->data[1]), 0x56);
  ASSERT_EQ(visi->data.size(), 2u);
  EXPECT_EQ(fo4::previs::parse_xcri_bytes(xcri->data).mesh_ids, req.xcri.mesh_ids);
}

// Regression: bake must refuse sources that are not shipped-style _OC.NIF (Shared geom extra).
TEST(PrevisBake, RejectsNonOcSource) {
  const std::vector<std::byte> source{std::byte{0x4E}, std::byte{0x49}, std::byte{0x46},
                                      std::byte{0x20}, std::byte{0x01}};
  const auto out = std::filesystem::temp_directory_path() / "previs_bake_reject" / "x_OC.NIF";
  EXPECT_EQ(fo4::previs::bake_oc_single_source_bytes(source, out),
            fo4::previs::BakeOcStatus::unsupported);
}

