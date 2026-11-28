#include "fo4/error.hpp"
#include "fo4/io/sha256.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "fo4/previs/bake_oc.hpp"
#include "fo4/previs/oc_nif_rewrite.hpp"
#include "fo4/previs/shared_geom_extra.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/previs/precombine_plan.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/stamp.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/uvd_tome.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/flags.hpp"
#include "fo4/plugin/reader.hpp"
#include "env_node_fixture.hpp"
#include "nif_build.hpp"
#include "plugin_bytes.hpp"
#include "previs_test_support.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using fo4::test::previs::bytes_of;
using fo4::test::TempDataDir;
using fo4::test::previs::find_issue;
using fo4::test::previs::make_uvd_bytes;

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::vector<std::byte> out(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    out[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return out;
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

// Exterior grids must survive signed 12-bit encode and decode; an interior row must keep its grid zero or the measured layout is violated.
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

// A tome edit that breaks the CRC or alignment must be flagged: the game would ignore or reject the tome, so the validator must flag it before release.
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

// A repair patch that stamps VISI without a tome must be broken: it falls back to individual references in game; staleness must be outdated, never broken.
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
                                .xcri = fo4::previs::XcriRaw{.mesh_ids = {1}, .refs = {{0x55, 1}}},
                                .rvis_root = fo4::RawFormId{0x00000801u}};
  ASSERT_EQ(fo4::previs::stamp_cell_previs(plugin, cell, req), fo4::previs::StampStatus::ok);
  const auto* record = fo4::find_record(plugin, cell);
  ASSERT_NE(record, nullptr);
  const auto sig_pcmb = fo4::Signature::from_string("PCMB");
  const auto sig_visi = fo4::Signature::from_string("VISI");
  const auto sig_xcri = fo4::Signature::from_string("XCRI");
  const auto sig_rvis = fo4::Signature::from_string("RVIS");
  const fo4::Subrecord* pcmb = nullptr;
  const fo4::Subrecord* visi = nullptr;
  const fo4::Subrecord* xcri = nullptr;
  const fo4::Subrecord* rvis = nullptr;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == sig_pcmb) pcmb = &sub;
    if (sub.signature == sig_visi) visi = &sub;
    if (sub.signature == sig_xcri) xcri = &sub;
    if (sub.signature == sig_rvis) rvis = &sub;
  }
  // Detects: a 2-byte RVIS, which the scanner reports as rvis_malformed; the root must be a 4-byte Form-ID.
  ASSERT_NE(rvis, nullptr);
  ASSERT_EQ(rvis->data.size(), 4u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(rvis->data[0]), 0x01);
  EXPECT_EQ(std::to_integer<std::uint8_t>(rvis->data[1]), 0x08);
  EXPECT_EQ(std::to_integer<std::uint8_t>(rvis->data[2]), 0x00);
  EXPECT_EQ(std::to_integer<std::uint8_t>(rvis->data[3]), 0x00);
  ASSERT_NE(pcmb, nullptr);
  ASSERT_NE(visi, nullptr);
  ASSERT_NE(xcri, nullptr);
  ASSERT_EQ(pcmb->data.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(pcmb->data[0]), 0x78);
  EXPECT_EQ(std::to_integer<std::uint8_t>(pcmb->data[1]), 0x56);
  ASSERT_EQ(visi->data.size(), 2u);
  EXPECT_EQ(fo4::previs::parse_xcri_bytes(xcri->data).mesh_ids, req.xcri.mesh_ids);
}

// Regression: bake must refuse sources that are not shipped-style _OC.NIF (Shared geom extra). Regression: interior STAT groups must be eligible for native precombine planning.
TEST(PrevisPlan, GroupsInteriorStatInstances) {
  const auto k_cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
  const auto k_cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
  const auto bytes = fo4::test::envnode::make_plugin(
      {},
      {fo4::test::envnode::top(
           "STAT",
           {fo4::test::envnode::rec(
                "STAT", 0x810u, {{.signature = "MODL", .data = fo4::test::envnode::zstring("Meshes\\Rock.nif")}})}),
       fo4::test::envnode::top(
           "CELL",
           {fo4::test::envnode::grp(3u, 2, {
                fo4::test::envnode::grp(0u, 3, {
                    fo4::test::envnode::rec("CELL", 0x801u,
                                            {{.signature = "DATA", .data = {std::byte{1}}}}),
                    fo4::test::envnode::grp(0x801u, k_cell_children,
                                            {fo4::test::envnode::grp(
                                                 0x801u, k_cell_temporary,
                                                 {fo4::test::envnode::rec(
                                                      "REFR", 0x820u,
                                                      {{.signature = "NAME", .data = fo4::test::envnode::bytes_of({0x810u})},
                                                       {.signature = "DATA", .data = fo4::test::envnode::floats({0, 0, 0, 0, 0, 0})}}),
                                                  fo4::test::envnode::rec(
                                                      "REFR", 0x821u,
                                                      {{.signature = "NAME", .data = fo4::test::envnode::bytes_of({0x810u})},
                                                       {.signature = "DATA", .data = fo4::test::envnode::floats({0, 0, 0, 0, 0, 0})}})})}),
                }),
            })})});

  fo4::test::TempDataDir dir;
  dir.write("Mod.esm", bytes, 0);
  const std::array plugin_paths = {dir.path() / "Mod.esm"};
  const auto env = fo4::env::Environment::load(std::span<const std::filesystem::path>(plugin_paths));
  const auto plan = fo4::previs::build_precombine_plan(env, fo4::FormKey::parse("000801:Mod.esm"), "Mod.esm", 2);
  ASSERT_TRUE(plan.has_value());
  EXPECT_TRUE(plan->interior);
  ASSERT_EQ(plan->groups.size(), 1u);
  EXPECT_EQ(plan->groups[0].instances.size(), 2u);
  EXPECT_EQ(plan->groups[0].model_path, "Meshes\\Rock.nif");
}

