#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> bytes_from_string(std::string_view text) {
  std::vector<std::byte> bytes;
  bytes.reserve(text.size());
  for (const char ch : text) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  return bytes;
}

std::vector<std::byte> make_ctda(std::uint16_t function, std::uint32_t param1) {
  fo4::test::ByteWriter ctda;
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.u32(param1);
  ctda.u32(0);
  ctda.u32(0);
  ctda.u32(0);
  ctda.u32(static_cast<std::uint32_t>(-1));
  return ctda.bytes;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DropMe.esm\0", 11});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"KeepMe.esm\0", 11});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "COBJ", 0);
  const auto record = fo4::test::begin_record(out, "COBJ", 0, 0x03000800);
  constexpr std::uint16_t get_base_value = 277;
  fo4::test::write_subrecord(out, "CTDA", make_ctda(get_base_value, 0x02000812));
  fo4::test::write_subrecord(out, "CIS1", std::string_view{"KeepMe.esm|SomeString\0", 22});
  fo4::test::write_subrecord(out, "CIS2", std::string_view{"02000812 is text\0", 17});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(FormLink, CtdaCisStringsRemainOpaqueAcrossMasterRemap) {
  auto plugin = fo4::read_plugin(make_plugin(), "ConditionStrings.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ConditionStrings.esp");
  const auto* record = fo4::find_record(
      reread, fo4::FormKey::parse("000800:ConditionStrings.esp"));
  ASSERT_NE(record, nullptr);

  const auto* ctda = find_sub(*record, "CTDA");
  const auto* cis1 = find_sub(*record, "CIS1");
  const auto* cis2 = find_sub(*record, "CIS2");
  ASSERT_NE(ctda, nullptr);
  ASSERT_NE(cis1, nullptr);
  ASSERT_NE(cis2, nullptr);

  std::uint32_t remapped = 0;
  std::memcpy(&remapped, ctda->data.data() + 12, 4);
  EXPECT_EQ(remapped, 0x01000812u);

  EXPECT_EQ(cis1->data, bytes_from_string(std::string_view{"KeepMe.esm|SomeString\0", 22}));
  EXPECT_EQ(cis2->data, bytes_from_string(std::string_view{"02000812 is text\0", 17}));
}
