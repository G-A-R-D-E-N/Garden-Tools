#include "fo4/archive/ba2_reader.hpp"
#include "fo4/c_api_env.h"
#include "fo4/c_api_previs.h"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/error.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shape_mesh.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "fo4/previs/previs_data.hpp"
#include "fo4/previs/shared_geom_extra.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "fo4/signature.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

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

// Regression: a per-cell CSG verdict keyed on anything but the _OC.NIF data offset must fail on the shipped containers.
TEST(PrevisReal, CsgRowTableJudgesEveryShippedContainer) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  constexpr std::string_view k_csg_suffix = " - geometry.csg";
  std::size_t files = 0;
  std::size_t rows_total = 0;
  std::size_t closing_rows = 0;
  std::size_t misses = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    const auto name = lowered(entry.path().filename().string());
    if (name.size() < k_csg_suffix.size() ||
        name.compare(name.size() - k_csg_suffix.size(), k_csg_suffix.size(), k_csg_suffix) != 0) {
      continue;
    }
    SCOPED_TRACE(entry.path().string());
    const auto file = fo4::previs::parse_csg(read_bytes(entry.path()));
    const auto index = fo4::previs::CsgRowIndex::build(file);
    ASSERT_EQ(index.rows().size(), file.rows.size());
    ASSERT_FALSE(index.rows().empty());
    EXPECT_EQ(index.unsorted_rows(), 0u) << "a shipped row table is not sorted by stream offset";
    ++files;
    rows_total += index.rows().size();

    std::vector<fo4::previs::CsgMeshSpan> meshes;
    meshes.reserve(index.rows().size());
    for (std::size_t i = 0; i < index.rows().size(); ++i) {
      const auto& row = index.rows()[i];
      ASSERT_TRUE(fo4::previs::csg_vertex_stride(row).has_value()) << "row " << i << " uses an unmeasured descriptor";
      ASSERT_EQ(index.find(row.data_offset), &row) << "row " << i << " is not reachable by its offset";
      meshes.push_back({static_cast<std::uint32_t>(i + 1), row.data_offset, row.vertex_count, row.index_count,
                        row.vertex_desc});
      if (i + 1 < index.rows().size()) {
        const auto delta = std::uint64_t{index.rows()[i + 1].data_offset} - std::uint64_t{row.data_offset};
        if (delta == fo4::previs::csg_row_stream_bytes(row)) {
          ++closing_rows;
        } else {
          ++misses;
        }
      } else {
        EXPECT_EQ(std::uint64_t{row.data_offset} + fo4::previs::csg_row_stream_bytes(row), index.stream_length())
            << "the last row's mesh does not end at the inflated stream";
      }
    }

    const auto verdict = fo4::previs::check_csg_cell(index, meshes);
    ASSERT_TRUE(verdict.checked);
    ASSERT_FALSE(verdict.broken) << verdict.issues.front();

    auto tampered = meshes;
    ++tampered.front().index_count;
    EXPECT_TRUE(fo4::previs::check_csg_cell(index, tampered).broken);
  }
  RecordProperty("rows", static_cast<int>(rows_total));
  RecordProperty("rows_closing", static_cast<int>(closing_rows));
  RecordProperty("rows_missing_the_mesh_end", static_cast<int>(misses));
  if (!exact_counts_known(build)) GTEST_SKIP() << "exact CSG counts are not measured for this build";
  EXPECT_EQ(files, 6u);
  EXPECT_EQ(rows_total, 69869u);
  EXPECT_EQ(closing_rows, 69863u);
  EXPECT_EQ(misses, 0u);
}

// Regression: a wrong CRC variant, body range, banner offset or alignment rule must reject a shipped .uvd.
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

