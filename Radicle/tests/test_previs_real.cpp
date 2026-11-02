#include "fo4/archive/ba2_reader.hpp"
#include "fo4/c_api_env.h"
#include "fo4/c_api_previs.h"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/previs/previs_data.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/signature.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

// A skip must stay inside the TEST: GTEST_SKIP cannot return a non-void value.
std::optional<fs::path> data_dir_from_env() {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr || *dir == '\0') return std::nullopt;
  return fs::path{dir};
}

std::vector<std::byte> read_bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  const std::streamoff size = in.tellg();
  if (size < 0) throw std::runtime_error("cannot size " + path.string());
  std::vector<std::byte> out(static_cast<std::size_t>(size));
  in.seekg(0);
  if (!out.empty()) {
    in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in) throw std::runtime_error("cannot read " + path.string());
  }
  return out;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

std::uint16_t sub_u16(const fo4::Subrecord* sub) {
  if (sub == nullptr || sub->data.size() < 2) return 0;
  std::uint16_t value = 0;
  std::memcpy(&value, sub->data.data(), 2);
  return value;
}

// Load order invariant: lowest priority first, exactly the six shipped masters.
std::vector<fs::path> official_masters(const fs::path& dir) {
  const std::vector<std::string> names{"Fallout4.esm",        "DLCRobot.esm",      "DLCworkshop01.esm",
                                       "DLCCoast.esm",        "DLCworkshop03.esm", "DLCNukaWorld.esm"};
  std::vector<fs::path> paths;
  for (const auto& name : names) {
    const auto path = dir / name;
    if (!fs::is_regular_file(path)) return {};
    paths.push_back(path);
  }
  return paths;
}

bool is_general_archive(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::array<char, 12> header{};
  in.read(header.data(), static_cast<std::streamsize>(header.size()));
  if (!in) return false;
  return header[0] == 'B' && header[1] == 'T' && header[2] == 'D' && header[3] == 'X' &&
         header[8] == 'G' && header[9] == 'N' && header[10] == 'R' && header[11] == 'L';
}

std::string lowered(std::string text) {
  for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

std::string archive_entry_key(const std::string& stored) {
  auto key = lowered(stored);
  std::replace(key.begin(), key.end(), '/', '\\');
  return key;
}

// Load order invariant: light plugins are the .esl files, lowest priority first.
std::vector<fs::path> light_plugins(const fs::path& dir) {
  std::vector<fs::path> paths;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (lowered(entry.path().extension().string()) == ".esl") paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

std::string issue_codes(const fo4::previs::CellPrevisStatus& status) {
  std::vector<std::string> codes;
  codes.reserve(status.issues.size());
  for (const auto& issue : status.issues) codes.emplace_back(fo4::previs::to_string(issue.code));
  std::sort(codes.begin(), codes.end());
  std::string joined;
  for (std::size_t i = 0; i < codes.size(); ++i) {
    if (i != 0) joined.push_back(',');
    joined += codes[i];
  }
  return joined;
}

}  // namespace

// Regression: a misread bcdx header must fail here, not pass synthetic tests only.
TEST(PrevisReal, CdxParsesAndRoundTrips) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const std::map<std::string, std::size_t> expected_rows{
      {"fallout4.cdx", 1460027},   {"dlccoast.cdx", 427946},  {"dlcrobot.cdx", 76520},
      {"dlcnukaworld.cdx", 320839}, {"dlcworkshop01.cdx", 135}, {"dlcworkshop03.cdx", 43525}};
  std::size_t files = 0;
  std::set<std::string> seen;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (lowered(entry.path().extension().string()) != ".cdx") continue;
    SCOPED_TRACE(entry.path().string());
    const auto name = lowered(entry.path().filename().string());
    const auto bytes = read_bytes(entry.path());
    const auto issues = fo4::previs::validate_cdx(bytes);
    ASSERT_TRUE(issues.empty()) << issues.front();
    const auto file = fo4::previs::parse_cdx(bytes);
    EXPECT_EQ(fo4::previs::write_cdx(file), bytes);
    ++files;
    seen.insert(name);
    // Row order invariant: word1 groups then word0, and interior rows keep grid 0.
    for (std::size_t i = 1; i < file.rows.size(); ++i) {
      const auto current = std::tie(file.rows[i].form, file.rows[i].key);
      const auto previous = std::tie(file.rows[i - 1].form, file.rows[i - 1].key);
      ASSERT_FALSE(current < previous) << "CDX rows descend at row " << i;
    }
    std::set<std::uint8_t> interior_kinds;
    for (const auto& row : file.rows) {
      ASSERT_LE(row.form >> 24u, 1u);
      if (fo4::previs::cdx_kind_is_interior(row.key)) {
        ASSERT_EQ(row.key >> 8u, 0u);
        interior_kinds.insert(fo4::previs::cdx_kind(row.key));
      }
    }
    for (const auto kind : interior_kinds) {
      EXPECT_TRUE(kind == 128 || kind == 129 || kind == 130 || kind == 131 || kind == 132 ||
                  kind == 134 || kind == 135 || kind == 136)
          << "unexpected interior kind " << static_cast<int>(kind);
    }
    if (exact_counts_known(build)) {
      const auto expected = expected_rows.find(name);
      ASSERT_NE(expected, expected_rows.end()) << "unexpected CDX file";
      EXPECT_EQ(file.rows.size(), expected->second);
    }
  }
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact CDX counts are not measured for this build";
  EXPECT_EQ(files, 6u);
  EXPECT_EQ(seen.size(), 6u);
}

