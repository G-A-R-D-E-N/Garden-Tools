#include "fo4/io/sha256.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/previs/bake_oc.hpp"
#include "fo4/previs/shared_geom_extra.hpp"

#include <fstream>
#include <filesystem>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

fs::path oracle_root() {
  return fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles";
}

std::vector<std::byte> read_bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::vector<std::byte> out(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    out[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return out;
}

}  // namespace

// Regression: pinned _OC.NIF oracles must match manifest SHA-256 and parse as FO4 NIF.
TEST(PrevisOracle, ManifestMatchesPinnedBytes) {
  const auto manifest_path = oracle_root() / "manifest.json";
  if (!fs::exists(manifest_path)) GTEST_SKIP() << "missing " << manifest_path;
  const auto manifest = nlohmann::json::parse(std::ifstream(manifest_path));
  const auto& files = manifest.at("files");
  if (files.empty()) GTEST_SKIP() << "no oracle files pinned yet";

  for (const auto& row : files) {
    const auto rel = row.at("path").get<std::string>();
    const auto expected = row.at("sha256").get<std::string>();
    const auto path = oracle_root() / fs::path(rel).filename();
    ASSERT_TRUE(fs::exists(path)) << rel;
    const auto bytes = read_bytes(path);
    ASSERT_EQ(fo4::io::sha256_hex(bytes), expected) << rel;
    const auto nif = fo4::nif::read_nif(bytes);
    bool shared = false;
    for (const auto& block : nif.blocks()) {
      if (block.type == "BSPackedCombinedSharedGeomDataExtra") shared = true;
    }
    EXPECT_TRUE(shared) << rel;
  }
}

// Regression: bake must preserve pinned oracle bytes when output is a valid _OC.NIF path.
TEST(PrevisOracle, BakePreservesPinnedFixture) {
  namespace previs = fo4::previs;
  const auto manifest_path = oracle_root() / "manifest.json";
  if (!fs::exists(manifest_path)) GTEST_SKIP();
  const auto manifest = nlohmann::json::parse(std::ifstream(manifest_path));
  if (manifest.at("files").empty()) GTEST_SKIP();
  const auto& row = manifest.at("files").front();
  const auto path = oracle_root() / fs::path(row.at("path").get<std::string>()).filename();
  const auto bytes = read_bytes(path);
  const auto dir = fs::temp_directory_path() / "previs_oracle_bake";
  fs::remove_all(dir);
  const auto out = dir / path.filename();
  ASSERT_EQ(previs::bake_oc_single_source_bytes(bytes, out), previs::BakeOcStatus::ok);
  ASSERT_EQ(fo4::io::sha256_hex(read_bytes(out)), row.at("sha256").get<std::string>());
}

// Regression: Shared extra blocks in pinned _OC.NIF oracles must match the measured no-embedded-geometry layout.
TEST(PrevisSharedGeomExtra, OracleBlockLayout) {
  const auto manifest_path = oracle_root() / "manifest.json";
  if (!fs::exists(manifest_path)) GTEST_SKIP();
  const auto manifest = nlohmann::json::parse(std::ifstream(manifest_path));
  std::size_t walked_blocks = 0;
  for (const auto& row : manifest.at("files")) {
    const auto path = oracle_root() / fs::path(row.at("path").get<std::string>()).filename();
    const auto bytes = read_bytes(path);
    const auto nif = fo4::nif::read_nif(bytes);
    for (const auto& block : nif.blocks()) {
      if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
      const auto span = std::span<const std::byte>(bytes).subspan(block.offset, block.size);
      if (!fo4::previs::walk_shared_combined_geom_extra(span)) continue;
      ++walked_blocks;
      fo4::previs::SharedGeomExtraLayout layout;
      if (fo4::previs::measure_shared_combined_geom_extra(span, layout)) {
        constexpr std::size_t k_ni_extra_name = 4;
        EXPECT_EQ(block.size,
                  k_ni_extra_name + fo4::previs::shared_combined_geom_extra_size(layout.object_count,
                                                                                 layout.combined_per_object))
            << path.filename().string();
      }
      const auto detected = fo4::previs::detect_shared_geom_layout(span);
      ASSERT_TRUE(detected.has_value()) << path.filename().string();
      EXPECT_GE(detected->object_count, 1u);
    }
  }
  EXPECT_GT(walked_blocks, 0u);
}

