#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/signature.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/xcri.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path data_dir_or_skip() {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  return std::filesystem::path{dir};
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
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

}  // namespace

// Regression: a misread bcdx header must fail here, not pass synthetic tests only.
TEST(PrevisReal, CdxParsesAndRoundTrips) {
  const auto dir = data_dir_or_skip();
  record_game_build(detect_game_build(dir));
  bool saw_any = false;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".cdx") continue;
    saw_any = true;
    SCOPED_TRACE(entry.path().string());
    const auto bytes = read_bytes(entry.path());
    const auto file = fo4::previs::parse_cdx(bytes);
    EXPECT_TRUE(fo4::previs::validate_cdx(bytes).empty());
    EXPECT_EQ(fo4::previs::write_cdx(file), bytes);
    RecordProperty("cdx_rows", static_cast<int>(file.rows.size()));
  }
  if (!saw_any) GTEST_SKIP() << "no .cdx files beside the Data folder";
}

// Regression: a wrong header word order or chunk entry must fail on real files.
TEST(PrevisReal, CsgParsesAndRoundTrips) {
  const auto dir = data_dir_or_skip();
  record_game_build(detect_game_build(dir));
  bool saw_any = false;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    // Only .csg: a .psg must fail closed until its layout is measured.
    const auto name = entry.path().filename().string();
    if (name.find("Geometry.csg") == std::string::npos) {
      continue;
    }
    saw_any = true;
    SCOPED_TRACE(entry.path().string());
    const auto bytes = read_bytes(entry.path());
    const auto file = fo4::previs::parse_csg(bytes);
    EXPECT_TRUE(fo4::previs::validate_csg(bytes).empty());
    EXPECT_EQ(fo4::previs::write_csg(file), bytes);
    RecordProperty("csg_rows", static_cast<int>(file.rows.size()));
    RecordProperty("csg_chunks", static_cast<int>(file.chunks.size()));
  }
  if (!saw_any) GTEST_SKIP() << "no Geometry.csg beside the Data folder";
}

// Regression: a wrong CRC variant, body range, banner offset or alignment
// rule must reject a shipped .uvd if the assumption is wrong.
TEST(PrevisReal, UvdParsesAndValidates) {
  const auto dir = data_dir_or_skip();
  record_game_build(detect_game_build(dir));
  const auto vis = dir / "Vis";
  if (!std::filesystem::exists(vis)) GTEST_SKIP() << "no Vis folder beside the Data folder";
  std::size_t checked = 0;
  std::size_t bad = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(vis)) {
    if (entry.path().extension() != ".uvd") continue;
    SCOPED_TRACE(entry.path().string());
    const auto bytes = read_bytes(entry.path());
    const auto issues = fo4::previs::validate_uvd(bytes);
    if (!issues.empty()) {
      ++bad;
      for (const auto& issue : issues) ADD_FAILURE() << issue;
    }
    ++checked;
  }
  RecordProperty("uvd_checked", static_cast<int>(checked));
  RecordProperty("uvd_bad", static_cast<int>(bad));
  if (checked == 0) GTEST_SKIP() << "no .uvd files under Vis";
  EXPECT_EQ(bad, 0u);
}

// Regression: an XCRI decode using xEdit's 8-byte pairs must not be assumed;
// mis-split on real cells; the word-pair form must hold for every cell.
TEST(PrevisReal, XcriCensusParses) {
  const auto dir = data_dir_or_skip();
  record_game_build(detect_game_build(dir));
  std::size_t cells = 0;
  std::size_t with_xcri = 0;
  std::size_t with_pcmb = 0;
  std::size_t with_rvis = 0;
  // One load order: winners_of must reflect the load-order winners the engine reads.
  std::vector<std::filesystem::path> paths;
  for (const auto& name : installed_master_and_cc_plugins(dir)) {
    const auto path = dir / name;
    if (std::filesystem::exists(path)) paths.push_back(path);
  }
  if (paths.empty()) GTEST_SKIP() << "no installed masters beside the Data folder";
  const auto env = fo4::env::Environment::load(paths);
  for (const auto* ref : env.winners_of(fo4::Signature::from_string("CELL"))) {
    ++cells;
    const auto data = fo4::env::decode_cell(env, *ref);
    if (data.precombined_timestamp) ++with_pcmb;
    if (data.previs_root) ++with_rvis;
    if (!data.combined_index) continue;
    ++with_xcri;
    SCOPED_TRACE(ref->record->form_key.to_string());
    EXPECT_FALSE(data.combined_index->refs.empty());
  }
  RecordProperty("cells", static_cast<int>(cells));
  RecordProperty("with_xcri", static_cast<int>(with_xcri));
  RecordProperty("with_pcmb", static_cast<int>(with_pcmb));
  RecordProperty("with_rvis", static_cast<int>(with_rvis));
  if (cells == 0) GTEST_SKIP() << "no CELL records in installed masters";
  EXPECT_GT(with_xcri, 0u);
}
