#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

void write_u64(fo4::test::ByteWriter& out, std::uint64_t value) {
  out.u32(static_cast<std::uint32_t>(value));
  out.u32(static_cast<std::uint32_t>(value >> 32));
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter first_data;
  write_u64(first_data, 0x1122334455667788ull);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"First.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", first_data.bytes);

  fo4::test::ByteWriter second_data;
  write_u64(second_data, 0x0102030405060708ull);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Second.esp\0", 11});
  fo4::test::write_subrecord(out, "DATA", second_data.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::size_t begin_group_raw(fo4::test::ByteWriter& out,
                            std::uint32_t label,
                            fo4::GroupType type) {
  const auto offset = out.size();
  out.sig("GRUP");
  out.u32(0);
  out.u32(label);
  out.i32(static_cast<std::int32_t>(type));
  out.u16(0);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  return offset;
}

void end_group_raw(fo4::test::ByteWriter& out, std::size_t offset) {
  out.patch_u32(offset + 4, static_cast<std::uint32_t>(out.size() - offset));
}

std::vector<std::byte> make_nested_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::write_subrecord(out, "MAST", std::string_view{"First.esm\0", 10});
  fo4::test::ByteWriter first_data;
  first_data.u32(0);
  first_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", first_data.bytes);

  fo4::test::write_subrecord(out, "MAST", std::string_view{"Second.esp\0", 11});
  fo4::test::ByteWriter second_data;
  second_data.u32(0);
  second_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", second_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);

  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000800u);
  const std::vector<std::byte> cell_data{std::byte{1}, std::byte{0}};
  fo4::test::write_subrecord(out, "DATA", cell_data);
  fo4::test::end_record(out, cell);

  const auto children = begin_group_raw(
      out, 0x01000800u, fo4::GroupType::cell_children);
  const auto temporary = begin_group_raw(
      out, 0x01000800u, fo4::GroupType::cell_temporary_children);
  const auto reference = fo4::test::begin_record(out, "REFR", 0, 0x01000801u);
  fo4::test::ByteWriter name;
  name.u32(0x00000802u);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter transform;
  for (int index = 0; index < 6; ++index) transform.f32(static_cast<float>(index + 1));
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  fo4::test::end_record(out, reference);
  end_group_raw(out, temporary);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

const fo4::Group* find_group(const fo4::Plugin& plugin,
                             fo4::GroupType type,
                             std::uint32_t label) {
  const fo4::Group* found = nullptr;
  const auto visit = [&](const auto& self, const fo4::Group& group) -> void {
    if (group.header.group_type == static_cast<std::int32_t>(type) &&
        group.header.label_as_u32() == label) {
      EXPECT_EQ(found, nullptr);
      found = &group;
    }
    for (const auto& child : group.children) {
      if (const auto* nested = std::get_if<fo4::Group>(&child); nested != nullptr) {
        self(self, *nested);
      }
    }
  };
  for (const auto& group : plugin.groups) visit(visit, group);
  return found;
}

std::vector<std::byte> make_single_master_plugin(bool light, std::uint32_t local) {
  fo4::test::ByteWriter out;
  const auto flags = light ? fo4::RecordFlags::small_file : 0u;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"First.esm\0", 10});
  fo4::test::ByteWriter data;
  write_u64(data, 0x1122334455667788ull);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x01000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Local\0", 6});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(MasterMetadata, ReorderPreservesMatchingDataSizes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  ASSERT_EQ(plugin.header.masters.size(), 2u);
  ASSERT_EQ(plugin.header.masters[0].data_size, 0x1122334455667788ull);
  ASSERT_EQ(plugin.header.masters[1].data_size, 0x0102030405060708ull);

  std::vector<fo4::MasterDescriptor> reordered{
      {fo4::PluginKey::from_filename("Second.esp"), fo4::PluginKind::full},
      {fo4::PluginKey::from_filename("First.esm"), fo4::PluginKind::full},
  };
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(reordered));

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_EQ(reloaded.header.masters.size(), 2u);
  EXPECT_EQ(reloaded.header.masters[0].key.filename(), "Second.esp");
  EXPECT_EQ(reloaded.header.masters[0].data_size, 0x0102030405060708ull);
  EXPECT_EQ(reloaded.header.masters[1].key.filename(), "First.esm");
  EXPECT_EQ(reloaded.header.masters[1].data_size, 0x1122334455667788ull);
}

