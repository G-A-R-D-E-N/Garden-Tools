#include "fo4/plugin/record_flags.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Subrecords = std::vector<std::pair<std::string_view, std::string>>;

struct CellPlugin {
  std::string_view name{"Cells.esp"};
  std::vector<std::string_view> masters{};
  std::uint32_t cell_form{0x900};
  std::uint8_t cell_data{1};
  fo4::GroupType bucket{fo4::GroupType::cell_temporary_children};
  std::string_view ref_signature{"REFR"};
  std::uint32_t ref_flags{0};
  std::uint32_t ref_form{0x800};
  Subrecords ref_subs{
      {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
      {"DATA", std::string(24, '\0')},
  };
};

std::string label_of(std::uint32_t value) {
  std::string out;
  for (std::size_t shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
  return out;
}

void write_header(fo4::test::ByteWriter& out, const std::vector<std::string_view>& masters) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::string name{master};
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter size;
    size.u32(0);
    size.u32(0);
    fo4::test::write_subrecord(out, "DATA", size.bytes);
  }
  fo4::test::end_record(out, tes4);
}

void put_record(fo4::test::ByteWriter& out,
                std::string_view signature,
                std::uint32_t flags,
                std::uint32_t form_id,
                const Subrecords& subrecords) {
  fo4::test::ByteWriter payload;
  for (const auto& [name, data] : subrecords) {
    fo4::test::write_subrecord(payload, name, data);
  }
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  if ((flags & fo4::RecordFlags::compressed) != 0) {
    out.u32(static_cast<std::uint32_t>(payload.size()));
    out.raw(fo4::test::zlib_compress(payload.bytes));
  } else {
    out.raw(payload.bytes);
  }
  fo4::test::end_record(out, record);
}

Bytes build_plugin(const CellPlugin& layout) {
  fo4::test::ByteWriter out;
  write_header(out, layout.masters);
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_record(out, "CELL", 0, layout.cell_form,
             {{"DATA", std::string{static_cast<char>(layout.cell_data), '\0'}}});
  const auto children = fo4::test::begin_group(out, label_of(layout.cell_form), 6);
  const auto bucket_group = fo4::test::begin_group(
      out, label_of(layout.cell_form), static_cast<std::int32_t>(layout.bucket));
  put_record(out, layout.ref_signature, layout.ref_flags, layout.ref_form, layout.ref_subs);
  fo4::test::end_group(out, bucket_group);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

fo4::Plugin load(const CellPlugin& layout) {
  return fo4::read_plugin(build_plugin(layout), layout.name);
}

Bytes weapon_plugin() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WEAP", 0);
  put_record(out, "WEAP", 0, 0x800,
             {{"EDID", std::string("Blade", 6)}, {"DATA", std::string(4, '\0')}});
  fo4::test::end_group(out, top);
  return out.bytes;
}

const fo4::Group* find_group(const fo4::Group& group, fo4::GroupType type, std::uint32_t label) {
  if (group.header.group_type == static_cast<std::int32_t>(type) &&
      group.header.label_as_u32() == label) {
    return &group;
  }
  for (const auto& child : group.children) {
    const auto* nested = std::get_if<fo4::Group>(&child);
    if (nested == nullptr) continue;
    if (const auto* found = find_group(*nested, type, label)) return found;
  }
  return nullptr;
}

const fo4::Group* find_group(const fo4::Plugin& plugin, fo4::GroupType type, std::uint32_t label) {
  for (const auto& group : plugin.groups) {
    if (const auto* found = find_group(group, type, label)) return found;
  }
  return nullptr;
}

const fo4::Group* parent_group(const fo4::Group& group, const fo4::FormKey& key) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child)) {
      if (record->form_key == key) return &group;
    } else if (const auto* found = parent_group(std::get<fo4::Group>(child), key)) {
      return found;
    }
  }
  return nullptr;
}

const fo4::Group* parent_group(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (const auto* found = parent_group(group, key)) return found;
  }
  return nullptr;
}

}  // namespace

