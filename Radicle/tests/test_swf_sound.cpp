#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_sound.hpp"
#include "fo4/swf/swf_sound_edit.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace {

using fo4::swf::decode_define_sound;
using fo4::swf::decode_sound_stream_head;
using fo4::swf::decode_start_sound;
using fo4::swf::decode_start_sound2;
using fo4::swf::encode_define_sound;
using fo4::swf::encode_sound_stream_head;
using fo4::swf::read_swf;
using fo4::swf::SwfDefineSoundData;
using fo4::swf::replace_sound_data;
using fo4::swf::retarget_start_sound;
using fo4::swf::SwfSound;
using fo4::swf::SwfSoundEditStatus;
using fo4::swf::SwfSoundStatus;
using fo4::swf::SwfStartSound;
using fo4::swf::SwfStartSound2;
using fo4::swf::SwfSoundStreamHead;
using fo4::swf::SwfStatus;
using fo4::swf::write_swf;
using swf_test::movie_of;
using swf_test::sprite_tag;
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

// Invariant: SyncStop and SyncNoMultiple swapped, or the envelope count read as u16, shifts later fields.
TEST(SwfSoundTest, DecodesStartSoundInfoWithInPointLoopsAndEnvelope) {
  const auto payload = bytes_of({0x02, 0x01, 0x1F, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                 0x00, 0x03, 0x00, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80,
                                 0x00, 0x40, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F});
  SwfStartSound start;
  ASSERT_EQ(decode_start_sound(payload, start), SwfSoundStatus::ok);
  EXPECT_EQ(start.sound_id, 0x0102);
  EXPECT_FALSE(start.info.sync_stop);
  EXPECT_TRUE(start.info.sync_no_multiple);
  ASSERT_TRUE(start.info.in_point.has_value());
  EXPECT_EQ(*start.info.in_point, 16u);
  ASSERT_TRUE(start.info.out_point.has_value());
  EXPECT_EQ(*start.info.out_point, 256u);
  ASSERT_TRUE(start.info.loop_count.has_value());
  EXPECT_EQ(*start.info.loop_count, 3);
  ASSERT_TRUE(start.info.envelope.has_value());
  ASSERT_EQ(start.info.envelope->size(), 2u);
  EXPECT_EQ((*start.info.envelope)[0].position, 0x40u);
  EXPECT_EQ((*start.info.envelope)[0].left, 0x8000);
  EXPECT_EQ((*start.info.envelope)[0].right, 0x4000);
  EXPECT_EQ((*start.info.envelope)[1].position, 0x200u);
  EXPECT_EQ((*start.info.envelope)[1].left, 0);
  EXPECT_EQ((*start.info.envelope)[1].right, 0x7FFF);
}

// Invariant: a StartSound2 class name read length-prefixed, or its terminator skipped, misplaces SOUNDINFO.
TEST(SwfSoundTest, DecodesStartSound2ClassNameThenSoundInfo) {
  const auto payload = bytes_of({0x46, 0x6F, 0x6F, 0x00, 0x21, 0x04, 0x03, 0x02, 0x01});
  SwfStartSound2 start;
  ASSERT_EQ(decode_start_sound2(payload, start), SwfSoundStatus::ok);
  EXPECT_EQ(start.class_name, std::string("Foo"));
  EXPECT_TRUE(start.info.sync_stop);
  EXPECT_FALSE(start.info.sync_no_multiple);
  ASSERT_TRUE(start.info.in_point.has_value());
  EXPECT_EQ(*start.info.in_point, 0x01020304u);
  EXPECT_FALSE(start.info.out_point.has_value());
  EXPECT_FALSE(start.info.loop_count.has_value());
  EXPECT_FALSE(start.info.envelope.has_value());
}

