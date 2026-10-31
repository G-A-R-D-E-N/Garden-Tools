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
#include <unordered_set>
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
  // A long banner must grow the image, not overflow the header: the banner
  // lives at +0xBC; the image must stay 16-byte aligned.
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

}  // namespace

// Sorted rows are an invariant: an unsorted rewrite would detach a cell's art.
TEST(PrevisCdx, RoundTripsAndFlagsUnsorted) {
  fo4::previs::CdxFile file;
  file.rows.push_back({1, 0x000800u, 2, 3});
  file.rows.push_back({1, 0x000801u, 4, 5});
  const auto bytes = fo4::previs::write_cdx(file);
  EXPECT_TRUE(fo4::previs::validate_cdx(bytes).empty());
  const auto back = fo4::previs::parse_cdx(bytes);
  ASSERT_EQ(back.rows.size(), 2u);
  EXPECT_EQ(back.rows[0].cell, 0x000800u);
  EXPECT_EQ(fo4::previs::write_cdx(back), bytes);
  EXPECT_TRUE(fo4::previs::cdx_has_data_for_cell(back, 0x000800u));
  EXPECT_FALSE(fo4::previs::cdx_has_data_for_cell(back, 0x000802u));

  fo4::previs::CdxFile flipped;
  flipped.rows.push_back({1, 0x000801u, 4, 5});
  flipped.rows.push_back({1, 0x000800u, 2, 3});
  const auto bad = fo4::previs::write_cdx(flipped);
  EXPECT_FALSE(fo4::previs::validate_cdx(bad).empty());

  auto truncated = bytes;
  truncated.pop_back();
  EXPECT_THROW(fo4::previs::parse_cdx(truncated), fo4::parse_error);
}

// Detects a table edit that moves a chunk offset past the file: parse must
// must fail closed instead of slicing out of bounds and corrupting geometry.
TEST(PrevisCsg, RoundTripsAndRejectsBadChunk) {
  fo4::previs::CsgFile file;
  fo4::previs::CsgRow row{};
  row.bytes[0] = std::byte{0x7A};
  file.rows.push_back(row);
  file.chunks.push_back(std::vector<std::byte>{std::byte{0x78}, std::byte{0x9C}, std::byte{0x03},
                                                std::byte{0x00}});
  const auto bytes = fo4::previs::write_csg(file);
  EXPECT_TRUE(fo4::previs::validate_csg(bytes).empty());
  const auto back = fo4::previs::parse_csg(bytes);
  ASSERT_EQ(back.rows.size(), 1u);
  ASSERT_EQ(back.chunks.size(), 1u);
  EXPECT_EQ(fo4::previs::write_csg(back), bytes);

  auto bad = bytes;
  // Corrupt the first chunk offset to run past the end of the file.
  bad[12] = std::byte{0xFF};
  bad[13] = std::byte{0xFF};
  bad[14] = std::byte{0xFF};
  bad[15] = std::byte{0x7F};
  EXPECT_FALSE(fo4::previs::validate_csg(bad).empty());

  auto magic = bytes;
  magic[0] = std::byte{'x'};
  EXPECT_FALSE(fo4::previs::validate_csg(magic).empty());
}