// Detects: a scripted or enable-parented reference must be refused and counted once, and linked, teleport, activate parent, base script and base material swap must still be planned.
TEST(PrevisPlan, DropsScriptedAndEnableParentedReferences) {
  namespace en = fo4::test::envnode;
  const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
  const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
  const auto join = [](std::vector<std::byte> head, const std::vector<std::byte>& tail) {
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
  };
  const std::vector<std::byte> vmad{std::byte{6}, std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}};
  const auto placed = [](std::uint32_t form_id, std::uint32_t base, std::vector<en::Sub> extra = {}) {
    std::vector<en::Sub> subs{{.signature = "NAME", .data = en::bytes_of({base})},
                              {.signature = "DATA", .data = en::floats({0, 0, 0, 0, 0, 0})}};
    for (auto& sub : extra) subs.push_back(std::move(sub));
    return en::rec("REFR", form_id, std::move(subs));
  };
  const auto xtel = join(join(en::bytes_of({0x830u}), en::floats({1, 2, 3, 0, 0, 0})), en::bytes_of({0u, 0x831u}));
  const auto bytes = en::make_plugin(
      {},
      {en::top("STAT",
               {en::rec("STAT", 0x810u, {{.signature = "MODL", .data = en::zstring("Meshes\\Rock.nif")}}),
                en::rec("STAT", 0x811u,
                        {{.signature = "MODL", .data = en::zstring("Meshes\\Scripted.nif")},
                         {.signature = "VMAD", .data = vmad}}),
                en::rec("STAT", 0x812u,
                        {{.signature = "MODL", .data = en::zstring("Meshes\\Swapped.nif")},
                         {.signature = "MODS", .data = en::bytes_of({0x840u})}})}),
       en::top("CELL",
               {en::grp(3u, 2,
                        {en::grp(0u, 3,
                                 {en::rec("CELL", 0x801u, {{.signature = "DATA", .data = {std::byte{1}}}}),
                                  en::grp(0x801u, cell_children,
                                          {en::grp(0x801u, cell_temporary,
                                                   {placed(0x820u, 0x810u),
                                                    placed(0x821u, 0x810u),
                                                    placed(0x822u, 0x810u, {{.signature = "VMAD", .data = vmad}}),
                                                    placed(0x823u, 0x810u,
                                                           {{.signature = "XESP", .data = en::bytes_of({0x820u, 1u})}}),
                                                    placed(0x824u, 0x810u,
                                                           {{.signature = "XLKR", .data = en::bytes_of({0x832u, 0x820u})}}),
                                                    placed(0x826u, 0x810u, {{.signature = "XTEL", .data = xtel}}),
                                                    placed(0x827u, 0x810u, {{.signature = "XAPD", .data = {std::byte{1}}}}),
                                                    placed(0x828u, 0x810u,
                                                           {{.signature = "XAPR",
                                                             .data = join(en::bytes_of({0x820u}), en::floats({1.0f}))}}),
                                                    placed(0x829u, 0x811u),
                                                    placed(0x82Au, 0x811u),
                                                    placed(0x82Bu, 0x812u),
                                                    placed(0x82Cu, 0x812u),
                                                    placed(0x82Du, 0x810u,
                                                           {{.signature = "VMAD", .data = vmad},
                                                            {.signature = "XESP", .data = en::bytes_of({0x820u, 1u})}})})})})})})});

  fo4::test::TempDataDir dir;
  dir.write("Mod.esm", bytes, 0);
  const std::array plugin_paths = {dir.path() / "Mod.esm"};
  const auto env = fo4::env::Environment::load(std::span<const std::filesystem::path>(plugin_paths));
  const auto plan = fo4::previs::build_precombine_plan(env, fo4::FormKey::parse("000801:Mod.esm"), "Mod.esm", 1);
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->temporary_considered, 13u);
  EXPECT_EQ(plan->skipped.scripted, 2u);
  EXPECT_EQ(plan->skipped.enable_parent, 1u);
  ASSERT_EQ(plan->groups.size(), 3u);
  EXPECT_EQ(plan->groups[0].model_path, "Meshes\\Rock.nif");
  ASSERT_EQ(plan->groups[0].instances.size(), 6u);
  EXPECT_EQ(plan->groups[0].instances[0].reference, fo4::FormKey::parse("000820:Mod.esm"));
  EXPECT_EQ(plan->groups[0].instances[1].reference, fo4::FormKey::parse("000821:Mod.esm"));
  EXPECT_EQ(plan->groups[0].instances[2].reference, fo4::FormKey::parse("000824:Mod.esm"));
  EXPECT_EQ(plan->groups[0].instances[3].reference, fo4::FormKey::parse("000826:Mod.esm"));
  EXPECT_EQ(plan->groups[0].instances[4].reference, fo4::FormKey::parse("000827:Mod.esm"));
  EXPECT_EQ(plan->groups[0].instances[5].reference, fo4::FormKey::parse("000828:Mod.esm"));
  EXPECT_EQ(plan->groups[1].model_path, "Meshes\\Scripted.nif");
  EXPECT_EQ(plan->groups[1].instances.size(), 2u);
  EXPECT_EQ(plan->groups[2].model_path, "Meshes\\Swapped.nif");
  EXPECT_EQ(plan->groups[2].instances.size(), 2u);
}

