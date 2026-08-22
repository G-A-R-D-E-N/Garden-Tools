#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

TEST(Xxxx, ReadsExtendedSizeSubrecordWithoutDiscardingPayload) {
  std::vector<std::byte> payload(70000, std::byte{0xAB});
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "STAT", 0);
  const auto rec = fo4::test::begin_record(out, "STAT", 0, 0x01000800);
  fo4::test::write_xxxx_subrecord(out, "DATA", payload);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_xxxx);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "DATA");
  EXPECT_TRUE(record->subrecords[1].extended_size);
  EXPECT_EQ(record->subrecords[1].data, payload);
}
