#include "fo4/c_api.h"
#include "fo4/c_api_save.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Spec {
  std::string player_name{"Nora"};
  std::string location{"Diamond City"};
  std::string playtime{"1d.2h.3m"};
  std::string race{"HumanRace"};
  std::uint32_t version{15};
  std::uint32_t width{2};
  std::uint32_t height{3};
  std::vector<std::string> plugins{"Fallout4.esm", "DLCRobot.esm"};
  std::vector<std::string> light_plugins{"Mod.esl"};
  bool light_block{true};
};

struct HeaderDeleter {
  void operator()(radicle_save_header* header) const { radicle_save_header_free(header); }
};

using HeaderPtr = std::unique_ptr<radicle_save_header, HeaderDeleter>;

void put_u8(std::vector<std::uint8_t>& out, std::uint32_t value) { out.push_back(static_cast<std::uint8_t>(value)); }

void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u8(out, value & 0xFFU);
  put_u8(out, (value >> 8) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u16(out, value & 0xFFFFU);
  put_u16(out, value >> 16);
}

void put_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  put_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
  put_u32(out, static_cast<std::uint32_t>(value >> 32));
}

void put_f32(std::vector<std::uint8_t>& out, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  put_u32(out, bits);
}

void put_wstring(std::vector<std::uint8_t>& out, const std::string& text) {
  put_u16(out, static_cast<std::uint32_t>(text.size()));
  for (const char ch : text) put_u8(out, static_cast<unsigned char>(ch));
}

std::uint8_t pixel_byte(std::size_t index) { return static_cast<std::uint8_t>(index * 7U + 1U); }

std::vector<std::uint8_t> build(const Spec& spec) {
  std::vector<std::uint8_t> header;
  put_u32(header, spec.version);
  put_u32(header, 7);
  put_wstring(header, spec.player_name);
  put_u32(header, 12);
  put_wstring(header, spec.location);
  put_wstring(header, spec.playtime);
  put_wstring(header, spec.race);
  put_u16(header, 1);
  put_f32(header, 12.5F);
  put_f32(header, 300.0F);
  put_u64(header, 0x01DD26F9A3BF2C30ULL);
  put_u32(header, spec.width);
  put_u32(header, spec.height);

  std::vector<std::uint8_t> plugins;
  put_u8(plugins, static_cast<std::uint32_t>(spec.plugins.size()));
  for (const auto& name : spec.plugins) put_wstring(plugins, name);
  if (spec.light_block) {
    put_u16(plugins, static_cast<std::uint32_t>(spec.light_plugins.size()));
    for (const auto& name : spec.light_plugins) put_wstring(plugins, name);
  }

  std::vector<std::uint8_t> file;
  for (const char ch : std::string("FO4_SAVEGAME")) put_u8(file, static_cast<unsigned char>(ch));
  put_u32(file, static_cast<std::uint32_t>(header.size()));
  file.insert(file.end(), header.begin(), header.end());
  for (std::size_t i = 0; i < std::size_t{spec.width} * spec.height * 4U; ++i) put_u8(file, pixel_byte(i));
  put_u8(file, 68);
  put_wstring(file, "1.10.984");
  put_u32(file, static_cast<std::uint32_t>(plugins.size()));
  file.insert(file.end(), plugins.begin(), plugins.end());
  return file;
}