// Regression gate: false broken or missed verdicts must fail against the measured vanilla list, which must come from measurement and not from this code.
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
  std::size_t csg_rows_missing = 0;
  std::size_t csg_not_run = 0;
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
    for (const auto& issue : diagnosis.status.issues) {
      if (issue.code == fo4::previs::PrevisIssue::csg_rows_missing) ++csg_rows_missing;
    }
    for (const auto& skip : diagnosis.status.skips) {
      if (skip.rfind("geometry check did not run", 0) == 0) ++csg_not_run;
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
  RecordProperty("csg_rows_missing", static_cast<int>(csg_rows_missing));
  RecordProperty("csg_not_run", static_cast<int>(csg_not_run));
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
  // Regression: a CSG row verdict that misses or invents a row must break vanilla cells here first.
  EXPECT_EQ(csg_rows_missing, 0u);
  // Regression: a vanilla cell whose geometry check did not run must fail here.
  EXPECT_EQ(csg_not_run, 0u);
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
  EXPECT_STREQ(radicle_previs_issue_name(9), "pcmb_ref_newer");
  EXPECT_STREQ(radicle_previs_issue_name(10), "pcmb_record_untimed");
  EXPECT_STREQ(radicle_previs_issue_name(11), "previs_disabled_by_override");
  EXPECT_STREQ(radicle_previs_issue_name(12), "pcmb_dropped_by_override");
  EXPECT_STREQ(radicle_previs_issue_name(13), "tome_unreadable");
  EXPECT_STREQ(radicle_previs_issue_name(14), "csg_rows_missing");
  EXPECT_STREQ(radicle_previs_issue_name(15), "unknown");
  EXPECT_STREQ(radicle_previs_issue_name(256), "unknown");
  EXPECT_STREQ(radicle_previs_issue_name(-1), "unknown");

  radicle_previs_report_destroy(report);
  radicle_env_destroy(env);
  std::error_code cleanup_error;
  fs::remove_all(scratch, cleanup_error);
}

// Constraint: a .psg or a light plugin shipping previs index files in the measured inventory.
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

// Constraint: a light winner that stops being a skip or loses its Fallout4.cdx row.
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

// Constraint: a wrong reading of where combined geometry lives: an object past the stream or indexing vertices it lacks.
TEST(PrevisReal, SharedGeometryOffsetsAddressVerticesAndTrianglesInTheCsg) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"DLCworkshop01.esm", "DLCRobot.esm", "DLCworkshop03.esm",
                                         "DLCCoast.esm"};
  std::size_t plugins_checked = 0;
  std::size_t nifs = 0;
  std::size_t objects = 0;
  for (const auto& plugin : plugins) {
    const auto stem = fs::path(plugin).stem().string();
    const auto csg_path = dir / (stem + " - Geometry.csg");
    const auto archive_path = dir / (stem + " - Main.ba2");
    if (!fs::is_regular_file(csg_path) || !fs::is_regular_file(archive_path)) continue;
    SCOPED_TRACE(plugin);
    const auto csg = fo4::previs::parse_csg(read_bytes(csg_path));
    std::vector<std::byte> stream;
    stream.reserve(csg.chunks.size() * fo4::previs::k_csg_inflated_chunk_max);
    for (const auto& chunk : csg.chunks) {
      const auto inflated = fo4::previs::decompress_csg_chunk(chunk);
      stream.insert(stream.end(), inflated.begin(), inflated.end());
    }
    const auto folder = "meshes\\precombined\\" + lowered(plugin) + "\\";
    auto reader = fo4::archive::Ba2Reader::open(archive_path);
    std::size_t in_plugin = 0;
    for (std::size_t index = 0; index < reader.entries().size(); ++index) {
      const auto key = archive_entry_key(reader.entries()[index].path);
      if (key.compare(0, folder.size(), folder) != 0) continue;
      if (key.size() < 7 || key.compare(key.size() - 7, 7, "_oc.nif") != 0) continue;
      SCOPED_TRACE(key);
      const auto bytes = reader.extract(index);
      const auto nif = fo4::nif::read_nif(bytes);
      ++nifs;
      ++in_plugin;
      for (const auto& block : nif.blocks()) {
        if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
        const auto header = fo4::previs::read_shared_geom_header(
            std::span<const std::byte>(bytes).subspan(block.offset, block.size));
        ASSERT_TRUE(header.has_value());
        for (const auto& object : header->objects) {
          const auto vertex_bytes = std::size_t{object.vertex_count} * object.vertex_bytes();
          const auto end = std::size_t{object.data_offset} + object.stream_bytes();
          ASSERT_GT(object.vertex_bytes(), 0u);
          ASSERT_LE(end, stream.size());
          const auto* triangles = stream.data() + object.data_offset + vertex_bytes;
          for (std::size_t word = 0; word < object.triangles() * 3; ++word) {
            std::uint16_t vertex = 0;
            std::memcpy(&vertex, triangles + 2 * word, 2);
            ASSERT_LT(vertex, object.vertex_count) << "triangle word " << word;
          }
          ++objects;
        }
      }
    }
    if (in_plugin != 0) ++plugins_checked;
  }
  RecordProperty("oc_nifs", static_cast<int>(nifs));
  RecordProperty("objects", static_cast<int>(objects));
  if (nifs == 0) GTEST_SKIP() << "no DLC ships _OC.NIF files in its own plugin folder here";
  EXPECT_GT(plugins_checked, 0u);
  EXPECT_GT(objects, 0u);
}

