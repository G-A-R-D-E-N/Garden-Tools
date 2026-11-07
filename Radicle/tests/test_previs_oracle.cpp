#include "fo4/io/sha256.hpp"
#include "fo4/nif/nif.hpp"

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
    EXPECT_NO_THROW((void)fo4::nif::read_nif(bytes)) << rel;
  }
}
