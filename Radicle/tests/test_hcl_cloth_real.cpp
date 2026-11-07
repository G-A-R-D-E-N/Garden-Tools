#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/hcl_cloth.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uint8_t k_cloth_tail_length = 0x10;
constexpr std::size_t k_entry_stride = 5;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) {
  return std::filesystem::path(lower(path)).extension() == ".nif";
}

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

}  // namespace

// Detects: a cloth packfile read with the 0x40 header instead of 0x40 plus its u16 at 0x3E, a tail that is dropped on rebuild, or a byte the rebuild changes; every sampled cloth packfile must carry its 16-byte tail and rebuild exactly.
TEST(HclClothRealData, VanillaClothPackfilesRebuildByteForByteWithTheirHeaderTail) {
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
  std::size_t plain_headers = 0;
  std::size_t cloth = 0;
  std::size_t identical = 0;
  std::size_t collidables = 0;
  std::size_t capsules = 0;
  std::size_t tapered = 0;
  std::size_t bones = 0;
  std::size_t unexpected_headers = 0;
  std::size_t failures = 0;
  std::string first_failure;
  std::string first_mismatch;
  std::string first_unexpected;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      ++nifs;
      const auto bytes = reader.extract(i);
      for (const std::size_t m : packfile_starts(bytes)) {
        const std::size_t length = u32_at(bytes, m - 4);
        if (length < 0x50 || length > bytes.size() - m) continue;
        const std::uint16_t tail = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[m + 0x3E]) |
                                                              (std::to_integer<std::uint8_t>(bytes[m + 0x3F]) << 8));
        if (tail == 0) {
          ++plain_headers;
          continue;
        }
        if (tail != k_cloth_tail_length) {
          ++unexpected_headers;
          if (first_unexpected.empty()) first_unexpected = path + " tail " + std::to_string(tail);
          continue;
        }
        const std::span<const std::byte> packfile(bytes.data() + m, length);
        try {
          const auto pack = fo4::nif::read_hknp_packfile(packfile);
          ++cloth;
          if (pack.header_tail.size() != tail) {
            ++failures;
            if (first_failure.empty()) first_failure = path + ": header tail length";
            continue;
          }
          const auto rebuilt = fo4::nif::write_hknp_packfile(pack);
          if (std::equal(rebuilt.begin(), rebuilt.end(), packfile.begin(), packfile.end())) {
            ++identical;
          } else if (first_mismatch.empty()) {
            first_mismatch = path + " at " + std::to_string(m);
          }
          const auto typed = fo4::nif::decode_hcl_cloth(pack);
          collidables += typed.collidables.size();
          capsules += typed.capsules.size();
          tapered += typed.tapered_capsules.size();
          bones += typed.bone_names.size();
          for (const auto& collidable : typed.collidables) {
            if (collidable.shape_object == fo4::nif::k_no_object) continue;
            const auto& shape = pack.objects[collidable.shape_object].class_name;
            if (shape != "hclCapsuleShape" && shape != "hclTaperedCapsuleShape") {
              ++failures;
              if (first_failure.empty()) first_failure = path + ": collidable shape is " + shape;
            }
          }
        } catch (const fo4::parse_error& error) {
          ++failures;
          if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
        }
      }
    }
  }

  std::cout << "nifs " << nifs << ", physics packfiles " << plain_headers << ", cloth packfiles " << cloth << ", rebuilt identical " << identical << ", collidables " << collidables << ", capsules " << capsules
            << ", tapered capsules " << tapered << ", bone names " << bones << ", unexpected headers "
            << unexpected_headers << ", failures " << failures << '\n';
  if (!first_unexpected.empty()) std::cout << "first unexpected header: " << first_unexpected << '\n';
  RecordProperty("cloth_packfile_count", std::to_string(cloth));
  RecordProperty("cloth_rebuilt_identical_count", std::to_string(identical));
  RecordProperty("physics_packfile_count", std::to_string(plain_headers));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(unexpected_headers, 0U) << first_unexpected;
  EXPECT_EQ(identical, cloth) << first_mismatch;
  EXPECT_GT(cloth, 0U);
}
