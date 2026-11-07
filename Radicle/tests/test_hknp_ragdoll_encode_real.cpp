#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode_ragdoll.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::size_t> packfile_starts(const std::vector<std::byte>& bytes) {
  static constexpr std::array<std::byte, 4> k_magic = {std::byte{0x57}, std::byte{0xE0}, std::byte{0xE0},
                                                       std::byte{0x57}};
  std::vector<std::size_t> starts;
  if (bytes.size() < 8) return starts;
  auto hit = std::search(bytes.begin() + 4, bytes.end(), k_magic.begin(), k_magic.end());
  while (hit != bytes.end()) {
    starts.push_back(static_cast<std::size_t>(hit - bytes.begin()));
    hit = std::search(hit + 4, bytes.end(), k_magic.begin(), k_magic.end());
  }
  return starts;
}

std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8 * i);
  }
  return value;
}

bool mentions_ragdoll(const std::vector<std::byte>& bytes) {
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return text.find("hknpRagdollData") != std::string_view::npos;
}

}  // namespace

// Detects: a vanilla ragdoll whose root, skeleton, bindings, bone links, material records or constraint objects do not rebuild to the stored bytes, or a ragdoll the decoder rejects.
TEST(HknpRagdollRealData, EveryVanillaRagdollRebuildsByteForByte) {
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
  std::size_t ragdolls = 0;
  std::size_t rebuilt = 0;
  std::size_t plain_mismatches = 0;
  std::size_t mismatches = 0;
  std::size_t failures = 0;
  std::string first_plain;
  std::string first_mismatch;
  std::string first_failure;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> kits;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (path.size() < 4 || path.compare(path.size() - 4, 4, ".nif") != 0) continue;
      const auto bytes = reader.extract(i);
      if (!mentions_ragdoll(bytes)) continue;
      for (const std::size_t m : packfile_starts(bytes)) {
        const std::size_t length = u32_at(bytes, m - 4);
        if (length < 0x50 || length > bytes.size() - m) continue;
        const std::span<const std::byte> packfile(bytes.data() + m, length);
        const std::vector<std::byte> stored(packfile.begin(), packfile.end());
        try {
          auto pack = fo4::nif::read_hknp_packfile(packfile);
          if (pack.objects.empty() || pack.objects.front().class_name != "hknpRagdollData") continue;
          ++ragdolls;
          if (fo4::nif::write_hknp_packfile(pack) != stored) {
            ++plain_mismatches;
            if (first_plain.empty()) first_plain = path + " at " + std::to_string(m);
          }
          const auto sys = fo4::nif::decode_hknp_system(std::move(pack));
          ++kits[{sys.bodies.size(), sys.bones.size()}];
          if (fo4::nif::write_hknp_packfile(fo4::nif::encode_hknp_ragdoll(sys)) == stored) {
            ++rebuilt;
          } else {
            ++mismatches;
            if (first_mismatch.empty()) first_mismatch = path + " at " + std::to_string(m);
          }
        } catch (const fo4::parse_error& error) {
          ++failures;
          if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
        } catch (const std::invalid_argument& error) {
          ++failures;
          if (first_failure.empty()) first_failure = path + ": " + error.what();
        }
      }
    }
  }

  std::cout << "ragdoll packfiles " << ragdolls << ", rebuilt byte for byte " << rebuilt << ", plain packfile mismatches "
            << plain_mismatches << ", mismatches " << mismatches << ", failures " << failures << '\n';
  for (const auto& [kit, count] : kits) {
    std::cout << "bodies " << kit.first << " bones " << kit.second << ": " << count << '\n';
  }
  if (!first_plain.empty()) std::cout << "first plain mismatch: " << first_plain << '\n';
  if (!first_mismatch.empty()) std::cout << "first rebuild mismatch: " << first_mismatch << '\n';
  if (!first_failure.empty()) std::cout << "first failure: " << first_failure << '\n';
  RecordProperty("ragdoll_count", std::to_string(ragdolls));
  RecordProperty("ragdoll_rebuilt_count", std::to_string(rebuilt));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(plain_mismatches, 0U) << first_plain;
  EXPECT_EQ(mismatches, 0U) << first_mismatch;
  EXPECT_GT(ragdolls, 0U);
  EXPECT_EQ(rebuilt, ragdolls);
  const auto kit_count = [&kits](std::size_t bodies, std::size_t bones) { return kits[{bodies, bones}]; };
  EXPECT_GE(kit_count(48, 11), 1U);
  EXPECT_GE(kit_count(24, 9), 1U);
  EXPECT_GE(kit_count(5, 4), 1U);
}
