#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::uint16_t read_u16(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint16_t value = 0;
  std::memcpy(&value, data.data() + offset, 2);
  return value;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

void append_zeros(fo4::test::ByteWriter& out, std::size_t count) {
  out.bytes.insert(out.bytes.end(), count, std::byte{0});
}

void write_common_header(fo4::test::ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
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

  const auto group = fo4::test::begin_group(out, "NAVM", 0);
  const auto record = fo4::test::begin_record(out, "NAVM", 0, 0x03000800);

  fo4::test::ByteWriter onam;
  onam.u32(0x00000007u);
  onam.u32(0x02000812u);
  onam.u32(0x02000813u);
  fo4::test::write_subrecord(out, "ONAM", onam.bytes);

  fo4::test::ByteWriter pnam;
  pnam.u16(0x0002u);
  pnam.u16(0x0200u);
  pnam.u16(0x0812u);
  pnam.u16(0x1234u);
  fo4::test::write_subrecord(out, "PNAM", pnam.bytes);

  fo4::test::ByteWriter nnam;
  nnam.u16(0x0003u);
  nnam.u16(0x0200u);
  nnam.u16(0x0813u);
  nnam.u16(0x5678u);
  fo4::test::write_subrecord(out, "NNAM", nnam.bytes);

  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_four_byte_vertex_index_plugin() {
  fo4::test::ByteWriter out;
  write_common_header(out);

  const auto group = fo4::test::begin_group(out, "NAVM", 0);
  const auto record = fo4::test::begin_record(out, "NAVM", 0, 0x03000800);

  fo4::test::ByteWriter onam;
  onam.u32(0x02000812u);
  fo4::test::write_subrecord(out, "ONAM", onam.bytes);

  fo4::test::ByteWriter pnam;
  pnam.u32(0x02000813u);
  fo4::test::write_subrecord(out, "PNAM", pnam.bytes);

  fo4::test::ByteWriter nnam;
  nnam.u32(0x02000814u);
  fo4::test::write_subrecord(out, "NNAM", nnam.bytes);

  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_four_byte_pnam_plugin(std::string_view signature) {
  fo4::test::ByteWriter out;
  write_common_header(out);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x03000800);

  fo4::test::ByteWriter pnam;
  pnam.u32(0x02000812u);
  fo4::test::write_subrecord(out, "PNAM", pnam.bytes);

  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::test::ByteWriter make_nvnm(bool malformed = false) {
  fo4::test::ByteWriter nvnm;
  nvnm.u32(15);           // Version
  nvnm.u32(0x11111111u);  // Pathing Cell CRC
  nvnm.u32(0x02000100u);  // Parent World: KeepMe.esm
  nvnm.u16(2);            // exterior Grid Y
  nvnm.u16(3);            // exterior Grid X

  nvnm.u32(malformed ? 0xFFFFFFFFu : 1u);  // vertices
  nvnm.f32(1.0f);
  nvnm.f32(2.0f);
  nvnm.f32(3.0f);

  nvnm.u32(1);  // triangles
  append_zeros(nvnm, 21);

  nvnm.u32(1);           // edge links
  nvnm.u32(0);           // Type
  nvnm.u32(0x02000812);  // Navmesh: KeepMe.esm
  nvnm.u16(7);           // Triangle
  nvnm.u8(1);            // Edge Index

  nvnm.u32(1);           // door links
  nvnm.u16(8);           // Triangle
  nvnm.u32(0x22222222);  // CRC
  nvnm.u32(0x02000813);  // Door Ref: KeepMe.esm

  nvnm.u32(1);  // cover
  append_zeros(nvnm, 8);

  nvnm.u32(1);  // cover triangle mappings
  nvnm.u16(0);
  nvnm.u16(0);

  nvnm.u32(1);  // waypoints
  nvnm.f32(4.0f);
  nvnm.f32(5.0f);
  nvnm.f32(6.0f);
  nvnm.u16(0);
  nvnm.u32(0);

  nvnm.u32(1);  // grid divisor -> one cell
  nvnm.f32(1.0f);
  nvnm.f32(1.0f);
  for (int i = 0; i < 6; ++i) nvnm.f32(static_cast<float>(i));
  nvnm.u32(1);  // cell triangle count
  nvnm.u16(0);
  return nvnm;
}

fo4::test::ByteWriter make_nvmi() {
  fo4::test::ByteWriter nvmi;
  nvmi.u32(0x02000820);  // Navmesh: KeepMe.esm
  nvmi.u32(0);           // Flags
  nvmi.f32(1.0f);
  nvmi.f32(2.0f);
  nvmi.f32(3.0f);
  nvmi.f32(0.5f);        // Preferred %

  nvmi.u32(1);           // Edge Links
  nvmi.u32(0x02000821);
  nvmi.u32(1);           // Preferred Edge Links
  nvmi.u32(0x02000822);
  nvmi.u32(1);           // Door Links
  nvmi.u32(0x33333333);  // CRC
  nvmi.u32(0x02000823);  // Door Ref

  nvmi.u8(1);            // Has Island Data
  for (int i = 0; i < 6; ++i) nvmi.f32(static_cast<float>(i));  // bounds
  nvmi.u32(1);           // island triangles
  nvmi.u16(0);
  nvmi.u16(1);
  nvmi.u16(2);
  nvmi.u32(1);           // island vertices
  nvmi.f32(1.0f);
  nvmi.f32(2.0f);
  nvmi.f32(3.0f);

  nvmi.u32(0x44444444);  // Pathing Cell CRC
  nvmi.u32(0);           // Parent World = NULL => interior union arm
  nvmi.u32(0x02000824);  // Parent Cell: KeepMe.esm
  return nvmi;
}

fo4::test::ByteWriter make_nvpp() {
  fo4::test::ByteWriter nvpp;
  nvpp.u32(1);  // one precomputed path
  nvpp.u32(2);  // two NAVM FormIDs in that path
  nvpp.u32(0x02000830);
  nvpp.u32(0x02000831);
  nvpp.u32(1);  // road-marker count
  nvpp.u32(0x02000832);
  nvpp.u32(7);  // marker index
  return nvpp;
}

fo4::test::ByteWriter make_mnam(bool malformed = false) {
  fo4::test::ByteWriter mnam;
  mnam.u32(0x02000850);  // Reference: KeepMe.esm
  mnam.u16(malformed ? 0xFFFFu : 3u);
  mnam.u16(0x0200u);     // triangle indexes, deliberately FormID-looking
  mnam.u16(0x0850u);
  mnam.u16(0x1234u);
  if (!malformed) {
    mnam.u32(0x02000851);  // second Reference: KeepMe.esm
    mnam.u16(2);
    mnam.u16(0x0200u);
    mnam.u16(0x0851u);
  }
  return mnam;
}

std::vector<std::byte> make_mixed_navmesh_plugin(bool malformed_nvnm = false,
                                                  bool malformed_mnam = false) {
  fo4::test::ByteWriter out;
  write_common_header(out);

  const auto navm_group = fo4::test::begin_group(out, "NAVM", 0);
  const auto navm = fo4::test::begin_record(out, "NAVM", 0, 0x03000800);
  const auto nvnm = make_nvnm(malformed_nvnm);
  fo4::test::write_subrecord(out, "NVNM", nvnm.bytes);
  const auto mnam = make_mnam(malformed_mnam);
  fo4::test::write_subrecord(out, "MNAM", mnam.bytes);
  fo4::test::end_record(out, navm);
  fo4::test::end_group(out, navm_group);

  const auto navi_group = fo4::test::begin_group(out, "NAVI", 0);
  const auto navi = fo4::test::begin_record(out, "NAVI", 0, 0x03000801);
  const auto nvmi = make_nvmi();
  fo4::test::write_subrecord(out, "NVMI", nvmi.bytes);
  const auto nvpp = make_nvpp();
  fo4::test::write_subrecord(out, "NVPP", nvpp.bytes);
  fo4::test::ByteWriter nvsi;
  nvsi.u32(0x02000840);
  nvsi.u32(0x02000841);
  fo4::test::write_subrecord(out, "NVSI", nvsi.bytes);
  fo4::test::end_record(out, navi);
  fo4::test::end_group(out, navi_group);
  return out.bytes;
}

std::vector<fo4::FormLinkHit> hits_for(const fo4::Plugin& plugin,
                                       const fo4::FormKey& key,
                                       std::string_view subrecord) {
  std::vector<fo4::FormLinkHit> result;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == key && hit.subrecord.to_string() == subrecord) {
      result.push_back(hit);
    }
  }
  return result;
}

}

