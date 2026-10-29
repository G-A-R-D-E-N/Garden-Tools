#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out,
                  std::initializer_list<std::string_view> masters) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::vector<std::byte> name;
    name.reserve(master.size() + 1);
    for (const char ch : master) {
      name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> header_bytes(std::initializer_list<std::string_view> masters) {
  fo4::test::ByteWriter out;
  write_header(out, masters);
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

void write_cell(fo4::test::ByteWriter& out, std::uint32_t raw) {
  const auto cell = fo4::test::begin_record(out, "CELL", 0, raw);
  const std::vector<std::byte> data{std::byte{1}, std::byte{0}};
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::end_record(out, cell);
}

void write_reference(fo4::test::ByteWriter& out,
                     std::uint32_t raw,
                     std::uint32_t base_raw) {
  const auto refr = fo4::test::begin_record(out, "REFR", 0, raw);
  fo4::test::ByteWriter name;
  name.u32(base_raw);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter data;
  for (int index = 0; index < 6; ++index) {
    data.f32(10.0f + static_cast<float>(index));
  }
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, refr);
}

std::vector<std::byte> source_owning_its_reference() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});

  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto temporary = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);
  write_reference(out, 0x01000801u, 0x00000802u);
  end_group_raw(out, temporary);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

fo4::Group top_group(std::string_view signature) {
  fo4::Group group;
  group.header.group_type = static_cast<std::int32_t>(fo4::GroupType::top);
  const auto sig = fo4::Signature::from_string(signature).bytes();
  for (std::size_t i = 0; i < sig.size(); ++i) {
    group.header.label[i] = std::byte{static_cast<std::uint8_t>(sig[i])};
  }
  return group;
}

fo4::Record record(std::string_view signature, std::uint32_t form_id) {
  fo4::Record value;
  value.header.signature = fo4::Signature::from_string(signature);
  value.header.form_id = fo4::RawFormId{form_id};
  value.header.form_version = 131;
  return value;
}

}  // namespace

TEST(ExtensionLightKinds, OwnerEqualToEslSourceIsLightMaster) {
  const auto source = fo4::read_plugin(source_owning_its_reference(), "Source.esl");
  auto target = fo4::read_plugin(header_bytes({"Other.esm"}), "Patch.esp");
  const auto reference = fo4::FormKey::parse("000801:Source.esl");

  EXPECT_EQ(fo4::forms::copy_placed_reference_as_override(target, source, reference),
            fo4::forms::PlacedReferenceCopyStatus::inserted);

  const auto& masters = target.header.masters;
  const auto source_master = std::find_if(masters.begin(), masters.end(), [](const auto& master) {
    return master.key == fo4::PluginKey::from_filename("Source.esl");
  });
  ASSERT_NE(source_master, masters.end());
  EXPECT_EQ(source_master->kind, fo4::PluginKind::light);
}

TEST(ExtensionLightKinds, DirtyEslAndEsmWithoutMasterFlagRebuildOnam) {
  for (const auto* name : {"Patch.esl", "Patch.esm"}) {
    SCOPED_TRACE(name);
    auto plugin = fo4::read_plugin(header_bytes({"Base.esm"}), name);
    ASSERT_FALSE(plugin.header.tes4_flags.is_master());

    auto dialogue = top_group("DIAL");
    dialogue.children.emplace_back(record("DIAL", 0x00000222u));
    plugin.groups.push_back(std::move(dialogue));
    plugin.original_range.clear();

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), name);
    ASSERT_EQ(reread.header.overrides.size(), 1u);
    EXPECT_EQ(reread.header.overrides[0], fo4::RawFormId{0x00000222u});
  }
}

TEST(ExtensionLightKinds, EsmExtensionWithoutMasterFlagIsMaster) {
  EXPECT_TRUE(fo4::is_master_plugin(fo4::RecordFlags{}, fo4::PluginKey::from_filename("Patch.esm")));
  EXPECT_TRUE(fo4::is_master_plugin(fo4::RecordFlags{}, fo4::PluginKey::from_filename("Patch.ESM")));
  EXPECT_EQ(fo4::classify_plugin_kind(fo4::RecordFlags{}, fo4::PluginKey::from_filename("Patch.esm")),
            fo4::PluginKind::full);
  EXPECT_FALSE(fo4::is_master_plugin(fo4::RecordFlags{}, fo4::PluginKey::from_filename("Patch.esp")));
}

TEST(ExtensionLightKinds, EslExtensionWithoutFlagRejectsRemovingLastMasterWithLowIds) {
  auto plugin = fo4::read_plugin(header_bytes({"Base.esm"}), "Patch.esl");
  ASSERT_FALSE(plugin.header.tes4_flags.is_small_file());

  auto dialogue = top_group("DIAL");
  dialogue.children.emplace_back(record("DIAL", 0x01000222u));
  plugin.groups.push_back(std::move(dialogue));

  EXPECT_THROW(fo4::remap_plugin_masters(std::move(plugin), std::vector<fo4::MasterDescriptor>{}),
               fo4::FormIdError);
}
