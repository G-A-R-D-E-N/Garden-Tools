#include "fo4/archive/assets.hpp"
#include "fo4/audio/sound_pcm.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/sound.hpp"
#include "real_data_build.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>

TEST(SoundPcmRealData, SoundDescriptorFilesDecodeThroughTheResolverAsWavAndXwma) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const std::filesystem::path data{dir};
  const auto master = data / "Fallout4.esm";
  const auto sounds = data / "Fallout4 - Sounds.ba2";
  if (!std::filesystem::exists(master) || !std::filesystem::exists(sounds)) {
    GTEST_SKIP() << "no Fallout4.esm or Fallout4 - Sounds.ba2 in FO4_DATA_DIR";
  }
  const std::filesystem::path plugins[] = {master};
  const auto env = fo4::env::Environment::load(plugins);
  const std::filesystem::path archives[] = {sounds};
  const auto assets = fo4::archive::AssetResolver::open(data, archives);

  std::size_t wav = 0, xwma = 0, missing = 0, seen = 0;
  for (const auto* ref : env.plugin_records(0)) {
    if (wav >= 12 && xwma >= 12) break;
    if (ref->record->header.signature.to_string() != "SNDR") continue;
    const auto descriptor = fo4::env::decode_sound_descriptor(env, *ref);
    for (const auto file : descriptor.files) {
      if (++seen % 13 != 0) continue;
      SCOPED_TRACE(std::string(file));
      const auto sound = fo4::audio::read_sound(assets, file);
      if (!sound) {
        ++missing;
        continue;
      }
      const auto& pcm = sound->pcm;
      EXPECT_TRUE(pcm.bits_per_sample == 8 || pcm.bits_per_sample == 16);
      EXPECT_GE(pcm.channels, 1u);
      EXPECT_LE(pcm.channels, 2u);
      EXPECT_GE(pcm.sample_rate, 4000u);
      EXPECT_LE(pcm.sample_rate, 192000u);
      ASSERT_FALSE(pcm.pcm.empty());
      EXPECT_EQ(pcm.pcm.size() % (pcm.channels * (pcm.bits_per_sample / 8u)), 0u);
      if (pcm.container == fo4::audio::SoundContainerKind::wav) ++wav;
      if (pcm.container == fo4::audio::SoundContainerKind::xwma) ++xwma;
    }
  }
  EXPECT_GT(wav, 0u);
  EXPECT_GT(xwma, 0u);
  EXPECT_LT(missing, seen);
}
