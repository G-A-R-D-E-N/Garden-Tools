#include "fo4/audio/lip_file.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_nif.h"
#include "fo4/c_api_sound.h"
#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using fo4::audio::LipAnimation;
using fo4::audio::fuz_lip_block;
using fo4::audio::parse_lip;

namespace {

enum class Shape { A, B, C, D, E, F };

struct Spec {
  std::uint32_t flags{13};
  std::uint16_t phonemes{43};
  Shape shape{Shape::A};
  std::int32_t preroll{-8};
  std::uint32_t trailing_pad{0};
};

void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>(value >> 8u));
}

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  put_u16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  put_u16(out, static_cast<std::uint16_t>(value >> 16u));
}

void put_bytes(std::vector<std::byte>& out, std::initializer_list<std::uint8_t> bytes) {
  for (const auto byte : bytes) out.push_back(static_cast<std::byte>(byte));
}

std::uint32_t modifiers_for(std::uint32_t flags) { return (flags & 4u) != 0 ? 17u : 0u; }

std::vector<std::byte> encode_lip(const std::vector<float>& grid, std::uint16_t frames, const Spec& spec) {
  std::size_t leading = 0;
  while (leading < grid.size() && grid[leading] == 0.0f) ++leading;

  std::vector<std::byte> payload;
  std::size_t zero_bytes = 0;
  const auto flush = [&] {
    while (zero_bytes != 0) {
      const auto run = static_cast<std::uint16_t>(zero_bytes > 65535 ? 65535 : zero_bytes);
      payload.push_back(std::byte{0});
      put_u16(payload, run);
      zero_bytes -= run;
    }
  };
  for (std::size_t cell = leading; cell < grid.size(); ++cell) {
    std::byte raw[4];
    std::memcpy(raw, &grid[cell], 4);
    for (const auto byte : raw) {
      if (byte == std::byte{0}) {
        ++zero_bytes;
      } else {
        flush();
        payload.push_back(byte);
      }
    }
  }
  flush();
  for (std::uint32_t i = 0; i < spec.trailing_pad; ++i) payload.push_back(std::byte{0});

  std::vector<std::byte> out;
  put_u32(out, 1);
  put_u32(out, static_cast<std::uint32_t>(grid.size() * 4 + 28));
  put_u32(out, spec.flags);
  put_u16(out, frames);
  switch (spec.shape) {
    case Shape::A: put_bytes(out, {3, 0}); break;
    case Shape::B: put_bytes(out, {0, 2, 0}); break;
    case Shape::C: put_bytes(out, {0, 1, 0, 2, 0}); break;
    case Shape::D: put_bytes(out, {7, 0}); break;
    case Shape::E: put_bytes(out, {0, 6, 0}); break;
    case Shape::F: put_bytes(out, {0, 1, 0, 6, 0}); break;
  }
  if (spec.shape == Shape::A || spec.shape == Shape::B || spec.shape == Shape::C) {
    put_u32(out, static_cast<std::uint32_t>(spec.preroll));
  }
  put_u16(out, spec.phonemes);
  put_u16(out, static_cast<std::uint16_t>((leading << 2u) | 3u));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::vector<std::byte> wrap_fuz(const std::vector<std::byte>& lip, std::size_t audio_bytes = 16) {
  std::vector<std::byte> out{std::byte{'F'}, std::byte{'U'}, std::byte{'Z'}, std::byte{'E'}};
  put_u32(out, 1);
  put_u32(out, static_cast<std::uint32_t>(lip.size()));
  out.insert(out.end(), lip.begin(), lip.end());
  for (std::size_t i = 0; i < audio_bytes; ++i) out.push_back(static_cast<std::byte>(0x40 + i));
  return out;
}

std::vector<float> sample_grid(std::uint16_t frames, std::uint16_t phonemes, std::uint32_t modifiers) {
  const std::size_t slots = phonemes + modifiers;
  std::vector<float> grid(static_cast<std::size_t>(frames) * slots, 0.0f);
  grid[1 * slots + 2] = 0.5f;
  grid[2 * slots + 2] = 0.75f;
  grid[2 * slots + slots - 1] = -0.25f;
  grid[3 * slots + 0] = 1.0f;
  return grid;
}

std::vector<float> sample_grid_for(std::uint16_t frames, const Spec& spec) {
  return sample_grid(frames, spec.phonemes, modifiers_for(spec.flags));
}

std::string take_path(uint8_t* path, size_t size) {
  std::string text(reinterpret_cast<const char*>(path), size);
  radicle_free_bytes(path);
  return text;
}

template <typename Fn>
void expect_parse_error(Fn&& fn, const std::string& fragment) {
  try {
    fn();
    ADD_FAILURE() << "expected a parse_error containing: " << fragment;
  } catch (const fo4::parse_error& error) {
    EXPECT_NE(std::string(error.what()).find(fragment), std::string::npos) << error.what();
  }
}

}  // namespace

