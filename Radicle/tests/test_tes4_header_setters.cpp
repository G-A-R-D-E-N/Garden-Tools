#include "fo4/c_api.h"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_tes4(std::uint32_t flags, bool has_master) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (has_master) {
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> make_tes4_with_ignored_subrecords() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter ofst;
  ofst.u32(0x0B0A0908u);
  fo4::test::write_subrecord(out, "OFST", ofst.bytes);
  fo4::test::ByteWriter dele;
  dele.u8(0x01);
  fo4::test::write_subrecord(out, "DELE", dele.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter scrn;
  scrn.u32(0xDEADBEEFu);
  fo4::test::write_subrecord(out, "SCRN", scrn.bytes);
  fo4::test::ByteWriter tnam;
  tnam.u32(0x4Eu);
  tnam.u32(0x00000B02u);
  fo4::test::write_subrecord(out, "TNAM", tnam.bytes);
  fo4::test::ByteWriter intv;
  intv.u32(7);
  fo4::test::write_subrecord(out, "INTV", intv.bytes);
  fo4::test::ByteWriter incc;
  incc.u32(0xDEADBEEFu);
  fo4::test::write_subrecord(out, "INCC", incc.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> subrecord_bytes(const fo4::Plugin& plugin, std::string_view signature) {
  for (const auto& sub : plugin.tes4.subrecords) {
    if (sub.signature.to_string() == signature) {
      return sub.data;
    }
  }
  return {};
}

std::vector<std::string> subrecord_names(const fo4::Plugin& plugin) {
  std::vector<std::string> names;
  for (const auto& sub : plugin.tes4.subrecords) {
    names.push_back(sub.signature.to_string());
  }
  return names;
}

fo4::Plugin round_trip(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Header.esp");
}

struct TempDir {
  TempDir() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("radicle_tes4_header_" + std::to_string(id));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

std::filesystem::path write_source(const TempDir& dir, const std::string& name, std::uint32_t flags,
                                   bool has_master) {
  const auto path = dir.path / name;
  const auto bytes = make_tes4(flags, has_master);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

}  // namespace

TEST(Tes4Header, AuthorSetAndReplaceRoundTrip) {
  auto plugin = fo4::read_plugin(make_tes4(0, false), "Header.esp");
  plugin = fo4::set_plugin_author(std::move(plugin), "Ricky");
  auto reloaded = round_trip(plugin);
  EXPECT_EQ(reloaded.header.author, "Ricky");
  EXPECT_EQ(subrecord_names(reloaded), (std::vector<std::string>{"HEDR", "CNAM"}));

  plugin = fo4::set_plugin_author(std::move(reloaded), "Nomad");
  reloaded = round_trip(plugin);
  EXPECT_EQ(reloaded.header.author, "Nomad");
  EXPECT_EQ(subrecord_names(reloaded), (std::vector<std::string>{"HEDR", "CNAM"}));
}

TEST(Tes4Header, DescriptionSetReplaceAndClear) {
  auto plugin = fo4::read_plugin(make_tes4(0, false), "Header.esp");
  plugin = fo4::set_plugin_description(std::move(plugin), "First");
  EXPECT_EQ(round_trip(plugin).header.description, "First");

  plugin = fo4::set_plugin_description(std::move(plugin), "Second");
  auto reloaded = round_trip(plugin);
  EXPECT_EQ(reloaded.header.description, "Second");

  plugin = fo4::set_plugin_description(std::move(reloaded), "");
  reloaded = round_trip(plugin);
  EXPECT_TRUE(reloaded.header.description.empty());
  EXPECT_EQ(subrecord_names(reloaded), (std::vector<std::string>{"HEDR"}));
}

TEST(Tes4Header, InsertsCnamBeforeSnamBeforeMast) {
  auto description_first = fo4::read_plugin(make_tes4(0, true), "Header.esp");
  description_first = fo4::set_plugin_description(std::move(description_first), "Desc");
  description_first = fo4::set_plugin_author(std::move(description_first), "Auth");
  const auto reloaded = round_trip(description_first);
  EXPECT_EQ(subrecord_names(reloaded),
            (std::vector<std::string>{"HEDR", "CNAM", "SNAM", "MAST", "DATA"}));
  ASSERT_EQ(reloaded.header.masters.size(), 1u);
  EXPECT_EQ(reloaded.header.author, "Auth");
  EXPECT_EQ(reloaded.header.description, "Desc");

  auto author_first = fo4::read_plugin(make_tes4(0, true), "Header.esp");
  author_first = fo4::set_plugin_author(std::move(author_first), "Auth");
  author_first = fo4::set_plugin_description(std::move(author_first), "Desc");
  EXPECT_EQ(subrecord_names(round_trip(author_first)),
            (std::vector<std::string>{"HEDR", "CNAM", "SNAM", "MAST", "DATA"}));
}

TEST(Tes4Header, RejectsEmbeddedNulAndUnrepresentableText) {
  auto plugin = fo4::read_plugin(make_tes4(0, false), "Header.esp");
  const std::string_view with_nul{"Ab\0Cd", 5};
  const std::string_view unrepresentable{"\xE7\x8C\xAB"};
  EXPECT_THROW(static_cast<void>(fo4::set_plugin_author(plugin, with_nul)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::set_plugin_description(plugin, unrepresentable)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(fo4::set_plugin_author(plugin, unrepresentable)),
               std::invalid_argument);
}

TEST(Tes4Header, AuthorAndDescriptionAreZstringsOnLocalizedPlugin) {
  auto plugin = fo4::read_plugin(make_tes4(0x80, false), "Header.esp");
  plugin = fo4::set_plugin_author(std::move(plugin), "Ricky");
  plugin = fo4::set_plugin_description(std::move(plugin), "Desc");
  const auto reloaded = round_trip(plugin);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_localized());
  EXPECT_EQ(reloaded.header.author, "Ricky");
  EXPECT_EQ(reloaded.header.description, "Desc");
  EXPECT_EQ(subrecord_names(reloaded), (std::vector<std::string>{"HEDR", "CNAM", "SNAM"}));
}

TEST(Tes4Header, MasterFlagSetAndClearKeepsSmallFile) {
  auto plugin = fo4::read_plugin(make_tes4(0x200, false), "Flagged.esp");
  plugin = fo4::set_plugin_master_flag(std::move(plugin), true);
  EXPECT_TRUE(plugin.header.tes4_flags.is_master());
  EXPECT_TRUE(plugin.tes4.header.flags.is_master());

  auto reloaded = round_trip(plugin);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_master());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());

  plugin = fo4::set_plugin_master_flag(std::move(reloaded), false);
  reloaded = round_trip(plugin);
  EXPECT_FALSE(reloaded.header.tes4_flags.is_master());
  EXPECT_FALSE(reloaded.tes4.header.flags.is_master());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());
}

