#include <fo4/avm2/sound.hpp>
#include <fo4/avm2/sound_decode.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/display.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

fo4::avm2::SoundResource make_resource(std::uint8_t format, bool sample_16bit, bool stereo,
                                       std::uint32_t sample_count, std::uint8_t rate,
                                       std::vector<std::uint8_t> data) {
  fo4::avm2::SoundResource resource;
  resource.format = format;
  resource.sample_16bit = sample_16bit;
  resource.stereo = stereo;
  resource.sample_count = sample_count;
  resource.rate = rate;
  resource.payload = {0, 0, 0, 0, 0, 0, 0};
  resource.payload.insert(resource.payload.end(), data.begin(), data.end());
  return resource;
}

}  // namespace

TEST(Avm2SoundDecode, DecodesRawSixteenBitBigEndian) {
  const fo4::avm2::SoundResource resource =
      make_resource(0, true, false, 2, 3, {0x00, 0x10, 0xFF, 0xF0});
  fo4::avm2::DecodedSound decoded;
  ASSERT_TRUE(fo4::avm2::decode_sound(resource, decoded));
  EXPECT_EQ(decoded.sample_rate, 44100u);
  EXPECT_EQ(decoded.channels, 1u);
  ASSERT_EQ(decoded.samples.size(), 2u);
  EXPECT_EQ(decoded.samples[0], 16);
  EXPECT_EQ(decoded.samples[1], -16);
}

TEST(Avm2SoundDecode, DecodesRawEightBitAsScaledSamples) {
  const fo4::avm2::SoundResource resource =
      make_resource(0, false, false, 2, 2, {0x80, 0x7F});
  fo4::avm2::DecodedSound decoded;
  ASSERT_TRUE(fo4::avm2::decode_sound(resource, decoded));
  EXPECT_EQ(decoded.sample_rate, 22050u);
  ASSERT_EQ(decoded.samples.size(), 2u);
  EXPECT_EQ(decoded.samples[0], -32768);
  EXPECT_EQ(decoded.samples[1], 32512);
}

TEST(Avm2SoundDecode, DecodesSwfAdpcmNibbles) {
  const std::vector<std::uint8_t> data{0x00, 0x00, 0x00, 0x01, 0x23};
  const fo4::avm2::SoundResource resource = make_resource(1, true, false, 5, 0, data);
  fo4::avm2::DecodedSound decoded;
  ASSERT_TRUE(fo4::avm2::decode_sound(resource, decoded));
  EXPECT_EQ(decoded.sample_rate, 5512u);
  ASSERT_EQ(decoded.samples.size(), 5u);
  EXPECT_EQ(decoded.samples[0], 0);
  EXPECT_EQ(decoded.samples[1], 0);
  EXPECT_EQ(decoded.samples[2], 7);
}

TEST(Avm2SoundDecode, RefusesMp3AndTruncatedBodies) {
  fo4::avm2::DecodedSound decoded;
  EXPECT_FALSE(fo4::avm2::decode_sound(make_resource(2, true, false, 2, 3, {0, 0, 0, 0}), decoded));
  EXPECT_FALSE(fo4::avm2::decode_sound(make_resource(0, true, false, 4, 3, {0, 0}), decoded));
  fo4::avm2::SoundResource empty;
  EXPECT_FALSE(fo4::avm2::decode_sound(empty, decoded));
}

TEST(Avm2SoundDecode, SnapshotCarriesThePlacedColourTransform) {
  fo4::avm2::DisplayObject object(nullptr);
  const double multiply[4] = {0.5, 0.25, 0.75, 0.5};
  const double add[4] = {0.0, 1.0, -1.0, 0.0};
  object.set_color_multiply(multiply);
  object.set_color_add(add);
  const fo4::avm2::FrameSnapshot snapshot = fo4::avm2::build_frame_snapshot(object);
  ASSERT_EQ(snapshot.entries.size(), 1u);
  EXPECT_DOUBLE_EQ(snapshot.entries[0].color.red_multiplier, 0.5);
  EXPECT_DOUBLE_EQ(snapshot.entries[0].color.green_multiplier, 0.25);
  EXPECT_DOUBLE_EQ(snapshot.entries[0].color.blue_multiplier, 0.75);
  EXPECT_DOUBLE_EQ(snapshot.entries[0].color.alpha_multiplier, 0.5);
  EXPECT_EQ(snapshot.entries[0].color.green_offset, 1);
  EXPECT_EQ(snapshot.entries[0].color.blue_offset, -1);
}
