#include "fo4/audio/sound_container.hpp"

#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using fo4::audio::SoundContainer;
using fo4::audio::parse_sound_container;

namespace {

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  append_u16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  append_u16(out, static_cast<std::uint16_t>((value >> 16u) & 0xFFFFu));
}

void append_signature(std::vector<std::byte>& out, const char* id) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::byte>(id[i]));
  }
}

void set_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFu);
  bytes[offset + 1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
  bytes[offset + 2] = static_cast<std::byte>((value >> 16u) & 0xFFu);
  bytes[offset + 3] = static_cast<std::byte>((value >> 24u) & 0xFFu);
}

void overwrite_signature(std::vector<std::byte>& bytes, std::size_t offset, const char* id) {
  for (int i = 0; i < 4; ++i) {
    bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::byte>(id[i]);
  }
}

std::size_t find_signature(const std::vector<std::byte>& bytes, const char* id) {
  for (std::size_t i = 0; i + 4 <= bytes.size(); ++i) {
    bool match = true;
    for (int k = 0; k < 4; ++k) {
      if (bytes[i + static_cast<std::size_t>(k)] != static_cast<std::byte>(id[k])) {
        match = false;
        break;
      }
    }
    if (match) {
      return i;
    }
  }
  return bytes.size();
}

std::vector<std::byte> format_payload(std::uint16_t tag = 0x0161,
                                      std::uint16_t channels = 1,
                                      std::uint32_t rate = 44100,
                                      std::uint16_t bits = 16) {
  std::vector<std::byte> out;
  append_u16(out, tag);
  append_u16(out, channels);
  append_u32(out, rate);
  append_u32(out, 8000);
  append_u16(out, 1487);
  append_u16(out, bits);
  append_u16(out, 0);
  return out;
}

void append_chunk(std::vector<std::byte>& out, const char* id, const std::vector<std::byte>& payload) {
  append_signature(out, id);
  append_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  if ((payload.size() & 1u) != 0) {
    out.push_back(std::byte{0});
  }
}

std::vector<std::byte> make_riff(const std::vector<std::byte>& body) {
  std::vector<std::byte> out;
  append_signature(out, "RIFF");
  append_u32(out, static_cast<std::uint32_t>(body.size() + 4));
  append_signature(out, "XWMA");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::vector<std::byte> make_xwm(std::uint16_t tag = 0x0161,
                                std::uint16_t channels = 1,
                                std::uint32_t rate = 44100,
                                std::uint16_t bits = 16,
                                std::size_t dpds_size = 8,
                                std::size_t data_size = 16) {
  std::vector<std::byte> body;
  append_chunk(body, "fmt ", format_payload(tag, channels, rate, bits));
  append_chunk(body, "dpds", std::vector<std::byte>(dpds_size, std::byte{0}));
  append_chunk(body, "data", std::vector<std::byte>(data_size, std::byte{0x11}));
  return make_riff(body);
}

std::vector<std::byte> wrap_in_fuze(const std::vector<std::byte>& xwm, std::uint32_t lip_size = 8) {
  std::vector<std::byte> out;
  append_signature(out, "FUZE");
  append_u32(out, 1);
  append_u32(out, lip_size);
  out.resize(out.size() + lip_size, std::byte{0x20});
  out.insert(out.end(), xwm.begin(), xwm.end());
  return out;
}

}  // namespace

