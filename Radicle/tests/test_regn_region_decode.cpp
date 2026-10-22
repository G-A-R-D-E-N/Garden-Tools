#include "fo4/environment/region.hpp"
#include "env_node_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::envnode::bytes_of;
using fo4::test::envnode::floats;
using fo4::test::envnode::rec;
using fo4::test::envnode::top;

Bytes text_bytes(std::string_view text) {
  Bytes out;
  for (const char c : text) out.push_back(static_cast<std::byte>(c));
  out.push_back(std::byte{0});
  return out;
}

TEST(RegnRows, RefusesPayloadsThatDoNotMatchTheirEntryType) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN", {rec("REGN", 0x0F01, {{"RDAT", bytes_of({3, 0})}, {"RDOT", Bytes(52)}}),
                        rec("REGN", 0x0F02, {{"RDAT", bytes_of({2, 0})}, {"RDWT", Bytes(12)}}),
                        rec("REGN", 0x0F03, {{"RDAT", bytes_of({4, 0})}, {"RDSA", Bytes(12)}}),
                        rec("REGN", 0x0F04, {{"RDAT", bytes_of({6, 0})}, {"RDMO", bytes_of({1})}}),
                        rec("REGN", 0x0F05, {{"RDAT", bytes_of({7, 0})}, {"RDGS", Bytes(8)}}),
                        rec("REGN", 0x0F06, {{"RDAT", bytes_of({5, 0})}, {"RDMP", text_bytes("Map")}}),
                        rec("REGN", 0x0F07, {{"RDAT", bytes_of({0, 0})}, {"RDWT", Bytes(12)}}),
                        rec("REGN", 0x0F08, {{"RDAT", bytes_of({3, 0})}, {"RDGS", Bytes(8)}}),
                        rec("REGN", 0x0F09, {{"RDAT", bytes_of({3, 0})}, {"RDWT", Bytes(12)}}),
                        rec("REGN", 0x0F0A, {{"RDAT", bytes_of({7, 0})},
                                             {"RDMO", bytes_of({1})},
                                             {"RDSA", Bytes(12)}})})});
  fo4::test::TempDataDir dir;
  dir.write("Types.esp", bytes, 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Types.esp"};
  const auto env = fo4::env::Environment::load(ordered);

  for (const auto* local : {"000F01", "000F02", "000F03", "000F04", "000F05", "000F06", "000F07",
                            "000F08"}) {
    const auto* ref = env.winner(fo4::FormKey::parse(std::string(local) + ":Types.esp"));
    ASSERT_NE(ref, nullptr) << local;
    EXPECT_THROW(fo4::env::decode_region(env, *ref), fo4::env::WorldDecodeError) << local;
  }

  const auto* weather = env.winner(fo4::FormKey::parse("000F09:Types.esp"));
  ASSERT_NE(weather, nullptr);
  const auto weather_region = fo4::env::decode_region(env, *weather);
  ASSERT_EQ(weather_region.entries.size(), 1u);
  EXPECT_EQ(weather_region.entries[0].type, 3u);
  EXPECT_EQ(weather_region.entries[0].weathers.size(), 1u);

  const auto* sound = env.winner(fo4::FormKey::parse("000F0A:Types.esp"));
  ASSERT_NE(sound, nullptr);
  const auto sound_region = fo4::env::decode_region(env, *sound);
  ASSERT_EQ(sound_region.entries.size(), 1u);
  EXPECT_EQ(sound_region.entries[0].type, 7u);
  EXPECT_TRUE(sound_region.entries[0].music.has_value());
  EXPECT_EQ(sound_region.entries[0].sounds.size(), 1u);
}