TEST(RecordFlags, InteriorPersistentChangeMovesReferenceBetweenBucketsAndRoundTrips) {
  auto plugin = load({});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* persistent = parent_group(reread, ref);
  ASSERT_NE(persistent, nullptr);
  EXPECT_EQ(persistent->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  const auto* children = find_group(reread, fo4::GroupType::cell_children, 0x900u);
  ASSERT_NE(children, nullptr);
  EXPECT_EQ(children->children.size(), 1u);
  const auto* moved = fo4::find_record(reread, ref);
  ASSERT_NE(moved, nullptr);
  EXPECT_TRUE(moved->header.flags.is_persistent());

  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);
  const auto back = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* temporary = parent_group(back, ref);
  ASSERT_NE(temporary, nullptr);
  EXPECT_EQ(temporary->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  const auto* back_children = find_group(back, fo4::GroupType::cell_children, 0x900u);
  ASSERT_NE(back_children, nullptr);
  EXPECT_EQ(back_children->children.size(), 1u);
  const auto* restored = fo4::find_record(back, ref);
  ASSERT_NE(restored, nullptr);
  EXPECT_FALSE(restored->header.flags.is_persistent());
}

TEST(RecordFlags, PersistentChangeOnInteriorNavmeshKeepsItsBucket) {
  CellPlugin layout;
  layout.ref_signature = "NAVM";
  layout.ref_subs = {{"NVER", std::string(4, '\0')}};
  auto plugin = load(layout);
  const auto navm = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, navm, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* bucket = parent_group(reread, navm);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_persistent_children, 0x900u), nullptr);
  const auto* record = fo4::find_record(reread, navm);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_persistent());
}

TEST(RecordFlags, ChangingDeletedBitThroughSetRecordFlagsIsRefused) {
  auto plugin = load({});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref,
                                  fo4::RecordFlags::deleted | fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::deleted_bit);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, ExteriorCellRefusesRelocationWithoutChange) {
  auto plugin = load(CellPlugin{.cell_data = 0});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::exterior_relocation_unsupported);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, NonTes4RecordDropsMasterBitAndKeepsCompressed) {
  auto plugin = load(CellPlugin{.ref_flags = fo4::RecordFlags::master | fo4::RecordFlags::compressed});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref,
                                  fo4::RecordFlags::master | fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_master());
  EXPECT_TRUE(record->header.flags.is_compressed());
  EXPECT_TRUE(record->header.flags.is_persistent());
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
}

TEST(RecordFlags, PersistentChangeIsIgnoredOnDeletedRecord) {
  auto plugin = load(CellPlugin{.ref_flags = fo4::RecordFlags::deleted});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  EXPECT_EQ(fo4::set_record_flags(plugin, ref,
                                  fo4::RecordFlags::deleted | fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::unchanged);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_persistent());
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, MarkOverrideDeletedKeepsOnlyNameOnMasterReferenceAndRoundTrips) {
  const auto layout = CellPlugin{
      .name = "Patch.esp",
      .masters = {"Base.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::compressed,
      .ref_subs =
          {
              {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
              {"DATA", std::string(24, '\0')},
              {"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}},
          },
  };
  auto plugin = load(layout);
  const auto ref = fo4::FormKey::parse("000800:Base.esm");
  ASSERT_EQ(fo4::mark_override_deleted(plugin, ref), fo4::MarkDeletedStatus::marked);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_deleted());
  EXPECT_FALSE(record->header.flags.is_compressed());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::Signature::from_string("NAME"));
  ASSERT_EQ(record->subrecords[0].data.size(), 4u);
  EXPECT_EQ(std::to_integer<int>(record->subrecords[0].data[1]), 0x08);
}

TEST(RecordFlags, MarkOverrideDeletedLeavesNoSubrecordsOnNonReference) {
  auto plugin = fo4::read_plugin(weapon_plugin(), "Patch.esp");
  const auto weapon = fo4::FormKey::parse("000800:Base.esm");
  ASSERT_EQ(fo4::mark_override_deleted(plugin, weapon), fo4::MarkDeletedStatus::marked);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, weapon);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->subrecords.empty());
}

TEST(RecordFlags, LocalRecordAndAlreadyDeletedReturnWithoutChange) {
  auto local = load({});
  const auto local_before = fo4::write_plugin(local);
  EXPECT_EQ(fo4::mark_override_deleted(local, fo4::FormKey::parse("000800:Cells.esp")),
            fo4::MarkDeletedStatus::local_record);
  EXPECT_EQ(fo4::write_plugin(local), local_before);

  auto deleted = load(CellPlugin{
      .masters = {"Base.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted,
  });
  const auto deleted_before = fo4::write_plugin(deleted);
  EXPECT_EQ(fo4::mark_override_deleted(deleted, fo4::FormKey::parse("000800:Base.esm")),
            fo4::MarkDeletedStatus::already_deleted);
  EXPECT_EQ(fo4::write_plugin(deleted), deleted_before);
}