// Constraint: a wrong reading of where a shipped _OC object's geometry lives; measured 429 objects and 275 headers.
TEST(PrevisReal, SharedObjectGeometryIsAddressedThroughTheCsgTable) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  record_game_build(detect_game_build(dir));
  const std::vector<std::string> plugins{"DLCworkshop01.esm", "DLCRobot.esm", "DLCworkshop03.esm",
                                         "DLCCoast.esm"};
  constexpr std::size_t k_oc_per_plugin = 12;
  std::size_t objects = 0;
  std::size_t headers = 0;
  std::size_t hash_mismatches = 0;
  std::size_t slab_violations = 0;
  std::size_t index_violations = 0;
  std::size_t count_violations = 0;
  std::size_t shapes_seen = 0;
  for (const auto& plugin : plugins) {
    const auto stem = fs::path(plugin).stem().string();
    const auto csg_path = dir / (stem + " - Geometry.csg");
    const auto archive_path = dir / (stem + " - Main.ba2");
    if (!fs::is_regular_file(csg_path) || !fs::is_regular_file(archive_path)) continue;
    SCOPED_TRACE(plugin);
    const auto csg = fo4::previs::parse_csg(read_bytes(csg_path));
    std::vector<std::byte> stream;
    stream.reserve(csg.chunks.size() * fo4::previs::k_csg_inflated_chunk_max);
    for (const auto& chunk : csg.chunks) {
      const auto inflated = fo4::previs::decompress_csg_chunk(chunk);
      stream.insert(stream.end(), inflated.begin(), inflated.end());
    }
    const auto hash = fo4::previs::csg_file_hash(plugin);
    const auto folder = "meshes\\precombined\\" + lowered(plugin) + "\\";
    auto reader = fo4::archive::Ba2Reader::open(archive_path);
    std::size_t in_plugin = 0;
    for (std::size_t entry = 0; entry < reader.entries().size() && in_plugin < k_oc_per_plugin; ++entry) {
      const auto key = archive_entry_key(reader.entries()[entry].path);
      if (key.compare(0, folder.size(), folder) != 0) continue;
      if (key.size() < 7 || key.compare(key.size() - 7, 7, "_oc.nif") != 0) continue;
      ++in_plugin;
      SCOPED_TRACE(key);
      const auto bytes = reader.extract(entry);
      const auto nif = fo4::nif::read_nif(bytes);
      // Constraint: the count of shapes is recorded, not asserted, because the shipped _OC stores geometry only in the CSG.
      for (std::uint32_t block_index = 0; block_index < nif.blocks().size(); ++block_index) {
        if (fo4::nif::read_shape_mesh(nif, block_index).has_value()) ++shapes_seen;
      }
      for (const auto& block : nif.blocks()) {
        if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
        const auto header = fo4::previs::read_shared_geom_header(
            std::span<const std::byte>(bytes).subspan(block.offset, block.size));
        ASSERT_TRUE(header.has_value());
        ++headers;
        std::uint64_t counted_vertices = 0;
        std::uint64_t counted_triangles = 0;
        for (const auto& object : header->objects) {
          counted_vertices += std::uint64_t{object.vertex_count} * object.combined_count;
          counted_triangles += std::uint64_t{object.triangles()} * object.combined_count;
        }
        // Constraint: the header counts cover every instance's slab, so a mismatch means the table is misread.
        if (counted_vertices != header->vertex_count || counted_triangles != header->triangle_count) {
          ++count_violations;
        }
        for (const auto& object : header->objects) {
          // Constraint: an object must name the container it was read with, or it reads another plugin's stream.
          if (object.file_hash != hash) {
            ++hash_mismatches;
            continue;
          }
          ++objects;
          // Constraint: the object's slab and triangle words must lie inside this plugin's inflated stream.
          if (std::size_t{object.data_offset} + object.stream_bytes() > stream.size()) {
            ++slab_violations;
            continue;
          }
          const auto vertex_bytes = std::size_t{object.vertex_count} * object.vertex_bytes();
          const auto* triangles = stream.data() + object.data_offset + vertex_bytes;
          for (std::size_t word = 0; word < object.triangles() * 3; ++word) {
            std::uint16_t vertex = 0;
            std::memcpy(&vertex, triangles + 2 * word, 2);
            if (vertex >= object.vertex_count) {
              ++index_violations;
              break;
            }
          }
        }
      }
    }
  }
  RecordProperty("objects", static_cast<int>(objects));
  RecordProperty("headers", static_cast<int>(headers));
  RecordProperty("shapes_seen", static_cast<int>(shapes_seen));
  RecordProperty("hash_mismatches", static_cast<int>(hash_mismatches));
  RecordProperty("slab_violations", static_cast<int>(slab_violations));
  RecordProperty("index_violations", static_cast<int>(index_violations));
  RecordProperty("count_violations", static_cast<int>(count_violations));
  if (objects == 0) GTEST_SKIP() << "no DLC _OC object addresses a CSG of this Data folder";
  EXPECT_GT(headers, 0u);
  EXPECT_EQ(hash_mismatches, 0u);
  EXPECT_EQ(slab_violations, 0u);
  EXPECT_EQ(index_violations, 0u);
  EXPECT_EQ(count_violations, 0u);
}