// Regression: a wrong header word order or chunk entry must fail on real files.
TEST(PrevisReal, CsgParsesAndRoundTrips) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  struct Expected {
    std::size_t rows;
    std::size_t chunks;
  };
  const std::map<std::string, Expected> expected_files{
      {"fallout4 - geometry.csg", {32370, 6841}},
      {"dlccoast - geometry.csg", {14247, 2733}},
      {"dlcnukaworld - geometry.csg", {14988, 2821}},
      {"dlcrobot - geometry.csg", {5290, 866}},
      {"dlcworkshop03 - geometry.csg", {2956, 589}},
      {"dlcworkshop01 - geometry.csg", {18, 1}}};
  std::size_t files = 0;
  std::size_t inflated_chunks = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    const auto name = lowered(entry.path().filename().string());
    constexpr std::string_view k_csg_suffix = " - geometry.csg";
    if (name.size() < k_csg_suffix.size() ||
        name.compare(name.size() - k_csg_suffix.size(), k_csg_suffix.size(), k_csg_suffix) != 0) {
      continue;
    }
    SCOPED_TRACE(entry.path().string());
    const auto bytes = read_bytes(entry.path());
    const auto issues = fo4::previs::validate_csg(bytes);
    ASSERT_TRUE(issues.empty()) << issues.front();
    const auto file = fo4::previs::parse_csg(bytes);
    EXPECT_EQ(fo4::previs::write_csg(file), bytes);
    ++files;
    // Measured invariant: every chunk inflates to 65536 bytes except a smaller final one.
    for (std::size_t i = 0; i < file.chunks.size(); ++i) {
      const auto inflated = fo4::previs::decompress_csg_chunk(file.chunks[i]);
      ++inflated_chunks;
      if (i + 1 < file.chunks.size()) {
        ASSERT_EQ(inflated.size(), fo4::previs::k_csg_inflated_chunk_max);
      } else {
        ASSERT_LE(inflated.size(), fo4::previs::k_csg_inflated_chunk_max);
      }
    }
    if (exact_counts_known(build)) {
      const auto expected = expected_files.find(name);
      ASSERT_NE(expected, expected_files.end()) << "unexpected CSG file";
      EXPECT_EQ(file.rows.size(), expected->second.rows);
      EXPECT_EQ(file.chunks.size(), expected->second.chunks);
    }
  }
  RecordProperty("inflated_chunks", static_cast<int>(inflated_chunks));
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact CSG counts are not measured for this build";
  EXPECT_EQ(files, 6u);
}