TEST(PrevisPlan, CountsForeignPluginStatReferences) {
  namespace en = fo4::test::envnode;
  const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
  const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
  const auto placed = [](std::uint32_t form_id, float x) {
    return en::rec("REFR", form_id,
                   {{.signature = "NAME", .data = en::bytes_of({0x01000810u})},
                    {.signature = "DATA", .data = en::floats({x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}});
  };
  const auto base = en::make_plugin({}, {en::top("CELL", {en::rec("CELL", 0x801u, {{.signature = "DATA", .data = {std::byte{1}}}})})});
  const auto addon = en::make_plugin(
      {"Base.esm"},
      {en::top("STAT", {en::rec("STAT", 0x01000810u,
                                  {{.signature = "MODL", .data = en::zstring("Meshes\\Rock.nif")}})}),
       en::top("CELL", {en::rec("CELL", 0x00000801u, {{.signature = "DATA", .data = {std::byte{1}}}}),
                        en::grp(0x00000801u, cell_children,
                                {en::grp(0x00000801u, cell_temporary,
                                         {placed(0x01000820u, 0.0f), placed(0x01000821u, 1.0f)})})})});

  fo4::test::TempDataDir dir;
  dir.write("Base.esm", base, 0);
  dir.write("Addon.esp", addon, 1);
  const std::array plugin_paths = {dir.path() / "Base.esm", dir.path() / "Addon.esp"};
  const auto env = fo4::env::Environment::load(std::span<const std::filesystem::path>(plugin_paths));
  const auto plan =
      fo4::previs::build_precombine_plan(env, fo4::FormKey::parse("000801:Base.esm"), "Base.esm", 1);
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->skipped.foreign_plugin, 2u);
  EXPECT_TRUE(plan->groups.empty());
}

// Regression: exterior cells must plan their own temporary STAT groups instead of being refused.
TEST(PrevisPlan, GroupsExteriorStatInstances) {
  namespace en = fo4::test::envnode;
  const auto world_children = static_cast<std::int32_t>(fo4::GroupType::world_children);
  const auto exterior_block = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_block);
  const auto exterior_subblock = static_cast<std::int32_t>(fo4::GroupType::exterior_cell_subblock);
  const auto cell_children = static_cast<std::int32_t>(fo4::GroupType::cell_children);
  const auto cell_temporary = static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);
  const auto placed = [](std::uint32_t form_id, float x) {
    return en::rec("REFR", form_id,
                   {{.signature = "NAME", .data = en::bytes_of({0x810u})},
                    {.signature = "DATA", .data = en::floats({x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}});
  };
  const auto bytes = en::make_plugin(
      {},
      {en::top("STAT", {en::rec("STAT", 0x810u,
                                {{.signature = "MODL", .data = en::zstring("Meshes\\Rock.nif")}})}),
       en::top("WRLD", {en::rec("WRLD", 0x800u),
                        en::grp(0x800u, world_children,
                                {en::grp(0u, exterior_block,
                                         {en::grp(0u, exterior_subblock,
                                                  {en::rec("CELL", 0x801u,
                                                           {{.signature = "XCLC",
                                                             .data = en::bytes_of({0u, 0u, 0u})}}),
                                                   en::grp(0x801u, cell_children,
                                                           {en::grp(0x801u, cell_temporary,
                                                                    {placed(0x820u, 0.0f),
                                                                     placed(0x821u, 64.0f)})})})})})})});

  fo4::test::TempDataDir dir;
  dir.write("Mod.esm", bytes, 0);
  const std::array plugin_paths = {dir.path() / "Mod.esm"};
  const auto env = fo4::env::Environment::load(std::span<const std::filesystem::path>(plugin_paths));
  const auto plan =
      fo4::previs::build_precombine_plan(env, fo4::FormKey::parse("000801:Mod.esm"), "Mod.esm", 2);
  ASSERT_TRUE(plan.has_value());
  EXPECT_FALSE(plan->interior);
  ASSERT_EQ(plan->groups.size(), 1u);
  EXPECT_EQ(plan->groups[0].model_path, "Meshes\\Rock.nif");
  ASSERT_EQ(plan->groups[0].instances.size(), 2u);

  const std::array<std::uint32_t, 1> oc_ids{1u};
  const fo4::PluginFormIdContext own{.current_plugin = fo4::PluginKey::from_filename("Mod.esm"),
                                     .current_kind = fo4::PluginKind::full,
                                     .masters = {}};
  const auto xcri = fo4::previs::xcri_from_plan(*plan, oc_ids, own);
  ASSERT_EQ(xcri.mesh_ids.size(), 1u);
  EXPECT_EQ(xcri.mesh_ids[0], 1u);
  ASSERT_EQ(xcri.refs.size(), 2u);
  EXPECT_EQ(xcri.refs[0].first, 0x820u);
  EXPECT_EQ(xcri.refs[0].second, 1u);
  EXPECT_EQ(xcri.refs[1].first, 0x821u);
  EXPECT_EQ(xcri.refs[1].second, 1u);
}

// Regression: each planned REFR must pair with its own group's OC id, or XCRI names the wrong mesh.
TEST(PrevisPlan, XcriPairsInstancesWithGroupOcIds) {
  fo4::previs::PrecombinePlan plan;
  plan.cell = fo4::FormKey::parse("000801:Mod.esm");
  plan.interior = true;
  const auto instance = [](std::string_view key) {
    fo4::previs::PrecombinePlanInstance inst;
    inst.reference = fo4::FormKey::parse(key);
    return inst;
  };
  fo4::previs::PrecombineModelGroup rocks;
  rocks.model_path = "Meshes\\Rock.nif";
  rocks.instances = {instance("000821:Mod.esm"), instance("000820:Mod.esm")};
  fo4::previs::PrecombineModelGroup pipe;
  pipe.model_path = "Meshes\\Pipe.nif";
  pipe.instances = {instance("000830:Mod.esm")};
  plan.groups = {rocks, pipe};

  const std::array<std::uint32_t, 2> oc_ids{3u, 7u};
  const fo4::PluginFormIdContext own{.current_plugin = fo4::PluginKey::from_filename("Mod.esm"),
                                     .current_kind = fo4::PluginKind::full,
                                     .masters = {}};
  const auto xcri = fo4::previs::xcri_from_plan(plan, oc_ids, own);
  ASSERT_EQ(xcri.mesh_ids.size(), 2u);
  EXPECT_EQ(xcri.mesh_ids[0], 3u);
  EXPECT_EQ(xcri.mesh_ids[1], 7u);
  ASSERT_EQ(xcri.refs.size(), 3u);
  EXPECT_EQ(xcri.refs[0].first, 0x820u);
  EXPECT_EQ(xcri.refs[0].second, 3u);
  EXPECT_EQ(xcri.refs[1].first, 0x821u);
  EXPECT_EQ(xcri.refs[1].second, 3u);
  EXPECT_EQ(xcri.refs[2].first, 0x830u);
  EXPECT_EQ(xcri.refs[2].second, 7u);

  // Regression: an OC id list that does not cover every group must not emit a half-filled XCRI.
  const std::array<std::uint32_t, 1> short_ids{1u};
  const auto mismatched = fo4::previs::xcri_from_plan(plan, short_ids, own);
  EXPECT_TRUE(mismatched.mesh_ids.empty());
  EXPECT_TRUE(mismatched.refs.empty());

// Detects: XCRI written with bare local ids, which a patch with masters resolves to its first master instead of the REFR's owner; each word must carry the owner's index in the stamped plugin.
  const fo4::PluginFormIdContext patch{
      .current_plugin = fo4::PluginKey::from_filename("Patch.esp"),
      .current_kind = fo4::PluginKind::full,
      .masters = {{fo4::PluginKey::from_filename("Base.esm"), fo4::PluginKind::full},
                  {fo4::PluginKey::from_filename("Mod.esm"), fo4::PluginKind::full}}};
  const auto patched = fo4::previs::xcri_from_plan(plan, oc_ids, patch);
  ASSERT_EQ(patched.refs.size(), 3u);
  EXPECT_EQ(patched.refs[0].first, 0x01000820u);
  EXPECT_EQ(patched.refs[1].first, 0x01000821u);
  EXPECT_EQ(patched.refs[2].first, 0x01000830u);

  // Constraint: detects: a REFR owner the stamped plugin does not declare being written anyway as a wrong id.
  const fo4::PluginFormIdContext undeclared{.current_plugin = fo4::PluginKey::from_filename("Patch.esp"),
                                            .current_kind = fo4::PluginKind::full,
                                            .masters = {}};
  EXPECT_THROW((void)fo4::previs::xcri_from_plan(plan, oc_ids, undeclared), fo4::FormIdError);
}

TEST(PrevisBake, EulerPlacementProducesRotationMatrix) {
  const auto xform = fo4::previs::placement_transform_from_euler(0, 0, 0, 0, 0, 0, 1.0f);
  EXPECT_FLOAT_EQ(xform.rotation[0], 1.0f);
  EXPECT_FLOAT_EQ(xform.rotation[4], 1.0f);
  EXPECT_FLOAT_EQ(xform.rotation[8], 1.0f);
}

// Regression: placement bake must rewrite combined-record transforms inside compact Shared extra.
TEST(PrevisBakeFromPlacements, PatchesCompactSharedExtraTransform) {
  const fs::path manifest_path = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "manifest.json";
  const fs::path template_oc = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(template_oc) || !fs::exists(manifest_path)) GTEST_SKIP();
  const auto manifest = nlohmann::json::parse(std::ifstream(manifest_path));
  fo4::previs::BakeOcPlacementsRequest request{.template_oc_nif = template_oc, .placements = {}};
  const auto template_bytes = read_bytes(template_oc);
  const auto nif = fo4::nif::read_nif(template_bytes);
  std::size_t patchable_slots = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto span = std::span<const std::byte>(template_bytes).subspan(block.offset, block.size);
    const auto layout = fo4::previs::detect_shared_geom_layout(span);
    if (!layout.has_value() || layout->combined_per_object != 1) continue;
    patchable_slots += layout->object_count;
  }
  if (patchable_slots == 0) GTEST_SKIP() << "no patchable Shared extra in oracle";
  std::size_t slot_index = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto span = std::span<const std::byte>(template_bytes).subspan(block.offset, block.size);
    const auto layout = fo4::previs::detect_shared_geom_layout(span);
    if (!layout.has_value() || layout->combined_per_object != 1) continue;
    for (std::size_t slot = 0; slot < layout->object_count; ++slot) {
      fo4::previs::SharedCombinedRecordXform xform;
      ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(span, slot, 0, xform));
      fo4::previs::OcPlacement placement;
      for (std::size_t i = 0; i < 3; ++i) placement.xform.translation[i] = xform.translation[i];
      for (std::size_t i = 0; i < 9; ++i) placement.xform.rotation[i] = xform.rotation[i];
      placement.xform.scale = xform.scale;
      if (slot_index == 0) placement.xform.translation[0] += 1.0f;
      ++slot_index;
      request.placements.push_back(std::move(placement));
    }
  }
  ASSERT_EQ(request.placements.size(), patchable_slots);
  const auto out_dir = fs::temp_directory_path() / "previs_bake_patch";
  fs::create_directories(out_dir);
  const auto out = out_dir / "patched_OC.NIF";
  ASSERT_EQ(fo4::previs::bake_oc_from_placements(request, out), fo4::previs::BakeOcStatus::ok);
  const auto baked = read_bytes(out);
  ASSERT_NE(fo4::io::sha256_hex(baked), manifest.at("files").at(2).at("sha256").get<std::string>());
  const auto baked_nif = fo4::nif::read_nif(baked);
  for (const auto& baked_block : baked_nif.blocks()) {
    if (baked_block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto baked_span = std::span<const std::byte>(baked).subspan(baked_block.offset, baked_block.size);
    fo4::previs::SharedCombinedRecordXform patched;
    ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(baked_span, 0, 0, patched));
    EXPECT_FLOAT_EQ(patched.translation[0], request.placements[0].xform.translation[0]);
    return;
  }
  FAIL() << "baked _OC missing Shared extra";
}

