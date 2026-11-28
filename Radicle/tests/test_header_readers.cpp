#include "fo4/c_api_lod.h"
#include "fo4/c_api_nif.h"
#include "fo4/nif/header.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

void put_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
}

void put_sized(std::vector<std::uint8_t>& bytes, const std::string& text, int prefix) {
  if (prefix == 1) {
    bytes.push_back(static_cast<std::uint8_t>(text.size()));
  } else {
    put_u32(bytes, static_cast<std::uint32_t>(text.size()));
  }
  bytes.insert(bytes.end(), text.begin(), text.end());
}

std::vector<std::uint8_t> fo4_header(const std::vector<std::string>& types) {
  const std::string banner = "Gamebryo File Format, Version 20.2.0.7\n";
  std::vector<std::uint8_t> bytes(banner.begin(), banner.end());
  put_u32(bytes, 0x14020007U);
  bytes.push_back(1);
  put_u32(bytes, 12);
  put_u32(bytes, static_cast<std::uint32_t>(types.size()));
  put_u32(bytes, 130);
  for (const char* text : {"a", "b", "c", "d"}) put_sized(bytes, text, 1);
  bytes.push_back(static_cast<std::uint8_t>(types.size() & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((types.size() >> 8) & 0xFFU));
  for (const auto& type : types) put_sized(bytes, type, 4);
  return bytes;
}

std::span<const std::byte> as_bytes(const std::vector<std::uint8_t>& bytes, std::size_t size) {
  return {reinterpret_cast<const std::byte*>(bytes.data()), size};
}

}  // namespace

TEST(NifHeader, ListsTypesAndVersions) {
  const auto bytes = fo4_header({"NiNode", "BSSkin::Instance"});
  const auto info = fo4::nif::read_nif_header(as_bytes(bytes, bytes.size()));
  ASSERT_TRUE(info.readable);
  EXPECT_EQ(info.file_version, 0x14020007U);
  EXPECT_EQ(info.user_version, 12U);
  EXPECT_EQ(info.block_count, 2U);
  ASSERT_EQ(info.block_types.size(), 2U);
  EXPECT_EQ(info.block_types[1], "BSSkin::Instance");
}

TEST(NifHeader, TruncatedTableKeepsEarlierNames) {
  const auto bytes = fo4_header({"NiNode", "NiSkinInstance"});
  const auto info = fo4::nif::read_nif_header(as_bytes(bytes, bytes.size() - 3));
  EXPECT_FALSE(info.readable);
  ASSERT_EQ(info.block_types.size(), 1U);
  EXPECT_EQ(info.block_types[0], "NiNode");
}

TEST(NifHeader, RejectsNonNifAndOldVersions) {
  const std::string text = "not a nif\n";
  const std::vector<std::uint8_t> junk(text.begin(), text.end());
  EXPECT_FALSE(fo4::nif::read_nif_header(as_bytes(junk, junk.size())).readable);
  const std::string old = "Gamebryo File Format, Version 3.0.0.0\n";
  const std::vector<std::uint8_t> old_bytes(old.begin(), old.end());
  EXPECT_FALSE(fo4::nif::read_nif_header(as_bytes(old_bytes, old_bytes.size())).readable);
}

