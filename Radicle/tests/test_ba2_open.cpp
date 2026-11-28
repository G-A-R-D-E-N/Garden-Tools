#include "fo4/archive/ba2_open.hpp"
#include "fo4/archive/ba2_texture.hpp"
#include "ba2_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

using ba2_fixture::Ba2ReaderTest;
using ba2_fixture::general;
using fo4::archive::Ba2TextureArchive;
using fo4::archive::Ba2TextureEntry;

constexpr std::uint32_t k_magic = 0x58445442u;
constexpr std::uint32_t k_gnrl = 0x4C524E47u;

std::vector<std::byte> header_bytes(std::uint32_t magic, std::uint32_t tag) {
  fo4::test::ByteWriter out;
  out.u32(magic);
  out.u32(1);
  out.u32(tag);
  return out.bytes;
}

TEST_F(Ba2ReaderTest, OpenBa2ReadsGeneralArchive) {
  const auto path = write_general("open.ba2", {general("meshes/a.nif", "payload")});
  const auto archive = fo4::archive::open_ba2(path);
  ASSERT_TRUE(std::holds_alternative<fo4::archive::Ba2GeneralArchive>(archive));
  EXPECT_EQ(std::get<fo4::archive::Ba2GeneralArchive>(archive).entries.size(), 1u);
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsNonBa2Extension) {
  const auto path = write_file(data() / "open.bsa", header_bytes(k_magic, k_gnrl));
  try {
    fo4::archive::open_ba2(path);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find(".ba2 files only"), std::string::npos);
  }
}

TEST_F(Ba2ReaderTest, OpenBa2AcceptsUppercaseBa2Extension) {
  const auto path = write_general("open.BA2", {general("meshes/a.nif", "payload")});
  const auto archive = fo4::archive::open_ba2(path);
  ASSERT_TRUE(std::holds_alternative<fo4::archive::Ba2GeneralArchive>(archive));
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsMissingFile) {
  EXPECT_THROW(fo4::archive::open_ba2(data() / "missing.ba2"), std::invalid_argument);
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsTruncatedHeader) {
  auto bytes = header_bytes(k_magic, k_gnrl);
  bytes.resize(6);
  EXPECT_THROW(fo4::archive::open_ba2(write_file(data() / "short.ba2", bytes)), std::invalid_argument);
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsBadMagic) {
  const auto path = write_file(data() / "magic.ba2", header_bytes(0x12345678u, k_gnrl));
  EXPECT_THROW(fo4::archive::open_ba2(path), std::invalid_argument);
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsUnsupportedContentTag) {
  const auto path = write_file(data() / "tag.ba2", header_bytes(k_magic, 0x30303030u));
  try {
    fo4::archive::open_ba2(path);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& error) {
    const std::string message(error.what());
    EXPECT_NE(message.find("unsupported BA2 content format 0x"), std::string::npos);
    EXPECT_NE(message.find("0x30303030"), std::string::npos);
  }
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsEntriesWithoutStringTable) {
  const auto path = write_general("nameless.ba2", {general("meshes/a.nif", "payload")}, false);
  try {
    fo4::archive::open_ba2(path);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("no string table"), std::string::npos);
  }
}

TEST_F(Ba2ReaderTest, OpenBa2ReadsTextureArchive) {
  Ba2TextureArchive archive;
  Ba2TextureEntry entry;
  entry.path = "Textures\\Rock_d.dds";
  entry.texture = {4, 8, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(std::vector<std::byte>(16, std::byte{0x5A})));
  archive.entries.push_back(entry);
  const auto path = data() / "textures.ba2";
  fo4::archive::write_ba2_texture_file(archive, path);

  const auto opened = fo4::archive::open_ba2(path);
  ASSERT_TRUE(std::holds_alternative<fo4::archive::Ba2TextureArchive>(opened));
  EXPECT_EQ(std::get<fo4::archive::Ba2TextureArchive>(opened).entries.size(), 1u);
}

TEST_F(Ba2ReaderTest, OpenBa2RejectsDx10EntriesWithoutStringTable) {
  Ba2TextureArchive archive;
  archive.has_string_table = false;
  Ba2TextureEntry entry;
  entry.name_hash = 0x11111111u;
  entry.extension_hash = 0x00736464u;
  entry.directory_hash = 0x22222222u;
  entry.texture = {4, 8, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(std::vector<std::byte>(16, std::byte{0x5A})));
  archive.entries.push_back(entry);
  const auto path = data() / "nameless-textures.ba2";
  fo4::archive::write_ba2_texture_file(archive, path);
  try {
    fo4::archive::open_ba2(path);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("no string table"), std::string::npos);
  }
}

}