// Regression: a patched _OC must address the patch's own container, so the object takes the request's hash, offset and sphere.
TEST(PrevisBakeFromPlacements, AddressesThePatchContainerHashAndOffset) {
  const fs::path template_oc = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(template_oc)) GTEST_SKIP();
  const auto template_bytes = read_bytes(template_oc);
  const auto nif = fo4::nif::read_nif(template_bytes);
  std::optional<fo4::previs::SharedGeomHeader> header;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    header = fo4::previs::read_shared_geom_header(
        std::span<const std::byte>(template_bytes).subspan(block.offset, block.size));
    if (header.has_value() && !header->objects.empty()) break;
    header.reset();
  }
  if (!header.has_value()) GTEST_SKIP() << "oracle has no table-layout Shared extra";
  const auto shipped = header->objects.front();
  fo4::previs::BakeOcPlacementsRequest request{.template_oc_nif = template_oc, .placements = {}};
  request.csg_file_hash = 0xDEADBEEFu;
  request.csg_data_offset = 0x1234u;
  request.local_center[0] = 5.0f;
  request.local_center[1] = 6.0f;
  request.local_center[2] = 7.0f;
  request.local_radius = 2.0f;
  for (int i = 0; i < 2; ++i) {
    fo4::previs::OcPlacement placement;
    placement.xform = fo4::previs::placement_transform_from_euler(
        10.0f * static_cast<float>(i), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    request.placements.push_back(placement);
  }
  // Constraint: a container address without a measured sphere cannot place a bound.
  auto no_sphere = request;
  no_sphere.local_radius = 0.0f;
  EXPECT_EQ(fo4::previs::bake_oc_from_placements(no_sphere,
                                                fs::temp_directory_path() / "previs_container_oc" / "no_sphere.NIF"),
            fo4::previs::BakeOcStatus::invalid_input);
  const auto out = fs::temp_directory_path() / "previs_container_oc" / "container_OC.NIF";
  fs::create_directories(out.parent_path());
  ASSERT_EQ(fo4::previs::bake_oc_from_placements(request, out), fo4::previs::BakeOcStatus::ok);
  const auto baked = read_bytes(out);
  const auto baked_nif = fo4::nif::read_nif(baked);
  for (const auto& block : baked_nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto span = std::span<const std::byte>(baked).subspan(block.offset, block.size);
    const auto baked_header = fo4::previs::read_shared_geom_header(span);
    if (!baked_header.has_value() || baked_header->objects.empty()) continue;
    const auto& object = baked_header->objects.front();
    EXPECT_EQ(object.file_hash, 0xDEADBEEFu);
    EXPECT_EQ(object.data_offset, 0x1234u);
    EXPECT_EQ(object.vertex_count, shipped.vertex_count);
    for (std::size_t level = 0; level < fo4::previs::k_previs_lod_levels; ++level) {
      EXPECT_EQ(object.triangle_count[level], shipped.triangle_count[level]);
    }
    EXPECT_EQ(object.combined_count, 2u);
    fo4::previs::SharedCombinedRecordXform record;
    ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(span, 0, 0, record));
    EXPECT_FLOAT_EQ(record.bound_center[0], 5.0f);
    EXPECT_FLOAT_EQ(record.bound_center[1], 6.0f);
    EXPECT_FLOAT_EQ(record.bound_center[2], 7.0f);
    EXPECT_FLOAT_EQ(record.bound_radius, 2.0f);
    return;
  }
  FAIL() << "baked _OC missing Shared extra";
}