TEST(FormLink, NavmBaseObjectOnamArrayRemapsWithoutTouchingVertexIndexes) {
  auto plugin = fo4::read_plugin(make_plugin(), "NavmeshPatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(plugin), "NavmeshPatch.esp");
  const auto* record = fo4::find_record(
      reread, fo4::FormKey::parse("000800:NavmeshPatch.esp"));
  ASSERT_NE(record, nullptr);

  const auto* onam = find_sub(*record, "ONAM");
  const auto* pnam = find_sub(*record, "PNAM");
  const auto* nnam = find_sub(*record, "NNAM");
  ASSERT_NE(onam, nullptr);
  ASSERT_NE(pnam, nullptr);
  ASSERT_NE(nnam, nullptr);

  ASSERT_EQ(onam->data.size(), 12u);
  EXPECT_EQ(read_u32(onam->data, 0), 0x00000007u);
  EXPECT_EQ(read_u32(onam->data, 4), 0x01000812u);
  EXPECT_EQ(read_u32(onam->data, 8), 0x01000813u);

  ASSERT_EQ(pnam->data.size(), 8u);
  EXPECT_EQ(read_u16(pnam->data, 0), 0x0002u);
  EXPECT_EQ(read_u16(pnam->data, 2), 0x0200u);
  EXPECT_EQ(read_u16(pnam->data, 4), 0x0812u);
  EXPECT_EQ(read_u16(pnam->data, 6), 0x1234u);

  ASSERT_EQ(nnam->data.size(), 8u);
  EXPECT_EQ(read_u16(nnam->data, 0), 0x0003u);
  EXPECT_EQ(read_u16(nnam->data, 2), 0x0200u);
  EXPECT_EQ(read_u16(nnam->data, 4), 0x0813u);
  EXPECT_EQ(read_u16(nnam->data, 6), 0x5678u);
}

TEST(FormLink, NavmCollectionReportsOnlyOnamBaseObjectsFromThisSlice) {
  auto plugin = fo4::read_plugin(make_plugin(), "NavmeshPatch.esp");
  const auto hits = fo4::collect_form_links(plugin);

  std::vector<fo4::RawFormId> onamHits;
  for (const auto& hit : hits) {
    if (hit.record == fo4::FormKey::parse("000800:NavmeshPatch.esp") &&
        hit.subrecord.to_string() == "ONAM") {
      onamHits.push_back(hit.raw);
    }
    EXPECT_NE(hit.subrecord.to_string(), "PNAM");
    EXPECT_NE(hit.subrecord.to_string(), "NNAM");
  }

  ASSERT_EQ(onamHits.size(), 3u);
  EXPECT_EQ(onamHits[0], fo4::RawFormId{0x00000007u});
  EXPECT_EQ(onamHits[1], fo4::RawFormId{0x02000812u});
  EXPECT_EQ(onamHits[2], fo4::RawFormId{0x02000813u});
}

TEST(FormLink, NavmNvnmMixedPayloadRemapsOnlyProvenFormIdSlots) {
  auto plugin = fo4::read_plugin(make_mixed_navmesh_plugin(), "NavmeshPatch.esp");
  const auto key = fo4::FormKey::parse("000800:NavmeshPatch.esp");
  const auto before = hits_for(plugin, key, "NVNM");
  ASSERT_EQ(before.size(), 3u);
  EXPECT_EQ(before[0].offset, 8u);
  EXPECT_EQ(before[0].key, fo4::FormKey::parse("000100:KeepMe.esm"));
  EXPECT_EQ(before[1].key, fo4::FormKey::parse("000812:KeepMe.esm"));
  EXPECT_EQ(before[2].key, fo4::FormKey::parse("000813:KeepMe.esm"));

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "NavmeshPatch.esp");
  const auto after = hits_for(reread, key, "NVNM");
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t index = 0; index < before.size(); ++index) {
    EXPECT_EQ(after[index].key, before[index].key);
    EXPECT_NE(after[index].raw, before[index].raw);
    EXPECT_EQ(after[index].raw.index_byte(), 1u);
  }
}