// Regression: a wrong CRC variant, body range, banner offset or alignment
// rule must reject a shipped .uvd.
TEST(PrevisReal, UvdTomesInArchives) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const std::map<std::string, std::size_t> expected_archives{
      {"fallout4 - meshesextra.ba2", 966},
      {"dlccoast - main.ba2", 246},
      {"dlcnukaworld - main.ba2", 183},
      {"dlcrobot - main.ba2", 15},
      {"dlcworkshop03 - main.ba2", 9},
      {"dlcworkshop01 - main.ba2", 3}};
  std::vector<fs::path> archives;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (lowered(entry.path().extension().string()) != ".ba2") continue;
    if (!is_general_archive(entry.path())) continue;
    archives.push_back(entry.path());
  }
  std::sort(archives.begin(), archives.end());
  std::size_t checked = 0;
  std::map<std::string, std::size_t> per_archive;
  for (const auto& path : archives) {
    auto reader = fo4::archive::Ba2Reader::open(path);
    std::size_t in_archive = 0;
    for (std::size_t index = 0; index < reader.entries().size(); ++index) {
      const auto& stored = reader.entries()[index].path;
      const auto lowered_path = lowered(stored);
        if (lowered_path.size() < 13 || lowered_path.substr(lowered_path.size() - 4) != ".uvd") {
        continue;
      }
      if (lowered_path.compare(0, 4, "vis\\") != 0 && lowered_path.compare(0, 4, "vis/") != 0) {
        continue;
      }
      ++in_archive;
      const auto bytes = reader.extract(index);
      SCOPED_TRACE(stored);
      // Structure and CRC invariant: every shipped tome must pass the validator.
      const auto issues = fo4::previs::validate_uvd(bytes);
      ASSERT_TRUE(issues.empty()) << issues.front();
      const auto tome = fo4::previs::parse_uvd(bytes);
      ASSERT_GE(bytes.size(), fo4::previs::k_uvd_header_min);
      ASSERT_FALSE(tome.banner.empty());
      ASSERT_EQ(tome.banner.front(), 'T');
      ASSERT_EQ(fo4::previs::crc32c(tome.body()), tome.stored_crc);
    }
    if (in_archive != 0) ++checked;
    per_archive[lowered(path.filename().string())] = in_archive;
  }
  std::size_t total = 0;
  for (const auto& [name, count] : per_archive) {
    static_cast<void>(name);
    total += count;
  }
  RecordProperty("tomes", static_cast<int>(total));
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact tome counts are not measured for this build";
  EXPECT_EQ(checked, 6u);
  EXPECT_EQ(total, 1422u);
  for (const auto& [name, expected] : expected_archives) {
    const auto found = per_archive.find(name);
    ASSERT_NE(found, per_archive.end()) << name;
    EXPECT_EQ(found->second, expected) << name;
  }
}

