#include "fo4/audio/xwma_decoder.hpp"

#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using fo4::audio::decode_xwma;

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

void append_chunk(std::vector<std::byte>& out, const char* id, const std::vector<std::byte>& payload) {
  append_signature(out, id);
  append_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  if ((payload.size() & 1u) != 0) {
    out.push_back(std::byte{0});
  }
}

constexpr std::uint16_t k_block_align = 64;

std::vector<std::byte> make_xwm(const std::vector<std::byte>& data, std::uint16_t channels = 1,
                                std::uint32_t rate = 44100) {
  std::vector<std::byte> fmt;
  append_u16(fmt, 0x0161);
  append_u16(fmt, channels);
  append_u32(fmt, rate);
  append_u32(fmt, 8000);
  append_u16(fmt, k_block_align);
  append_u16(fmt, 16);
  append_u16(fmt, 0);
  std::vector<std::byte> body;
  append_chunk(body, "fmt ", fmt);
  append_chunk(body, "dpds", std::vector<std::byte>(8, std::byte{0}));
  append_chunk(body, "data", data);
  std::vector<std::byte> out;
  append_signature(out, "RIFF");
  append_u32(out, static_cast<std::uint32_t>(body.size() + 4));
  append_signature(out, "XWMA");
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::vector<std::byte> silent_packets(std::size_t count) {
  std::vector<std::byte> data(count * k_block_align, std::byte{0});
  for (std::size_t i = 0; i < count; ++i) {
    data[i * k_block_align] = static_cast<std::byte>(i == 0 ? 0x02 : 0x01);
  }
  return data;
}

}  // namespace

TEST(XwmaDecoder, RejectsBytesThatAreNotAContainer) {
  const std::vector<std::byte> junk(32, std::byte{0x5A});
  EXPECT_THROW(decode_xwma(junk), fo4::parse_error);
}

TEST(XwmaDecoder, RejectsMoreThanTwoChannels) {
  EXPECT_THROW(decode_xwma(make_xwm(silent_packets(2), 6)), fo4::parse_error);
}

TEST(XwmaDecoder, CountsUndecodablePacketsInsteadOfThrowing) {
  const auto pcm = decode_xwma(make_xwm(std::vector<std::byte>(3 * k_block_align, std::byte{0})));
  EXPECT_EQ(pcm.bad_packets, 3u);
  EXPECT_EQ(pcm.sample_rate, 44100u);
  EXPECT_EQ(pcm.channels, 1u);
}

TEST(XwmaDecoder, ZeroPadsAShortFinalPacket) {
  auto data = silent_packets(2);
  data.resize(data.size() - 10);
  const auto pcm = decode_xwma(make_xwm(data));
  EXPECT_EQ(pcm.bad_packets, 0u);
}

TEST(XwmaDecoder, SilentFramesDecodeToExactlyTheExpectedSilentSampleCount) {
  constexpr std::size_t packets = 3;
  const auto pcm = decode_xwma(make_xwm(silent_packets(packets)));
  EXPECT_EQ(pcm.bad_packets, 0u);
  EXPECT_EQ(pcm.samples.size(), (packets - 1) * 2048u);
  EXPECT_TRUE(std::all_of(pcm.samples.begin(), pcm.samples.end(), [](std::int16_t v) { return v == 0; }));
}