TEST(LipFile, DecodesTheCompressedGridWithModifiers) {
  const Spec spec;
  const auto grid = sample_grid_for(5, spec);
  const auto lip = parse_lip(encode_lip(grid, 5, spec));
  EXPECT_EQ(lip.frame_count, 5u);
  EXPECT_EQ(lip.preroll_frames, -8);
  EXPECT_EQ(lip.phoneme_count, 43u);
  EXPECT_EQ(lip.modifier_count, 17u);
  EXPECT_EQ(lip.slot_count(), 60u);
  EXPECT_EQ(lip.flags, 13u);
  EXPECT_EQ(lip.clamped_cells, 0u);
  ASSERT_EQ(lip.weights.size(), grid.size());
  EXPECT_EQ(lip.weights, grid);
  EXPECT_FLOAT_EQ(lip.weight(2, 2), 0.75f);
  EXPECT_FLOAT_EQ(lip.weight(2, 59), -0.25f);
}

TEST(LipFile, DecodesTheGridWithoutModifiersWhenTheGestureFlagIsClear) {
  Spec spec;
  spec.flags = 9;
  spec.phonemes = 16;
  const auto grid = sample_grid_for(5, spec);
  const auto lip = parse_lip(encode_lip(grid, 5, spec));
  EXPECT_EQ(lip.modifier_count, 0u);
  EXPECT_EQ(lip.slot_count(), 16u);
  EXPECT_EQ(lip.flags, 9u);
  EXPECT_EQ(lip.weights, grid);
  EXPECT_FLOAT_EQ(lip.weight(2, 15), -0.25f);
}

TEST(LipFile, DecodesEveryHeaderShape) {
  for (const auto shape : {Shape::A, Shape::B, Shape::C, Shape::D, Shape::E, Shape::F}) {
    Spec spec;
    spec.shape = shape;
    const auto grid = sample_grid_for(5, spec);
    const auto lip = parse_lip(encode_lip(grid, 5, spec));
    const bool prerolled = shape == Shape::A || shape == Shape::B || shape == Shape::C;
    EXPECT_EQ(lip.preroll_frames, prerolled ? -8 : 0) << static_cast<int>(shape);
    EXPECT_EQ(lip.phoneme_count, 43u) << static_cast<int>(shape);
    EXPECT_EQ(lip.weights, grid) << static_cast<int>(shape);
  }
}

TEST(LipFile, DecodesAHandWrittenGoldenBlock) {
  std::vector<std::byte> golden;
  put_bytes(golden, {0x01, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0xF8,
                     0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x07, 0x00, 0x00, 0x03, 0x00, 0x3F, 0x00, 0x06, 0x00, 0x80, 0x3F, 0x00,
                     0x08, 0x00});
  const auto lip = parse_lip(golden);
  EXPECT_EQ(lip.frame_count, 2u);
  EXPECT_EQ(lip.preroll_frames, -8);
  EXPECT_EQ(lip.phoneme_count, 3u);
  EXPECT_EQ(lip.modifier_count, 0u);
  EXPECT_EQ(lip.weights, (std::vector<float>{0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f}));
}

TEST(LipFile, ReadsTheBlockEmbeddedInAFuzContainer) {
  const Spec spec;
  const auto grid = sample_grid_for(5, spec);
  const auto raw = encode_lip(grid, 5, spec);
  const auto fuz = wrap_fuz(raw);
  const auto block = fuz_lip_block(fuz);
  EXPECT_EQ(block.size(), raw.size());
  EXPECT_EQ(static_cast<std::size_t>(block.data() - fuz.data()), 12u);
  EXPECT_EQ(parse_lip(fuz).weights, grid);
}