// Regression: the measured word-pair form must hold and must round-trip on every payload.
TEST(PrevisReal, XcriCensusParses) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const auto paths = official_masters(dir);
  if (paths.empty()) GTEST_SKIP() << "the six installed masters are not present";
  const auto env = fo4::env::Environment::load(paths);
  const std::map<std::string, std::size_t> expected{
      {"Fallout4.esm", 4600}, {"DLCCoast.esm", 1499}, {"DLCNukaWorld.esm", 1172},
      {"DLCRobot.esm", 116},  {"DLCworkshop03.esm", 73}, {"DLCworkshop01.esm", 3}};
  const auto cell_signature = fo4::Signature::from_string("CELL");
  const auto xcri_signature = fo4::Signature::from_string("XCRI");
  const auto xpri_signature = fo4::Signature::from_string("XPRI");
  const auto visi_signature = fo4::Signature::from_string("VISI");
  const auto rvis_signature = fo4::Signature::from_string("RVIS");
  const auto pcmb_signature = fo4::Signature::from_string("PCMB");
  std::size_t all_cells = 0;
  std::size_t all_xcri = 0;
  std::size_t base_cells = 0;
  std::size_t base_visi = 0;
  std::size_t base_rvis = 0;
  std::size_t base_pcmb = 0;
  std::size_t base_xpri = 0;
  for (std::size_t plugin = 0; plugin < env.plugins().size(); ++plugin) {
    const auto& filename = env.plugins()[plugin].key.filename();
    std::size_t cells = 0;
    std::size_t with_xcri = 0;
    for (const auto* ref : env.plugin_records(plugin)) {
      if (ref == nullptr || ref->record == nullptr) continue;
      if (ref->record->header.signature != cell_signature) continue;
      ++cells;
      const bool base = filename == "Fallout4.esm";
      if (base) ++base_cells;
      if (find_subrecord(*ref->record, xpri_signature) != nullptr) {
        if (base) ++base_xpri;
      }
      // Census invariant: VISI and PCMB count nonzero stamps, RVIS counts presence.
      if (sub_u16(find_subrecord(*ref->record, pcmb_signature)) != 0) {
        if (base) ++base_pcmb;
      }
      if (sub_u16(find_subrecord(*ref->record, visi_signature)) != 0) {
        if (base) ++base_visi;
      }
      if (find_subrecord(*ref->record, rvis_signature) != nullptr) {
        if (base) ++base_rvis;
      }
      const auto* xcri = find_subrecord(*ref->record, xcri_signature);
      if (xcri == nullptr) continue;
      ++with_xcri;
      ++all_xcri;
      SCOPED_TRACE(ref->record->form_key.to_string());
      // Measured layout invariant: no vanilla XCRI below form version 0x72.
      ASSERT_GE(ref->record->header.form_version, fo4::previs::k_xcri_min_form_version);
      const auto raw = fo4::previs::parse_xcri_bytes(xcri->data);
      // Wire size invariant: 8 + 4*M + 4*W with W = 2 * refs.
      EXPECT_EQ(xcri->data.size(), static_cast<std::size_t>(8 + 4 * raw.mesh_ids.size() +
                                                            8 * raw.refs.size()));
      EXPECT_EQ(fo4::previs::write_xcri_bytes(raw), xcri->data);
      EXPECT_FALSE(raw.mesh_ids.empty());
    }
    all_cells += cells;
    if (exact_counts_known(build)) {
      const auto expected_count = expected.find(std::string(filename));
      ASSERT_NE(expected_count, expected.end()) << filename;
      EXPECT_EQ(with_xcri, expected_count->second) << filename;
    }
  }
  RecordProperty("cells", static_cast<int>(all_cells));
  RecordProperty("xcri", static_cast<int>(all_xcri));
  RecordProperty("base_cells", static_cast<int>(base_cells));
  RecordProperty("base_visi", static_cast<int>(base_visi));
  RecordProperty("base_rvis", static_cast<int>(base_rvis));
  RecordProperty("base_pcmb", static_cast<int>(base_pcmb));
  RecordProperty("base_xpri", static_cast<int>(base_xpri));
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact XCRI counts are not measured for this build";
  // Census invariant: the six-master XCRI sum and the Fallout4 CELL census must match.
  EXPECT_EQ(all_xcri, 7463u);
  EXPECT_EQ(base_cells, 40165u);
  EXPECT_EQ(base_visi, 6900u);
  EXPECT_EQ(base_rvis, 6445u);
  EXPECT_EQ(base_pcmb, 6902u);
  EXPECT_EQ(base_xpri, 1975u);
}

namespace {

std::map<std::string, std::string> load_expected_broken(const fs::path& dir) {
  std::ifstream in(dir / "previs" / "vanilla_1_11_240_broken.tsv");
  if (!in) throw std::runtime_error("cannot open the pinned broken-cell table");
  std::map<std::string, std::string> expected;
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line.front() == '#') continue;
    const auto tab = line.find('\t');
    if (tab == std::string::npos) throw std::runtime_error("malformed pinned broken-cell row");
    expected.emplace(line.substr(0, tab), line.substr(tab + 1));
  }
  return expected;
}

}  // namespace