std::vector<std::byte> synthetic_stat_nif(std::size_t extra_data = 0) {
  const fo4::nif::Transform moved{{7.0f, 8.0f, 9.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  nif_build::LightingFields fields;
  fields.texture_set = 4;
  fields.root_material = 2;
  auto stat_shape = nif_build::shape_with_refs("BSTriShape", 1, moved, nif_build::k_full, 16, -1, 3, 2,
                                               nif_build::triangle_vertices(), {0, 1, 2}, nif_build::k_triangle_sphere);
  std::vector<nif_build::Block> blocks;
  blocks.push_back(nif_build::named_node_block("NiNode", 0, fo4::nif::Transform{}, {1}));
  blocks.push_back({});
  blocks.push_back(nif_build::alpha_block(0x1234, 7));
  blocks.push_back(nif_build::full_lighting_shader_block(fields));
  blocks.push_back(nif_build::texture_set_record({"stat.dds"}));
  nif_build::Writer extra;
  extra.i32(2);
  extra.i32(1);
  std::vector<std::byte> extra_refs;
  for (std::size_t i = 0; i < extra_data; ++i) {
    blocks.push_back(nif_build::Block{"NiStringExtraData", extra.bytes()});
    const auto ref = static_cast<std::uint32_t>(blocks.size() - 1);
    for (std::size_t byte = 0; byte < 4; ++byte) extra_refs.push_back(static_cast<std::byte>((ref >> (8 * byte)) & 0xFFu));
  }
  stat_shape.data[4] = static_cast<std::byte>(extra_data);
  stat_shape.data.insert(stat_shape.data.begin() + 8, extra_refs.begin(), extra_refs.end());
  blocks[1] = std::move(stat_shape);
  return nif_build::build_nif({"StatRoot", "StatShape", "stat.bgsm"}, blocks, {0});
}

nif_build::Block shared_extra_block(std::uint32_t hash, std::uint32_t vertices, std::array<std::uint32_t, 3> triangles) {
  nif_build::Writer w;
  w.i32(-1);
  w.u64(5u);
  w.u32(1u);
  w.u32(1u);
  w.u64(0u);
  w.u32(1u);
  w.u32(hash);
  w.u32(0x500u);
  w.u32(vertices);
  w.u32(3u);
  std::uint32_t words = 0;
  for (const auto count : triangles) {
    w.u32(count);
    w.u32(words);
    words += 3u * count;
  }
  w.u32(1u);
  w.u32(0u);
  for (const float value : {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}) {
    w.f32(value);
  }
  w.u64(5u);
  return nif_build::Block{"BSPackedCombinedSharedGeomDataExtra", w.bytes()};
}

nif_build::Block root_with_extras(const std::vector<std::int32_t>& extras, const std::vector<std::int32_t>& children) {
  nif_build::Writer w;
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(extras.size()));
  for (const auto extra : extras) w.i32(extra);
  w.i32(-1);
  w.u32(0);
  w.transform(fo4::nif::Transform{});
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  return nif_build::Block{"NiNode", w.bytes()};
}

std::uint32_t u32_in(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8 * i);
  return value;
}