TEST(RegnRows, DecodesAnamInBothXEditPositionsAndEntryLodMultiplier) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN",
               {rec("REGN", 0x0F20, {{"RPLI", bytes_of({80})},
                                     {"RPLD", floats({1.0f, 2.0f})},
                                     {"ANAM", floats({2048.0f})}}),
                rec("REGN", 0x0F21, {{"RPLI", bytes_of({80})},
                                     {"RPLD", floats({1.0f, 2.0f})},
                                     {"RDAT", bytes_of({3, 0})},
                                     {"RDWT", Bytes(12)},
                                     {"RLDM", floats({2.5f})},
                                     {"ANAM", floats({4096.0f})}}),
                rec("REGN", 0x0F22, {{"ANAM", floats({1.0f})}}),
                rec("REGN", 0x0F23, {{"RLDM", floats({1.0f})}}),
                rec("REGN", 0x0F24, {{"RPLI", bytes_of({80})}, {"ANAM", Bytes(2)}}),
                rec("REGN", 0x0F25, {{"RPLI", bytes_of({80})},
                                     {"RPLD", floats({1.0f, 2.0f})},
                                     {"RDAT", bytes_of({3, 0})},
                                     {"RDWT", Bytes(12)},
                                     {"RPLI", bytes_of({120})},
                                     {"ANAM", floats({2048.0f})}}),
                rec("REGN", 0x0F26, {{"RPLI", bytes_of({80})},
                                     {"RDAT", bytes_of({3, 0})},
                                     {"RPLI", bytes_of({120})},
                                     {"RDWT", Bytes(12)}}),
                rec("REGN", 0x0F27, {{"RPLI", bytes_of({80})},
                                     {"RDAT", bytes_of({3, 0})},
                                     {"RPLI", bytes_of({120})},
                                     {"RLDM", floats({1.0f})}})})});
  fo4::test::TempDataDir dir;
  dir.write("Anam.esp", bytes, 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Anam.esp"};
  const auto env = fo4::env::Environment::load(ordered);

  const auto* area_ref = env.winner(fo4::FormKey::parse("000F20:Anam.esp"));
  ASSERT_NE(area_ref, nullptr);
  const auto area_region = fo4::env::decode_region(env, *area_ref);
  ASSERT_EQ(area_region.areas.size(), 1u);
  ASSERT_TRUE(area_region.areas[0].unknown_anam.has_value());
  EXPECT_EQ(*area_region.areas[0].unknown_anam, 2048.0f);
  EXPECT_TRUE(area_region.entries.empty());

  const auto* entry_ref = env.winner(fo4::FormKey::parse("000F21:Anam.esp"));
  ASSERT_NE(entry_ref, nullptr);
  const auto entry_region = fo4::env::decode_region(env, *entry_ref);
  ASSERT_EQ(entry_region.areas.size(), 1u);
  EXPECT_FALSE(entry_region.areas[0].unknown_anam.has_value());
  ASSERT_EQ(entry_region.entries.size(), 1u);
  ASSERT_TRUE(entry_region.entries[0].occlusion_accuracy_dist.has_value());
  EXPECT_EQ(*entry_region.entries[0].occlusion_accuracy_dist, 4096.0f);
  ASSERT_TRUE(entry_region.entries[0].lod_display_distance_multiplier.has_value());
  EXPECT_EQ(*entry_region.entries[0].lod_display_distance_multiplier, 2.5f);

  const auto* reopened_ref = env.winner(fo4::FormKey::parse("000F25:Anam.esp"));
  ASSERT_NE(reopened_ref, nullptr);
  const auto reopened_region = fo4::env::decode_region(env, *reopened_ref);
  ASSERT_EQ(reopened_region.areas.size(), 2u);
  ASSERT_TRUE(reopened_region.areas[1].unknown_anam.has_value());
  EXPECT_EQ(*reopened_region.areas[1].unknown_anam, 2048.0f);
  ASSERT_EQ(reopened_region.entries.size(), 1u);
  EXPECT_FALSE(reopened_region.entries[0].occlusion_accuracy_dist.has_value());

  for (const auto* local : {"000F22", "000F23", "000F24", "000F26", "000F27"}) {
    const auto* ref = env.winner(fo4::FormKey::parse(std::string(local) + ":Anam.esp"));
    ASSERT_NE(ref, nullptr) << local;
    EXPECT_THROW(fo4::env::decode_region(env, *ref), fo4::env::WorldDecodeError) << local;
  }
}

}  // namespace