// Regression gate: false broken or missed verdicts must fail against the measured
// vanilla list, which must come from measurement and not from this code.
TEST(PrevisReal, VanillaLoadOrderGate) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const auto paths = official_masters(dir);
  if (paths.empty()) GTEST_SKIP() << "the six installed masters are not present";
  const auto env = fo4::env::Environment::load(paths);
  const std::filesystem::path roots[]{dir};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto report = fo4::previs::diagnose_load_order(env, data.indexes());

  std::size_t broken = 0;
  std::size_t outdated = 0;
  std::size_t skipped = 0;
  std::map<std::string, std::string> actual;
  std::map<std::string, std::size_t> actual_broken_per_plugin;
  std::map<std::string, std::size_t> pcmb_stale_per_plugin;
  std::map<std::string, std::size_t> visi_stale_per_plugin;
  std::map<std::string, std::size_t> skipped_per_plugin;
  for (const auto& diagnosis : report) {
    const std::string filename(env.plugins()[diagnosis.plugin].key.filename());
    if (diagnosis.status.skip_unsupported) {
      ++skipped;
      ++skipped_per_plugin[filename];
    }
    if (diagnosis.status.outdated) ++outdated;
    for (const auto& issue : diagnosis.status.outdated_issues) {
      if (issue.code == fo4::previs::PrevisIssue::pcmb_stale) ++pcmb_stale_per_plugin[filename];
      if (issue.code == fo4::previs::PrevisIssue::visi_stale) ++visi_stale_per_plugin[filename];
    }
    if (!diagnosis.status.broken) continue;
    ++broken;
    ++actual_broken_per_plugin[filename];
    actual.emplace(diagnosis.cell.to_string(), issue_codes(diagnosis.status));
  }

  if (exact_counts_known(build)) {
    // Index census invariant: every measured container and merge must be counted.
    EXPECT_EQ(data.summary().cdx_files, 6u);
    EXPECT_EQ(data.summary().csg_files, 6u);
    EXPECT_EQ(data.summary().tome_files, 1422u);
    EXPECT_EQ(data.summary().oc_nif_files, 176569u);
    EXPECT_TRUE(data.summary().problems.empty());
    EXPECT_EQ(data.summary().cdx_rows, 2328992u);
    EXPECT_EQ(data.summary().csg_rows, 69869u);
    EXPECT_EQ(data.summary().csg_chunks, 13851u);
  }

  RecordProperty("cells", static_cast<int>(report.size()));
  RecordProperty("broken", static_cast<int>(broken));
  RecordProperty("outdated", static_cast<int>(outdated));
  RecordProperty("skipped", static_cast<int>(skipped));
  std::size_t skipped_total = 0;
  for (const auto& [name, count] : skipped_per_plugin) {
    static_cast<void>(name);
    skipped_total += count;
  }
  EXPECT_EQ(skipped_total, skipped);
  std::size_t pcmb_stale_total = 0;
  for (const auto& [name, count] : pcmb_stale_per_plugin) {
    static_cast<void>(name);
    pcmb_stale_total += count;
  }
  RecordProperty("pcmb_stale", static_cast<int>(pcmb_stale_total));
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact verdict counts are not measured for this build";

  const auto expected = load_expected_broken(fs::path(FO4_TESTDATA_DIR));
  EXPECT_EQ(report.size(), 62393u);
  EXPECT_EQ(broken, expected.size());
  // A VISI cell without RVIS whose cell-named tome is absent requires a skip, not a verdict.
  EXPECT_EQ(skipped, 207u);
  EXPECT_EQ(skipped_per_plugin["Fallout4.esm"], 205u);
  EXPECT_EQ(skipped_per_plugin["DLCNukaWorld.esm"], 2u);

  // Exact cell list invariant: a falsely broken cell or a missed true gap must change this map.
  std::size_t mismatches = 0;
  for (const auto& [cell, codes] : expected) {
    const auto found = actual.find(cell);
    if (found == actual.end()) {
      ++mismatches;
      if (mismatches <= 10) ADD_FAILURE() << "expected broken but not reported: " << cell;
      continue;
    }
    if (found->second != codes) {
      ++mismatches;
      if (mismatches <= 10) {
        ADD_FAILURE() << cell << " reported " << found->second << ", expected " << codes;
      }
    }
  }
  for (const auto& [cell, codes] : actual) {
    if (expected.contains(cell)) continue;
    ++mismatches;
    if (mismatches <= 10) ADD_FAILURE() << "unexpected broken cell: " << cell << " (" << codes << ")";
  }
  EXPECT_EQ(mismatches, 0u);

  // Staleness must stay outdated, never broken; the date field was measured on vanilla.
  const std::map<std::string, std::size_t> expected_pcmb{
      {"Fallout4.esm", 343},  {"DLCRobot.esm", 94}, {"DLCworkshop01.esm", 3},
      {"DLCCoast.esm", 142},  {"DLCworkshop03.esm", 65}, {"DLCNukaWorld.esm", 114}};
  const std::map<std::string, std::size_t> expected_visi{
      {"Fallout4.esm", 458}, {"DLCRobot.esm", 4}, {"DLCworkshop01.esm", 3},
      {"DLCCoast.esm", 66},  {"DLCworkshop03.esm", 0}, {"DLCNukaWorld.esm", 175}};
  for (const auto& [name, count] : expected_pcmb) {
    EXPECT_EQ(pcmb_stale_per_plugin[name], count) << name;
  }
  for (const auto& [name, count] : expected_visi) {
    EXPECT_EQ(visi_stale_per_plugin[name], count) << name;
  }
  const std::map<std::string, std::size_t> expected_broken_per_plugin{
      {"Fallout4.esm", 6}, {"DLCNukaWorld.esm", 264}, {"DLCCoast.esm", 100}};
  EXPECT_EQ(actual_broken_per_plugin.size(), expected_broken_per_plugin.size());
  for (const auto& [name, count] : expected_broken_per_plugin) {
    EXPECT_EQ(actual_broken_per_plugin[name], count) << name;
  }
}


