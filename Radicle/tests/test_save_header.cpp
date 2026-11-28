#include "fo4/save/save_header.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::save::SaveHeader;

struct Spec {
  std::uint32_t version{fo4::save::k_save_version};
  std::uint32_t save_number{7};
  std::string player_name{"Nora"};
  std::uint32_t level{12};
  std::string location{"Diamond City"};
  std::string playtime{"1d.2h.3m"};
  std::string race{"HumanRace"};
  std::uint16_t sex{1};
  float current_xp{12.5F};
  float required_xp{300.0F};
  std::uint64_t filetime{0x01DD26F9A3BF2C30ULL};
  std::uint32_t width{2};
  std::uint32_t height{3};
  std::uint8_t form_version{68};
  std::string game_version{"1.10.984"};
  std::vector<std::string> plugins{"Fallout4.esm", "DLCRobot.esm"};
  std::vector<std::string> light_plugins{"Mod.esl"};
  bool light_block{true};
  std::uint32_t header_padding{0};
  std::int32_t header_size_delta{0};
  std::int32_t plugin_size_delta{0};
};

constexpr std::size_t k_magic_size = 12;
constexpr std::size_t k_header_size_at = 12;
constexpr std::uint32_t k_built_pixel_limit = 64;

void put_u8(std::vector<std::byte>& out, std::uint32_t value) { out.push_back(static_cast<std::byte>(value)); }

void put_u16(std::vector<std::byte>& out, std::uint32_t value) {
  put_u8(out, value & 0xFFU);
  put_u8(out, (value >> 8) & 0xFFU);
}

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  put_u16(out, value & 0xFFFFU);
  put_u16(out, value >> 16);
}

void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
  put_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
  put_u32(out, static_cast<std::uint32_t>(value >> 32));
}

void put_f32(std::vector<std::byte>& out, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  put_u32(out, bits);
}

void put_wstring(std::vector<std::byte>& out, const std::string& text) {
  put_u16(out, static_cast<std::uint32_t>(text.size()));
  for (const char ch : text) {
    put_u8(out, static_cast<unsigned char>(ch));
  }
}

std::uint8_t pixel_byte(std::size_t index) { return static_cast<std::uint8_t>(index * 7U + 1U); }

std::vector<std::byte> build(const Spec& spec) {
  std::vector<std::byte> header;
  put_u32(header, spec.version);
  put_u32(header, spec.save_number);
  put_wstring(header, spec.player_name);
  put_u32(header, spec.level);
  put_wstring(header, spec.location);
  put_wstring(header, spec.playtime);
  put_wstring(header, spec.race);
  put_u16(header, spec.sex);
  put_f32(header, spec.current_xp);
  put_f32(header, spec.required_xp);
  put_u64(header, spec.filetime);
  put_u32(header, spec.width);
  put_u32(header, spec.height);
  for (std::uint32_t i = 0; i < spec.header_padding; ++i) {
    put_u8(header, 0xEE);
  }

  std::vector<std::byte> plugins;
  put_u8(plugins, static_cast<std::uint32_t>(spec.plugins.size()));
  for (const auto& name : spec.plugins) {
    put_wstring(plugins, name);
  }
  if (spec.light_block) {
    put_u16(plugins, static_cast<std::uint32_t>(spec.light_plugins.size()));
    for (const auto& name : spec.light_plugins) {
      put_wstring(plugins, name);
    }
  }

  std::vector<std::byte> file;
  for (const char ch : std::string("FO4_SAVEGAME")) {
    put_u8(file, static_cast<unsigned char>(ch));
  }
  put_u32(file, static_cast<std::uint32_t>(static_cast<std::int64_t>(header.size()) + spec.header_size_delta));
  file.insert(file.end(), header.begin(), header.end());
  const bool small = spec.width <= k_built_pixel_limit && spec.height <= k_built_pixel_limit;
  const std::size_t pixel_bytes = small ? std::size_t{spec.width} * spec.height * 4U : 0U;
  for (std::size_t i = 0; i < pixel_bytes; ++i) {
    put_u8(file, pixel_byte(i));
  }
  put_u8(file, spec.form_version);
  put_wstring(file, spec.game_version);
  put_u32(file, static_cast<std::uint32_t>(static_cast<std::int64_t>(plugins.size()) + spec.plugin_size_delta));
  file.insert(file.end(), plugins.begin(), plugins.end());
  return file;
}

bool parse(const std::vector<std::byte>& bytes, SaveHeader& out, std::string& error) {
  return fo4::save::parse_save_header(bytes, out, error);
}

}  // namespace