// Constraint: a generated layout must equal vanilla in stride, attribute order and every byte for a model vanilla previs also holds.
TEST(PrevisReal, GeneratedLayoutsMatchVanillaCsgBytes) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  const auto build = detect_game_build(dir);
  record_game_build(build);
  const auto archive_path = dir / "Fallout4 - Meshes.ba2";
  if (!fs::is_regular_file(archive_path)) GTEST_SKIP() << "no base-game Meshes archive here";

  struct Container {
    std::vector<std::byte> stream;
    std::vector<fo4::previs::CsgMeshRow> rows;
  };
  std::vector<Container> containers;
  std::map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> by_first_vertex;
  for (const std::string stem : {"DLCRobot", "DLCworkshop03"}) {
    const auto path = dir / (stem + " - Geometry.csg");
    if (!fs::is_regular_file(path)) continue;
    const auto file = fo4::previs::parse_csg(read_bytes(path));
    Container container;
    container.stream = fo4::previs::inflate_csg_stream(file);
    for (const auto& raw : file.rows) container.rows.push_back(fo4::previs::decode_csg_row(raw.bytes));
    for (std::size_t i = 0; i < container.rows.size(); ++i) {
      const auto& row = container.rows[i];
      const auto stride = fo4::previs::csg_vertex_stride(row);
      ASSERT_TRUE(stride.has_value()) << stem << " row " << i;
      ASSERT_EQ(fo4::nif::vertex_layout_fault(fo4::nif::VertexDesc{row.vertex_desc}), "") << stem << " row " << i;
      std::string key(reinterpret_cast<const char*>(&row.vertex_desc), sizeof row.vertex_desc);
      key.append(reinterpret_cast<const char*>(container.stream.data() + row.data_offset), *stride);
      by_first_vertex[key].emplace_back(containers.size(), i);
    }
    containers.push_back(std::move(container));
  }
  if (containers.empty()) GTEST_SKIP() << "no DLC Geometry.csg here";

  constexpr std::array<fo4::nif::VertexAttribute, 5> attributes{
      fo4::nif::VertexAttribute::Position, fo4::nif::VertexAttribute::UV0, fo4::nif::VertexAttribute::Normal,
      fo4::nif::VertexAttribute::Binormal, fo4::nif::VertexAttribute::Color};
  std::map<std::pair<std::uint64_t, bool>, std::size_t> exact;
  std::size_t shapes = 0;
  std::size_t refused = 0;
  std::size_t first_vertex_only = 0;
  auto reader = fo4::archive::Ba2Reader::open(archive_path);
  for (std::size_t entry = 0; entry < reader.entries().size(); ++entry) {
    const auto key = archive_entry_key(reader.entries()[entry].path);
    if (key.size() < 4 || key.compare(key.size() - 4, 4, ".nif") != 0) continue;
    if (key.compare(0, 14, "meshes\\actors\\") == 0 || key.find("\\precombined\\") != std::string::npos) continue;
    const auto bytes = reader.extract(entry);
    if (bytes.empty()) continue;
    std::optional<fo4::nif::NifFile> nif;
    try {
      nif = fo4::nif::read_nif(bytes);
    } catch (const std::exception&) {
      continue;
    }
    for (std::uint32_t block = 0; block < nif->blocks().size(); ++block) {
      const auto shape = fo4::nif::read_shape_mesh(*nif, block);
      if (!shape.has_value()) continue;
      ++shapes;
      std::optional<fo4::previs::CsgMeshObject> mesh;
      try {
        mesh = fo4::previs::csg_mesh_from_shape(*shape);
      } catch (const fo4::parse_error&) {
        ++refused;
        continue;
      }
      const auto stride = fo4::previs::csg_vertex_stride(mesh->vertex_desc);
      ASSERT_TRUE(stride.has_value()) << key;
      ASSERT_EQ(mesh->vertices.size(), std::size_t{mesh->vertex_count} * *stride) << key;
      std::string first(reinterpret_cast<const char*>(&mesh->vertex_desc), sizeof mesh->vertex_desc);
      first.append(reinterpret_cast<const char*>(mesh->vertices.data()), *stride);
      const auto held = by_first_vertex.find(first);
      if (held == by_first_vertex.end()) continue;
      const fo4::nif::VertexDesc vanilla{mesh->vertex_desc};
      const bool narrowed = fo4::nif::VertexDesc{shape->vertex_desc}.full_precision();
      bool matched = false;
      for (const auto& [which, row_index] : held->second) {
        const auto& container = containers[which];
        const auto& row = container.rows[row_index];
        if (mesh->vertex_count > row.vertex_count) continue;
        bool same = true;
        for (std::size_t v = 0; v < mesh->vertex_count && same; ++v) {
          for (const auto attribute : attributes) {
            if (attribute != fo4::nif::VertexAttribute::Position && !vanilla.has(attribute)) continue;
            const auto offset = attribute == fo4::nif::VertexAttribute::Position ? 0u : vanilla.attribute_offset(attribute);
            const auto width = vanilla.attribute_bytes(attribute);
            if (std::memcmp(container.stream.data() + row.data_offset + v * *stride + offset,
                            mesh->vertices.data() + v * *stride + offset, width) != 0) {
              same = false;
              break;
            }
          }
        }
        if (same) {
          matched = true;
          break;
        }
      }
      if (matched) {
        ++exact[{mesh->vertex_desc, narrowed}];
      } else {
        ++first_vertex_only;
      }
    }
  }
  RecordProperty("shapes", static_cast<int>(shapes));
  RecordProperty("refused", static_cast<int>(refused));
  RecordProperty("first_vertex_only", static_cast<int>(first_vertex_only));
  for (const auto& [kind, count] : exact) {
    RecordProperty((std::string(kind.second ? "narrowed_" : "kept_") + std::to_string((kind.first & 0xFu) * 4u)).c_str(),
                   static_cast<int>(count));
  }
  if (!exact_counts_known(build)) GTEST_SKIP() << "the vanilla matches are not measured for this build";
  EXPECT_GT((exact[{0x0001B00000430205ull, false}]), 0u);
  EXPECT_GT((exact[{0x0003B00005430206ull, false}]), 0u);
  EXPECT_GT((exact[{0x0001B00000430205ull, true}]), 0u);
}

