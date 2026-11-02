#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

void write_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_master(fo4::test::ByteWriter& out, std::string_view name) {
  std::vector<std::byte> bytes;
  bytes.reserve(name.size() + 1);
  for (const char ch : name) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  fo4::test::write_subrecord(out, "MAST", bytes);

  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
}

fo4::test::ByteWriter plugin_header() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  write_master(out, "Fallout4.esm");
  write_master(out, "DLC01.esm");
  write_master(out, "RadiationOverhaul.esp");
  fo4::test::end_record(out, tes4);
  return out;
}

void append_ipds_record(fo4::test::ByteWriter& out,
                        std::uint32_t form_id,
                        const std::vector<std::vector<std::byte>>& pnams) {
  const auto group = fo4::test::begin_group(out, "IPDS", 0);
  const auto record = fo4::test::begin_record(out, "IPDS", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  for (const auto& pnam : pnams) {
    fo4::test::write_subrecord(out, "PNAM", pnam);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void append_misc_record(fo4::test::ByteWriter& out,
                        std::uint32_t form_id,
                        std::vector<std::byte> pnam) {
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Record\0", 7});
  fo4::test::write_subrecord(out, "PNAM", std::move(pnam));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void append_refr_record(fo4::test::ByteWriter& out,
                        std::uint32_t form_id,
                        std::uint32_t target_form_id) {
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Ref\0", 4});
  fo4::test::ByteWriter name;
  name.u32(target_form_id);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

std::vector<const fo4::Subrecord*> find_all_subs(const fo4::Record& record,
                                                 std::string_view signature) {
  std::vector<const fo4::Subrecord*> result;
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) result.push_back(&sub);
  }
  return result;
}

std::vector<fo4::PluginKey> reordered_masters() {
  return {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
          fo4::PluginKey::from_filename("Fallout4.esm")};
}

}

TEST(FormLink, IpdsPnamRemapsMaterialAndImpactLanes) {
  fo4::test::ByteWriter pnam0;
  pnam0.u32(0x02000042);
  pnam0.u32(0x00000099);

  fo4::test::ByteWriter pnam1;
  pnam1.u32(0x02000043);
  pnam1.u32(0x0000009A);

  fo4::test::ByteWriter control;
  control.u32(0x02000052);
  control.u32(0x0000009B);

  auto out = plugin_header();
  append_ipds_record(out, 0x03000800, {pnam0.bytes, pnam1.bytes});
  append_misc_record(out, 0x03000801, control.bytes);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* ipds = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(ipds, nullptr);
  const auto ipds_pnams = find_all_subs(*ipds, "PNAM");
  ASSERT_EQ(ipds_pnams.size(), 2);

  auto expected0 = pnam0.bytes;
  write_u32(expected0, 0, 0x00000042);
  write_u32(expected0, 4, 0x01000099);
  EXPECT_EQ(ipds_pnams[0]->data, expected0);

  auto expected1 = pnam1.bytes;
  write_u32(expected1, 0, 0x00000043);
  write_u32(expected1, 4, 0x0100009A);
  EXPECT_EQ(ipds_pnams[1]->data, expected1);

  const auto links = fo4::collect_form_links(reread);
  ASSERT_EQ(links.size(), 4);
  EXPECT_EQ(links[0].subrecord_ordinal, 0);
  EXPECT_EQ(links[0].offset, 0);
  EXPECT_EQ(links[1].subrecord_ordinal, 0);
  EXPECT_EQ(links[1].offset, 4);
  EXPECT_EQ(links[2].subrecord_ordinal, 1);
  EXPECT_EQ(links[2].offset, 0);
  EXPECT_EQ(links[3].subrecord_ordinal, 1);
  EXPECT_EQ(links[3].offset, 4);

  const auto* misc = fo4::find_record(reread, fo4::FormKey::parse("000801:Patch.esp"));
  ASSERT_NE(misc, nullptr);
  const auto* misc_pnam = find_sub(*misc, "PNAM");
  ASSERT_NE(misc_pnam, nullptr);
  EXPECT_EQ(misc_pnam->data, control.bytes);
}

TEST(FormLink, IpdsPnamInvalidSizedPayloadsFailClosedWithoutMutation) {
  for (const std::size_t size : {11u, 16u, 0u}) {
    SCOPED_TRACE(size);
    std::vector<std::byte> payload(size);
    for (std::size_t i = 0; i < payload.size(); ++i) {
      payload[i] = std::byte{static_cast<std::uint8_t>(i)};
    }
    auto out = plugin_header();
    append_ipds_record(out, 0x03000800, {payload});
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    const auto before = fo4::write_plugin(plugin);

    EXPECT_THROW(
        (void)fo4::remap_plugin_masters(plugin, reordered_masters()),
        fo4::FormIdError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(FormLink, IpdsPnamCollisionControlsPreserveNavmAndRemapNpc) {
  auto out = plugin_header();

  fo4::test::ByteWriter navm_pnam;
  navm_pnam.u16(0x0042);
  navm_pnam.u16(0x0043);
  navm_pnam.u16(0x0044);
  navm_pnam.u16(0x0045);
  const auto navm_pnam_bytes = navm_pnam.bytes;

  fo4::test::ByteWriter npc_pnam;
  npc_pnam.u32(0x02000042);

  const auto navm_grp = fo4::test::begin_group(out, "NAVM", 0);
  const auto navm_rec = fo4::test::begin_record(out, "NAVM", 0, 0x03000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NavmRecord\0", 11});
  fo4::test::write_subrecord(out, "PNAM", navm_pnam.bytes);
  fo4::test::end_record(out, navm_rec);
  fo4::test::end_group(out, navm_grp);

  const auto npc_grp = fo4::test::begin_group(out, "NPC_", 0);
  const auto npc_rec = fo4::test::begin_record(out, "NPC_", 0, 0x03000802);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"NpcRecord\0", 10});
  fo4::test::write_subrecord(out, "PNAM", npc_pnam.bytes);
  fo4::test::end_record(out, npc_rec);
  fo4::test::end_group(out, npc_grp);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), reordered_masters());
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* navm = fo4::find_record(reread, fo4::FormKey::parse("000801:Patch.esp"));
  ASSERT_NE(navm, nullptr);
  const auto* navm_sub = find_sub(*navm, "PNAM");
  ASSERT_NE(navm_sub, nullptr);
  EXPECT_EQ(navm_sub->data, navm_pnam_bytes);

  const auto* npc = fo4::find_record(reread, fo4::FormKey::parse("000802:Patch.esp"));
  ASSERT_NE(npc, nullptr);
  const auto* npc_sub = find_sub(*npc, "PNAM");
  ASSERT_NE(npc_sub, nullptr);
  std::uint32_t npc_head_part = 0;
  std::memcpy(&npc_head_part, npc_sub->data.data(), sizeof(npc_head_part));
  EXPECT_EQ(npc_head_part, 0x00000042);
}

TEST(FormLink, IpdsPnamDirectRekeyPersistsThroughWriter) {
  fo4::test::ByteWriter pnam;
  pnam.u32(0x02000042);
  pnam.u32(0x00000099);

  auto out = plugin_header();
  append_ipds_record(out, 0x03000800, {pnam.bytes});

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto old_key = fo4::FormKey::parse("000042:RadiationOverhaul.esp");
  const auto new_key = fo4::FormKey::parse("000043:RadiationOverhaul.esp");

  fo4::rekey_form_links(plugin, old_key, new_key);
  EXPECT_TRUE(fo4::plugin_original_bytes(plugin).empty());

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* ipds = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(ipds, nullptr);
  const auto* ipds_pnam = find_sub(*ipds, "PNAM");
  ASSERT_NE(ipds_pnam, nullptr);
  std::uint32_t rekeyed_material = 0;
  std::memcpy(&rekeyed_material, ipds_pnam->data.data(), sizeof(rekeyed_material));
  EXPECT_EQ(rekeyed_material, 0x02000043);
}

TEST(FormLink, IpdsPnamCollectionPreservesTraversalOrder) {
  fo4::test::ByteWriter pnam0;
  pnam0.u32(0x02000042);
  pnam0.u32(0x00000099);

  fo4::test::ByteWriter pnam1;
  pnam1.u32(0x02000043);
  pnam1.u32(0x0000009A);

  auto out = plugin_header();
  append_refr_record(out, 0x03000799, 0x00000042);
  append_ipds_record(out, 0x03000800, {pnam0.bytes, pnam1.bytes});
  append_refr_record(out, 0x03000802, 0x00000043);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto links = fo4::collect_form_links(plugin);
  ASSERT_EQ(links.size(), 6);

  EXPECT_EQ(links[0].record, fo4::FormKey::parse("000799:Patch.esp"));
  EXPECT_EQ(links[0].subrecord.to_string(), "NAME");

  EXPECT_EQ(links[1].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[1].subrecord.to_string(), "PNAM");
  EXPECT_EQ(links[1].subrecord_ordinal, 0);
  EXPECT_EQ(links[1].offset, 0);

  EXPECT_EQ(links[2].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[2].subrecord.to_string(), "PNAM");
  EXPECT_EQ(links[2].subrecord_ordinal, 0);
  EXPECT_EQ(links[2].offset, 4);

  EXPECT_EQ(links[3].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[3].subrecord.to_string(), "PNAM");
  EXPECT_EQ(links[3].subrecord_ordinal, 1);
  EXPECT_EQ(links[3].offset, 0);

  EXPECT_EQ(links[4].record, fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(links[4].subrecord.to_string(), "PNAM");
  EXPECT_EQ(links[4].subrecord_ordinal, 1);
  EXPECT_EQ(links[4].offset, 4);

  EXPECT_EQ(links[5].record, fo4::FormKey::parse("000802:Patch.esp"));
  EXPECT_EQ(links[5].subrecord.to_string(), "NAME");
}
