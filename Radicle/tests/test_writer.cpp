#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <variant>

namespace {

std::filesystem::path testdata(const char* name) {
  return std::filesystem::path{FO4_TESTDATA_DIR} / name;
}

}  // namespace

TEST(Writer, UnmodifiedFixturesAreByteIdentical) {
  for (const char* name : {"FW_HeatherSurvivalPatch.esp", "SlowTime.esp"}) {
    SCOPED_TRACE(name);
    auto plugin = fo4::read_plugin_file(testdata(name));
    const auto expected = fo4::test::original_bytes(plugin);
    plugin.original_range.clear();
    fo4::visit_records(plugin, [](auto& record) { record.original_range.clear(); });
    EXPECT_EQ(fo4::write_plugin(plugin), expected);
  }
}

TEST(Writer, RebuildsCompressedAndXxxxRecords) {
  std::vector<std::byte> payload(70000, std::byte{0xCD});
  fo4::test::ByteWriter inner;
  fo4::test::write_subrecord(inner, "EDID", std::string_view{"Zipped\0", 7});
  const auto compressed = fo4::test::zlib_compress(inner.bytes);

  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto g1 = fo4::test::begin_group(out, "MISC", 0);
  const auto zipped =
      fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000800);
  out.u32(static_cast<std::uint32_t>(inner.bytes.size()));
  out.raw(compressed);
  fo4::test::end_record(out, zipped);
  fo4::test::end_group(out, g1);

  const auto g2 = fo4::test::begin_group(out, "STAT", 0);
  const auto big = fo4::test::begin_record(out, "STAT", 0, 0x01000801);
  fo4::test::write_xxxx_subrecord(out, "DATA", payload);
  fo4::test::end_record(out, big);
  fo4::test::end_group(out, g2);

  auto plugin = fo4::read_plugin(out.bytes, "rebuild.esp");
  for (auto& group : plugin.groups) {
    for (auto& child : group.children) {
      if (auto* record = std::get_if<fo4::Record>(&child)) {
        record->original_range.clear();
      }
    }
  }
  plugin.tes4.original_range.clear();
  plugin.original_range.clear();

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "rebuild.esp");
  EXPECT_EQ(fo4::find_record(reread, fo4::RawFormId{0x01000800})->subrecords[0].signature,
            fo4::sig_edid);
  const auto* stat = fo4::find_record(reread, fo4::RawFormId{0x01000801});
  ASSERT_NE(stat, nullptr);
  ASSERT_GE(stat->subrecords.size(), 2u);
  EXPECT_EQ(stat->subrecords.back().data.size(), payload.size());
}

TEST(Writer, MasterRemovalRewritesRecordHeadersFromFormKeys) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"RadiationOverhaul.esp\0", 22});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "KYWD", 0);
  const auto local = fo4::test::begin_record(out, "KYWD", 0, 0x03000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Local\0", 6});
  fo4::test::end_record(out, local);
  const auto over = fo4::test::begin_record(out, "KYWD", 0, 0x0200009A);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"FromRad\0", 8});
  fo4::test::end_record(out, over);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x0200009A})->form_key,
            fo4::FormKey::parse("00009A:RadiationOverhaul.esp"));

  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("RadiationOverhaul.esp")});

  EXPECT_EQ(plugin.header.masters.size(), 2u);
  EXPECT_EQ(plugin.header.masters[1].key,
            fo4::PluginKey::from_filename("RadiationOverhaul.esp"));

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "Patch.esp");
  const auto* still_local =
      fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  const auto* still_rad =
      fo4::find_record(reread, fo4::FormKey::parse("00009A:RadiationOverhaul.esp"));
  ASSERT_NE(still_local, nullptr);
  ASSERT_NE(still_rad, nullptr);
  EXPECT_EQ(still_local->header.form_id, fo4::RawFormId{0x02000800});
  EXPECT_EQ(still_rad->header.form_id, fo4::RawFormId{0x0100009A});
  EXPECT_EQ(fo4::find_record(reread, fo4::RawFormId{0x0200009A}), nullptr);
}
