#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace {

using fo4::swf::encode_define_sound;
using fo4::swf::encode_sound_stream_head;
using fo4::swf::read_swf;
using fo4::swf::SwfStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

std::vector<std::byte> bytes_of(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

std::vector<std::byte> movie_with(std::uint16_t code, const std::vector<std::byte>& payload) {
  SwfBuilder builder;
  builder.rect(0, 0, 0, 0, 0);
  builder.u16(24u * 256u);
  builder.u16(1);
  builder.tag(code, payload, false);
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

TEST(SwfSoundTest, DecodesDefineSoundHeaderAndRebuildsItsPayloadByteExactly) {
  const auto payload = bytes_of({0x02, 0x01, 0x0F, 0x03, 0x00, 0x00, 0x00, 0x10, 0x20, 0x30,
                                 0x40, 0x50, 0x60});
  const auto result = read_swf(movie_with(14, payload));
  ASSERT_EQ(result.status, SwfStatus::ok) << result.message;
  ASSERT_GE(result.file.tags.size(), 1u);
  const auto& current = result.file.tags[0];
  ASSERT_TRUE(current.sound.has_value());
  EXPECT_EQ(current.sound->sound_id, 0x0102);
  EXPECT_EQ(current.sound->format, 0);
  EXPECT_EQ(current.sound->rate, 3);
  EXPECT_TRUE(current.sound->sample_16bit);
  EXPECT_TRUE(current.sound->stereo);
  EXPECT_EQ(current.sound->sample_count, 3u);
  EXPECT_EQ(current.sound->data_size, 6u);
  EXPECT_EQ(encode_define_sound(*current.sound, std::span<const std::byte>(payload).subspan(7)),
            payload);
}

TEST(SwfSoundTest, DecodesStreamHeadAndKeepsTheReservedBits) {
  const auto payload = bytes_of({0xAF, 0x0E, 0x00, 0x01});
  const auto result = read_swf(movie_with(18, payload));
  ASSERT_EQ(result.status, SwfStatus::ok) << result.message;
  ASSERT_GE(result.file.tags.size(), 1u);
  const auto& head = result.file.tags[0].sound_stream_head;
  ASSERT_TRUE(head.has_value());
  EXPECT_EQ(head->reserved, 0x0Au);
  EXPECT_EQ(head->playback_rate, 3);
  EXPECT_TRUE(head->playback_16bit);
  EXPECT_TRUE(head->playback_stereo);
  EXPECT_EQ(head->compression, 0);
  EXPECT_EQ(head->rate, 3);
  EXPECT_TRUE(head->sample_16bit);
  EXPECT_FALSE(head->stereo);
  EXPECT_EQ(head->sample_count, 256u);
  EXPECT_FALSE(head->latency_seek.has_value());
  EXPECT_EQ(encode_sound_stream_head(*head), payload);
}

TEST(SwfSoundTest, DecodesMp3StreamHeadWithItsSignedLatencySeek) {
  const auto payload = bytes_of({0x0F, 0x2F, 0x00, 0x04, 0xFF, 0xFF});
  const auto result = read_swf(movie_with(45, payload));
  ASSERT_EQ(result.status, SwfStatus::ok) << result.message;
  ASSERT_GE(result.file.tags.size(), 1u);
  const auto& head = result.file.tags[0].sound_stream_head;
  ASSERT_TRUE(head.has_value());
  EXPECT_EQ(head->compression, 2);
  EXPECT_EQ(head->sample_count, 1024u);
  ASSERT_TRUE(head->latency_seek.has_value());
  EXPECT_EQ(*head->latency_seek, -1);
  EXPECT_EQ(encode_sound_stream_head(*head), payload);
}

TEST(SwfSoundTest, RefusesMp3StreamHeadWithoutItsLatencySeek) {
  const auto payload = bytes_of({0x0F, 0x2F, 0x00, 0x04});
  const auto result = read_swf(movie_with(18, payload));
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  ASSERT_GE(result.file.tags.size(), 1u);
  EXPECT_FALSE(result.file.tags[0].sound_stream_head.has_value());
  EXPECT_EQ(result.file.tags[0].payload, payload);
}

}  // namespace