std::span<const std::byte> block_bytes(const std::vector<std::byte>& image, const fo4::nif::NifFile& nif, std::size_t block) {
  return std::span<const std::byte>(image).subspan(nif.blocks()[block].offset, nif.blocks()[block].size);
}

// Constraint: detects: a graft that pastes the STAT shape block over the shell's, or reads the STAT's offsets with the shell's extra-data count (run with no and two STAT extras).
TEST(PrevisBake, ShellGraftKeepsTheShellsReferencesAndTakesTheStatMesh) {
  auto shell_shape = nif_build::shape_with_refs("BSTriShape", 1, fo4::nif::Transform{}, nif_build::k_full, 16, -1, 1, 3,
                                                nif_build::triangle_vertices(), {0, 1, 2});
  shell_shape.data[4] = std::byte{1};
  shell_shape.data.insert(shell_shape.data.begin() + 8, {std::byte{5}, std::byte{0}, std::byte{0}, std::byte{0}});
  nif_build::LightingFields fields;
  fields.texture_set = 2;
  nif_build::Writer extra;
  extra.i32(2);
  extra.i32(1);
  const auto shell_bytes = nif_build::build_nif(
      {"ShellRoot", "ShellShape", "ShellExtra"},
      {nif_build::named_node_block("NiNode", 0, fo4::nif::Transform{}, {4}), nif_build::full_lighting_shader_block(fields),
       nif_build::texture_set_record({"shell.dds"}), nif_build::alpha_block(0x00ECu, 1), shell_shape,
       nif_build::Block{"NiStringExtraData", extra.bytes()}},
      {0});
  const auto shell_nif = fo4::nif::read_nif(shell_bytes);

  for (const std::size_t stat_extras : {std::size_t{0}, std::size_t{2}}) {
    SCOPED_TRACE(stat_extras);
    const auto stat_bytes = synthetic_stat_nif(stat_extras);
    const std::size_t at = 4 * stat_extras;
    const auto grafted = fo4::previs::graft_first_shape_mesh_into_shell(shell_bytes, stat_bytes);
    ASSERT_TRUE(grafted.has_value());
    const auto stat_nif = fo4::nif::read_nif(stat_bytes);
    const auto out_nif = fo4::nif::read_nif(*grafted);
    ASSERT_EQ(out_nif.blocks().size(), shell_nif.blocks().size());
    const auto shape = block_bytes(*grafted, out_nif, 4);
    const auto stat_shape = block_bytes(stat_bytes, stat_nif, 1);
    EXPECT_EQ(u32_in(stat_shape, 4), stat_extras);
    EXPECT_EQ(u32_in(shape, 0), 1u);
    EXPECT_EQ(u32_in(shape, 4), 1u);
    EXPECT_EQ(u32_in(shape, 8), 5u);
    EXPECT_EQ(u32_in(shape, 12), 0xFFFFFFFFu);
    EXPECT_EQ(u32_in(shape, 72), 0xFFFFFFFFu);
    EXPECT_EQ(u32_in(shape, 92), 0xFFFFFFFFu);
    EXPECT_EQ(u32_in(shape, 96), 1u);
    EXPECT_EQ(u32_in(shape, 100), 3u);
    ASSERT_EQ(shape.size() - 104, stat_shape.size() - (100 + at));
    EXPECT_TRUE(std::equal(shape.begin() + 104, shape.end(), stat_shape.begin() + 100 + at));
    EXPECT_TRUE(std::equal(shape.begin() + 16, shape.begin() + 72, stat_shape.begin() + 12 + at));
    EXPECT_TRUE(std::equal(shape.begin() + 76, shape.begin() + 92, stat_shape.begin() + 72 + at));
    for (const std::size_t block : {0u, 1u, 2u, 3u, 5u}) {
      const auto before = block_bytes(shell_bytes, shell_nif, block);
      const auto after = block_bytes(*grafted, out_nif, block);
      EXPECT_TRUE(std::equal(before.begin(), before.end(), after.begin(), after.end())) << block;
    }
  }
}

// Detects: an authoring path that only ever ran against a shipped oracle and skipped when the graft failed. The shell and STAT are synthetic, so the graft and the placement apply must both succeed here.
TEST(PrevisBake, AuthorsOcFromStatMeshViaShellGraft) {
  const auto stat_bytes = synthetic_stat_nif();
  ASSERT_TRUE(fo4::previs::stat_mesh_nif(stat_bytes));
  const auto shell_bytes = nif_build::build_nif(
      {},
      {root_with_extras({2}, {1}),
       nif_build::shape_with_refs("BSTriShape", -1, fo4::nif::Transform{}, nif_build::k_full, 16, -1, -1, -1,
                                  nif_build::triangle_vertices(), {0, 1, 2}),
       shared_extra_block(0x2088054Du, 300u, {200u, 100u, 50u})},
      {0});
  const auto dir = fs::temp_directory_path() / "previs_stat_author";
  fs::create_directories(dir);
  const auto stat_path = dir / "tri.nif";
  const auto shell_path = dir / "shell_OC.NIF";
  for (const auto& [path, bytes] : {std::pair{stat_path, &stat_bytes}, std::pair{shell_path, &shell_bytes}}) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
  }
  fo4::previs::OcPlacement placement;
  placement.xform = fo4::previs::placement_transform_from_euler(1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f);
  const auto out = dir / "authored_OC.NIF";
  ASSERT_EQ(fo4::previs::bake_oc_author_from_stat_shell(stat_path, shell_path,
                                                        std::span<const fo4::previs::OcPlacement>(&placement, 1), out),
            fo4::previs::BakeOcStatus::ok);
  const auto baked = read_bytes(out);
  ASSERT_TRUE(fo4::previs::oc_nif_has_shared_combined_geom(baked));
  const auto nif = fo4::nif::read_nif(baked);
  const auto stat_nif = fo4::nif::read_nif(stat_bytes);
  const auto shape = block_bytes(baked, nif, 1);
  const auto stat_shape = block_bytes(stat_bytes, stat_nif, 1);
  EXPECT_EQ(u32_in(shape, 92), 0xFFFFFFFFu);
  EXPECT_EQ(u32_in(shape, 96), 0xFFFFFFFFu);
  ASSERT_EQ(shape.size() - 100, stat_shape.size() - 100);
  EXPECT_TRUE(std::equal(shape.begin() + 100, shape.end(), stat_shape.begin() + 100));
  fo4::previs::SharedCombinedRecordXform record{};
  ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(block_bytes(baked, nif, 2), 0, 0, record));
  EXPECT_FLOAT_EQ(record.translation[0], 1.0f);
  EXPECT_FLOAT_EQ(record.translation[1], 2.0f);
  EXPECT_FLOAT_EQ(record.translation[2], 3.0f);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// Constraint: detects: a bake that writes the request's one container address into every Shared extra, so extras past the first address the wrong mesh.