// A tome edit that breaks the CRC or alignment must be flagged: the game would
// ignore or reject the tome, so the validator must flag it before release.
TEST(PrevisUvd, ValidatesCrcAlignmentAndBanner) {
  const auto bytes = make_uvd_bytes("T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0");
  EXPECT_TRUE(fo4::previs::validate_uvd(bytes).empty());
  const auto file = fo4::previs::parse_uvd(bytes);
  EXPECT_NE(file.banner.find("3.3.17"), std::string::npos);

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

TEST(PrevisDate, DecodesPackedStampAndStaleness) {
  const auto date = fo4::previs::decode_packed_date(0x306Fu);
  EXPECT_EQ(date.year, 2024u);
  EXPECT_EQ(date.month, 3u);
  EXPECT_EQ(date.day, 15u);
  EXPECT_TRUE(fo4::previs::is_precalc_stale(0x306Fu, 0x3070u));
  EXPECT_FALSE(fo4::previs::is_precalc_stale(0x3070u, 0x306Fu));
  EXPECT_FALSE(fo4::previs::is_precalc_stale(0, 0x3070u));
}

// A repair patch that stamps VISI without a tome must be broken: it silently falls back.
TEST(PrevisDetect, FlagsMissingLinkage) {
  fo4::previs::CellPrevisInput clean{};
  EXPECT_FALSE(fo4::previs::diagnose_cell(clean).broken);

  fo4::previs::CellPrevisInput visi_no_tome{};
  visi_no_tome.has_visi = true;
  visi_no_tome.has_rvis = true;
  visi_no_tome.uvd_checked = true;  // verified absent
  EXPECT_TRUE(fo4::previs::diagnose_cell(visi_no_tome).broken);

  fo4::previs::CellPrevisInput visi_no_rvis{};
  visi_no_rvis.has_visi = true;
  visi_no_rvis.uvd_checked = true;
  visi_no_rvis.has_uvd = true;
  visi_no_rvis.uvd_valid = true;
  EXPECT_TRUE(fo4::previs::diagnose_cell(visi_no_rvis).broken);

  fo4::previs::CellPrevisInput xcri_no_cdx{};
  xcri_no_cdx.has_pcmb = true;
  xcri_no_cdx.has_xcri = true;
  xcri_no_cdx.cdx_checked = true;  // verified absent
  xcri_no_cdx.csg_checked = true;
  xcri_no_cdx.has_csg_rows = true;
  EXPECT_TRUE(fo4::previs::diagnose_cell(xcri_no_cdx).broken);

  fo4::previs::CellPrevisInput stale{};
  stale.has_pcmb = true;
  stale.has_xcri = true;
  stale.cdx_checked = true;
  stale.has_cdx_row = true;
  stale.csg_checked = true;
  stale.has_csg_rows = true;
  stale.pcmb_stale = true;
  EXPECT_TRUE(fo4::previs::diagnose_cell(stale).broken);
}

// An unchecked index must not produce a false missing verdict: without a
// .uvd lookup or CDX/CSG answers the cell requires a skip, not a broken verdict.
TEST(PrevisDetect, UncheckedIndexesBecomeSkipsNotBroken) {
  fo4::previs::CellPrevisInput visi{};
  visi.has_visi = true;
  visi.has_rvis = true;
  const auto unvis = fo4::previs::diagnose_cell(visi);
  EXPECT_FALSE(unvis.broken);
  EXPECT_TRUE(unvis.skip_unsupported);
  EXPECT_FALSE(unvis.skips.empty());

  fo4::previs::CellPrevisInput xcri{};
  xcri.has_pcmb = true;
  xcri.has_xcri = true;
  const auto unx = fo4::previs::diagnose_cell(xcri);
  EXPECT_FALSE(unx.broken);
  EXPECT_TRUE(unx.skip_unsupported);
  EXPECT_EQ(unx.skips.size(), 2u);  // CDX + CSG
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

// Inputs come from a loaded Environment plus index answers; a malformed CELL must be reported.
TEST_F(PrevisLoadOrderTest, ComputesInputsFromRealIndexes) {
  const std::filesystem::path paths[] = {write("Previs.esp", make_previs_plugin())};
  const auto env = fo4::env::Environment::load(paths);

  fo4::previs::CdxFile cdx;
  cdx.rows.push_back({0, 0x801, 0, 0});
  const std::unordered_set<std::uint32_t> csg{0x801u};
  fo4::previs::LoadOrderIndexes indexes;
  indexes.cdx = &cdx;
  indexes.csg_cells = &csg;
  indexes.uvd_issues = [](std::uint32_t local) -> std::optional<std::vector<std::string>> {
    if (local == 0x800u) return std::vector<std::string>{};
    return std::nullopt;
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
  EXPECT_TRUE(visi->input.uvd_valid);
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
  EXPECT_FALSE(bad->status.issues.empty());
}

// Missing index answers require skips: no false missing-tome or missing-row verdicts.
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

// Mesh/reference order is an invariant: the round trip pins the exact byte layout.
TEST(PrevisXcri, WriteRoundTrips) {
  const auto bytes = bytes_of({1, 2, 0x55, 0x00013002u, 0x56});
  const auto raw = fo4::previs::parse_xcri_bytes(bytes);
  EXPECT_EQ(fo4::previs::write_xcri_bytes(raw), bytes);
  EXPECT_EQ(fo4::previs::write_xcri_bytes(fo4::previs::XcriRaw{}), bytes_of({0, 0}));
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

const fo4::env::RecordRef* find_ref(const fo4::env::Environment& env, std::uint32_t local) {
  for (const auto* ref : env.winners_of(fo4::Signature::from_string("CELL"))) {
    if (ref != nullptr && ref->record != nullptr &&
        (ref->record->form_key.local().value() & 0xFFFFFFu) == local) {
      return ref;
    }
  }
  return nullptr;
}

}  // namespace

// A malformed XCRI in a mod must not fail the whole cell read: decode_cell
// must succeed and the report carries the parse error instead.
TEST_F(PrevisLoadOrderTest, MalformedXcriStillDecodes) {
  const std::filesystem::path paths[] = {write("PrevisXcriVer.esp", make_xcri_version_plugin())};
  const auto env = fo4::env::Environment::load(paths);
  const auto* ref = find_ref(env, 0x901u);
  ASSERT_NE(ref, nullptr);
  fo4::env::CellData data;
  EXPECT_NO_THROW(data = fo4::env::decode_cell(env, *ref));
  EXPECT_TRUE(data.precombined_timestamp.has_value());
}

// The malformed-XCRI cell must be reported broken while the scan continues.
TEST_F(PrevisLoadOrderTest, MalformedXcriReportedBroken) {
  const std::filesystem::path paths[] = {write("PrevisXcriVer.esp", make_xcri_version_plugin())};
  const auto env = fo4::env::Environment::load(paths);
  const auto report = fo4::previs::diagnose_load_order(env, fo4::previs::LoadOrderIndexes{});
  const auto* bad = find_cell(report, 0x901u);
  ASSERT_NE(bad, nullptr);
  EXPECT_TRUE(bad->status.broken);
  ASSERT_FALSE(bad->status.issues.empty());
  EXPECT_NE(bad->status.issues.front().find("XCRI"), std::string::npos);
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
