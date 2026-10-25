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

// Constraint: both candidate layouts start with the packfile magic, at offset 0 or offset 4.
bool has_packfile_magic(const std::vector<std::byte>& bytes, std::size_t offset) {
  static constexpr std::uint8_t k_magic[4] = {0x57, 0xE0, 0xE0, 0x57};
  if (bytes.size() < offset + 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<std::uint8_t>(bytes[offset + i]) != k_magic[i]) return false;
  }
  return true;
}

std::string hex_prefix(const std::vector<std::byte>& bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (std::size_t i = 0; i < bytes.size() && i < 8; ++i) {
    const auto value = std::to_integer<std::uint8_t>(bytes[i]);
    if (!out.empty()) out.push_back(' ');
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 0x0FU]);
  }
  return out;
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
  std::size_t odd_payloads = 0;
  std::size_t attributed = 0;
  std::size_t attributed_with_node = 0;
  std::size_t flagged = 0;
  std::size_t unattributed = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_odd_payload;
  std::string first_unattributed;

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
            if (blob.bytes.empty()) {
              ++empty_payloads;
            } else if (has_packfile_magic(blob.bytes, 0)) {
              ++magic_at_zero;
            } else if (has_packfile_magic(blob.bytes, 4)) {
              ++magic_after_length;
            } else {
              // Invariant: prefix layout is unresolved, so this case is counted and never asserted.
              ++odd_payloads;
              if (first_odd_payload.empty()) first_odd_payload = path + " (" + hex_prefix(blob.bytes) + ")";
            }
          } else if (blob.type == "bhkRagdollSystem") {
            ++ragdoll;
          } else {
            ADD_FAILURE() << path << ": unexpected collision block type " << blob.type;
          }
          if (blob.owner >= 0) {
            ++attributed;
            if (blob.target_node >= 0) ++attributed_with_node;
            if (blob.flags != 0) ++flagged;
          } else {
            ++unattributed;
            if (first_unattributed.empty()) {
              first_unattributed = path + " block " + std::to_string(blob.block);
            }
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
            << ", packfile at 0: " << magic_at_zero << ", packfile after a 4-byte prefix: " << magic_after_length
            << ", empty payloads " << empty_payloads << ", payloads with neither magic: " << odd_payloads
            << ", attributed systems " << attributed << ", with a target node " << attributed_with_node
            << ", with object flags " << flagged
            << ", unattributed systems " << unattributed << ", failures " << failures << '\n';
  if (!first_odd_payload.empty()) {
    std::cout << "first payload without a packfile magic: " << first_odd_payload << '\n';
  }
  if (!first_unattributed.empty()) {
    std::cout << "first system without a collision object: " << first_unattributed << '\n';
  }
  RecordProperty("nif_count", std::to_string(nifs));
  RecordProperty("physics_system_count", std::to_string(physics));
  RecordProperty("ragdoll_system_count", std::to_string(ragdoll));
  RecordProperty("packfile_at_zero_count", std::to_string(magic_at_zero));
  RecordProperty("packfile_after_prefix_count", std::to_string(magic_after_length));
  RecordProperty("payload_without_known_magic_count", std::to_string(odd_payloads));
  RecordProperty("attributed_system_count", std::to_string(attributed));
  RecordProperty("target_node_count", std::to_string(attributed_with_node));
  RecordProperty("unattributed_system_count", std::to_string(unattributed));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_GT(nifs, 0U);
  EXPECT_GT(physics, 0U);
}