TEST(NifHeader, CApiReturnsNewlineSeparatedNames) {
  const auto bytes = fo4_header({"NiNode", "NiSkinInstance"});
  std::uint32_t file = 0, user = 0, count = 0;
  std::int32_t readable = 0;
  std::uint8_t* names = nullptr;
  std::size_t names_size = 0;
  ASSERT_EQ(radicle_nif_header_read(bytes.data(), bytes.size(), &file, &user, &count, &readable, &names, &names_size),
            RADICLE_STATUS_OK);
  EXPECT_EQ(readable, 1);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(names), names_size), std::string("NiNode\0NiSkinInstance", 21));
  radicle_free_bytes(names);
  EXPECT_EQ(radicle_nif_header_read(nullptr, 0, &file, &user, &count, &readable, &names, &names_size),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(LodSettings, CApiDecodesSixteenBytes) {
  std::vector<std::uint8_t> bytes(16, 0);
  bytes[0] = 0xC0;
  bytes[1] = 0xFF;
  bytes[4] = 4;
  bytes[8] = 4;
  bytes[12] = 32;
  std::int32_t x = 0, y = 0, stride = 0, min = 0, max = 0;
  ASSERT_EQ(radicle_lod_settings_read(bytes.data(), bytes.size(), &x, &y, &stride, &min, &max), RADICLE_STATUS_OK);
  EXPECT_EQ(x, -64);
  EXPECT_EQ(y, 0);
  EXPECT_EQ(stride, 4);
  EXPECT_EQ(min, 4);
  EXPECT_EQ(max, 32);
  EXPECT_EQ(radicle_lod_settings_read(bytes.data(), 15, &x, &y, &stride, &min, &max), RADICLE_STATUS_PARSE_ERROR);
}

namespace {

using Skin = fo4::nif::NifSkinScan;

struct SyntheticOptions {
  std::uint32_t file{0x14020007U};
  std::uint32_t user{12};
  std::uint32_t stream{130};
  std::uint32_t blocks{4};
  std::string line{"Gamebryo File Format, Version 20.2.0.7"};
  std::uint8_t endian{1};
  bool bethesda{true};
  std::vector<std::uint8_t> embed{};
};

std::vector<std::uint8_t> synthetic(const std::vector<std::string>& types, const SyntheticOptions& o = {}) {
  std::vector<std::uint8_t> bytes(o.line.begin(), o.line.end());
  bytes.push_back('\n');
  put_u32(bytes, o.file);
  if (o.file >= 0x14000003U) bytes.push_back(o.endian);
  if (o.file >= 0x0A000108U) put_u32(bytes, o.user);
  put_u32(bytes, o.blocks);
  if (o.bethesda) {
    put_u32(bytes, o.stream);
    put_sized(bytes, "Creator", 1);
    if (o.stream > 130) put_u32(bytes, 0);
    put_sized(bytes, "Export one", 1);
    put_sized(bytes, "Export two", 1);
    if (o.stream == 130) put_sized(bytes, "Export three", 1);
  } else if (o.file >= 0x1E000002U) {
    put_u32(bytes, static_cast<std::uint32_t>(o.embed.size()));
    bytes.insert(bytes.end(), o.embed.begin(), o.embed.end());
  }
  bytes.push_back(static_cast<std::uint8_t>(types.size() & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((types.size() >> 8) & 0xFFU));
  for (const auto& type : types) put_sized(bytes, type, 4);
  return bytes;
}

std::vector<std::uint8_t> with_tail(std::vector<std::uint8_t> bytes) {
  bytes.insert(bytes.end(), 64, 0xAB);
  return bytes;
}

Skin scan(const std::vector<std::uint8_t>& bytes) { return fo4::nif::scan_nif_skin(as_bytes(bytes, bytes.size())); }

const std::vector<std::string> k_plain{"BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"};
const std::vector<std::string> k_with_skin{"BSFadeNode", "BSTriShape", "BSSkin::Instance", "BSSkin::BoneData"};

}  // namespace

TEST(NifSkinScan, SkinInstanceTypeIsSkinAndPlainIsNoSkin) {
  EXPECT_EQ(scan(synthetic(k_with_skin)), Skin::skin);
  EXPECT_EQ(scan(with_tail(synthetic(k_plain))), Skin::no_skin);
}

TEST(NifSkinScan, BoneDataAloneIsNoSkin) {
  EXPECT_EQ(scan(synthetic({"BSFadeNode", "BSSkin::BoneData", "NiSkinData", "NiSkinPartition"})), Skin::no_skin);
}

TEST(NifSkinScan, EverySkinInstanceTypeIsFoundWhereverItSits) {
  for (const std::string type : {"BSSkin::Instance", "NiSkinInstance", "BSDismemberSkinInstance"}) {
    EXPECT_EQ(scan(with_tail(synthetic({"NiNode", type, "NiTriShape"}))), Skin::skin) << type;
    EXPECT_EQ(scan(with_tail(synthetic({"NiNode", "NiTriShape", type}))), Skin::skin) << type;
    EXPECT_EQ(scan(with_tail(synthetic({type}))), Skin::skin) << type;
  }
}

TEST(NifSkinScan, NameIsCutAtItsFirstNul) {
  EXPECT_EQ(scan(with_tail(synthetic({"NiNode", std::string("BSSkin::Instance\0junk", 21)}))), Skin::skin);
  EXPECT_EQ(scan(with_tail(synthetic({"NiNode", std::string("\0BSSkin::Instance", 17)}))), Skin::no_skin);
}

TEST(NifSkinScan, NamesMatchWholeAndCaseSensitively) {
  EXPECT_EQ(scan(synthetic({"BSSkin::Instance2", "bsskin::instance", "NiSkinInstanceX", "XNiSkinInstance"})),
            Skin::no_skin);
}

TEST(NifSkinScan, OtherStreamVersionsKeepTheFieldsInStep) {
  for (const std::uint32_t stream : {100U, 155U, 83U}) {
    SyntheticOptions o;
    o.stream = stream;
    EXPECT_EQ(scan(synthetic(k_with_skin, o)), Skin::skin) << stream;
    EXPECT_EQ(scan(synthetic(k_plain, o)), Skin::no_skin) << stream;
  }
}

TEST(NifSkinScan, OblivionRangeHeaderReadsTheSameFields) {
  SyntheticOptions o;
  o.file = 0x14000005U;
  o.user = 11;
  o.stream = 11;
  o.line = "Gamebryo File Format, Version 20.0.0.5";
  EXPECT_EQ(scan(synthetic(k_with_skin, o)), Skin::skin);
  EXPECT_EQ(scan(synthetic(k_plain, o)), Skin::no_skin);
}

TEST(NifSkinScan, NonBethesda20205HasNoStreamVersionOrExportStrings) {
  SyntheticOptions o;
  o.file = 0x14020005U;
  o.user = 0;
  o.bethesda = false;
  o.line = "Gamebryo File Format, Version 20.2.0.5";
  EXPECT_EQ(scan(synthetic(k_with_skin, o)), Skin::skin);
  EXPECT_EQ(scan(synthetic(k_plain, o)), Skin::no_skin);
}

TEST(NifSkinScan, Version30002SkipsItsEmbeddedData) {
  SyntheticOptions o;
  o.file = 0x1E000002U;
  o.user = 0;
  o.bethesda = false;
  o.line = "Gamebryo File Format, Version 30.0.0.2";
  o.embed = {1, 2, 3, 4, 5};
  EXPECT_EQ(scan(synthetic(k_with_skin, o)), Skin::skin);
  EXPECT_EQ(scan(synthetic(k_plain, o)), Skin::no_skin);
}

TEST(NifSkinScan, HeaderOlderThan5001HasNoTypeListAndIsUnknown) {
  const std::string line = "NetImmerse File Format, Version 4.0.0.2\n";
  std::vector<std::uint8_t> bytes(line.begin(), line.end());
  put_u32(bytes, 0x04000002U);
  put_u32(bytes, 3);
  bytes.insert(bytes.end(), 64, 0);
  EXPECT_EQ(scan(bytes), Skin::unknown);
}

TEST(NifSkinScan, NintendoDsAndBigEndianAreUnknown) {
  const std::string line = "NDSNIF....@....@...., Version 20.2.0.7\n";
  std::vector<std::uint8_t> nds(line.begin(), line.end());
  nds.insert(nds.end(), 64, 0);
  EXPECT_EQ(scan(nds), Skin::unknown);
  SyntheticOptions o;
  o.endian = 0;
  EXPECT_EQ(scan(synthetic(k_with_skin, o)), Skin::unknown);
}

TEST(NifSkinScan, EveryTruncationWithoutSkinIsUnknown) {
  const auto full = synthetic(k_plain);
  EXPECT_EQ(scan(full), Skin::no_skin);
  for (std::size_t length = 0; length < full.size(); ++length) {
    EXPECT_EQ(fo4::nif::scan_nif_skin(as_bytes(full, length)), Skin::unknown) << length;
  }
}

TEST(NifSkinScan, TruncationAfterTheSkinTypeIsStillSkin) {
  const auto full = synthetic({"BSSkin::Instance", "BSTriShape"});
  EXPECT_EQ(fo4::nif::scan_nif_skin(as_bytes(full, full.size() - 8)), Skin::skin);
}

TEST(NifSkinScan, TextAndBinaryThatAreNotANifAreUnknown) {
  for (const std::string text : {"", "hello", "hello\nworld\n", "Gamebryo File Format"}) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    EXPECT_EQ(scan(bytes), Skin::unknown) << text;
  }
  const std::vector<std::uint8_t> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0x0D, 0x49, 0x48, 0x44, 0x52};
  EXPECT_EQ(scan(png), Skin::unknown);
  const std::vector<std::uint8_t> one_zero(1, 0);
  EXPECT_EQ(scan(one_zero), Skin::unknown);
}