// Detects: a bare xwm whose format, packet count and payload offsets must be reported exactly.
TEST(SoundContainer, ParsesABareXwmStream) {
  const auto bytes = make_xwm();
  const auto container = parse_sound_container(bytes);
  EXPECT_FALSE(container.fuz);
  EXPECT_EQ(container.lip_offset, 0u);
  EXPECT_EQ(container.lip_size, 0u);
  EXPECT_EQ(container.riff_offset, 0u);
  EXPECT_EQ(container.riff_size, bytes.size() - 8u);
  EXPECT_EQ(container.format.format_tag, 0x0161u);
  EXPECT_EQ(container.format.channels, 1u);
  EXPECT_EQ(container.format.sample_rate, 44100u);
  EXPECT_EQ(container.format.average_bytes_per_second, 8000u);
  EXPECT_EQ(container.format.block_align, 1487u);
  EXPECT_EQ(container.format.bits_per_sample, 16u);
  EXPECT_EQ(container.packet_count, 2u);
  EXPECT_EQ(container.data_size, 16u);
  EXPECT_EQ(container.data_offset + container.data_size, bytes.size());
  EXPECT_EQ(bytes[container.data_offset], std::byte{0x11});
}

// Detects: a FUZE wrapper whose lip prefix must stay counted in every reported offset.
TEST(SoundContainer, ParsesAFuzeWrapperAndKeepsTheLipPrefixOffsets) {
  const auto bytes = wrap_in_fuze(make_xwm(), 8);
  const auto container = parse_sound_container(bytes);
  EXPECT_TRUE(container.fuz);
  EXPECT_EQ(container.lip_offset, 12u);
  EXPECT_EQ(container.lip_size, 8u);
  EXPECT_EQ(container.riff_offset, 20u);
  EXPECT_EQ(container.format.sample_rate, 44100u);
  EXPECT_EQ(container.data_offset + container.data_size, bytes.size());
}

// Detects: a zero-length lip, which shipped files must accept and which still reports the wrapper.
TEST(SoundContainer, ParsesAFuzeWrapperWithAnEmptyLip) {
  const auto bytes = wrap_in_fuze(make_xwm(), 0);
  const auto container = parse_sound_container(bytes);
  EXPECT_TRUE(container.fuz);
  EXPECT_EQ(container.lip_offset, 12u);
  EXPECT_EQ(container.lip_size, 0u);
  EXPECT_EQ(container.riff_offset, 12u);
}