// Regression: the C ABI must surface the same report the C++ layer builds.
TEST(PrevisReal, CApiReportSurface) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const auto paths = official_masters(dir);
  if (paths.empty()) GTEST_SKIP() << "the six installed masters are not present";

  // A scratch Data folder must hold only the CDX files: every ABI accessor then runs.
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto scratch = fs::temp_directory_path() / ("radicle_previs_capi_" + std::to_string(stamp));
  std::error_code scratch_error;
  fs::create_directories(scratch, scratch_error);
  ASSERT_FALSE(scratch_error) << scratch_error.message();
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (lowered(entry.path().extension().string()) != ".cdx") continue;
    fs::copy_file(entry.path(), scratch / entry.path().filename(),
                  fs::copy_options::overwrite_existing, scratch_error);
    ASSERT_FALSE(scratch_error) << scratch_error.message();
  }

  std::vector<std::string> path_text;
  path_text.reserve(paths.size());
  for (const auto& path : paths) path_text.push_back(path.string());
  std::vector<const char*> path_pointers;
  path_pointers.reserve(path_text.size());
  for (const auto& text : path_text) path_pointers.push_back(text.c_str());

  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(path_pointers.data(), path_pointers.size(), &env),
            RADICLE_STATUS_OK);
  radicle_previs_report* report = nullptr;
  ASSERT_EQ(radicle_previs_scan(env, scratch.string().c_str(), &report), RADICLE_STATUS_OK);
  ASSERT_NE(report, nullptr);

  radicle_previs_summary summary{};
  ASSERT_EQ(radicle_previs_report_summary(report, &summary), RADICLE_STATUS_OK);
  EXPECT_EQ(summary.cells, radicle_previs_report_cell_count(report));
  EXPECT_GT(summary.broken, 0u);
  if (exact_counts_known(build)) {
    EXPECT_EQ(summary.cells, 62393u);
  }

  bool saw_issue = false;
  for (std::size_t index = 0; index < summary.cells; ++index) {
    radicle_previs_cell cell{};
    ASSERT_EQ(radicle_previs_report_cell(report, index, &cell), RADICLE_STATUS_OK);
    if (cell.broken == 0 || cell.issue_count == 0) continue;
    int32_t code = -1;
    const char* detail = nullptr;
    ASSERT_EQ(radicle_previs_report_issue(report, index, 0, &code, &detail), RADICLE_STATUS_OK);
    ASSERT_NE(detail, nullptr);
    ASSERT_NE(cell.cell.plugin, nullptr);
    const char* name = radicle_previs_issue_name(code);
    ASSERT_NE(name, nullptr);
    EXPECT_STRNE(name, "unknown");
    saw_issue = true;
    break;
  }
  EXPECT_TRUE(saw_issue);
  // Range invariant: a truncated or wrapped code must not name a real issue.
  EXPECT_STREQ(radicle_previs_issue_name(7), "visi_stale");
  EXPECT_STREQ(radicle_previs_issue_name(8), "rvis_malformed");
  EXPECT_STREQ(radicle_previs_issue_name(9), "unknown");
  EXPECT_STREQ(radicle_previs_issue_name(256), "unknown");
  EXPECT_STREQ(radicle_previs_issue_name(-1), "unknown");

  radicle_previs_report_destroy(report);
  radicle_env_destroy(env);
  std::error_code cleanup_error;
  fs::remove_all(scratch, cleanup_error);
}