// Detects a reader that drops, reorders or mis-decodes a header field.
TEST(SaveHeader, RoundTripsEveryField) {
  Spec spec;
  spec.player_name = "Nora\xE9";
  spec.location = "Caf\x80";
  const auto bytes = build(spec);
  SaveHeader header;
  std::string error;
  ASSERT_TRUE(parse(bytes, header, error)) << error;
  EXPECT_EQ(header.version, 15U);
  EXPECT_EQ(header.save_number, 7U);
  EXPECT_EQ(header.player_name, "Nora\xC3\xA9");
  EXPECT_EQ(header.level, 12U);
  EXPECT_EQ(header.location, "Caf\xE2\x82\xAC");
  EXPECT_EQ(header.playtime, "1d.2h.3m");
  EXPECT_EQ(header.race, "HumanRace");
  EXPECT_EQ(header.sex, 1U);
  EXPECT_EQ(header.current_xp, 12.5F);
  EXPECT_EQ(header.required_xp, 300.0F);
  EXPECT_EQ(header.filetime, 0x01DD26F9A3BF2C30ULL);
  EXPECT_EQ(header.screenshot_width, 2U);
  EXPECT_EQ(header.screenshot_height, 3U);
  EXPECT_EQ(header.form_version, 68U);
  EXPECT_EQ(header.game_version, "1.10.984");
  EXPECT_EQ(header.plugins, (std::vector<std::string>{"Fallout4.esm", "DLCRobot.esm"}));
  EXPECT_EQ(header.light_plugins, (std::vector<std::string>{"Mod.esl"}));
  ASSERT_EQ(header.screenshot.size(), 24U);
  EXPECT_GE(header.screenshot.data(), bytes.data());
  EXPECT_LE(header.screenshot.data() + header.screenshot.size(), bytes.data() + bytes.size());
  for (std::size_t i = 0; i < header.screenshot.size(); ++i) {
    EXPECT_EQ(std::to_integer<std::uint8_t>(header.screenshot[i]), pixel_byte(i)) << i;
  }
}

// Detects a reader that requires the light-plugin block or mishandles its absence and an empty list.
TEST(SaveHeader, LightPluginBlockIsOptionalAndMayBeEmpty) {
  for (const bool block : {false, true}) {
    Spec spec;
    spec.light_block = block;
    spec.light_plugins.clear();
    SaveHeader header;
    std::string error;
    ASSERT_TRUE(parse(build(spec), header, error)) << error;
    EXPECT_EQ(header.plugins.size(), 2U);
    EXPECT_TRUE(header.light_plugins.empty());
  }
}

// Detects a reader that rejects or mis-slices a save with no plugins and no screenshot pixels.
TEST(SaveHeader, AcceptsZeroPluginsAndZeroSizeScreenshot) {
  const std::pair<std::uint32_t, std::uint32_t> shapes[] = {{0, 0}, {0, 5}, {5, 0}};
  for (const auto& [width, height] : shapes) {
    Spec spec;
    spec.plugins.clear();
    spec.light_plugins.clear();
    spec.width = width;
    spec.height = height;
    SaveHeader header;
    std::string error;
    ASSERT_TRUE(parse(build(spec), header, error)) << error;
    EXPECT_TRUE(header.screenshot.empty());
    EXPECT_TRUE(header.plugins.empty());
    EXPECT_EQ(header.game_version, "1.10.984");
  }
}

// Detects a reader that tries to consume the game state after the plugin list, which is not part of the header.
TEST(SaveHeader, IgnoresBytesAfterThePluginList) {
  auto bytes = build(Spec{});
  bytes.insert(bytes.end(), 64, std::byte{0xAB});
  SaveHeader header;
  std::string error;
  ASSERT_TRUE(parse(bytes, header, error)) << error;
  EXPECT_EQ(header.plugins.size(), 2U);
  EXPECT_EQ(header.light_plugins.size(), 1U);
}

// Detects a field read that is not bounds-checked: every strict prefix of a valid save must fail cleanly.
TEST(SaveHeader, EveryTruncationPointFails) {
  const auto bytes = build(Spec{});
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    SaveHeader header;
    header.level = 99;
    std::string error;
    EXPECT_FALSE(fo4::save::parse_save_header(std::span<const std::byte>(bytes.data(), length), header, error))
        << length;
    EXPECT_FALSE(error.empty()) << length;
    EXPECT_EQ(header.level, 99U) << length;
  }
}

