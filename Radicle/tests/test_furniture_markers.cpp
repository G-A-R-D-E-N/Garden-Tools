#include "fo4/forms/furniture_markers.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

struct Piece {
  std::string_view signature;
  Bytes data;
};

fo4::FormKey furn_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey misc_key() { return fo4::FormKey::parse("000910:Patch.esp"); }

Bytes u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Piece>& pieces, std::uint16_t form_version = 131) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "FURN", 0);
  const auto record = fo4::test::begin_record(out, "FURN", 0, 0x00000800, form_version);
  for (const auto& piece : pieces) fo4::test::write_subrecord(out, piece.signature, piece.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000910);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);
  return out.bytes;
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  std::vector<std::string> out;
  for (const auto& subrecord : fo4::find_record(plugin, furn_key())->subrecords) {
    out.push_back(subrecord.signature.to_string());
  }
  return out;
}

Bytes payload(const fo4::Plugin& plugin, std::string_view signature) {
  for (const auto& subrecord : fo4::find_record(plugin, furn_key())->subrecords) {
    if (subrecord.signature.to_string() == signature) return subrecord.data;
  }
  return {};
}

std::uint32_t word(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

constexpr std::uint32_t k_upper = 0xFFC00000u;

fo4::forms::FurnitureMarkerEdit marker(bool enabled, float x, float y, float z, float rotation, std::uint8_t types) {
  return {enabled, x, y, z, rotation, types};
}

}  // namespace

TEST(FurnitureMarkers, InsertsAMissingSnamBetweenXmrkAndAppr) {
  auto plugin = fo4::read_plugin(
      make_plugin({{"MNAM", u32_bytes(k_upper | 0x3Fu)}, {"WBDT", Bytes(1)}, {"XMRK", Bytes(1)}, {"APPR", Bytes(4)}}),
      "Patch.esp");
  const std::vector<fo4::forms::FurnitureMarkerEdit> markers{marker(true, 1.0f, 2.0f, 3.0f, -90.0f, 11),
                                                         marker(false, 0.0f, 0.0f, 0.0f, 0.0f, 255)};
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, furn_key(), markers), fo4::forms::FurnitureMarkersWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"MNAM", "WBDT", "XMRK", "SNAM", "APPR"}));
  const auto snam = payload(after, "SNAM");
  ASSERT_EQ(snam.size(), 48u);
  EXPECT_EQ(word(snam, 0), 0x3F800000u);
  EXPECT_EQ(word(snam, 4), 0x40000000u);
  EXPECT_EQ(word(snam, 8), 0x40400000u);
  EXPECT_EQ(word(snam, 12), 0xC2B40000u);
  EXPECT_EQ(word(snam, 16), 0u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(snam[20]), 11);
  EXPECT_EQ(std::to_integer<std::uint8_t>(snam[44]), 255);
  EXPECT_EQ(word(payload(after, "MNAM"), 0), k_upper | 0x1u);
}

TEST(FurnitureMarkers, ReplacesAnExistingSnamAndKeepsTheUpperMnamBits) {
  auto plugin = fo4::read_plugin(
      make_plugin({{"MNAM", u32_bytes(k_upper | 0x7u)}, {"SNAM", Bytes(72)}, {"APPR", Bytes(4)}}), "Patch.esp");
  const std::vector<fo4::forms::FurnitureMarkerEdit> markers{marker(true, 0, 0, 0, 0, 1)};
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, furn_key(), markers), fo4::forms::FurnitureMarkersWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"MNAM", "SNAM", "APPR"}));
  EXPECT_EQ(payload(after, "SNAM").size(), 24u);
  EXPECT_EQ(word(payload(after, "MNAM"), 0), k_upper | 0x1u);
}

TEST(FurnitureMarkers, AnEmptyListRemovesTheSnamAndClearsTheInteractionBits) {
  auto plugin = fo4::read_plugin(make_plugin({{"MNAM", u32_bytes(k_upper | 0x3u)}, {"SNAM", Bytes(48)}}), "Patch.esp");
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, furn_key(), {}), fo4::forms::FurnitureMarkersWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"MNAM"}));
  EXPECT_EQ(word(payload(after, "MNAM"), 0), k_upper);
}

TEST(FurnitureMarkers, AddsAnMnamWhenTheRecordHasNoneAndAMarkerIsEnabled) {
  auto plugin = fo4::read_plugin(make_plugin({{"WBDT", Bytes(1)}, {"XMRK", Bytes(1)}}), "Patch.esp");
  const std::vector<fo4::forms::FurnitureMarkerEdit> markers{marker(true, 0, 0, 0, 0, 255)};
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, furn_key(), markers), fo4::forms::FurnitureMarkersWrite::written);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after), (std::vector<std::string>{"MNAM", "WBDT", "XMRK", "SNAM"}));
  EXPECT_EQ(word(payload(after, "MNAM"), 0), 1u);
}

TEST(FurnitureMarkers, FormVersionBelow125WritesTwentyByteElements) {
  auto plugin = fo4::read_plugin(make_plugin({{"MNAM", u32_bytes(0)}, {"XMRK", Bytes(1)}}, 124), "Patch.esp");
  const std::vector<fo4::forms::FurnitureMarkerEdit> markers{marker(true, 4.0f, 0, 0, 0, 255),
                                                         marker(true, 0, 0, 0, 0, 255)};
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, furn_key(), markers), fo4::forms::FurnitureMarkersWrite::written);
  const auto after = reparse(plugin);
  const auto snam = payload(after, "SNAM");
  ASSERT_EQ(snam.size(), 40u);
  EXPECT_EQ(word(snam, 0), 0x40800000u);
  EXPECT_EQ(word(payload(after, "MNAM"), 0), 3u);
}

TEST(FurnitureMarkers, RefusalsChangeNothing) {
  auto plugin = fo4::read_plugin(make_plugin({{"MNAM", u32_bytes(0)}, {"SNAM", Bytes(24)}, {"ZZZZ", Bytes(1)}}),
                                 "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  const std::vector<fo4::forms::FurnitureMarkerEdit> many(23, marker(true, 0, 0, 0, 0, 255));
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, furn_key(), many),
            fo4::forms::FurnitureMarkersWrite::too_many_markers);
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, misc_key(), many),
            fo4::forms::FurnitureMarkersWrite::unsupported_record);
  EXPECT_EQ(fo4::forms::set_furniture_markers(plugin, fo4::FormKey::parse("000999:Patch.esp"), {}),
            fo4::forms::FurnitureMarkersWrite::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  auto bare = fo4::read_plugin(make_plugin({{"ZZZZ", Bytes(1)}}), "Patch.esp");
  const auto bare_before = fo4::write_plugin(bare);
  const std::vector<fo4::forms::FurnitureMarkerEdit> one{marker(true, 0, 0, 0, 0, 255)};
  EXPECT_EQ(fo4::forms::set_furniture_markers(bare, furn_key(), one),
            fo4::forms::FurnitureMarkersWrite::no_insertion_point);
  EXPECT_EQ(fo4::write_plugin(bare), bare_before);
}
