#include "fo4/plugin/onam.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> make_header(std::vector<std::string_view> masters) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  for (const auto master : masters) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::ByteWriter stale;
  stale.u32(0x0000DEADu);
  fo4::test::write_subrecord(out, "ONAM", stale.bytes);
  fo4::test::end_record(out, tes4);
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

fo4::Record record(std::string_view signature,
                   std::uint32_t form_id,
                   std::uint32_t flags = 0) {
  fo4::Record value;
  value.header.signature = fo4::Signature::from_string(signature);
  value.header.form_id = fo4::RawFormId{form_id};
  value.header.flags = fo4::RecordFlags{flags};
  value.header.form_version = 131;
  return value;
}

fo4::Group nested_group(fo4::GroupType type) {
  fo4::Group group;
  group.header.group_type = static_cast<std::int32_t>(type);
  return group;
}

std::size_t onam_subrecord_count(const fo4::Plugin& plugin) {
  std::size_t count = 0;
  for (const auto& sub : plugin.tes4.subrecords) {
    if (sub.signature == fo4::sig_onam) ++count;
  }
  return count;
}

}  // namespace

TEST(Onam, RebuildIncludesOnlyNonPersistentMasterOwnedFo4Participants) {
  auto plugin = fo4::read_plugin(make_header({"Base.esm"}), "Patch.esm");

  auto cell = top_group("CELL");
  cell.children.emplace_back(record("CELL", 0x00000010u));  // CELL itself is never emitted.
  auto children = nested_group(fo4::GroupType::cell_children);
  auto persistent = nested_group(fo4::GroupType::cell_persistent_children);
  persistent.children.emplace_back(
      record("REFR", 0x00000101u, fo4::RecordFlags::persistent));
  auto temporary = nested_group(fo4::GroupType::cell_temporary_children);
  temporary.children.emplace_back(record("REFR", 0x00000100u));
  temporary.children.emplace_back(record("PHZD", 0x00000103u));
  // xEdit's ONAM predicate is the record flag, not the containing GRUP type.
  // Even if a malformed file leaves this in Temporary, the persistent bit
  // keeps it out of ONAM.
  temporary.children.emplace_back(
      record("REFR", 0x00000104u, fo4::RecordFlags::persistent));
  temporary.children.emplace_back(record("WEAP", 0x00000102u));  // Not an ONAM family.
  temporary.children.emplace_back(record("REFR", 0x01000800u));  // Local record.
  children.children.emplace_back(std::move(persistent));
  children.children.emplace_back(std::move(temporary));
  cell.children.emplace_back(std::move(children));
  plugin.groups.push_back(std::move(cell));

  auto dialogue = top_group("DIAL");
  dialogue.children.emplace_back(record("DIAL", 0x00000200u));
  plugin.groups.push_back(std::move(dialogue));

  plugin.original_range.clear();
  fo4::rebuild_tes4_onam(plugin);

  ASSERT_EQ(plugin.header.overrides.size(), 3u);
  EXPECT_EQ(plugin.header.overrides[0], fo4::RawFormId{0x00000100u});
  EXPECT_EQ(plugin.header.overrides[1], fo4::RawFormId{0x00000103u});
  EXPECT_EQ(plugin.header.overrides[2], fo4::RawFormId{0x00000200u});
  EXPECT_EQ(onam_subrecord_count(plugin), 1u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esm");
  ASSERT_EQ(reread.header.overrides.size(), 3u);
  EXPECT_EQ(reread.header.overrides[0], fo4::RawFormId{0x00000100u});
  EXPECT_EQ(reread.header.overrides[1], fo4::RawFormId{0x00000103u});
  EXPECT_EQ(reread.header.overrides[2], fo4::RawFormId{0x00000200u});
}

TEST(Onam, DirtyMasterWriteRebuildsStaleOnamAutomatically) {
  auto plugin = fo4::read_plugin(make_header({"Base.esm"}), "Patch.esm");
  ASSERT_EQ(plugin.header.overrides.size(), 1u);
  EXPECT_EQ(plugin.header.overrides[0], fo4::RawFormId{0x0000DEADu});

  auto dialogue = top_group("DIAL");
  dialogue.children.emplace_back(record("DIAL", 0x00000222u));
  plugin.groups.push_back(std::move(dialogue));
  plugin.original_range.clear();

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esm");
  ASSERT_EQ(reread.header.overrides.size(), 1u);
  EXPECT_EQ(reread.header.overrides[0], fo4::RawFormId{0x00000222u});
}

TEST(Onam, DirtyMasterWriteDropsOnamAfterOverrideRemoval) {
  auto plugin = fo4::read_plugin(make_header({"Base.esm"}), "Patch.esm");
  auto dialogue = top_group("DIAL");
  dialogue.children.emplace_back(record("DIAL", 0x00000222u));
  plugin.groups.push_back(std::move(dialogue));
  plugin.original_range.clear();

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esm");
  ASSERT_EQ(reread.header.overrides.size(), 1u);

  reread.groups.clear();
  reread.original_range.clear();
  const auto without_override =
      fo4::read_plugin(fo4::write_plugin(reread), "Patch.esm");
  EXPECT_TRUE(without_override.header.overrides.empty());
  EXPECT_EQ(onam_subrecord_count(without_override), 0u);
}

TEST(Onam, EmptyRebuildRemovesStaleOnam) {
  auto plugin = fo4::read_plugin(make_header({"Base.esm"}), "Patch.esm");
  ASSERT_EQ(onam_subrecord_count(plugin), 1u);
  ASSERT_EQ(plugin.header.overrides.size(), 1u);

  plugin.original_range.clear();
  fo4::rebuild_tes4_onam(plugin);
  EXPECT_TRUE(plugin.header.overrides.empty());
  EXPECT_EQ(onam_subrecord_count(plugin), 0u);
}

TEST(Onam, MasterReorderPreservesLogicalOverrideIdentity) {
  auto plugin = fo4::read_plugin(
      make_header({"Base.esm", "Other.esm"}), "Patch.esm");
  auto dialogue = top_group("DIAL");
  dialogue.children.emplace_back(record("DIAL", 0x01000123u));
  plugin.groups.push_back(std::move(dialogue));
  plugin.original_range.clear();

  fo4::rebuild_tes4_onam(plugin);
  ASSERT_EQ(plugin.header.overrides.size(), 1u);
  auto before = fo4::resolve_plugin_form_id(
      plugin.header.overrides[0], fo4::plugin_form_id_context(plugin));
  EXPECT_EQ(before, fo4::FormKey::parse("000123:Other.esm"));

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("Other.esm"),
       fo4::PluginKey::from_filename("Base.esm")});
  fo4::rebuild_tes4_onam(plugin);

  ASSERT_EQ(plugin.header.overrides.size(), 1u);
  EXPECT_EQ(plugin.header.overrides[0], fo4::RawFormId{0x00000123u});
  const auto after = fo4::resolve_plugin_form_id(
      plugin.header.overrides[0], fo4::plugin_form_id_context(plugin));
  EXPECT_EQ(after, before);
}

TEST(Onam, Fo4ParticipantsIncludeParwAndNotAcre) {
  auto plugin = fo4::read_plugin(make_header({"Base.esm"}), "Patch.esm");
  auto cell = top_group("CELL");
  cell.children.emplace_back(record("CELL", 0x00000010u));
  auto children = nested_group(fo4::GroupType::cell_children);
  auto temporary = nested_group(fo4::GroupType::cell_temporary_children);
  temporary.children.emplace_back(record("PARW", 0x00000105u));
  temporary.children.emplace_back(record("ACRE", 0x00000106u));
  children.children.emplace_back(std::move(temporary));
  cell.children.emplace_back(std::move(children));
  plugin.groups.push_back(std::move(cell));
  plugin.original_range.clear();
  fo4::rebuild_tes4_onam(plugin);

  EXPECT_EQ(plugin.header.overrides, std::vector<fo4::RawFormId>{fo4::RawFormId{0x00000105u}});
}