// Detects: a .psg anywhere in the Data tree or a General archive, or a light plugin that ships a CDX, Geometry CSG, Vis tome folder or PreCombined folder, must fail the measured inventory.
TEST(PrevisReal, ShippedPsgAndLightPluginContainersAreAbsent) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  std::set<std::string> light_loose;
  std::vector<std::string> light_prefixes;
  for (const auto& light : light_plugins(dir)) {
    const auto name = lowered(light.filename().string());
    const auto stem = name.substr(0, name.size() - 4);
    light_loose.insert(stem + ".cdx");
    light_loose.insert(stem + " - geometry.csg");
    light_prefixes.push_back("vis\\" + name + "\\");
    light_prefixes.push_back("meshes\\precombined\\" + name + "\\");
  }
  std::vector<std::string> offenders;
  std::error_code walk_error;
  for (fs::recursive_directory_iterator it(dir, walk_error), end; !walk_error && it != end;
       it.increment(walk_error)) {
    const auto name = lowered(it->path().filename().string());
    if (lowered(it->path().extension().string()) == ".psg" || light_loose.contains(name)) {
      offenders.push_back(it->path().string());
    }
  }
  ASSERT_FALSE(walk_error) << walk_error.message();
  std::size_t entries = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (lowered(entry.path().extension().string()) != ".ba2") continue;
    if (!is_general_archive(entry.path())) continue;
    const auto reader = fo4::archive::Ba2Reader::open(entry.path());
    for (const auto& info : reader.entries()) {
      ++entries;
      const auto key = archive_entry_key(info.path);
      if (key.ends_with(".psg")) offenders.push_back(entry.path().string() + ": " + key);
      for (const auto& prefix : light_prefixes) {
        if (key.starts_with(prefix)) offenders.push_back(entry.path().string() + ": " + key);
      }
    }
  }
  RecordProperty("archive_entries", static_cast<int>(entries));
  if (!exact_counts_known(build)) GTEST_SKIP() << "the shipped container inventory is not measured for this build";
  for (std::size_t i = 0; i < offenders.size() && i < 10; ++i) ADD_FAILURE() << offenders[i];
  EXPECT_EQ(offenders.size(), 0u);
}