TEST(PrevisBakeFromPlacements, RedirectsOnlyTheFirstExtraAndLeavesTheRestUntouched) {
  const auto template_bytes = nif_build::build_nif(
      {},
      {root_with_extras({2, 3}, {1}),
       nif_build::shape_with_refs("BSTriShape", -1, fo4::nif::Transform{}, nif_build::k_full, 16, -1, -1, -1,
                                  nif_build::triangle_vertices(), {0, 1, 2}),
       shared_extra_block(0x2088054Du, 300u, {200u, 100u, 50u}),
       shared_extra_block(0x2088054Du, 400u, {500u, 250u, 125u})},
      {0});
  const auto dir = fs::temp_directory_path() / "previs_bake_redirect_first_extra";
  fs::create_directories(dir);
  const auto template_path = dir / "template_OC.NIF";
  {
    std::ofstream file(template_path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(template_bytes.data()), static_cast<std::streamsize>(template_bytes.size()));
  }
  fo4::previs::BakeOcPlacementsRequest request{.template_oc_nif = template_path, .placements = {}};
  request.csg_file_hash = 0xDEADBEEFu;
  request.csg_data_offset = 0x1234u;
  request.local_radius = 2.0f;
  for (int i = 0; i < 2; ++i) {
    fo4::previs::OcPlacement placement;
    placement.xform = fo4::previs::placement_transform_from_euler(10.0f * static_cast<float>(i), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    request.placements.push_back(placement);
  }
  const auto out = dir / "baked_OC.NIF";
  ASSERT_EQ(fo4::previs::bake_oc_from_placements(request, out), fo4::previs::BakeOcStatus::ok);
  const auto baked = read_bytes(out);
  const auto nif = fo4::nif::read_nif(baked);
  const auto template_nif = fo4::nif::read_nif(template_bytes);
  ASSERT_EQ(nif.blocks().size(), template_nif.blocks().size());
  const auto first = fo4::previs::read_shared_geom_header(block_bytes(baked, nif, 2));
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->objects.size(), 1u);
  EXPECT_EQ(first->objects[0].file_hash, 0xDEADBEEFu);
  EXPECT_EQ(first->objects[0].data_offset, 0x1234u);
  EXPECT_EQ(first->objects[0].combined_count, 2u);
  EXPECT_EQ(first->objects[0].vertex_count, 300u);
  const auto before = block_bytes(template_bytes, template_nif, 3);
  const auto after = block_bytes(baked, nif, 3);
  EXPECT_TRUE(std::equal(before.begin(), before.end(), after.begin(), after.end()));
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// Constraint: a retail _OC carries one Shared extra per LOD shape, so baking into the first extra alone leaves the others naming the vanilla container.
TEST(PrevisBake, BakesMultiExtraShellToASinglePatchOwnedExtra) {
  const fs::path shell_oc = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(shell_oc)) GTEST_SKIP() << "no shipped multi-extra _OC oracle";
  const auto shell_bytes = read_bytes(shell_oc);
  std::size_t shell_extras = 0;
  std::set<std::uint32_t> shipped_hashes;
  const auto shell_nif = fo4::nif::read_nif(shell_bytes);
  for (const auto& block : shell_nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    ++shell_extras;
    const auto header = fo4::previs::read_shared_geom_header(
        std::span<const std::byte>(shell_bytes).subspan(block.offset, block.size));
    if (!header.has_value()) continue;
    for (const auto& object : header->objects) shipped_hashes.insert(object.file_hash);
  }
  ASSERT_GE(shell_extras, 2u) << "fixture must be the multi-extra shell";
  ASSERT_FALSE(shipped_hashes.empty());

  constexpr std::string_view k_obj =
      "o tri\n"
      "v 0 0 0\n"
      "v 1 0 0\n"
      "v 0 1 0\n"
      "f 1 2 3\n";
  fo4::nif::ImportOptions options{.material = "PIPEMETAL:Materials\\metal\\PipeMetal.bgsm",
                                  .tex_diffuse = {},
                                  .tex_normal = {},
                                  .box_collision = false,
                                  .hull_collision = false,
                                  .mesh_collision = false,
                                  .body_mass = 0.0f,
                                  .from_blender = false,
                                  .skeleton = {}};
  std::vector<std::byte> stat_bytes;
  try {
    stat_bytes = fo4::nif::import_obj(k_obj, options);
  } catch (...) {
    GTEST_SKIP() << "import_obj unavailable";
  }
  ASSERT_TRUE(fo4::previs::stat_mesh_nif(stat_bytes));

  constexpr std::uint32_t k_patch_hash = 0x0BADC0DEu;
  constexpr std::uint32_t k_offset = 0x20000u;
  fo4::previs::OcPlacement placement;
  placement.xform = fo4::previs::placement_transform_from_euler(4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f, 1.0f);
  fo4::previs::SharedGeomObjectWrite object;
  object.data_offset = k_offset;
  object.vertex_count = 3;
  object.triangle_count = {1, 1, 1};
  const float local_center[3] = {0.0f, 0.0f, 0.0f};
  object.records.push_back(fo4::previs::record_for_placement(placement, local_center, 1.0f));

  const auto out = fs::temp_directory_path() / "previs_multi_extra" / "single_extra_OC.NIF";
  fs::create_directories(out.parent_path());
  ASSERT_EQ(fo4::previs::bake_oc_shared_objects_bytes(stat_bytes, shell_oc, k_patch_hash,
                                                      std::span<const fo4::previs::SharedGeomObjectWrite>(&object, 1),
                                                      out),
            fo4::previs::BakeOcStatus::ok);

  const auto baked = read_bytes(out);
  const auto baked_nif = fo4::nif::read_nif(baked);
  std::size_t extras = 0;
  std::size_t objects = 0;
  std::size_t foreign = 0;
  std::size_t kept_offsets = 0;
  for (const auto& block : baked_nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    ++extras;
    const auto header = fo4::previs::read_shared_geom_header(
        std::span<const std::byte>(baked).subspan(block.offset, block.size));
    ASSERT_TRUE(header.has_value());
    for (const auto& baked_object : header->objects) {
      ++objects;
      if (baked_object.file_hash != k_patch_hash) ++foreign;
      if (baked_object.data_offset == k_offset) ++kept_offsets;
    }
  }
  EXPECT_EQ(extras, 1u) << "every extra but the baked one must be dropped with its LOD shape";
  EXPECT_EQ(objects, 1u) << "the vanilla object of the second extra must not survive";
  EXPECT_EQ(foreign, 0u) << "a surviving object would name the vanilla container";
  EXPECT_EQ(kept_offsets, 1u) << "the baked object keeps the patch's row offset";
}

