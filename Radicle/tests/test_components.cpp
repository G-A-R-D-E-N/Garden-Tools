#include "fo4/forms/components.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

const fo4::Subrecord* find(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig) return &subrecord;
  }
  return nullptr;
}

std::size_t index_of(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  return record.subrecords.size();
}

// Two CMPO components (0x801, 0x802), a MISC (0x900) with DATA + an existing
// single-component CVPA/CDIX, and a COBJ (0xA00) with only EDID + ZNAM.
std::vector<std::byte> plugin_with_components() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(6);
  hedr.u32(0xB00);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto cmpo_group = fo4::test::begin_group(out, "CMPO", 0);
  for (const std::uint32_t id : {0x00000801u, 0x00000802u}) {
    const auto cmpo = fo4::test::begin_record(out, "CMPO", 0, id, 131);
    fo4::test::end_record(out, cmpo);
  }
  fo4::test::end_group(out, cmpo_group);

  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000900, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("ScrapItem"));
  fo4::test::ByteWriter data;
  data.u32(10);
  data.f32(1.0f);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter cvpa;
  cvpa.u32(0x00000801);
  cvpa.u32(3);
  fo4::test::write_subrecord(out, "CVPA", cvpa.bytes);
  fo4::test::ByteWriter cdix;
  cdix.bytes.push_back(std::byte{0});
  fo4::test::write_subrecord(out, "CDIX", cdix.bytes);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);

  const auto cobj_group = fo4::test::begin_group(out, "COBJ", 0);
  auto cobj = fo4::test::begin_record(out, "COBJ", 0, 0x00000A00, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Recipe"));
  fo4::test::ByteWriter znam;
  znam.u32(0);
  fo4::test::write_subrecord(out, "ZNAM", znam.bytes);
  fo4::test::end_record(out, cobj);

  auto sparse = fo4::test::begin_record(out, "COBJ", 0, 0x00000A01, 131);
  fo4::test::ByteWriter cnam;
  cnam.u32(0);
  fo4::test::write_subrecord(out, "CNAM", cnam.bytes);
  fo4::test::end_record(out, sparse);

  auto anchorless = fo4::test::begin_record(out, "COBJ", 0, 0x00000A02, 131);
  fo4::test::ByteWriter obnd;
  obnd.u32(0);
  fo4::test::write_subrecord(out, "OBND", obnd.bytes);
  fo4::test::end_record(out, anchorless);
  fo4::test::end_group(out, cobj_group);
  return out.bytes;
}

}  // namespace

TEST(Components, SupportsMiscAndCobjOnly) {
  EXPECT_TRUE(fo4::forms::record_supports_components(fo4::Signature::from_string("MISC")));
  EXPECT_TRUE(fo4::forms::record_supports_components(fo4::Signature::from_string("COBJ")));
  EXPECT_FALSE(fo4::forms::record_supports_components(fo4::Signature::from_string("WEAP")));
  EXPECT_FALSE(fo4::forms::record_supports_components(fo4::Signature::from_string("CMPO")));
}

TEST(Components, DecodesExistingCvpa) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto entries =
      fo4::forms::decode_components(plugin, fo4::FormKey::parse("000900:Comp.esp"));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].component, fo4::FormKey::parse("000801:Comp.esp"));
  EXPECT_EQ(entries[0].count, 3u);
}

TEST(Components, SetReplacesMiscComponentsAndRegeneratesCdix) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto key = fo4::FormKey::parse("000900:Comp.esp");
  const fo4::forms::ComponentEntry desired[] = {
      {fo4::FormKey::parse("000801:Comp.esp"), 5},
      {fo4::FormKey::parse("000802:Comp.esp"), 2},
  };
  EXPECT_EQ(fo4::forms::set_record_components(plugin, key, desired), 2u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Comp.esp");
  const auto* misc = fo4::find_record(reread, key);
  ASSERT_NE(misc, nullptr);
  const auto* cvpa = find(*misc, "CVPA");
  const auto* cdix = find(*misc, "CDIX");
  ASSERT_NE(cvpa, nullptr);
  ASSERT_NE(cdix, nullptr);
  EXPECT_EQ(cvpa->data.size(), 16u);
  ASSERT_EQ(cdix->data.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(cdix->data[0]), 0u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(cdix->data[1]), 1u);

  const auto entries = fo4::forms::decode_components(reread, key);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].component, fo4::FormKey::parse("000801:Comp.esp"));
  EXPECT_EQ(entries[0].count, 5u);
  EXPECT_EQ(entries[1].component, fo4::FormKey::parse("000802:Comp.esp"));
  EXPECT_EQ(entries[1].count, 2u);
}

