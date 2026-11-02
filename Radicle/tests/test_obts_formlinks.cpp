// OBTS carries OMOD include FormIDs and keyword FormIDs at offsets derived
// from counts in its own header. Without a walker they survive a master remap
// unchanged and end up naming a different plugin's records.

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"

#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace {

// One keyword and two includes, no properties.
std::vector<std::byte> obts(std::uint32_t keyword, std::uint32_t include_a,
                            std::uint32_t include_b) {
  fo4::test::ByteWriter o;
  o.u32(2);           // include count
  o.u32(0);           // property count
  o.u8(1); o.u8(0); o.u8(50); o.u8(0);   // level min / pad / level max / pad
  o.u16(0);           // addon index
  o.u8(0);            // default
  o.u8(1);            // keyword count
  o.u32(keyword);     // keyword FormID
  o.u8(0); o.u8(0);   // min level for ranks, alt levels per tier
  o.u32(include_a); o.u8(0); o.u8(0); o.u8(0);
  o.u32(include_b); o.u8(0); o.u8(0); o.u8(0);
  return o.bytes;
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f); hedr.u32(2); hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data; data.u32(0); data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Alpha.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Beta.esm\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "ARMO", 0);
  const auto rec = fo4::test::begin_record(out, "ARMO", 0, 0x02000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"A\0", 2});
  fo4::test::ByteWriter obte; obte.u32(1);
  fo4::test::write_subrecord(out, "OBTE", obte.bytes);
  // keyword owned by Alpha (index 0), includes owned by Beta (index 1).
  fo4::test::write_subrecord(out, "OBTS", obts(0x00000111, 0x01000222, 0x01000333));
  fo4::test::write_subrecord(out, "STOP", std::string_view{"", 0});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

std::vector<std::uint32_t> obts_form_ids(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Mod.esp"));
  std::vector<std::uint32_t> out;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != "OBTS") continue;
    std::uint32_t v = 0;
    std::memcpy(&v, sub.data.data() + 0x10, 4);           // keyword
    out.push_back(v);
    std::memcpy(&v, sub.data.data() + 0x12 + 4, 4);       // include 0
    out.push_back(v);
    std::memcpy(&v, sub.data.data() + 0x12 + 4 + 7, 4);   // include 1
    out.push_back(v);
  }
  return out;
}

}  // namespace

TEST(ObtsFormLinks, TemplateFormIdsAreCollected) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  std::size_t keyword = 0;
  std::size_t includes = 0;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.subrecord.to_string() != "OBTS") continue;
    if (hit.offset == 0x10) ++keyword;
    else ++includes;
  }
  EXPECT_EQ(keyword, 1u);
  EXPECT_EQ(includes, 2u);
}

TEST(ObtsFormLinks, TemplateFormIdsFollowAMasterRemap) {
  auto plugin = fo4::read_plugin(make_plugin(), "Mod.esp");
  EXPECT_EQ(obts_form_ids(plugin),
            (std::vector<std::uint32_t>{0x00000111, 0x01000222, 0x01000333}));

  // Swap the master order; Beta becomes index 0 and Alpha index 1.
  const auto remapped = fo4::remap_plugin_masters(
      plugin, std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("Beta.esm"),
                                          fo4::PluginKey::from_filename("Alpha.esm")});

  EXPECT_EQ(obts_form_ids(remapped),
            (std::vector<std::uint32_t>{0x01000111, 0x00000222, 0x00000333}))
      << "OBTS keyword/include FormIDs kept stale master indices";
}

// A body whose counts do not describe its length must fail closed rather than
// have arbitrary offsets rewritten.
TEST(ObtsFormLinks, MalformedTemplateBodyIsRefusedOnRemap) {
  auto bytes = make_plugin();
  auto plugin = fo4::read_plugin(bytes, "Mod.esp");
  for (auto& group : plugin.groups) {
    for (auto& child : group.children) {
      auto* record = std::get_if<fo4::Record>(&child);
      if (record == nullptr) continue;
      for (auto& sub : record->subrecords) {
        if (sub.signature.to_string() == "OBTS") sub.data.resize(sub.data.size() - 3);
      }
    }
  }
  EXPECT_THROW(static_cast<void>(fo4::remap_plugin_masters(
                   plugin, std::vector<fo4::PluginKey>{
                               fo4::PluginKey::from_filename("Beta.esm"),
                               fo4::PluginKey::from_filename("Alpha.esm")})),
               fo4::FormIdError);
}