// Detects: a FUZE version other than the measured 1, which must be refused.
TEST(SoundContainer, RejectsAnUnsupportedFuzeVersion) {
  auto bytes = wrap_in_fuze(make_xwm());
  set_u32(bytes, 4, 2);
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: a lip size that must not be trusted past the end of the buffer.
TEST(SoundContainer, RejectsAFuzeLipSizePastTheEnd) {
  auto bytes = wrap_in_fuze(make_xwm());
  set_u32(bytes, 8, 100000);
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: a payload that is neither FUZE nor RIFF and must be refused.
TEST(SoundContainer, RejectsANonRiffPayload) {
  const auto bytes = std::vector<std::byte>(32, std::byte{0});
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: a RIFF form other than the measured XWMA, which must be refused.
TEST(SoundContainer, RejectsANonXwmaRiffForm) {
  auto bytes = make_xwm();
  overwrite_signature(bytes, 8, "WAVE");
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: a declared RIFF size that must not run past the buffer.
TEST(SoundContainer, RejectsARiffSizePastTheEnd) {
  auto bytes = make_xwm();
  set_u32(bytes, 4, static_cast<std::uint32_t>(bytes.size() + 64));
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: trailing bytes that must not follow the declared stream, which the measured files never carry.
TEST(SoundContainer, RejectsTrailingBytesAfterTheRiffStream) {
  auto bytes = make_xwm();
  bytes.push_back(std::byte{0});
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: a codec tag that must match the measured WMA v2 tag.
TEST(SoundContainer, RejectsAWrongFormatTag) {
  EXPECT_THROW(parse_sound_container(make_xwm(0x0001)), fo4::parse_error);
}

// Detects: a zero channel count, which must be refused rather than read as an empty stream.
TEST(SoundContainer, RejectsAZeroChannelCount) {
  EXPECT_THROW(parse_sound_container(make_xwm(0x0161, 0)), fo4::parse_error);
}

// Detects: a non-16-bit sample size, which must be refused rather than decoded as 16-bit.
TEST(SoundContainer, RejectsANon16BitSampleSize) {
  EXPECT_THROW(parse_sound_container(make_xwm(0x0161, 1, 44100, 8)), fo4::parse_error);
}

// Detects: a dpds payload that must be a whole number of 4-byte packet sizes.
TEST(SoundContainer, RejectsAnOddDpdsPayload) {
  EXPECT_THROW(parse_sound_container(make_xwm(0x0161, 1, 44100, 16, 6)), fo4::parse_error);
}

// Detects: an empty data payload, which must be refused rather than reported as a valid stream.
TEST(SoundContainer, RejectsAnEmptyDataChunk) {
  EXPECT_THROW(parse_sound_container(make_xwm(0x0161, 1, 44100, 16, 8, 0)), fo4::parse_error);
}

// Detects: a stream missing the fmt chunk, which must be refused.
TEST(SoundContainer, RejectsAMissingFmtChunk) {
  std::vector<std::byte> body;
  append_chunk(body, "dpds", std::vector<std::byte>(8, std::byte{0}));
  append_chunk(body, "data", std::vector<std::byte>(16, std::byte{0x11}));
  EXPECT_THROW(parse_sound_container(make_riff(body)), fo4::parse_error);
}

// Detects: a stream missing the dpds chunk, which must be refused.
TEST(SoundContainer, RejectsAMissingDpdsChunk) {
  std::vector<std::byte> body;
  append_chunk(body, "fmt ", format_payload());
  append_chunk(body, "data", std::vector<std::byte>(16, std::byte{0x11}));
  EXPECT_THROW(parse_sound_container(make_riff(body)), fo4::parse_error);
}

// Detects: a stream missing the data chunk, which must be refused.
TEST(SoundContainer, RejectsAMissingDataChunk) {
  std::vector<std::byte> body;
  append_chunk(body, "fmt ", format_payload());
  append_chunk(body, "dpds", std::vector<std::byte>(8, std::byte{0}));
  EXPECT_THROW(parse_sound_container(make_riff(body)), fo4::parse_error);
}

// Detects: a chunk id outside the measured fmt/dpds/data set, which must be refused.
TEST(SoundContainer, RejectsAnUnknownChunk) {
  std::vector<std::byte> body;
  append_chunk(body, "fmt ", format_payload());
  append_chunk(body, "dpds", std::vector<std::byte>(8, std::byte{0}));
  append_chunk(body, "junk", std::vector<std::byte>(4, std::byte{0}));
  append_chunk(body, "data", std::vector<std::byte>(16, std::byte{0x11}));
  EXPECT_THROW(parse_sound_container(make_riff(body)), fo4::parse_error);
}

// Detects: a chunk size that must not run past the declared stream end.
TEST(SoundContainer, RejectsAChunkThatRunsPastTheStream) {
  auto bytes = make_xwm();
  const auto data_header = find_signature(bytes, "data");
  ASSERT_LT(data_header, bytes.size());
  set_u32(bytes, data_header + 4, static_cast<std::uint32_t>(bytes.size()));
  EXPECT_THROW(parse_sound_container(bytes), fo4::parse_error);
}

// Detects: a second fmt chunk, which must be refused instead of silently overwriting the format.
TEST(SoundContainer, RejectsADuplicateFmtChunk) {
  std::vector<std::byte> body;
  append_chunk(body, "fmt ", format_payload());
  append_chunk(body, "fmt ", format_payload());
  append_chunk(body, "dpds", std::vector<std::byte>(8, std::byte{0}));
  append_chunk(body, "data", std::vector<std::byte>(16, std::byte{0x11}));
  EXPECT_THROW(parse_sound_container(make_riff(body)), fo4::parse_error);
}

// Detects: an empty buffer, which must be refused without reading past it.
TEST(SoundContainer, RejectsAnEmptyBuffer) {
  EXPECT_THROW(parse_sound_container(std::vector<std::byte>{}), fo4::parse_error);
}
