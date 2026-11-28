#include "fo4/plugin/load_order.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace {

std::uint32_t sub_u32(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature && sub.data.size() == 4) {
      std::uint32_t value = 0;
      std::memcpy(&value, sub.data.data(), 4);
      return value;
    }
  }
  return 0;
}

void add_u32_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                    std::string_view subrecord, std::uint32_t value) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  fo4::test::ByteWriter payload;
  payload.u32(value);
  fo4::test::write_subrecord(out, subrecord, payload.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

}  // namespace

TEST(RemapScalarEnumSlots, EnumAndFlagValuesSurviveMasterReorder) {
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
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  add_u32_record(out, "CLFM", 0x00012350, "FNAM", 1);
  add_u32_record(out, "NPC_", 0x00012351, "NAM8", 1);
  add_u32_record(out, "MESG", 0x00012352, "DNAM", 1);
  add_u32_record(out, "DLBR", 0x00012353, "DNAM", 2);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* clfm = fo4::find_record(reread, fo4::FormKey::parse("012350:Fallout4.esm"));
  const auto* npc = fo4::find_record(reread, fo4::FormKey::parse("012351:Fallout4.esm"));
  const auto* mesg = fo4::find_record(reread, fo4::FormKey::parse("012352:Fallout4.esm"));
  const auto* dlbr = fo4::find_record(reread, fo4::FormKey::parse("012353:Fallout4.esm"));
  ASSERT_NE(clfm, nullptr);
  ASSERT_NE(npc, nullptr);
  ASSERT_NE(mesg, nullptr);
  ASSERT_NE(dlbr, nullptr);
  EXPECT_EQ(sub_u32(*clfm, "FNAM"), 1u);
  EXPECT_EQ(sub_u32(*npc, "NAM8"), 1u);
  EXPECT_EQ(sub_u32(*mesg, "DNAM"), 1u);
  EXPECT_EQ(sub_u32(*dlbr, "DNAM"), 2u);
}