// Detects: a light winner that stops being a skip, a light previs cell that no longer overrides a Fallout4 cell, or a light winner whose Fallout4.cdx row is missing, must fail.
TEST(PrevisReal, LightPluginPrevisOverridesBaseCellsAndStaysSkipped) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  auto paths = official_masters(dir);
  if (paths.empty()) GTEST_SKIP() << "the six installed masters are not present";
  const auto lights = light_plugins(dir);
  if (lights.empty()) GTEST_SKIP() << "no light plugins are installed";
  paths.insert(paths.end(), lights.begin(), lights.end());
  const auto env = fo4::env::Environment::load(paths);
  const std::filesystem::path roots[]{dir};
  const auto data = fo4::previs::PrevisData::load(roots, env);
  const auto indexes = data.indexes();
  const auto base = env.find_plugin("Fallout4.esm");
  ASSERT_TRUE(base.has_value());
  const auto cell_signature = fo4::Signature::from_string("CELL");
  const auto xcri_signature = fo4::Signature::from_string("XCRI");

  std::size_t light_previs_cells = 0;
  std::size_t light_own_previs_cells = 0;
  for (std::size_t plugin = 0; plugin < env.plugins().size(); ++plugin) {
    if (env.plugins()[plugin].kind != fo4::PluginKind::light) continue;
    for (const auto* ref : env.plugin_records(plugin)) {
      if (ref == nullptr || ref->record == nullptr) continue;
      if (ref->record->header.signature != cell_signature) continue;
      const bool previs = find_subrecord(*ref->record, xcri_signature) != nullptr ||
                          find_subrecord(*ref->record, fo4::Signature::from_string("VISI")) != nullptr ||
                          find_subrecord(*ref->record, fo4::Signature::from_string("PCMB")) != nullptr ||
                          find_subrecord(*ref->record, fo4::Signature::from_string("RVIS")) != nullptr;
      if (!previs) continue;
      ++light_previs_cells;
      if (ref->record->form_key.owner().filename() != "Fallout4.esm") ++light_own_previs_cells;
    }
  }

  std::size_t light_xcri = 0;
  std::size_t owner_rows = 0;
  for (const auto& diagnosis : fo4::previs::diagnose_load_order(env, indexes)) {
    if (env.plugins()[diagnosis.plugin].kind != fo4::PluginKind::light) continue;
    // Skip invariant: a light winner stays unmeasured, never a verdict, until its layout is measured.
    EXPECT_TRUE(diagnosis.status.skip_unsupported) << diagnosis.cell.to_string();
    EXPECT_FALSE(diagnosis.status.broken) << diagnosis.cell.to_string();
    const auto* ref = env.winner(diagnosis.cell);
    if (ref == nullptr || ref->record == nullptr) continue;
    if (find_subrecord(*ref->record, xcri_signature) == nullptr) continue;
    ++light_xcri;
    const auto cell_data = fo4::env::decode_cell(env, *ref);
    fo4::previs::CdxCellTarget target;
    target.cell = diagnosis.cell;
    target.world = cell_data.worldspace;
    if (cell_data.grid) target.grid = *cell_data.grid;
    const auto row = indexes.cdx_row(*base, target);
    if (row.has_value() && *row) {
      ++owner_rows;
    } else {
      ADD_FAILURE() << "no Fallout4.cdx row for light winner " << diagnosis.cell.to_string();
    }
  }

  RecordProperty("light_plugins", static_cast<int>(lights.size()));
  RecordProperty("light_previs_cells", static_cast<int>(light_previs_cells));
  RecordProperty("light_xcri", static_cast<int>(light_xcri));
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact light counts are not measured for this build";
  EXPECT_EQ(lights.size(), 9u);
  EXPECT_EQ(light_previs_cells, 32u);
  EXPECT_EQ(light_own_previs_cells, 0u);
  EXPECT_EQ(light_xcri, 31u);
  EXPECT_EQ(owner_rows, 31u);
}