TEST(Components, SetOnCobjWritesFvpaWithoutCdix) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto key = fo4::FormKey::parse("000A00:Comp.esp");
  const fo4::forms::ComponentEntry desired[] = {
      {fo4::FormKey::parse("000801:Comp.esp"), 1},
      {fo4::FormKey::parse("000802:Comp.esp"), 4},
  };
  EXPECT_EQ(fo4::forms::set_record_components(plugin, key, desired), 2u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Comp.esp");
  const auto* cobj = fo4::find_record(reread, key);
  ASSERT_NE(cobj, nullptr);
  const auto* fvpa = find(*cobj, "FVPA");
  ASSERT_NE(fvpa, nullptr);
  EXPECT_EQ(fvpa->data.size(), 16u);
  EXPECT_EQ(find(*cobj, "CDIX"), nullptr);
  EXPECT_GT(index_of(*cobj, "FVPA"), index_of(*cobj, "ZNAM"));

  const auto entries = fo4::forms::decode_components(reread, key);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[1].count, 4u);
}

TEST(Components, EmptyListClearsCvpaAndCdix) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto key = fo4::FormKey::parse("000900:Comp.esp");
  EXPECT_EQ(fo4::forms::set_record_components(plugin, key, {}), 0u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Comp.esp");
  const auto* misc = fo4::find_record(reread, key);
  ASSERT_NE(misc, nullptr);
  EXPECT_EQ(find(*misc, "CVPA"), nullptr);
  EXPECT_EQ(find(*misc, "CDIX"), nullptr);
  EXPECT_TRUE(fo4::forms::decode_components(reread, key).empty());
}

TEST(Components, CreatesCvpaAfterDataWhenAbsent) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto key = fo4::FormKey::parse("000900:Comp.esp");
  EXPECT_EQ(fo4::forms::set_record_components(plugin, key, {}), 0u);

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Comp.esp");
  const fo4::forms::ComponentEntry desired[] = {
      {fo4::FormKey::parse("000802:Comp.esp"), 7},
  };
  EXPECT_EQ(fo4::forms::set_record_components(reread, key, desired), 1u);

  const auto again = fo4::read_plugin(fo4::write_plugin(reread), "Comp.esp");
  const auto* misc = fo4::find_record(again, key);
  ASSERT_NE(misc, nullptr);
  const auto data = index_of(*misc, "DATA");
  const auto cvpa = index_of(*misc, "CVPA");
  const auto cdix = index_of(*misc, "CDIX");
  EXPECT_EQ(cvpa, data + 1);
  EXPECT_EQ(cdix, cvpa + 1);
}

TEST(Components, SparseCobjPlacesFvpaBeforeItsSuccessor) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto key = fo4::FormKey::parse("000A01:Comp.esp");
  const fo4::forms::ComponentEntry desired[] = {
      {fo4::FormKey::parse("000801:Comp.esp"), 1},
  };
  EXPECT_EQ(fo4::forms::set_record_components(plugin, key, desired), 1u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Comp.esp");
  const auto* cobj = fo4::find_record(reread, key);
  ASSERT_NE(cobj, nullptr);
  const auto fvpa = index_of(*cobj, "FVPA");
  const auto cnam = index_of(*cobj, "CNAM");
  ASSERT_NE(find(*cobj, "FVPA"), nullptr);
  EXPECT_LT(fvpa, cnam);
  EXPECT_EQ(find(*cobj, "CDIX"), nullptr);
}

TEST(Components, AnchorlessCobjFailsClosed) {
  auto plugin = fo4::read_plugin(plugin_with_components(), "Comp.esp");
  const auto key = fo4::FormKey::parse("000A02:Comp.esp");
  const fo4::forms::ComponentEntry desired[] = {
      {fo4::FormKey::parse("000801:Comp.esp"), 1},
  };
  EXPECT_THROW(fo4::forms::set_record_components(plugin, key, desired),
               fo4::forms::ComponentError);
}
