#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool has_extension(const std::string& path, std::string_view extension) {
  return std::filesystem::path(path).extension() == extension;
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8 * i);
  }
  return value;
}

std::uint64_t read_u64(std::span<const std::byte> bytes, std::size_t at) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8 * i);
  }
  return value;
}

struct Descriptor {
  std::size_t at{0};
  std::uint32_t flags{0};
  std::size_t stride{0};
  std::size_t vertex_count{0};
  std::size_t data_size{0};
};

Descriptor descriptor_of(const fo4::nif::NifFile& nif, const fo4::nif::BlockInfo& block) {
  const auto bytes = std::span<const std::byte>(nif.bytes());
  Descriptor out;
  const auto extra = static_cast<std::size_t>(read_u32(bytes, block.offset + 4));
  out.at = block.offset + 100 + 4 * extra;
  const auto descriptor = read_u64(bytes, out.at);
  out.stride = static_cast<std::size_t>(descriptor & 0xFU) * 4;
  out.flags = static_cast<std::uint32_t>(descriptor >> 44U);
  const auto vertex_count = static_cast<std::size_t>(read_u32(bytes, out.at + 12) & 0xFFFFU);
  out.vertex_count = vertex_count;
  out.data_size = static_cast<std::size_t>(read_u32(bytes, out.at + 14));
  return out;
}

bool corpus_open(const char*& reason) {
  if (std::getenv("FO4_DATA_DIR") == nullptr) {
    reason = "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
    return false;
  }
  reason = nullptr;
  return true;
}

std::vector<float> negated(const std::vector<float>& values) {
  std::vector<float> out(values.size());
  std::transform(values.begin(), values.end(), out.begin(), [](float value) { return -value; });
  return out;
}

std::vector<float> swapped(const std::vector<float>& values) {
  std::vector<float> out(values);
  for (std::size_t i = 0; i + 1 < out.size(); i += 2) std::swap(out[i], out[i + 1]);
  return out;
}

}  // namespace

TEST(NifCorpus, Uv2WritesRoundTripWithUv0ByteIdentical) {
  const char* reason = nullptr;
  if (!corpus_open(reason)) GTEST_SKIP() << reason;
  const char* dir = std::getenv("FO4_DATA_DIR");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  std::size_t uv2_shapes = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      const auto file = reader.extract(i);
      fo4::nif::NifFile nif;
      try {
        nif = fo4::nif::read_nif(file);
      } catch (const fo4::parse_error&) {
        continue;
      }
      for (const auto& shape : fo4::nif::read_shapes(nif)) {
        if (shape.uvs2.empty()) continue;
        ++uv2_shapes;
        const std::vector<fo4::nif::Edit> edits{
            fo4::nif::Edit{shape.block, "uvs2", negated(shape.uvs2)}};
        std::vector<std::byte> out;
        try {
          out = fo4::nif::apply_edits(nif, edits);
        } catch (const fo4::parse_error& error) {
          ADD_FAILURE() << path << ": UV2 write failed: " << error.what();
          continue;
        }
        const auto after = fo4::nif::read_shapes(fo4::nif::read_nif(out));
        const auto match = std::find_if(after.begin(), after.end(),
                                        [&shape](const fo4::nif::Shape& item) { return item.block == shape.block; });
        ASSERT_NE(match, after.end()) << path;
        EXPECT_EQ(match->uvs, shape.uvs) << path << ": a UV2 write changed UV0";
        const auto expected = negated(shape.uvs2);
        ASSERT_EQ(match->uvs2.size(), expected.size()) << path;
        for (std::size_t v = 0; v < expected.size(); ++v) {
          EXPECT_EQ(match->uvs2[v], expected[v]) << path << ": UV2 value " << v;
        }
        const std::vector<fo4::nif::Edit> noop{fo4::nif::Edit{shape.block, "uvs2", shape.uvs2}};
        EXPECT_EQ(fo4::nif::apply_edits(nif, noop), file) << path << ": re-writing the read UV2 bytes is not byte identical";
      }
    }
  }
  if (uv2_shapes == 0) GTEST_SKIP() << "the sample corpus contains no UV2 shapes";
}

