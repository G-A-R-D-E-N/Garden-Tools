#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

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

  const auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->compressed);
  EXPECT_EQ(record->uncompressed_size, inner.bytes.size());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
  EXPECT_GT(fo4::test::record_original_bytes(plugin, *record).size(), inner.bytes.size());
}

TEST(Compression, RejectsCorruptCompressedPayload) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000801);
  out.u32(32);
  out.raw(std::string_view{"not-zlib-data-at-all"});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  EXPECT_THROW(fo4::read_plugin(out.bytes), fo4::parse_error);
}

TEST(Compression, OversizedDeclaredSizeIsRefusedBeforeAllocating) {
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

  try {
    static_cast<void>(fo4::read_plugin(out.bytes));
    FAIL() << "expected the oversized declared size to be refused";
  } catch (const fo4::parse_error& error) {
    EXPECT_NE(std::string(error.what()).find("limit"), std::string::npos) << error.what();
  }
}