TEST(FormLink, NavmMnamPrecutMapEntriesRemapReferencesOnly) {
  auto plugin = fo4::read_plugin(make_mixed_navmesh_plugin(), "NavmeshPatch.esp");
  const auto key = fo4::FormKey::parse("000800:NavmeshPatch.esp");
  const auto before = hits_for(plugin, key, "MNAM");
  ASSERT_EQ(before.size(), 2u);
  EXPECT_EQ(before[0].offset, 0u);
  EXPECT_EQ(before[1].offset, 12u);
  EXPECT_EQ(before[0].key, fo4::FormKey::parse("000850:KeepMe.esm"));
  EXPECT_EQ(before[1].key, fo4::FormKey::parse("000851:KeepMe.esm"));

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "NavmeshPatch.esp");
  const auto after = hits_for(reread, key, "MNAM");
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t index = 0; index < before.size(); ++index) {
    EXPECT_EQ(after[index].key, before[index].key);
    EXPECT_NE(after[index].raw, before[index].raw);
    EXPECT_EQ(after[index].raw.index_byte(), 1u);
  }

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* mnam = find_sub(*record, "MNAM");
  ASSERT_NE(mnam, nullptr);
  ASSERT_EQ(mnam->data.size(), 22u);
  EXPECT_EQ(read_u16(mnam->data, 4), 3u);
  EXPECT_EQ(read_u16(mnam->data, 6), 0x0200u);
  EXPECT_EQ(read_u16(mnam->data, 8), 0x0850u);
  EXPECT_EQ(read_u16(mnam->data, 10), 0x1234u);
  EXPECT_EQ(read_u16(mnam->data, 16), 2u);
  EXPECT_EQ(read_u16(mnam->data, 18), 0x0200u);
  EXPECT_EQ(read_u16(mnam->data, 20), 0x0851u);
}