// Detects a reader that accepts non-save input.
TEST(SaveHeader, RejectsBadMagicAndEmptyInput) {
  for (std::size_t flip = 0; flip < k_magic_size; ++flip) {
    auto bytes = build(Spec{});
    bytes[flip] = static_cast<std::byte>(std::to_integer<unsigned>(bytes[flip]) ^ 0x20U);
    SaveHeader header;
    std::string error;
    ASSERT_FALSE(parse(bytes, header, error)) << flip;
    EXPECT_NE(error.find("bad magic"), std::string::npos) << error;
  }
  SaveHeader header;
  std::string error;
  EXPECT_FALSE(parse({}, header, error));
  EXPECT_NE(error.find("bad magic"), std::string::npos) << error;
}

// Detects a width*height*4 that overflows or allocates from an untrusted dimension.
TEST(SaveHeader, RejectsOversizedScreenshotDimensions) {
  const std::pair<std::uint32_t, std::uint32_t> shapes[] = {
      {fo4::save::k_max_screenshot_dimension + 1, 1},
      {1, fo4::save::k_max_screenshot_dimension + 1},
      {0xFFFFFFFFU, 0xFFFFFFFFU},
      {0, 0xFFFFFFFFU}};
  for (const auto& [width, height] : shapes) {
    Spec spec;
    spec.width = width;
    spec.height = height;
    SaveHeader header;
    std::string error;
    ASSERT_FALSE(parse(build(spec), header, error)) << width << "x" << height;
    EXPECT_NE(error.find("dimensions"), std::string::npos) << error;
  }
}

// Detects a reader that trusts a legal-looking size when the file does not hold that many pixels.
TEST(SaveHeader, RejectsScreenshotLargerThanTheFile) {
  Spec spec;
  spec.width = fo4::save::k_max_screenshot_dimension;
  spec.height = fo4::save::k_max_screenshot_dimension;
  SaveHeader header;
  std::string error;
  ASSERT_FALSE(parse(build(spec), header, error));
  EXPECT_NE(error.find("unexpected end of data"), std::string::npos) << error;
}

// Detects a reader that guesses the layout of a version it has not verified.
TEST(SaveHeader, RejectsUnsupportedVersions) {
  for (const std::uint32_t version : {0U, 11U, 14U, 16U, 0xFFFFFFFFU}) {
    Spec spec;
    spec.version = version;
    SaveHeader header;
    std::string error;
    ASSERT_FALSE(parse(build(spec), header, error)) << version;
    EXPECT_NE(error.find("unsupported save version " + std::to_string(version)), std::string::npos) << error;
  }
}

// Detects a reader that accepts a header block whose declared size disagrees with its fields.
TEST(SaveHeader, RejectsHeaderSizeThatDisagreesWithItsFields) {
  for (const std::int32_t delta : {-1, 1}) {
    Spec spec;
    spec.header_size_delta = delta;
    SaveHeader header;
    std::string error;
    EXPECT_FALSE(parse(build(spec), header, error)) << delta;
  }
  auto bytes = build(Spec{});
  for (std::size_t i = 1; i < 4; ++i) {
    bytes[k_header_size_at + i] = std::byte{0xFF};
  }
  SaveHeader header;
  std::string error;
  EXPECT_FALSE(parse(bytes, header, error));
}

// Detects a reader that skips the header-block end check: a padding byte inside the declared block keeps the screenshot aligned, so only that check rejects it.
TEST(SaveHeader, RejectsPaddingInsideTheHeaderBlock) {
  Spec spec;
  spec.header_padding = 1;
  SaveHeader header;
  std::string error;
  ASSERT_FALSE(parse(build(spec), header, error));
  EXPECT_NE(error.find("header size"), std::string::npos) << error;
}

// Detects a reader that accepts a plugin block whose declared size disagrees with its entries.
TEST(SaveHeader, RejectsPluginInfoSizeThatDisagreesWithItsEntries) {
  for (const std::int32_t delta : {-1, 1}) {
    Spec spec;
    spec.plugin_size_delta = delta;
    auto bytes = build(spec);
    bytes.push_back(std::byte{0});
    SaveHeader header;
    std::string error;
    EXPECT_FALSE(parse(bytes, header, error)) << delta;
  }
}

// Detects a reader that lets a plugin count promise more entries than the plugin block holds.
TEST(SaveHeader, RejectsPluginCountBeyondTheBlock) {
  Spec spec;
  spec.plugins = {"A.esm"};
  spec.light_block = false;
  auto bytes = build(spec);
  bytes[bytes.size() - (1 + 2 + 5)] = std::byte{200};
  SaveHeader header;
  std::string error;
  EXPECT_FALSE(parse(bytes, header, error));
}