// Invariant: a DefineSound format read from the wrong nibble, or data offset off by one, misreads it.
TEST(SwfSoundTest, DecodesDefineSoundHeaderAndViewsItsDataWithoutDecoding) {
  const auto payload = bytes_of({0x07, 0x00, 0x2B, 0x00, 0x04, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF});
  SwfDefineSoundData defined;
  ASSERT_EQ(decode_define_sound(payload, defined), SwfSoundStatus::ok);
  EXPECT_EQ(defined.header.sound_id, 7);
  EXPECT_EQ(defined.header.format, 2);
  EXPECT_EQ(defined.header.rate, 2);
  EXPECT_TRUE(defined.header.sample_16bit);
  EXPECT_TRUE(defined.header.stereo);
  EXPECT_EQ(defined.header.sample_count, 1024u);
  EXPECT_EQ(defined.header.data_size, 4u);
  EXPECT_EQ(defined.data.data(), payload.data() + 7);
  EXPECT_EQ(std::vector<std::byte>(defined.data.begin(), defined.data.end()),
            bytes_of({0xDE, 0xAD, 0xBE, 0xEF}));
}

// Invariant: an envelope count above the points present must refuse, not read past the payload.
TEST(SwfSoundTest, RefusesStartSoundWhoseEnvelopeIsCutShort) {
  const auto payload = bytes_of({0x02, 0x01, 0x1F, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                 0x00, 0x03, 0x00, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80,
                                 0x00, 0x40});
  SwfStartSound start;
  EXPECT_EQ(decode_start_sound(payload, start), SwfSoundStatus::truncated);
  EXPECT_EQ(start.sound_id, 0);
  EXPECT_FALSE(start.info.envelope.has_value());
}

// Invariant: a StartSound2 class name with no zero terminator must refuse, not run to the end.
TEST(SwfSoundTest, RefusesStartSound2WithUnterminatedClassName) {
  const auto payload = bytes_of({0x46, 0x6F, 0x6F, 0x21, 0x04, 0x03, 0x02, 0x01});
  SwfStartSound2 start;
  EXPECT_EQ(decode_start_sound2(payload, start), SwfSoundStatus::truncated);
  EXPECT_TRUE(start.class_name.empty());
}

// Invariant: a DefineSound cut inside its sample count must refuse and leave no data view behind.
TEST(SwfSoundTest, RefusesDefineSoundCutInsideItsSampleCount) {
  const auto payload = bytes_of({0x07, 0x00, 0x2B, 0x00, 0x04});
  SwfDefineSoundData defined;
  EXPECT_EQ(decode_define_sound(payload, defined), SwfSoundStatus::truncated);
  EXPECT_TRUE(defined.data.empty());
  EXPECT_EQ(defined.header.sound_id, 0);
}

// Invariant: an MP3 stream head cut before its latency must refuse, not default latency to zero.
TEST(SwfSoundTest, DecodesMp3StreamHeadWithItsLatencySeekAndRefusesWhenCut) {
  const auto payload = bytes_of({0x0F, 0x2B, 0x00, 0x04, 0x20, 0x01});
  SwfSoundStreamHead head;
  ASSERT_EQ(decode_sound_stream_head(payload, head), SwfSoundStatus::ok);
  EXPECT_EQ(head.reserved, 0);
  EXPECT_EQ(head.playback_rate, 3);
  EXPECT_TRUE(head.playback_16bit);
  EXPECT_TRUE(head.playback_stereo);
  EXPECT_EQ(head.compression, 2);
  EXPECT_EQ(head.rate, 2);
  EXPECT_TRUE(head.sample_16bit);
  EXPECT_TRUE(head.stereo);
  EXPECT_EQ(head.sample_count, 1024);
  ASSERT_TRUE(head.latency_seek.has_value());
  EXPECT_EQ(*head.latency_seek, 288);

  const auto cut = bytes_of({0x0F, 0x2B, 0x00, 0x04});
  SwfSoundStreamHead refused;
  EXPECT_EQ(decode_sound_stream_head(cut, refused), SwfSoundStatus::truncated);
  EXPECT_FALSE(refused.latency_seek.has_value());
}