TEST(FormLink, NaviNvmiNvppAndNvsiMixedPayloadsRemapStructurally) {
  auto plugin = fo4::read_plugin(make_mixed_navmesh_plugin(), "NavmeshPatch.esp");
  const auto key = fo4::FormKey::parse("000801:NavmeshPatch.esp");

  const auto nvmi_before = hits_for(plugin, key, "NVMI");
  const auto nvpp_before = hits_for(plugin, key, "NVPP");
  const auto nvsi_before = hits_for(plugin, key, "NVSI");
  ASSERT_EQ(nvmi_before.size(), 6u);
  ASSERT_EQ(nvpp_before.size(), 3u);
  ASSERT_EQ(nvsi_before.size(), 2u);
  EXPECT_TRUE(nvmi_before[4].raw.is_null());  // Parent World
  EXPECT_EQ(nvmi_before[5].key, fo4::FormKey::parse("000824:KeepMe.esm"));

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "NavmeshPatch.esp");

  const auto nvmi_after = hits_for(reread, key, "NVMI");
  const auto nvpp_after = hits_for(reread, key, "NVPP");
  const auto nvsi_after = hits_for(reread, key, "NVSI");
  ASSERT_EQ(nvmi_after.size(), nvmi_before.size());
  ASSERT_EQ(nvpp_after.size(), nvpp_before.size());
  ASSERT_EQ(nvsi_after.size(), nvsi_before.size());

  for (std::size_t index = 0; index < nvmi_before.size(); ++index) {
    EXPECT_EQ(nvmi_after[index].key, nvmi_before[index].key);
    if (!nvmi_before[index].raw.is_null()) {
      EXPECT_NE(nvmi_after[index].raw, nvmi_before[index].raw);
      EXPECT_EQ(nvmi_after[index].raw.index_byte(), 1u);
    }
  }
  for (std::size_t index = 0; index < nvpp_before.size(); ++index) {
    EXPECT_EQ(nvpp_after[index].key, nvpp_before[index].key);
    EXPECT_NE(nvpp_after[index].raw, nvpp_before[index].raw);
    EXPECT_EQ(nvpp_after[index].raw.index_byte(), 1u);
  }
  for (std::size_t index = 0; index < nvsi_before.size(); ++index) {
    EXPECT_EQ(nvsi_after[index].key, nvsi_before[index].key);
    EXPECT_NE(nvsi_after[index].raw, nvsi_before[index].raw);
    EXPECT_EQ(nvsi_after[index].raw.index_byte(), 1u);
  }
}

TEST(FormLink, MalformedNvnmFailsClosedInsteadOfScanningUint32s) {
  auto plugin = fo4::read_plugin(
      make_mixed_navmesh_plugin(true), "NavmeshPatch.esp");
  const auto key = fo4::FormKey::parse("000800:NavmeshPatch.esp");
  EXPECT_TRUE(hits_for(plugin, key, "NVNM").empty());

  EXPECT_THROW(
      fo4::remap_plugin_masters(
          plugin,
          {fo4::PluginKey::from_filename("Fallout4.esm"),
           fo4::PluginKey::from_filename("KeepMe.esm")}),
      fo4::FormIdError);
}