HeaderPtr parse_ok(const std::vector<std::uint8_t>& bytes) {
  radicle_save_header* raw = nullptr;
  EXPECT_EQ(radicle_save_header_parse(bytes.data(), bytes.size(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  return HeaderPtr(raw);
}

std::string plugin_name(const radicle_save_header* header, radicle_plugin_kind kind, std::size_t index) {
  const char* name = nullptr;
  EXPECT_EQ(radicle_save_header_plugin(header, kind, index, &name), RADICLE_STATUS_OK) << radicle_last_error();
  return name == nullptr ? std::string() : std::string(name);
}

}  // namespace

// Detects a wrapper that drops, reorders or mis-maps a header field on the way to the info struct.
TEST(CApiSave, RoundTripsEveryField) {
  Spec spec;
  spec.player_name = "Nora\xE9";
  spec.location = "Caf\x80";
  const auto bytes = build(spec);
  const auto header = parse_ok(bytes);
  ASSERT_NE(header.get(), nullptr);
  radicle_save_header_info info{};
  ASSERT_EQ(radicle_save_header_get_info(header.get(), &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.version, 15U);
  EXPECT_EQ(info.save_number, 7U);
  EXPECT_STREQ(info.player_name, "Nora\xC3\xA9");
  EXPECT_EQ(info.level, 12U);
  EXPECT_STREQ(info.location, "Caf\xE2\x82\xAC");
  EXPECT_STREQ(info.playtime, "1d.2h.3m");
  EXPECT_STREQ(info.race, "HumanRace");
  EXPECT_EQ(info.sex, 1U);
  EXPECT_EQ(info.current_xp, 12.5F);
  EXPECT_EQ(info.required_xp, 300.0F);
  EXPECT_EQ(info.filetime, 0x01DD26F9A3BF2C30ULL);
  EXPECT_EQ(info.screenshot_width, 2U);
  EXPECT_EQ(info.screenshot_height, 3U);
  EXPECT_EQ(info.form_version, 68U);
  EXPECT_STREQ(info.game_version, "1.10.984");
  EXPECT_EQ(info.plugin_count, 2U);
  EXPECT_EQ(info.light_plugin_count, 1U);
  EXPECT_EQ(plugin_name(header.get(), RADICLE_PLUGIN_FULL, 0), "Fallout4.esm");
  EXPECT_EQ(plugin_name(header.get(), RADICLE_PLUGIN_FULL, 1), "DLCRobot.esm");
  EXPECT_EQ(plugin_name(header.get(), RADICLE_PLUGIN_LIGHT, 0), "Mod.esl");
  ASSERT_EQ(info.screenshot_size, 24U);
  ASSERT_NE(info.screenshot, nullptr);
  for (std::size_t i = 0; i < info.screenshot_size; ++i) EXPECT_EQ(info.screenshot[i], pixel_byte(i)) << i;
}

// Detects a handle whose screenshot or strings still alias the caller's buffer.
TEST(CApiSave, HandleSurvivesTheCallersBuffer) {
  auto bytes = build(Spec{});
  const auto header = parse_ok(bytes);
  ASSERT_NE(header.get(), nullptr);
  radicle_save_header_info info{};
  ASSERT_EQ(radicle_save_header_get_info(header.get(), &info), RADICLE_STATUS_OK);
  EXPECT_FALSE(info.screenshot >= bytes.data() && info.screenshot < bytes.data() + bytes.size());
  std::fill(bytes.begin(), bytes.end(), std::uint8_t{0xCC});
  ASSERT_EQ(info.screenshot_size, 24U);
  for (std::size_t i = 0; i < info.screenshot_size; ++i) EXPECT_EQ(info.screenshot[i], pixel_byte(i)) << i;
  EXPECT_STREQ(info.player_name, "Nora");
  EXPECT_EQ(plugin_name(header.get(), RADICLE_PLUGIN_FULL, 0), "Fallout4.esm");
}

// Detects a wrapper that mishandles a missing light block, empty lists and a zero-size screenshot.
TEST(CApiSave, EmptyListsAndScreenshot) {
  Spec spec;
  spec.plugins.clear();
  spec.light_block = false;
  spec.width = 0;
  const auto header = parse_ok(build(spec));
  ASSERT_NE(header.get(), nullptr);
  radicle_save_header_info info{};
  ASSERT_EQ(radicle_save_header_get_info(header.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.plugin_count, 0U);
  EXPECT_EQ(info.light_plugin_count, 0U);
  EXPECT_EQ(info.screenshot, nullptr);
  EXPECT_EQ(info.screenshot_size, 0U);
  EXPECT_EQ(info.screenshot_height, 3U);
}

// Detects a plugin getter that reads past a list, mixes the two lists or accepts a made-up kind.
TEST(CApiSave, PluginGetterChecksKindAndIndex) {
  const auto header = parse_ok(build(Spec{}));
  ASSERT_NE(header.get(), nullptr);
  const char* name = reinterpret_cast<const char*>(0x1);
  EXPECT_EQ(radicle_save_header_plugin(header.get(), RADICLE_PLUGIN_FULL, 2, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(name, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("out of range"), std::string::npos);
  EXPECT_EQ(radicle_save_header_plugin(header.get(), RADICLE_PLUGIN_LIGHT, 1, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_save_header_plugin(header.get(), static_cast<radicle_plugin_kind>(7), 0, &name),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(name, nullptr);
}

// Detects a failed parse that writes *out, or reports no reason.
TEST(CApiSave, ParseFailureLeavesOutNullWithAnError) {
  const std::vector<std::uint8_t> garbage(64, 0x41);
  auto* out = reinterpret_cast<radicle_save_header*>(0x1);
  EXPECT_EQ(radicle_save_header_parse(garbage.data(), garbage.size(), &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("bad magic"), std::string::npos) << radicle_last_error();

  out = reinterpret_cast<radicle_save_header*>(0x1);
  EXPECT_EQ(radicle_save_header_parse(nullptr, 0, &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("bad magic"), std::string::npos) << radicle_last_error();

  Spec spec;
  spec.version = 14;
  const auto old = build(spec);
  out = reinterpret_cast<radicle_save_header*>(0x1);
  EXPECT_EQ(radicle_save_header_parse(old.data(), old.size(), &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("unsupported save version 14"), std::string::npos)
      << radicle_last_error();
}

// Detects a truncated save that is accepted, crashes the wrapper or leaks a handle.
TEST(CApiSave, EveryTruncationFailsCleanly) {
  const auto bytes = build(Spec{});
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    auto* out = reinterpret_cast<radicle_save_header*>(0x1);
    ASSERT_EQ(radicle_save_header_parse(bytes.data(), length, &out), RADICLE_STATUS_PARSE_ERROR) << length;
    EXPECT_EQ(out, nullptr) << length;
    EXPECT_STRNE(radicle_last_error(), "") << length;
  }
}

// Detects a missing null check; a bad argument must be INVALID_ARGUMENT, not a crash or a parse error.
TEST(CApiSave, NullArgumentsAreInvalid) {
  const auto bytes = build(Spec{});
  auto* out = reinterpret_cast<radicle_save_header*>(0x1);
  EXPECT_EQ(radicle_save_header_parse(nullptr, 5, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(radicle_save_header_parse(bytes.data(), bytes.size(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto header = parse_ok(bytes);
  ASSERT_NE(header.get(), nullptr);
  radicle_save_header_info info{};
  info.player_name = "stale";
  const char* name = nullptr;
  EXPECT_EQ(radicle_save_header_get_info(nullptr, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(info.player_name, nullptr);
  EXPECT_EQ(radicle_save_header_get_info(header.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_save_header_plugin(nullptr, RADICLE_PLUGIN_FULL, 0, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_save_header_plugin(header.get(), RADICLE_PLUGIN_FULL, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Detects a free that dereferences its argument.
TEST(CApiSave, FreeAcceptsNull) {
  radicle_save_header_free(nullptr);
  SUCCEED();
}