// Constraint: a reader that stops at 32 combined records must fail, because shipped objects carry up to 630.
TEST(PrevisReal, ShippedSharedExtrasCarryMoreThanThirtyTwoInstances) {
  const auto maybe_dir = data_dir_from_env();
  if (!maybe_dir) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const fs::path& dir = *maybe_dir;
  record_game_build(detect_game_build(dir));
  const auto archive_path = dir / "Fallout4 - MeshesExtra.ba2";
  if (!fs::is_regular_file(archive_path)) GTEST_SKIP() << "no base-game MeshesExtra archive here";
  auto reader = fo4::archive::Ba2Reader::open(archive_path);
  std::size_t extras = 0;
  std::size_t objects = 0;
  std::size_t above_32 = 0;
  std::size_t unreadable = 0;
  std::size_t max_combined = 0;
  for (std::size_t index = 0; index < reader.entries().size(); ++index) {
    const auto key = archive_entry_key(reader.entries()[index].path);
    if (key.size() < 7 || key.compare(key.size() - 7, 7, "_oc.nif") != 0) continue;
    const auto bytes = reader.extract(index);
    if (bytes.empty()) continue;
    const auto nif = fo4::nif::read_nif(bytes);
    for (const auto& block : nif.blocks()) {
      if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
      ++extras;
      const auto header = fo4::previs::read_shared_geom_header(
          std::span<const std::byte>(bytes).subspan(block.offset, block.size));
      // Constraint: every shipped extra must parse, so a cap or a wrong bound shows up as an unreadable block.
      if (!header.has_value()) {
        ++unreadable;
        continue;
      }
      for (const auto& object : header->objects) {
        ++objects;
        max_combined = std::max<std::size_t>(max_combined, object.combined_count);
        if (object.combined_count > 32) ++above_32;
      }
    }
  }
  RecordProperty("extras", static_cast<int>(extras));
  RecordProperty("objects", static_cast<int>(objects));
  RecordProperty("objects_above_32", static_cast<int>(above_32));
  RecordProperty("max_combined", static_cast<int>(max_combined));
  if (extras == 0) GTEST_SKIP() << "no _OC.NIF file in the base-game MeshesExtra archive";
  EXPECT_EQ(unreadable, 0u);
  EXPECT_GT(above_32, 0u);
  EXPECT_GE(max_combined, 100u);
}