// Invariant: reserved compression codes 9 and 12-15 must refuse, not decode as a real format.
TEST(SwfSoundTest, RefusesReservedCompressionCodesInDefineSoundAndStreamHead) {
  SwfDefineSoundData defined;
  EXPECT_EQ(decode_define_sound(bytes_of({0x07, 0x00, 0x90, 0x04, 0x00, 0x00, 0x00, 0xDE}),
                                defined),
            SwfSoundStatus::unknown_compression);
  EXPECT_TRUE(defined.data.empty());

  SwfSoundStreamHead head;
  EXPECT_EQ(decode_sound_stream_head(bytes_of({0x0F, 0xC0, 0x00, 0x04}), head),
            SwfSoundStatus::unknown_compression);
  EXPECT_FALSE(head.latency_seek.has_value());
}

SwfSound sound_header(std::uint16_t id, std::uint8_t format, std::uint8_t rate,
                      std::uint32_t samples) {
  SwfSound header;
  header.sound_id = id;
  header.format = format;
  header.rate = rate;
  header.sample_16bit = true;
  header.stereo = true;
  header.sample_count = samples;
  return header;
}

// Invariant: the rewritten DefineSound keeps its id, takes the new format byte and sample count, and leaves the StartSound alone.
TEST(SwfSoundEditTest, ReplacesDefineSoundDataAndWritesTheNewHeaderBack) {
  const auto source =
      movie_of(1, {{14, bytes_of({0x07, 0x00, 0x0F, 0x03, 0x00, 0x00, 0x00, 0x10, 0x20, 0x30, 0x40,
                                  0x50, 0x60})},
                   {15, bytes_of({0x07, 0x00, 0x00})}});
  auto result = read_swf(source);
  ASSERT_TRUE(result.ok()) << result.message;
  const auto data = bytes_of({0xDE, 0xAD, 0xBE, 0xEF});
  ASSERT_EQ(replace_sound_data(result.file, sound_header(7, 2, 3, 1024), data),
            SwfSoundEditStatus::ok);

  const auto reread = read_swf(write_swf(result.file));
  ASSERT_TRUE(reread.ok()) << reread.message;
  ASSERT_EQ(reread.file.tags.size(), 3u);
  EXPECT_EQ(reread.file.tags[0].payload,
            bytes_of({0x07, 0x00, 0x2F, 0x00, 0x04, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF}));
  EXPECT_EQ(reread.file.tags[1].payload, bytes_of({0x07, 0x00, 0x00}));
  SwfDefineSoundData defined;
  ASSERT_EQ(decode_define_sound(reread.file.tags[0].payload, defined), SwfSoundStatus::ok);
  EXPECT_EQ(defined.header.sound_id, 7);
  EXPECT_EQ(defined.header.format, 2);
  EXPECT_EQ(defined.header.rate, 3);
  EXPECT_TRUE(defined.header.sample_16bit);
  EXPECT_TRUE(defined.header.stereo);
  EXPECT_EQ(defined.header.sample_count, 1024u);
  EXPECT_EQ(defined.header.data_size, 4u);
  EXPECT_EQ(std::vector<std::byte>(defined.data.begin(), defined.data.end()), data);
}

