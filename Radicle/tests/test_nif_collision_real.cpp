#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_max_blobs = 200;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  return std::filesystem::path(lower(path)).extension() == ".nif";
}

// Invariant: both the NIF-embedded form and the standalone .hkx packfile start with this magic.
bool has_packfile_magic(const std::vector<std::byte>& bytes, std::size_t offset) {
  static constexpr std::uint8_t k_magic[4] = {0x57, 0xE0, 0xE0, 0x57};
  if (bytes.size() < offset + 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<std::uint8_t>(bytes[offset + i]) != k_magic[i]) return false;
  }
  return true;
}

}  // namespace

TEST(NifCollisionRealData, VanillaCollisionSystemsCarryPackfileBytes) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t nifs = 0;
  std::size_t physics = 0;
  std::size_t ragdoll = 0;
  std::size_t magic_at_zero = 0;
  std::size_t magic_after_length = 0;
  std::size_t empty_payloads = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_odd_payload;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && physics + ragdoll < k_max_blobs; ++i) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++nifs;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
          if (blob.type == "bhkPhysicsSystem") {
            ++physics;
            if (blob.bytes.empty()) ++empty_payloads;
            if (has_packfile_magic(blob.bytes, 0)) {
              ++magic_at_zero;
            } else if (has_packfile_magic(blob.bytes, 4)) {
              ++magic_after_length;
            } else if (first_odd_payload.empty()) {
              first_odd_payload = path;
            }
          } else if (blob.type == "bhkRagdollSystem") {
            ++ragdoll;
          } else {
            ADD_FAILURE() << path << ": unexpected collision block type " << blob.type;
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
    if (physics + ragdoll >= k_max_blobs) break;
  }

  std::cout << "nifs " << nifs << ", bhkPhysicsSystem " << physics << ", bhkRagdollSystem " << ragdoll
            << ", packfile at 0: " << magic_at_zero << ", packfile after a 4-byte prefix: "
            << magic_after_length << ", empty payloads " << empty_payloads << ", failures " << failures
            << '\n';
  RecordProperty("nif_count", std::to_string(nifs));
  RecordProperty("physics_system_count", std::to_string(physics));
  RecordProperty("ragdoll_system_count", std::to_string(ragdoll));
  RecordProperty("packfile_at_zero_count", std::to_string(magic_at_zero));
  RecordProperty("packfile_after_prefix_count", std::to_string(magic_after_length));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GT(nifs, 0U);
  EXPECT_GT(physics, 0U);
  EXPECT_TRUE(first_odd_payload.empty())
      << "a bhkPhysicsSystem payload does not start with a packfile magic at 0 or 4: " << first_odd_payload;
  EXPECT_EQ(magic_at_zero + magic_after_length + empty_payloads, physics);
}