TEST(NifCorpus, SkinnedPartitionShapesKeepSkinDataWhenUvsAreWritten) {
  const char* reason = nullptr;
  if (!corpus_open(reason)) GTEST_SKIP() << reason;
  const char* dir = std::getenv("FO4_DATA_DIR");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  std::size_t skinned = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      const auto file = reader.extract(i);
      fo4::nif::NifFile nif;
      try {
        nif = fo4::nif::read_nif(file);
      } catch (const fo4::parse_error&) {
        continue;
      }
      for (const auto& shape : fo4::nif::read_shapes(nif)) {
        if (shape.weights.empty() || shape.uvs.empty()) continue;
        ++skinned;
        const auto block_info = nif.blocks()[shape.block];
        const std::vector<fo4::nif::Edit> edits{
            fo4::nif::Edit{shape.block, "uvs", swapped(shape.uvs)}};
        std::vector<std::byte> out;
        try {
          out = fo4::nif::apply_edits(nif, edits);
        } catch (const fo4::parse_error& error) {
          ADD_FAILURE() << path << ": UV write on a skinned shape failed: " << error.what();
          continue;
        }
        const auto out_nif = fo4::nif::read_nif(out);
        const auto after = fo4::nif::read_shapes(out_nif);
        const auto match = std::find_if(after.begin(), after.end(),
                                        [&shape](const fo4::nif::Shape& item) { return item.block == shape.block; });
        ASSERT_NE(match, after.end()) << path;
        EXPECT_EQ(match->weights, shape.weights) << path << ": UV write changed the weights";
        EXPECT_EQ(match->bone_indices, shape.bone_indices) << path << ": UV write changed the bone indices";
        EXPECT_EQ(match->bones.size(), shape.bones.size()) << path << ": UV write changed the bone list";
        EXPECT_EQ(match->skin_ref, shape.skin_ref) << path << ": UV write changed the skin reference";
        EXPECT_EQ(match->tris, shape.tris) << path;
        EXPECT_EQ(match->verts, shape.verts) << path;
        EXPECT_EQ(out_nif.blocks()[shape.block].size, block_info.size)
            << path << ": UV write changed the block size, so partition data moved";
        ASSERT_EQ(match->uvs.size(), shape.uvs.size()) << path;
        for (std::size_t v = 0; v + 1 < shape.uvs.size(); v += 2) {
          EXPECT_EQ(match->uvs[v], shape.uvs[v + 1]) << path;
          EXPECT_EQ(match->uvs[v + 1], shape.uvs[v]) << path;
        }
      }
    }
  }
  if (skinned == 0) GTEST_SKIP() << "the sample corpus contains no skinned shapes";
}

TEST(NifCorpus, FullPrecisionShapesWriteUv0AndUv2AtTheSixteenByteOffsets) {
  const char* reason = nullptr;
  if (!corpus_open(reason)) GTEST_SKIP() << reason;
  const char* dir = std::getenv("FO4_DATA_DIR");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  constexpr std::uint32_t k_has_uv = 1U << 1;
  constexpr std::uint32_t k_has_uv2 = 1U << 2;
  constexpr std::uint32_t k_full_precision = 1U << 10;
  std::size_t full = 0;
  std::size_t full_uv2 = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      const auto file = reader.extract(i);
      fo4::nif::NifFile nif;
      try {
        nif = fo4::nif::read_nif(file);
      } catch (const fo4::parse_error&) {
        continue;
      }
      const auto before_bytes = std::span<const std::byte>(nif.bytes());
      for (const auto& shape : fo4::nif::read_shapes(nif)) {
        const auto info = descriptor_of(nif, nif.blocks()[shape.block]);
        if ((info.flags & k_full_precision) == 0 || (info.flags & k_has_uv) == 0) continue;
        ++full;
        const bool with_uv2 = (info.flags & k_has_uv2) != 0 && !shape.uvs2.empty();
        if (with_uv2) ++full_uv2;
        const std::vector<fo4::nif::Edit> edits{
            fo4::nif::Edit{shape.block, with_uv2 ? "uvs2" : "uvs",
                           negated(with_uv2 ? shape.uvs2 : shape.uvs)}};
        std::vector<std::byte> out;
        try {
          out = fo4::nif::apply_edits(nif, edits);
        } catch (const fo4::parse_error& error) {
          ADD_FAILURE() << path << ": full-precision UV write failed: " << error.what();
          continue;
        }
        const auto out_nif = fo4::nif::read_nif(out);
        const auto after = fo4::nif::read_shapes(out_nif);
        const auto match = std::find_if(after.begin(), after.end(),
                                        [&shape](const fo4::nif::Shape& item) { return item.block == shape.block; });
        ASSERT_NE(match, after.end()) << path;
        if (with_uv2) {
          EXPECT_EQ(match->uvs, shape.uvs) << path << ": a full-precision UV2 write changed UV0";
        } else {
          EXPECT_EQ(match->uvs2, shape.uvs2) << path;
        }
        const auto expected = negated(with_uv2 ? shape.uvs2 : shape.uvs);
        const auto& got = with_uv2 ? match->uvs2 : match->uvs;
        ASSERT_EQ(got.size(), expected.size()) << path;
        for (std::size_t v = 0; v < expected.size(); ++v) EXPECT_EQ(got[v], expected[v]) << path;
        const auto after_bytes = std::span<const std::byte>(out_nif.bytes());
        ASSERT_EQ(before_bytes.size(), after_bytes.size()) << path;
        const auto data_at = info.at + 18;
        for (std::size_t v = 0; v < info.vertex_count; ++v) {
          const auto uv_at = data_at + v * info.stride + 16;
          const auto uv2_at = data_at + v * info.stride + 20;
          for (std::size_t b = 0; b < info.stride; ++b) {
            const auto at = data_at + v * info.stride + b;
            const bool may_change = with_uv2 ? (b >= 20 && b < 24) : (b >= 16 && b < 20);
            if (!may_change) {
              EXPECT_EQ(before_bytes[at], after_bytes[at])
                  << path << ": byte " << at << " of a full-precision vertex changed (uv_at " << uv_at
                  << ", uv2_at " << uv2_at << ")";
            }
          }
        }
      }
    }
  }
  if (full == 0) GTEST_SKIP() << "the sample corpus contains no full-precision shapes";
  if (full_uv2 == 0) GTEST_SKIP() << "the sample corpus contains no full-precision shapes with UV2";
}
