#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

void write_hedr(fo4::test::ByteWriter& out, std::uint32_t count, bool trailing_bytes) {
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(count);
  hedr.u32(0x800);
  if (trailing_bytes) hedr.u32(0xDEADBEEFu);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
}

void write_data(fo4::test::ByteWriter& out) {
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
}

template <typename WriteHeader>
std::vector<std::byte> make_plugin(WriteHeader&& write_header) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  write_header(out);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Local\0", 6});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(Tes4Masters, UnpairedMasterIsKeptWhenAnotherMasterFollows) {
  const auto bytes = make_plugin([](fo4::test::ByteWriter& out) {
    write_hedr(out, 2, false);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Alpha.esm\0", 10});
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Beta.esm\0", 9});
    write_data(out);
  });
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  ASSERT_EQ(plugin.header.masters.size(), 2u);
  EXPECT_EQ(plugin.header.masters[0].key, fo4::PluginKey::from_filename("Alpha.esm"));
  EXPECT_EQ(plugin.header.masters[1].key, fo4::PluginKey::from_filename("Beta.esm"));
}

TEST(Tes4Masters, TrailingUnpairedMasterDecodesWindows1252) {
  const auto bytes = make_plugin([](fo4::test::ByteWriter& out) {
    write_hedr(out, 1, false);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Caf\xE9.esm\0", 9});
  });
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  ASSERT_EQ(plugin.header.masters.size(), 1u);
  EXPECT_EQ(std::string(plugin.header.masters.back().key.filename()), std::string("Caf\xC3\xA9.esm"));
}

TEST(HedrRebuild, LongerHedrKeepsTrailingBytesAcrossRebuild) {
  const auto bytes = make_plugin([](fo4::test::ByteWriter& out) {
    write_hedr(out, 999, true);
  });
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  fo4::repair_hedr_record_count(plugin);

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "Patch.esp");
  const auto hedr = std::find_if(reread.tes4.subrecords.begin(), reread.tes4.subrecords.end(),
                                 [](const auto& sub) { return sub.signature == fo4::sig_hedr; });
  ASSERT_NE(hedr, reread.tes4.subrecords.end());
  ASSERT_EQ(hedr->data.size(), 16u);
  EXPECT_EQ(reread.header.record_count, 2u);
  const std::vector<std::byte> trailing(hedr->data.begin() + 12, hedr->data.end());
  EXPECT_EQ(trailing, (std::vector<std::byte>{std::byte{0xEF}, std::byte{0xBE}, std::byte{0xAD},
                                              std::byte{0xDE}}));
}

TEST(HedrRebuild, NextFormIdUpdateKeepsTrailingBytes) {
  const auto bytes = make_plugin([](fo4::test::ByteWriter& out) {
    write_hedr(out, 2, true);
  });
  auto plugin = fo4::read_plugin(bytes, "Patch.esp");
  const auto key = fo4::create_local_record(plugin, fo4::Signature::from_string("EYES"));
  EXPECT_EQ(key, fo4::FormKey::parse("000801:Patch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);

  const auto hedr = std::find_if(plugin.tes4.subrecords.begin(), plugin.tes4.subrecords.end(),
                                 [](const auto& sub) { return sub.signature == fo4::sig_hedr; });
  ASSERT_NE(hedr, plugin.tes4.subrecords.end());
  ASSERT_EQ(hedr->data.size(), 16u);
  const std::vector<std::byte> next_and_trailing(hedr->data.begin() + 8, hedr->data.end());
  EXPECT_EQ(next_and_trailing,
            (std::vector<std::byte>{std::byte{0x02}, std::byte{0x08}, std::byte{0x00},
                                    std::byte{0x00}, std::byte{0xEF}, std::byte{0xBE},
                                    std::byte{0xAD}, std::byte{0xDE}}));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(reread.header.next_form_id, 0x802u);
  EXPECT_NE(fo4::find_record(reread, key), nullptr);
  const auto reread_hedr = std::find_if(reread.tes4.subrecords.begin(), reread.tes4.subrecords.end(),
                                        [](const auto& sub) { return sub.signature == fo4::sig_hedr; });
  ASSERT_NE(reread_hedr, reread.tes4.subrecords.end());
  ASSERT_EQ(reread_hedr->data.size(), 16u);
  const std::vector<std::byte> reread_trailing(reread_hedr->data.begin() + 12, reread_hedr->data.end());
  EXPECT_EQ(reread_trailing, (std::vector<std::byte>{std::byte{0xEF}, std::byte{0xBE}, std::byte{0xAD},
                                                     std::byte{0xDE}}));
}

TEST(Tes4Masters, ShortHedrIsRefusedByNextFormIdUpdate) {
  const auto bytes = make_plugin([](fo4::test::ByteWriter& out) {
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  });
  auto plugin = fo4::read_plugin(bytes, "ShortHedr.esp");
  try {
    fo4::set_hedr_next_form_id(plugin, 0x900);
    ADD_FAILURE() << "set_hedr_next_form_id accepted a TES4 HEDR shorter than 12 bytes";
  } catch (const fo4::FormIdError& error) {
    EXPECT_STREQ(error.what(), "TES4 HEDR must be at least 12 bytes to update next form ID");
  }
}
