#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

namespace {

std::vector<std::byte> compressed_plugin(std::span<const std::byte> stream,
                                       std::uint32_t declared_size) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x00000801);
  out.u32(declared_size);
  out.raw(stream);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

}

TEST(Compression, RawDeflateAndGzipFramingLoadsUndecodable) {
  fo4::test::ByteWriter inner;
  fo4::test::write_subrecord(inner, "EDID", std::string_view{"Framing\0", 8});
  const auto zlib = fo4::test::zlib_compress(inner.bytes);
  ASSERT_GT(zlib.size(), 6u);
  const auto raw = std::span{zlib}.subspan(2, zlib.size() - 6);
  const auto declared = static_cast<std::uint32_t>(inner.size());
  const auto raw_plugin = fo4::read_plugin(compressed_plugin(raw, declared));
  const auto* raw_record = fo4::find_record(raw_plugin, fo4::RawFormId{0x00000801});
  ASSERT_NE(raw_record, nullptr);
  EXPECT_TRUE(raw_record->undecodable);

  fo4::test::ByteWriter gzip;
  for (const auto byte : {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255}) {
    gzip.u8(static_cast<std::uint8_t>(byte));
  }
  gzip.raw(raw);
  gzip.u32(static_cast<std::uint32_t>(crc32(
      0, reinterpret_cast<const Bytef*>(inner.bytes.data()), declared)));
  gzip.u32(declared);
  const auto gzip_plugin = fo4::read_plugin(compressed_plugin(gzip.bytes, declared));
  const auto* gzip_record = fo4::find_record(gzip_plugin, fo4::RawFormId{0x00000801});
  ASSERT_NE(gzip_record, nullptr);
  EXPECT_TRUE(gzip_record->undecodable);
  EXPECT_NO_THROW(fo4::read_plugin(compressed_plugin(zlib, declared)));
}

TEST(Compression, BadZlibChecksumLoadsUndecodable) {
  fo4::test::ByteWriter inner;
  fo4::test::write_subrecord(inner, "EDID", std::string_view{"Checksum\0", 9});
  auto stream = fo4::test::zlib_compress(inner.bytes);
  stream.back() ^= std::byte{1};
  auto plugin = fo4::read_plugin(compressed_plugin(
      stream, static_cast<std::uint32_t>(inner.size())));
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x00000801});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
}

TEST(Compression, ShortInflatedOutputLoadsAtProducedLength) {
  fo4::test::ByteWriter inner;
  fo4::test::write_subrecord(inner, "EDID", std::string_view{"Short\0", 6});
  const auto stream = fo4::test::zlib_compress(inner.bytes);
  const auto declared = static_cast<std::uint32_t>(inner.size() + 16);
  auto plugin = fo4::read_plugin(compressed_plugin(stream, declared));
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x00000801});
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->undecodable);
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
}

TEST(Compression, PayloadUnderFourBytesFailsThePluginRead) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x00000801);
  out.u8(1);
  out.u8(2);
  out.u8(3);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  EXPECT_THROW(static_cast<void>(fo4::read_plugin(out.bytes)), fo4::parse_error);
}

TEST(Compression, CompressedExtendedSubrecordPreservesPhysicalMarker) {
  fo4::test::ByteWriter inner;
  const std::vector<std::byte> payload(0x10000, std::byte{0x42});
  fo4::test::write_xxxx_subrecord(inner, "ZZZZ", payload);
  const auto bytes = compressed_plugin(fo4::test::zlib_compress(inner.bytes),
                                       static_cast<std::uint32_t>(inner.size()));
  auto plugin = fo4::read_plugin(bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x00000801});
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_xxxx);
  EXPECT_TRUE(record->subrecords[1].extended_size);
  EXPECT_EQ(record->subrecords[1].data, payload);
  plugin.original_range.clear();
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(CompressionRealData, OfficialMastersUseZlibAndPreserveBytes) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  record_game_build(detect_game_build(dir));
  for (const auto* name : {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm",
                           "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm",
                           "DLCNukaWorld.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    ASSERT_TRUE(std::filesystem::exists(path)) << name;
    auto plugin = fo4::read_plugin_file(path);
    std::size_t count = 0;
    for (const auto* record : fo4::collect_records(plugin)) {
      if (!record->compressed) continue;
      const auto bytes = fo4::record_original_bytes(plugin, *record);
      ASSERT_GT(bytes.size(), fo4::k_record_header_size + 6) << name;
      const auto cmf = std::to_integer<unsigned>(bytes[fo4::k_record_header_size + 4]);
      const auto flg = std::to_integer<unsigned>(bytes[fo4::k_record_header_size + 5]);
      EXPECT_EQ(cmf & 15u, 8u) << name;
      EXPECT_EQ((cmf * 256u + flg) % 31u, 0u) << name;
      ++count;
    }
    EXPECT_GT(count, 0u) << name;
    const auto expected = fo4::test::original_bytes(plugin);
    plugin.original_range.clear();
    EXPECT_EQ(fo4::write_plugin(plugin), expected) << name;
    std::cout << name << ": " << count << " compressed records\n";
  }
}

#include <cstdint>
#include <random>
#include <string>
#include <vector>

TEST(Compression, InflatesCompressedRecordAndKeepsOriginalBytes) {
  fo4::test::ByteWriter inner;
  fo4::test::write_subrecord(inner, "EDID", std::string_view{"Compressed\0", 11});

  const auto compressed = fo4::test::zlib_compress(inner.bytes);

  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000801);
  out.u32(static_cast<std::uint32_t>(inner.bytes.size()));
  out.raw(compressed);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->compressed);
  EXPECT_EQ(record->uncompressed_size, inner.bytes.size());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
  EXPECT_GT(fo4::test::record_original_bytes(plugin, *record).size(), inner.bytes.size());
}

TEST(Compression, CorruptCompressedPayloadLoadsUndecodable) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000801);
  out.u32(32);
  out.raw(std::string_view{"not-zlib-data-at-all"});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
  EXPECT_TRUE(record->subrecords.empty());
}

TEST(Compression, OversizedDeclaredSizeLoadsUndecodable) {
  std::mt19937 rng{0x5EEDu};
  std::vector<std::byte> compressed(1'100'000);
  for (auto& byte : compressed) {
    byte = std::byte{static_cast<std::uint8_t>(rng())};
  }

  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000801);
  out.u32(0x40000001u);
  out.raw(compressed);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
  EXPECT_TRUE(record->subrecords.empty());
}