TEST(NifSkinScan, VersionLineWithNoNewlineIn128BytesIsUnknown) {
  const std::string line = "Gamebryo File Format, Version 20.2.0.7" + std::string(100, ' ');
  std::vector<std::uint8_t> bytes(line.begin(), line.end());
  bytes.insert(bytes.end(), 400, 'x');
  EXPECT_EQ(scan(bytes), Skin::unknown);
}

TEST(NifSkinScan, AbsurdTypeNameLengthIsUnknown) {
  auto full = synthetic({"BSFadeNode"});
  const auto at = full.size() - std::string("BSFadeNode").size() - 4;
  for (std::size_t i = 0; i < 3; ++i) full[at + i] = 0xFF;
  full[at + 3] = 0x7F;
  EXPECT_EQ(scan(full), Skin::unknown);
}

TEST(NifSkinScan, BlocksWithAnEmptyTypeListAreUnknown) {
  SyntheticOptions five;
  five.blocks = 5;
  SyntheticOptions none;
  none.blocks = 0;
  EXPECT_EQ(scan(synthetic({}, five)), Skin::unknown);
  EXPECT_EQ(scan(synthetic({}, none)), Skin::no_skin);
}

TEST(NifSkinScan, CApiReturnsTheScanAndChecksItsArguments) {
  const auto skin = with_tail(synthetic(k_with_skin));
  const auto plain = with_tail(synthetic(k_plain));
  const std::vector<std::uint8_t> junk{'n', 'o', 'p', 'e'};
  std::int32_t scan_value = -1;
  ASSERT_EQ(radicle_nif_skin_scan(skin.data(), skin.size(), &scan_value), RADICLE_STATUS_OK);
  EXPECT_EQ(scan_value, 1);
  ASSERT_EQ(radicle_nif_skin_scan(plain.data(), plain.size(), &scan_value), RADICLE_STATUS_OK);
  EXPECT_EQ(scan_value, 0);
  ASSERT_EQ(radicle_nif_skin_scan(junk.data(), junk.size(), &scan_value), RADICLE_STATUS_OK);
  EXPECT_EQ(scan_value, 2);
  EXPECT_EQ(radicle_nif_skin_scan(nullptr, 0, &scan_value), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(scan_value, 2);
  EXPECT_EQ(radicle_nif_skin_scan(skin.data(), 0, &scan_value), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_skin_scan(skin.data(), skin.size(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