TEST(LipFile, DecodesAStartTokenThatIsNotAWholeNumberOfCells) {
  std::vector<std::byte> block;
  put_bytes(block, {0x01, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0xF8,
                    0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x06, 0x00, 0x3F, 0x00, 0x14, 0x00});
  const auto lip = parse_lip(block);
  EXPECT_EQ(lip.weights, (std::vector<float>{0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
  block[22] = std::byte{0};
  expect_parse_error([&] { parse_lip(block); }, "start token is out of range");
}

TEST(LipFile, RefusesPayloadsThatMissTheGridByEvenOneByte) {
  const Spec spec;
  const auto grid = sample_grid_for(5, spec);
  const auto exact = encode_lip(grid, 5, spec);
  EXPECT_EQ(parse_lip(exact).weights, grid);
  Spec padded = spec;
  padded.trailing_pad = 2;
  expect_parse_error([&] { parse_lip(encode_lip(grid, 5, padded)); }, "ends inside a zero run");
  expect_parse_error([&] { parse_lip(encode_lip(grid, 2, spec)); }, "longer than its declared frame grid");
  auto more_frames = exact;
  more_frames[12] = std::byte{6};
  expect_parse_error([&] { parse_lip(more_frames); }, "shorter than its declared frame grid");
  auto shortened = exact;
  shortened.resize(shortened.size() - 3);
  expect_parse_error([&] { parse_lip(shortened); }, "shorter than its declared frame grid");
  auto one_cell_more = exact;
  one_cell_more[12] = std::byte{4};
  expect_parse_error([&] { parse_lip(one_cell_more); }, "longer than its declared frame grid");
}

TEST(LipFile, RefusesAnAbsurdGridBeforeAllocatingIt) {
  std::vector<std::byte> header;
  put_u32(header, 1);
  put_u32(header, 0);
  put_u32(header, 13);
  put_u16(header, 65535);
  put_bytes(header, {3, 0, 0, 0, 0, 0});
  put_u16(header, 128);
  put_u16(header, 3);
  ASSERT_EQ(header.size(), 24u);
  expect_parse_error([&] { parse_lip(header); }, "shorter than its declared frame grid");
}

TEST(LipFile, RefusesUnsupportedFlagsAndVersions) {
  const Spec spec;
  const auto grid = sample_grid_for(5, spec);
  const auto valid = encode_lip(grid, 5, spec);
  const auto with_flags = [&](std::uint32_t flags) {
    auto block = valid;
    block[8] = static_cast<std::byte>(flags);
    return block;
  };
  expect_parse_error([&] { parse_lip(with_flags(12)); }, "uncompressed lip files are not supported");
  expect_parse_error([&] { parse_lip(with_flags(8)); }, "uncompressed lip files are not supported");
  expect_parse_error([&] { parse_lip(with_flags(15)); }, "big-endian");
  expect_parse_error([&] { parse_lip(with_flags(29)); }, "unknown bits");
  auto tom3 = valid;
  tom3[0] = std::byte{'T'};
  tom3[1] = std::byte{'O'};
  tom3[2] = std::byte{'M'};
  tom3[3] = std::byte{'3'};
  expect_parse_error([&] { parse_lip(tom3); }, "TOM3");
  auto version_two = valid;
  version_two[0] = std::byte{2};
  expect_parse_error([&] { parse_lip(version_two); }, "version is not 1");
}

TEST(LipFile, RefusesAnUnrecognisedHeaderVariantWithoutBlamingTheLength) {
  const Spec spec;
  auto block = encode_lip(sample_grid_for(5, spec), 5, spec);
  ASSERT_GE(block.size(), 24u);
  block.resize(24);
  block[14] = std::byte{9};
  expect_parse_error([&] { parse_lip(block); }, "header variant is not recognised");
  block[14] = std::byte{0};
  block[15] = std::byte{5};
  expect_parse_error([&] { parse_lip(block); }, "header variant is not recognised");
}

TEST(LipFile, RefusesEveryTruncatedHeader) {
  const Spec spec;
  const auto block = encode_lip(sample_grid_for(5, spec), 5, spec);
  for (std::size_t size = 0; size < 24; ++size) {
    const std::vector<std::byte> prefix(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(size));
    EXPECT_THROW(parse_lip(prefix), fo4::parse_error) << size;
  }
}

TEST(LipFile, RefusesMalformedBlocks) {
  const Spec spec;
  const auto grid = sample_grid_for(5, spec);
  expect_parse_error([&] { parse_lip(encode_lip(grid, 0, spec)); }, "no frames");
  Spec no_phonemes = spec;
  no_phonemes.phonemes = 0;
  expect_parse_error([&] { parse_lip(encode_lip(grid, 5, no_phonemes)); }, "phoneme count is out of range");
  EXPECT_THROW(fuz_lip_block(std::vector<std::byte>(16)), fo4::parse_error);
}

TEST(LipFile, ReportsAnEmptyLipBlockInsideAFuzContainer) {
  const auto fuz = wrap_fuz({});
  EXPECT_EQ(fuz_lip_block(fuz).size(), 0u);
  expect_parse_error([&] { parse_lip(fuz); }, "lip block is empty");
}

TEST(LipFile, ClampsWeightsToTheSignedUnitRangeAndCountsThem) {
  Spec spec;
  spec.phonemes = 3;
  std::vector<float> grid(4 * 20, 0.0f);
  grid[3] = 4.0f;
  grid[7] = -9.0f;
  grid[9] = 0.5f;
  const auto lip = parse_lip(encode_lip(grid, 4, spec));
  EXPECT_FLOAT_EQ(lip.weights[3], 1.0f);
  EXPECT_FLOAT_EQ(lip.weights[7], -1.0f);
  EXPECT_FLOAT_EQ(lip.weights[9], 0.5f);
  EXPECT_EQ(lip.clamped_cells, 2u);
  EXPECT_FLOAT_EQ(lip.max_clamped_magnitude, 9.0f);
}

TEST(LipFileCApi, FormatsTheVoicePath) {
  uint8_t* path = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_audio_format_voice_path("Fallout4.esm", "MaleEvenToned", 0x1649A2u, 1, nullptr, nullptr, nullptr, &path,
                                            &size),
            RADICLE_STATUS_OK);
  EXPECT_EQ(take_path(path, size), "Sound\\Voice\\Fallout4.esm\\MaleEvenToned\\001649A2_1.fuz");

  ASSERT_EQ(radicle_audio_format_voice_path("DLCCoast.esm", "DLC03MaleDiMA", 0x2DFDDu, 2, "English", "", "hkx", &path,
                                            &size),
            RADICLE_STATUS_OK);
  EXPECT_EQ(take_path(path, size), "Sound\\Voice\\DLCCoast.esm\\DLC03MaleDiMA\\English\\0002DFDD_2.hkx");
}

TEST(LipFileCApi, RefusesBadVoicePathArguments) {
  uint8_t* path = reinterpret_cast<uint8_t*>(1);
  size_t size = 7;
  EXPECT_EQ(radicle_audio_format_voice_path("Fallout4.esm", "A\\B", 1, 1, nullptr, nullptr, nullptr, &path, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(path, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(radicle_audio_format_voice_path("Fallout4.esm", "Voice", 1, 0, nullptr, nullptr, nullptr, &path, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_audio_format_voice_path("Fallout4.esm", "Voice", 1, 1, nullptr, nullptr, "wav", &path, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_audio_format_voice_path(nullptr, "Voice", 1, 1, nullptr, nullptr, nullptr, &path, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(LipFileCApi, DecodesLipFromAFuzContainer) {
  for (const auto& spec : {Spec{}, Spec{9, 16, Shape::B, -3, 0}}) {
    const auto grid = sample_grid_for(5, spec);
    const auto raw = encode_lip(grid, 5, spec);
    const auto fuz = wrap_fuz(raw);
    const auto* bytes = reinterpret_cast<const uint8_t*>(fuz.data());

    uint32_t frames = 0;
    int32_t preroll = 0;
    uint32_t phonemes = 0;
    uint32_t modifiers = 0;
    float* weights = nullptr;
    size_t count = 0;
    ASSERT_EQ(radicle_audio_decode_lip(bytes, fuz.size(), &frames, &preroll, &phonemes, &modifiers, &weights, &count),
              RADICLE_STATUS_OK);
    EXPECT_EQ(frames, 5u);
    EXPECT_EQ(preroll, spec.preroll);
    EXPECT_EQ(phonemes, spec.phonemes);
    EXPECT_EQ(modifiers, modifiers_for(spec.flags));
    ASSERT_EQ(count, grid.size());
    EXPECT_EQ(std::vector<float>(weights, weights + count), grid);
    radicle_free_floats(weights);
  }

  uint32_t frames = 0;
  int32_t preroll = 0;
  uint32_t phonemes = 0;
  uint32_t modifiers = 0;
  float* weights = nullptr;
  size_t count = 0;
  const std::vector<std::byte> junk(32, std::byte{0x11});
  EXPECT_EQ(radicle_audio_decode_lip(reinterpret_cast<const uint8_t*>(junk.data()), junk.size(), &frames, &preroll,
                                     &phonemes, &modifiers, &weights, &count),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(weights, nullptr);
  EXPECT_EQ(radicle_audio_decode_lip(nullptr, 0, &frames, &preroll, &phonemes, &modifiers, &weights, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
}
