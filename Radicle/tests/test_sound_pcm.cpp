#include "fo4/archive/assets.hpp"
#include "fo4/audio/sound_pcm.hpp"
#include "fo4/c_api_behavior.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_sound.h"
#include "fo4/error.hpp"

#include "ba2_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using ba2_fixture::Ba2ReaderTest;
using ba2_fixture::general;
using fo4::audio::SoundContainerKind;
using fo4::audio::decode_sound;
using fo4::audio::decode_wav;
using fo4::audio::read_sound;
using fo4::audio::sound_path_candidates;

namespace {

using Bytes = std::vector<std::byte>;

void put_u16(Bytes& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void put_u32(Bytes& out, std::uint32_t value) {
  put_u16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  put_u16(out, static_cast<std::uint16_t>((value >> 16u) & 0xFFFFu));
}

void put_tag(Bytes& out, const char* tag) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>(tag[i]));
}

void put_chunk(Bytes& out, const char* tag, const Bytes& body) {
  put_tag(out, tag);
  put_u32(out, static_cast<std::uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
  if ((body.size() & 1u) != 0) out.push_back(std::byte{0});
}

Bytes fmt_body(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits, bool extensible) {
  Bytes fmt;
  const auto align = static_cast<std::uint16_t>(channels * (bits / 8u));
  put_u16(fmt, extensible ? std::uint16_t{0xFFFE} : format);
  put_u16(fmt, channels);
  put_u32(fmt, rate);
  put_u32(fmt, rate * align);
  put_u16(fmt, align);
  put_u16(fmt, bits);
  if (extensible) {
    put_u16(fmt, 22);
    put_u16(fmt, bits);
    put_u32(fmt, 3);
    put_u16(fmt, format);
    for (int i = 0; i < 14; ++i) fmt.push_back(std::byte{0});
  }
  return fmt;
}

Bytes make_wav(std::uint16_t channels, std::uint32_t rate, std::uint16_t bits, const std::vector<std::uint8_t>& samples,
               bool extensible = false, bool with_odd_chunk = false, std::uint16_t format = 1) {
  Bytes body;
  put_tag(body, "WAVE");
  put_chunk(body, "fmt ", fmt_body(format, channels, rate, bits, extensible));
  if (with_odd_chunk) put_chunk(body, "LIST", Bytes(3, std::byte{0x41}));
  Bytes data;
  for (const auto value : samples) data.push_back(static_cast<std::byte>(value));
  put_chunk(body, "data", data);
  Bytes wav;
  put_tag(wav, "RIFF");
  put_u32(wav, static_cast<std::uint32_t>(body.size()));
  wav.insert(wav.end(), body.begin(), body.end());
  return wav;
}

std::string_view view_of(const Bytes& bytes) {
  return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<std::uint8_t> raw_of(const Bytes& bytes) {
  std::vector<std::uint8_t> out;
  for (const auto b : bytes) out.push_back(std::to_integer<std::uint8_t>(b));
  return out;
}

}  // namespace

TEST(SoundPcm, SixteenBitStereoWavKeepsItsSamplesAndFormat) {
  const std::vector<std::uint8_t> samples{1, 2, 3, 4, 5, 6, 7, 8};
  const auto sound = decode_wav(make_wav(2, 22050, 16, samples));
  EXPECT_EQ(sound.container, SoundContainerKind::wav);
  EXPECT_EQ(sound.sample_rate, 22050u);
  EXPECT_EQ(sound.channels, 2u);
  EXPECT_EQ(sound.bits_per_sample, 16u);
  EXPECT_EQ(sound.bad_packets, 0u);
  EXPECT_EQ(raw_of(sound.pcm), samples);
}

TEST(SoundPcm, EightBitWavStaysUnsignedAndEightBit) {
  const std::vector<std::uint8_t> samples{0, 128, 255, 127};
  const auto sound = decode_wav(make_wav(1, 11025, 8, samples));
  EXPECT_EQ(sound.bits_per_sample, 8u);
  EXPECT_EQ(sound.channels, 1u);
  EXPECT_EQ(raw_of(sound.pcm), samples);
}

TEST(SoundPcm, TwentyFourBitWavKeepsTheHighTwoBytesOfEverySample) {
  const std::vector<std::uint8_t> samples{0x01, 0x02, 0x03, 0x11, 0x12, 0x13, 0x21, 0x22, 0x23, 0x31, 0x32, 0x33};
  const auto sound = decode_wav(make_wav(2, 44100, 24, samples));
  EXPECT_EQ(sound.bits_per_sample, 16u);
  EXPECT_EQ(raw_of(sound.pcm), (std::vector<std::uint8_t>{0x02, 0x03, 0x12, 0x13, 0x22, 0x23, 0x32, 0x33}));
}

TEST(SoundPcm, ThirtyTwoBitWavKeepsTheHighTwoBytesOfEverySample) {
  const std::vector<std::uint8_t> samples{0x01, 0x02, 0x03, 0x04, 0x11, 0x12, 0x13, 0x14};
  const auto sound = decode_wav(make_wav(1, 48000, 32, samples));
  EXPECT_EQ(sound.bits_per_sample, 16u);
  EXPECT_EQ(raw_of(sound.pcm), (std::vector<std::uint8_t>{0x03, 0x04, 0x13, 0x14}));
}

TEST(SoundPcm, PartialTrailingFrameIsDropped) {
  const auto sound = decode_wav(make_wav(2, 22050, 16, {1, 2, 3, 4, 5, 6}));
  EXPECT_EQ(sound.pcm.size(), 4u);
}

TEST(SoundPcm, ExtensiblePcmAndOddSizedChunksBeforeDataDecode) {
  const std::vector<std::uint8_t> samples{9, 8, 7, 6};
  const auto sound = decode_wav(make_wav(1, 32000, 16, samples, true, true));
  EXPECT_EQ(sound.sample_rate, 32000u);
  EXPECT_EQ(raw_of(sound.pcm), samples);
}

TEST(SoundPcm, MalformedWavIsAParseError) {
  const auto good = make_wav(1, 22050, 16, {1, 2, 3, 4});
  Bytes truncated(good.begin(), good.end() - 2);
  EXPECT_THROW(decode_wav(truncated), fo4::parse_error);
  EXPECT_THROW(decode_wav(Bytes{}), fo4::parse_error);
  EXPECT_THROW(decode_wav(make_wav(1, 22050, 16, {1, 2}, false, false, 3)), fo4::parse_error);
  EXPECT_THROW(decode_wav(make_wav(0, 22050, 16, {1, 2})), fo4::parse_error);
  EXPECT_THROW(decode_wav(make_wav(1, 0, 16, {1, 2})), fo4::parse_error);
  EXPECT_THROW(decode_wav(make_wav(1, 22050, 12, {1, 2})), fo4::parse_error);

  Bytes no_fmt;
  Bytes body;
  put_tag(body, "WAVE");
  put_chunk(body, "data", Bytes(4, std::byte{1}));
  put_tag(no_fmt, "RIFF");
  put_u32(no_fmt, static_cast<std::uint32_t>(body.size()));
  no_fmt.insert(no_fmt.end(), body.begin(), body.end());
  EXPECT_THROW(decode_wav(no_fmt), fo4::parse_error);

  Bytes no_data;
  Bytes body2;
  put_tag(body2, "WAVE");
  put_chunk(body2, "fmt ", fmt_body(1, 1, 22050, 16, false));
  put_tag(no_data, "RIFF");
  put_u32(no_data, static_cast<std::uint32_t>(body2.size()));
  no_data.insert(no_data.end(), body2.begin(), body2.end());
  EXPECT_THROW(decode_wav(no_data), fo4::parse_error);
}

TEST(SoundPcm, DecodeSoundRoutesWavAndRejectsUnknownContainers) {
  const auto sound = decode_sound(make_wav(1, 22050, 16, {1, 2, 3, 4}));
  EXPECT_EQ(sound.container, SoundContainerKind::wav);
  EXPECT_EQ(sound.pcm.size(), 4u);
  EXPECT_THROW(decode_sound(Bytes(32, std::byte{0x55})), fo4::parse_error);
  EXPECT_THROW(decode_sound(Bytes{}), fo4::parse_error);
  Bytes fuz;
  put_tag(fuz, "FUZE");
  fuz.resize(32, std::byte{0});
  EXPECT_THROW(decode_sound(fuz), std::exception);
}

TEST(SoundPcm, PathCandidatesTryTheGivenExtensionThenWavXwmFuzUnderEachRoot) {
  EXPECT_EQ(sound_path_candidates("data\\Sound\\FX\\UI\\a.wav"),
            (std::vector<std::string>{"Sound\\FX\\UI\\a.wav", "Sound\\FX\\UI\\a.xwm", "Sound\\FX\\UI\\a.fuz"}));
  EXPECT_EQ(sound_path_candidates("/ui/b.xwm"),
            (std::vector<std::string>{"ui\\b.xwm", "ui\\b.wav", "ui\\b.fuz", "sound\\ui\\b.xwm", "sound\\ui\\b.wav",
                                      "sound\\ui\\b.fuz", "sound\\fx\\ui\\b.xwm", "sound\\fx\\ui\\b.wav",
                                      "sound\\fx\\ui\\b.fuz"}));
  const auto bare = sound_path_candidates("ui\\c");
  ASSERT_EQ(bare.size(), 12u);
  EXPECT_EQ(bare.front(), "ui\\c");
  EXPECT_EQ(bare[1], "ui\\c.wav");
  EXPECT_EQ(bare.back(), "sound\\fx\\ui\\c.fuz");
}

TEST_F(Ba2ReaderTest, ReadSoundResolvesByExtensionAndRootFallbackAndPrefersLooseFiles) {
  const auto archive_wav = make_wav(2, 22050, 16, {1, 2, 3, 4});
  const auto archive = write_general("Sounds.ba2", {general("Sound\\FX\\UI\\a.wav", view_of(archive_wav)),
                                                    general("sound\\fx\\ui\\d.wav", view_of(archive_wav))});
  const auto loose_wav = make_wav(1, 11025, 8, {200, 100});
  write_file(data() / "sound" / "fx" / "ui" / "a.wav", loose_wav);
  const std::vector<std::filesystem::path> archives{archive};
  const auto assets = fo4::archive::AssetResolver::open(data(), archives);

  const auto loose = read_sound(assets, "data\\Sound\\FX\\UI\\a.wav");
  ASSERT_TRUE(loose.has_value());
  EXPECT_EQ(loose->path, "Sound\\FX\\UI\\a.wav");
  EXPECT_EQ(loose->pcm.sample_rate, 11025u);
  EXPECT_EQ(loose->pcm.bits_per_sample, 8u);

  const auto archived = read_sound(assets, "ui\\d.xwm");
  ASSERT_TRUE(archived.has_value());
  EXPECT_EQ(archived->path, "sound\\fx\\ui\\d.wav");
  EXPECT_EQ(archived->pcm.container, SoundContainerKind::wav);
  EXPECT_EQ(archived->pcm.sample_rate, 22050u);
  EXPECT_EQ(archived->pcm.channels, 2u);

  EXPECT_FALSE(read_sound(assets, "ui\\none.wav").has_value());
}

TEST_F(Ba2ReaderTest, CApiDecodesSoundBytesAndReadsANamedSoundThroughTheResolver) {
  const auto wav = make_wav(2, 22050, 16, {1, 2, 3, 4, 5, 6, 7, 8});
  const auto raw = raw_of(wav);
  std::uint8_t* pcm = nullptr;
  std::size_t pcm_size = 0;
  std::uint32_t rate = 0;
  std::uint16_t channels = 0, bits = 0;
  std::int32_t container = -1;
  std::size_t bad = 99;
  ASSERT_EQ(radicle_audio_decode_sound(raw.data(), raw.size(), &pcm, &pcm_size, &rate, &channels, &bits, &container, &bad),
            RADICLE_STATUS_OK);
  EXPECT_EQ(pcm_size, 8u);
  EXPECT_EQ(rate, 22050u);
  EXPECT_EQ(channels, 2u);
  EXPECT_EQ(bits, 16u);
  EXPECT_EQ(container, 0);
  EXPECT_EQ(bad, 0u);
  ASSERT_NE(pcm, nullptr);
  EXPECT_EQ(pcm[7], 8u);
  radicle_free_bytes(pcm);

  const std::uint8_t junk[4] = {1, 2, 3, 4};
  EXPECT_EQ(radicle_audio_decode_sound(junk, sizeof junk, &pcm, &pcm_size, &rate, &channels, &bits, &container, &bad),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(pcm, nullptr);
  EXPECT_EQ(radicle_audio_decode_sound(nullptr, 0, &pcm, &pcm_size, &rate, &channels, &bits, &container, &bad),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const auto archive = write_general("Sounds.ba2", {general("sound\\fx\\ui\\e.wav", view_of(wav))});
  const std::string data_dir = data().string();
  const std::string archive_path = archive.string();
  const char* archive_paths[] = {archive_path.c_str()};
  radicle_assets* assets = nullptr;
  ASSERT_EQ(radicle_assets_open(data_dir.c_str(), archive_paths, 1, &assets), RADICLE_STATUS_OK);
  std::uint8_t* path = nullptr;
  std::size_t path_size = 0;
  ASSERT_EQ(radicle_assets_read_sound(assets, "ui\\e.xwm", &path, &path_size, &pcm, &pcm_size, &rate, &channels, &bits,
                                      &container, &bad),
            RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(path), path_size), "sound\\fx\\ui\\e.wav");
  EXPECT_EQ(pcm_size, 8u);
  EXPECT_EQ(rate, 22050u);
  radicle_free_bytes(path);
  radicle_free_bytes(pcm);
  EXPECT_EQ(radicle_assets_read_sound(assets, "ui\\missing.wav", &path, &path_size, &pcm, &pcm_size, &rate, &channels,
                                      &bits, &container, &bad),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(path, nullptr);
  EXPECT_EQ(pcm, nullptr);
  EXPECT_EQ(radicle_assets_read_sound(nullptr, "x", &path, &path_size, &pcm, &pcm_size, &rate, &channels, &bits,
                                      &container, &bad),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_assets_destroy(assets);
}

TEST(SoundPcm, PackageProcedureNamesFollowTheNativeTableAndEndAtTheLastEntry) {
  EXPECT_STREQ(radicle_package_procedure_name(0), "Travel");
  EXPECT_STREQ(radicle_package_procedure_name(29), "Say");
  EXPECT_EQ(radicle_package_procedure_name(30), nullptr);
}