TEST(Tes4Header, LocalizedFlagSetClearAndIndependentOfMaster) {
  auto plugin = fo4::read_plugin(make_tes4(0x200, false), "Flagged.esp");
  plugin = fo4::set_plugin_localized_flag(std::move(plugin), true);
  EXPECT_TRUE(plugin.header.tes4_flags.is_localized());
  EXPECT_TRUE(plugin.tes4.header.flags.is_localized());
  EXPECT_TRUE(plugin.header.tes4_flags.is_small_file());

  plugin = fo4::set_plugin_localized_flag(std::move(plugin), true);
  EXPECT_TRUE(plugin.header.tes4_flags.is_localized());

  plugin = fo4::set_plugin_master_flag(std::move(plugin), true);
  EXPECT_TRUE(plugin.header.tes4_flags.is_master());
  EXPECT_TRUE(plugin.header.tes4_flags.is_localized());

  auto reloaded = round_trip(plugin);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_localized());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_master());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());

  reloaded = fo4::set_plugin_localized_flag(std::move(reloaded), false);
  EXPECT_FALSE(reloaded.header.tes4_flags.is_localized());
  EXPECT_FALSE(reloaded.tes4.header.flags.is_localized());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_master());

  const auto cleared = round_trip(reloaded);
  EXPECT_FALSE(cleared.header.tes4_flags.is_localized());
  EXPECT_FALSE(cleared.tes4.header.flags.is_localized());
  EXPECT_TRUE(cleared.header.tes4_flags.is_master());
}