// Invariant: an unknown id, a sprite id, a reserved or out-of-range format, rate 4 and data past the cap refuse and keep the bytes.
TEST(SwfSoundEditTest, RefusesABadSoundReplacementAndLeavesTheFileUntouched) {
  const auto source = movie_of(1, {{14, bytes_of({0x07, 0x00, 0x0F, 0x01, 0x00, 0x00, 0x00, 0xAA})},
                                   {39, sprite_tag(5, 1, {})}});
  auto result = read_swf(source);
  ASSERT_TRUE(result.ok()) << result.message;
  const auto before = write_swf(result.file);
  const auto data = bytes_of({0x01, 0x02});
  const std::vector<std::byte> oversized(64u * 1024u * 1024u + 1u);
  EXPECT_EQ(replace_sound_data(result.file, sound_header(9, 0, 2, 1), data),
            SwfSoundEditStatus::unknown_sound);
  EXPECT_EQ(replace_sound_data(result.file, sound_header(5, 0, 2, 1), data),
            SwfSoundEditStatus::not_a_define_sound);
  EXPECT_EQ(replace_sound_data(result.file, sound_header(7, 9, 2, 1), data),
            SwfSoundEditStatus::unknown_compression);
  EXPECT_EQ(replace_sound_data(result.file, sound_header(7, 17, 2, 1), data),
            SwfSoundEditStatus::unknown_compression);
  EXPECT_EQ(replace_sound_data(result.file, sound_header(7, 0, 4, 1), data),
            SwfSoundEditStatus::bad_rate);
  EXPECT_EQ(replace_sound_data(result.file, sound_header(7, 0, 2, 1), oversized),
            SwfSoundEditStatus::data_too_large);
  EXPECT_EQ(write_swf(result.file), before);
}

// Invariant: a retargeted StartSound takes the new id and keeps its SOUNDINFO bytes, so its in point survives.
TEST(SwfSoundEditTest, RetargetsAStartSoundAndKeepsItsSoundInfoBytes) {
  const auto source =
      movie_of(1, {{14, bytes_of({0x07, 0x00, 0x0F, 0x01, 0x00, 0x00, 0x00, 0xAA})},
                   {14, bytes_of({0x09, 0x00, 0x0F, 0x01, 0x00, 0x00, 0x00, 0xBB})},
                   {15, bytes_of({0x07, 0x00, 0x21, 0x10, 0x00, 0x00, 0x00})}});
  auto result = read_swf(source);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_EQ(retarget_start_sound(result.file, 2, 9), SwfSoundEditStatus::ok);

  const auto reread = read_swf(write_swf(result.file));
  ASSERT_TRUE(reread.ok()) << reread.message;
  ASSERT_EQ(reread.file.tags.size(), 4u);
  EXPECT_EQ(reread.file.tags[2].payload,
            bytes_of({0x09, 0x00, 0x21, 0x10, 0x00, 0x00, 0x00}));
  SwfStartSound start;
  ASSERT_EQ(decode_start_sound(reread.file.tags[2].payload, start), SwfSoundStatus::ok);
  EXPECT_EQ(start.sound_id, 9);
  EXPECT_TRUE(start.info.sync_stop);
  ASSERT_TRUE(start.info.in_point.has_value());
  EXPECT_EQ(*start.info.in_point, 16u);
}

// Invariant: a tag that is not a StartSound, an index past the tag list, or an id with no DefineSound refuses and leaves the file unchanged.
TEST(SwfSoundEditTest, RefusesABadRetargetAndLeavesTheFileUntouched) {
  const auto source = movie_of(1, {{14, bytes_of({0x07, 0x00, 0x0F, 0x01, 0x00, 0x00, 0x00, 0xAA})},
                                   {39, sprite_tag(5, 1, {})},
                                   {15, bytes_of({0x07, 0x00, 0x00})}});
  auto result = read_swf(source);
  ASSERT_TRUE(result.ok()) << result.message;
  const auto before = write_swf(result.file);
  EXPECT_EQ(retarget_start_sound(result.file, 0, 7), SwfSoundEditStatus::not_a_start_sound);
  EXPECT_EQ(retarget_start_sound(result.file, 9, 7), SwfSoundEditStatus::unknown_tag);
  EXPECT_EQ(retarget_start_sound(result.file, 2, 4), SwfSoundEditStatus::unknown_sound);
  EXPECT_EQ(retarget_start_sound(result.file, 2, 5), SwfSoundEditStatus::unknown_sound);
  EXPECT_EQ(write_swf(result.file), before);
}

}  // namespace