TEST(PrevisBakeFromPlacements, ResizesSharedExtraForExtraPlacements) {
  const fs::path oracle_dir = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles";
  fs::path template_oc;
  std::size_t patchable_slots = 0;
  for (const auto& entry : fs::directory_iterator(oracle_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".NIF" && entry.path().extension() != ".nif") continue;
    const auto template_bytes = read_bytes(entry.path());
    const auto nif = fo4::nif::read_nif(template_bytes);
    std::size_t blocks = 0;
    std::size_t slots = 0;
    for (const auto& block : nif.blocks()) {
      if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
      const auto span = std::span<const std::byte>(template_bytes).subspan(block.offset, block.size);
      const auto layout = fo4::previs::detect_shared_geom_layout(span);
      if (!layout.has_value() || layout->combined_per_object != 1) continue;
      ++blocks;
      slots += layout->object_count;
    }
    if (blocks == 1 && slots > 0) {
      template_oc = entry.path();
      patchable_slots = slots;
      break;
    }
  }
  if (template_oc.empty()) GTEST_SKIP() << "no single-block patchable _OC oracle";
  fo4::previs::BakeOcPlacementsRequest request{.template_oc_nif = template_oc, .placements = {}};
  for (std::size_t i = 0; i < patchable_slots + 1; ++i) {
    fo4::previs::OcPlacement placement;
    placement.xform = fo4::previs::placement_transform_from_euler(static_cast<float>(i), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    request.placements.push_back(placement);
  }
  const auto out = fs::temp_directory_path() / "previs_rebuild_oc" / "resized_OC.NIF";
  fs::create_directories(out.parent_path());
  ASSERT_EQ(fo4::previs::bake_oc_from_placements(request, out), fo4::previs::BakeOcStatus::ok);
}

TEST(PrevisBakeFromPlacements, ResizesOneBlockOnMultiExtraTemplate) {
  const fs::path template_oc = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_0A82376A_OC.NIF";
  if (!fs::exists(template_oc)) GTEST_SKIP();
  const auto template_bytes = read_bytes(template_oc);
  const auto nif = fo4::nif::read_nif(template_bytes);
  std::size_t patchable_slots = 0;
  std::size_t patchable_blocks = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    const auto span = std::span<const std::byte>(template_bytes).subspan(block.offset, block.size);
    const auto layout = fo4::previs::detect_shared_geom_layout(span);
    if (!layout.has_value() || layout->combined_per_object != 1) continue;
    ++patchable_blocks;
    patchable_slots += layout->object_count;
  }
  if (patchable_blocks < 2 || patchable_slots == 0) GTEST_SKIP();
  fo4::previs::BakeOcPlacementsRequest request{.template_oc_nif = template_oc, .placements = {}};
  for (std::size_t i = 0; i < patchable_slots + 1; ++i) {
    fo4::previs::OcPlacement placement;
    placement.xform = fo4::previs::placement_transform_from_euler(static_cast<float>(i), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    request.placements.push_back(placement);
  }
  const auto out = fs::temp_directory_path() / "previs_rebuild_oc" / "multi_resized_OC.NIF";
  fs::create_directories(out.parent_path());
  ASSERT_EQ(fo4::previs::bake_oc_from_placements(request, out), fo4::previs::BakeOcStatus::ok);
}

TEST(PrevisBake, RejectsNonOcSource) {
  const std::vector<std::byte> source{std::byte{0x4E}, std::byte{0x49}, std::byte{0x46},
                                      std::byte{0x20}, std::byte{0x01}};
  const auto out = std::filesystem::temp_directory_path() / "previs_bake_reject" / "x_OC.NIF";
  EXPECT_EQ(fo4::previs::bake_oc_single_source_bytes(source, out),
            fo4::previs::BakeOcStatus::unsupported);
}