TEST(Tes4Header, KeepsTnamScrnOfstAndDeleAcrossTextSetters) {
  auto plugin = fo4::read_plugin(make_tes4_with_ignored_subrecords(), "Header.esp");
  const auto ofst = subrecord_bytes(plugin, "OFST");
  const auto dele = subrecord_bytes(plugin, "DELE");
  const auto scrn = subrecord_bytes(plugin, "SCRN");
  const auto tnam = subrecord_bytes(plugin, "TNAM");
  EXPECT_EQ(ofst.size(), 4u);
  EXPECT_EQ(dele.size(), 1u);
  EXPECT_EQ(scrn.size(), 4u);
  EXPECT_EQ(tnam.size(), 8u);
  EXPECT_EQ(plugin.header.intv.value_or(0), 7u);
  EXPECT_EQ(plugin.header.incc.value_or(0), 0xDEADBEEFu);

  plugin = fo4::set_plugin_author(std::move(plugin), "Ricky");
  plugin = fo4::set_plugin_description(std::move(plugin), "Desc");
  const auto reloaded = round_trip(plugin);

  EXPECT_EQ(subrecord_names(reloaded),
            (std::vector<std::string>{"HEDR", "OFST", "DELE", "CNAM", "SNAM", "MAST", "DATA",
                                      "SCRN", "TNAM", "INTV", "INCC"}));
  EXPECT_EQ(subrecord_bytes(reloaded, "OFST"), ofst);
  EXPECT_EQ(subrecord_bytes(reloaded, "DELE"), dele);
  EXPECT_EQ(subrecord_bytes(reloaded, "SCRN"), scrn);
  EXPECT_EQ(subrecord_bytes(reloaded, "TNAM"), tnam);
  EXPECT_EQ(reloaded.header.author, "Ricky");
  EXPECT_EQ(reloaded.header.description, "Desc");
  EXPECT_EQ(reloaded.header.intv.value_or(0), 7u);
  ASSERT_EQ(reloaded.header.masters.size(), 1u);
  EXPECT_EQ(reloaded.header.masters.front().key.filename(), "Base.esm");
}

TEST(Tes4Header, ClearedMasterOnEslIsRestoredByPathSave) {
  auto plugin = fo4::read_plugin(make_tes4(0x201, false), "Flagged.esl");
  plugin = fo4::set_plugin_master_flag(std::move(plugin), false);
  const auto path_saved = fo4::read_plugin(fo4::write_plugin(plugin, "Flagged.esl"), "Flagged.esl");
  EXPECT_TRUE(path_saved.header.tes4_flags.is_master());
  const auto unsaved = fo4::read_plugin(fo4::write_plugin(plugin), "Flagged.esl");
  EXPECT_FALSE(unsaved.header.tes4_flags.is_master());
}

TEST(Tes4HeaderCApi, SetsAuthorAndDescriptionThroughTheHandle) {
  TempDir dir;
  const auto source = write_source(dir, "Header.esp", 0, false).string();
  const auto out = (dir.path / "Out.esp").string();

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(source.c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);
  ASSERT_EQ(radicle_plugin_set_author(plugin, "Ricky"), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_set_description(plugin, "Desc"), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, out.c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);

  const auto reloaded = fo4::read_plugin_file(out);
  EXPECT_EQ(reloaded.header.author, "Ricky");
  EXPECT_EQ(reloaded.header.description, "Desc");
}

TEST(Tes4HeaderCApi, SetsMasterFlagThroughTheHandle) {
  TempDir dir;
  const auto source = write_source(dir, "Header.esp", 0x200, false).string();
  const auto out = (dir.path / "Out.esp").string();

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(source.c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_set_master_flag(plugin, 1), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, out.c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);

  const auto reloaded = fo4::read_plugin_file(out);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_master());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());
}

TEST(Tes4HeaderCApi, SetsLocalizedFlagThroughTheHandle) {
  TempDir dir;
  const auto source = write_source(dir, "Header.esp", 0x200, false).string();
  const auto out = (dir.path / "Out.esp").string();

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(source.c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_set_localized_flag(plugin, 1), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, out.c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);

  const auto reloaded = fo4::read_plugin_file(out);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_localized());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());
}

TEST(Tes4HeaderCApi, RejectsBadArgumentsAndLeavesPluginUnchanged) {
  TempDir dir;
  const auto source = write_source(dir, "Header.esp", 0, false).string();
  const auto out = (dir.path / "Out.esp").string();

  EXPECT_EQ(radicle_plugin_set_author(nullptr, "Ricky"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_description(nullptr, "Desc"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_master_flag(nullptr, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_localized_flag(nullptr, 1), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(source.c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_set_author(plugin, "Ricky"), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_set_author(plugin, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_author(plugin, "\xE7\x8C\xAB"), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, out.c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);

  EXPECT_EQ(fo4::read_plugin_file(out).header.author, "Ricky");
}
