#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x803);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto keywords = fo4::test::begin_group(out, "KYWD", 0);
  auto record = fo4::test::begin_record(out, "KYWD", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"OldTarget\0", 10});
  fo4::test::end_record(out, record);
  record = fo4::test::begin_record(out, "KYWD", 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NewTarget\0", 10});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, keywords);

  const auto refs = fo4::test::begin_group(out, "REFR", 0);
  record = fo4::test::begin_record(out, "REFR", 0, 0x00000802);
  fo4::test::ByteWriter name;
  name.u32(0x00000800);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, refs);
  return out.bytes;
}

}  // namespace

TEST(FormLinkRekey, DirectRekeyInvalidatesSerializationCaches) {
  auto plugin = fo4::read_plugin(make_plugin(), "Rekey.esp");
  const auto old_key = fo4::FormKey::parse("000800:Rekey.esp");
  const auto new_key = fo4::FormKey::parse("000801:Rekey.esp");
  const auto ref_key = fo4::FormKey::parse("000802:Rekey.esp");

  fo4::rekey_form_links(plugin, old_key, new_key);
  EXPECT_TRUE(fo4::plugin_original_bytes(plugin).empty());

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Rekey.esp");
  bool found = false;
  for (const auto& link : fo4::collect_form_links(reread)) {
    if (link.record == ref_key && link.subrecord.to_string() == "NAME") {
      EXPECT_EQ(link.key, new_key);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST(FormLinkRekey, SameKeyStrictScanPreservesOriginalBytes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Rekey.esp");
  const auto key = fo4::FormKey::parse("000800:Rekey.esp");
  const auto original = fo4::test::original_bytes(plugin);

  fo4::rekey_form_links(plugin, key, key);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(fo4::write_plugin(plugin), original);
}