TEST(MasterMetadata, ReorderRemapsFormIdBearingCellGroupLabels) {
  auto plugin = fo4::read_plugin(make_nested_plugin(), "Patch.esp");
  const auto cell_key = fo4::FormKey::parse("000800:Second.esp");
  const auto reference_key = fo4::FormKey::parse("000801:Second.esp");

  ASSERT_NE(find_group(plugin, fo4::GroupType::cell_children, 0x01000800u), nullptr);
  ASSERT_NE(find_group(
      plugin, fo4::GroupType::cell_temporary_children, 0x01000800u), nullptr);
  ASSERT_EQ(fo4::forms::placed_references_for_cell(plugin, cell_key).size(), 1u);

  std::vector<fo4::MasterDescriptor> reordered{
      {fo4::PluginKey::from_filename("Second.esp"), fo4::PluginKind::full},
      {fo4::PluginKey::from_filename("First.esm"), fo4::PluginKind::full},
  };
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(reordered));

  EXPECT_EQ(fo4::find_record(plugin, cell_key)->header.form_id.value(), 0x00000800u);
  EXPECT_EQ(fo4::find_record(plugin, reference_key)->header.form_id.value(), 0x00000801u);
  EXPECT_EQ(find_group(plugin, fo4::GroupType::cell_children, 0x01000800u), nullptr);
  EXPECT_EQ(find_group(
      plugin, fo4::GroupType::cell_temporary_children, 0x01000800u), nullptr);
  EXPECT_NE(find_group(plugin, fo4::GroupType::cell_children, 0x00000800u), nullptr);
  EXPECT_NE(find_group(
      plugin, fo4::GroupType::cell_temporary_children, 0x00000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(plugin, cell_key);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, reference_key);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:First.esm");

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto reread_refs = fo4::forms::placed_references_for_cell(reloaded, cell_key);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->form_key, reference_key);
  ASSERT_TRUE(reread_refs[0].base.has_value());
  EXPECT_EQ(reread_refs[0].base->to_string(), "000802:First.esm");
}

TEST(MasterMetadata, RemovingLastMasterOfLightPluginRefusesLowOwnIds) {
  auto plugin = fo4::read_plugin(make_single_master_plugin(true, 0x001), "Patch.esp");
  ASSERT_TRUE(plugin.header.tes4_flags.is_small_file());
  ASSERT_EQ(plugin.header.masters.size(), 1u);

  EXPECT_THROW(fo4::remap_plugin_masters(plugin, std::vector<fo4::MasterDescriptor>{}),
               fo4::FormIdError);
}

TEST(MasterMetadata, RemovingLastMasterOfLightPluginAllowsIdsAtTheFloor) {
  auto plugin = fo4::read_plugin(make_single_master_plugin(true, 0x800), "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::vector<fo4::MasterDescriptor>{});

  EXPECT_TRUE(plugin.header.masters.empty());
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000800:Patch.esp")), nullptr);
}

TEST(MasterMetadata, RemovingLastMasterOfFullPluginAllowsLowOwnIds) {
  auto plugin = fo4::read_plugin(make_single_master_plugin(false, 0x001), "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::vector<fo4::MasterDescriptor>{});

  EXPECT_TRUE(plugin.header.masters.empty());
  EXPECT_NE(fo4::find_record(plugin, fo4::FormKey::parse("000001:Patch.esp")), nullptr);
}

TEST(MasterMetadata, MasterListStopsAtTheLastFullSlotBeforeTheRuntimeMarkers) {
  auto plugin = fo4::make_empty_plugin("Patch.esp");

  std::vector<fo4::MasterDescriptor> at_limit;
  at_limit.reserve(fo4::k_max_plugin_masters);
  for (std::size_t index = 0; index < fo4::k_max_plugin_masters; ++index) {
    at_limit.push_back({fo4::PluginKey::from_filename("Master" + std::to_string(index) + ".esp"),
                        fo4::PluginKind::full});
  }
  plugin = fo4::remap_plugin_masters(plugin, at_limit);
  ASSERT_EQ(plugin.header.masters.size(), fo4::k_max_plugin_masters);
  EXPECT_EQ(plugin.header.masters.back().key.filename(), "Master252.esp");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(reread.header.masters.size(), fo4::k_max_plugin_masters);

  at_limit.push_back(
      {fo4::PluginKey::from_filename("Overflow.esp"), fo4::PluginKind::full});
  EXPECT_THROW(fo4::remap_plugin_masters(plugin, at_limit), fo4::FormIdError);
}