TEST(FormLink, UnsupportedNvnmVersionsRefuseRemapWithoutChangingBytes) {
  for (const std::uint32_t version : {12u, 13u, 14u, 16u, 0xFFFFFFFFu}) {
    SCOPED_TRACE(version);
    auto plugin = fo4::read_plugin(make_mixed_navmesh_plugin(), "NavmeshPatch.esp");
    auto& record = std::get<fo4::Record>(plugin.groups[0].children[0]);
    std::memcpy(record.subrecords[0].data.data(), &version, sizeof(version));
    const auto before = fo4::write_plugin(plugin);
    const auto key = fo4::FormKey::parse("000800:NavmeshPatch.esp");
    EXPECT_TRUE(hits_for(plugin, key, "NVNM").empty());
    EXPECT_THROW(fo4::remap_plugin_masters(plugin,
        {fo4::PluginKey::from_filename("Fallout4.esm"),
         fo4::PluginKey::from_filename("KeepMe.esm")}), fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(FormLink, MalformedMnamFailsClosedInsteadOfScanningTriangleIndexes) {
  auto plugin = fo4::read_plugin(
      make_mixed_navmesh_plugin(false, true), "NavmeshPatch.esp");
  const auto key = fo4::FormKey::parse("000800:NavmeshPatch.esp");
  EXPECT_TRUE(hits_for(plugin, key, "MNAM").empty());

  EXPECT_THROW(
      fo4::remap_plugin_masters(
          plugin,
          {fo4::PluginKey::from_filename("Fallout4.esm"),
           fo4::PluginKey::from_filename("KeepMe.esm")}),
      fo4::FormIdError);
}

TEST(FormLink, NavmFourByteVertexIndexArraysSurviveMasterRemapByteForByte) {
  auto plugin = fo4::read_plugin(
      make_four_byte_vertex_index_plugin(), "NavmeshPatch.esp");
  const auto key = fo4::FormKey::parse("000800:NavmeshPatch.esp");

  const auto* before_record = fo4::find_record(plugin, key);
  ASSERT_NE(before_record, nullptr);
  const auto* before_onam = find_sub(*before_record, "ONAM");
  const auto* before_pnam = find_sub(*before_record, "PNAM");
  const auto* before_nnam = find_sub(*before_record, "NNAM");
  ASSERT_NE(before_onam, nullptr);
  ASSERT_NE(before_pnam, nullptr);
  ASSERT_NE(before_nnam, nullptr);
  ASSERT_EQ(before_onam->data.size(), 4u);
  ASSERT_EQ(before_pnam->data.size(), 4u);
  ASSERT_EQ(before_nnam->data.size(), 4u);
  EXPECT_EQ(read_u32(before_onam->data, 0), 0x02000812u);
  EXPECT_EQ(read_u32(before_pnam->data, 0), 0x02000813u);
  EXPECT_EQ(read_u32(before_nnam->data, 0), 0x02000814u);
  const auto pnam_bytes = before_pnam->data;
  const auto nnam_bytes = before_nnam->data;
  EXPECT_TRUE(hits_for(plugin, key, "PNAM").empty());
  EXPECT_TRUE(hits_for(plugin, key, "NNAM").empty());

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});
  const auto reread = fo4::read_plugin(
      fo4::write_plugin(plugin), "NavmeshPatch.esp");

  const auto after_onam = hits_for(reread, key, "ONAM");
  ASSERT_EQ(after_onam.size(), 1u);
  EXPECT_EQ(after_onam[0].raw, fo4::RawFormId{0x01000812u});

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* pnam = find_sub(*record, "PNAM");
  const auto* nnam = find_sub(*record, "NNAM");
  ASSERT_NE(pnam, nullptr);
  ASSERT_NE(nnam, nullptr);
  ASSERT_EQ(pnam->data.size(), pnam_bytes.size());
  ASSERT_EQ(nnam->data.size(), nnam_bytes.size());
  EXPECT_EQ(std::memcmp(pnam->data.data(), pnam_bytes.data(), pnam_bytes.size()), 0);
  EXPECT_EQ(std::memcmp(nnam->data.data(), nnam_bytes.data(), nnam_bytes.size()), 0);
  EXPECT_EQ(read_u32(pnam->data, 0), 0x02000813u);
  EXPECT_EQ(read_u32(nnam->data, 0), 0x02000814u);
}

TEST(FormLink, NpcFourBytePnamStillRemapsAsAHeadPartFormLink) {
  auto plugin = fo4::read_plugin(
      make_four_byte_pnam_plugin("NPC_"), "NpcPatch.esp");
  const auto key = fo4::FormKey::parse("000800:NpcPatch.esp");

  const auto before = hits_for(plugin, key, "PNAM");
  ASSERT_EQ(before.size(), 1u);
  EXPECT_EQ(before[0].offset, 0u);
  EXPECT_EQ(before[0].raw, fo4::RawFormId{0x02000812u});

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Fallout4.esm"),
       fo4::PluginKey::from_filename("KeepMe.esm")});
  const auto reread = fo4::read_plugin(
      fo4::write_plugin(plugin), "NpcPatch.esp");

  const auto after = hits_for(reread, key, "PNAM");
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after[0].raw, fo4::RawFormId{0x01000812u});
}
