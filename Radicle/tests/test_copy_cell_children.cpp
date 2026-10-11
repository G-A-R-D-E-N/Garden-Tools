#include "fo4/formid.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

enum class Shape { interior, exterior, persistent };

struct Leaf {
  std::int32_t type;
  std::string_view signature;
  std::uint32_t form_id;
};

std::string label_of(std::uint32_t value) {
  std::string out;
  for (std::size_t shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
  return out;
}

fo4::FormKey base_key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Base.esm"), fo4::LocalFormId{local}};
}

void write_header(fo4::test::ByteWriter& out,
                  std::uint32_t flags,
                  std::initializer_list<std::string_view> masters) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::string name{master};
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

void put_record(fo4::test::ByteWriter& out,
                std::string_view signature,
                std::uint32_t flags,
                std::uint32_t form_id,
                std::string_view edid) {
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  std::string data{edid};
  data.push_back('\0');
  fo4::test::write_subrecord(out, "EDID", data);
  fo4::test::end_record(out, record);
}

void put_cell_children(fo4::test::ByteWriter& out,
                       std::uint32_t cell_id,
                       std::initializer_list<Leaf> leaves) {
  const auto children = fo4::test::begin_group(out, label_of(cell_id), 6);
  for (const auto& leaf : leaves) {
    const auto group = fo4::test::begin_group(out, label_of(cell_id), leaf.type);
    put_record(out, leaf.signature, 0, leaf.form_id, "Leaf");
    fo4::test::end_group(out, group);
  }
  fo4::test::end_group(out, children);
}

Bytes nested_source(Shape shape,
                    std::uint32_t cell_id,
                    std::uint32_t cell_flags,
                    std::initializer_list<Leaf> leaves) {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm", "Other.esm"});
  if (shape == Shape::interior) {
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = fo4::test::begin_group(out, label_of(3), 2);
    const auto sub = fo4::test::begin_group(out, label_of(5), 3);
    put_record(out, "CELL", cell_flags, cell_id, "Cell");
    put_cell_children(out, cell_id, leaves);
    fo4::test::end_group(out, sub);
    fo4::test::end_group(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
  }

  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  put_record(out, "WRLD", 0, 0xA00, "World");
  const auto world_children = fo4::test::begin_group(out, label_of(0xA00), 1);
  if (shape == Shape::exterior) {
    const auto block = fo4::test::begin_group(out, label_of(0x00010002), 4);
    const auto sub = fo4::test::begin_group(out, label_of(0x00010002), 5);
    put_record(out, "CELL", cell_flags, cell_id, "Cell");
    put_cell_children(out, cell_id, leaves);
    fo4::test::end_group(out, sub);
    fo4::test::end_group(out, block);
  } else {
    put_record(out, "CELL", cell_flags, cell_id, "Cell");
    put_cell_children(out, cell_id, leaves);
  }
  fo4::test::end_group(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes empty_target(std::uint32_t flags = 0) {
  fo4::test::ByteWriter out;
  write_header(out, flags, {});
  return out.bytes;
}

Bytes target_with_cell_in_other_block() {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(7), 2);
  const auto sub = fo4::test::begin_group(out, label_of(9), 3);
  put_record(out, "CELL", 0, 0x900, "Cell");
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes source_without_parent_cell() {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_cell_children(out, 0x900, {{9, "REFR", 0x800}});
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes source_with_mismatched_labels() {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_record(out, "CELL", 0, 0x900, "Cell");
  const auto children = fo4::test::begin_group(out, label_of(0x900), 6);
  const auto temporary = fo4::test::begin_group(out, label_of(0x901), 9);
  put_record(out, "REFR", 0, 0x800, "Leaf");
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

fo4::Plugin reread_plugin(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Target.esp");
}

bool collect_path(const fo4::Group& group,
                  const fo4::FormKey& key,
                  std::vector<const fo4::Group*>& path) {
  path.push_back(&group);
  for (const auto& child : group.children) {
    if (const auto* sub = std::get_if<fo4::Group>(&child)) {
      if (collect_path(*sub, key, path)) return true;
    } else if (std::get<fo4::Record>(child).form_key == key) {
      return true;
    }
  }
  path.pop_back();
  return false;
}

std::vector<const fo4::Group*> containment_path(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  std::vector<const fo4::Group*> path;
  for (const auto& group : plugin.groups) {
    if (collect_path(group, key, path)) break;
  }
  return path;
}

std::vector<std::int32_t> group_types(const std::vector<const fo4::Group*>& path) {
  std::vector<std::int32_t> types;
  for (const auto* group : path) types.push_back(group->header.group_type);
  return types;
}

std::size_t count_records(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto records = fo4::collect_records(plugin);
  return static_cast<std::size_t>(std::count_if(
      records.begin(), records.end(),
      [&](const fo4::Record* record) { return record->form_key == key; }));
}

std::vector<std::int32_t> child_group_types(const fo4::Group& group) {
  std::vector<std::int32_t> types;
  for (const auto& child : group.children) {
    if (const auto* sub = std::get_if<fo4::Group>(&child)) types.push_back(sub->header.group_type);
  }
  return types;
}

std::size_t count_child_records(const fo4::Group& group, std::string_view signature) {
  std::size_t count = 0;
  for (const auto& child : group.children) {
    const auto* record = std::get_if<fo4::Record>(&child);
    if (record != nullptr && record->header.signature.to_string() == signature) ++count;
  }
  return count;
}

Bytes subrecord_data(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return sub.data;
  }
  return {};
}

Bytes target_with_world_group(bool with_block) {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  put_record(out, "WRLD", 0, 0xA00, "World");
  const auto world_children = fo4::test::begin_group(out, label_of(0xA00), 1);
  if (with_block) {
    fo4::test::end_group(out, fo4::test::begin_group(out, label_of(0x00010002), 4));
  }
  fo4::test::end_group(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes target_with_world_outside_world_group() {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  put_record(out, "WRLD", 0, 0xA00, "World");
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes source_with_record_before_cell_children() {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_record(out, "CELL", 0, 0x900, "Cell");
  put_record(out, "REFR", 0, 0x901, "Between");
  put_cell_children(out, 0x900, {{9, "REFR", 0x800}});
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes interior_source_with_image_space() {
  fo4::test::ByteWriter out;
  write_header(out, 0, {"Base.esm", "Other.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000900);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Cell\0", 5});
  fo4::test::ByteWriter image;
  image.u32(0x01000950);
  fo4::test::write_subrecord(out, "XCIM", image.bytes);
  fo4::test::end_record(out, cell);
  put_cell_children(out, 0x01000900, {{9, "REFR", 0x800}});
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

}  // namespace

TEST(CopyCellChildren, InteriorTemporaryRefrInsertsWithParentChain) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto key = base_key(0x800);

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  ASSERT_EQ(reread.header.masters.size(), 1u);
  EXPECT_EQ(reread.header.masters[0].key.filename(), "Base.esm");
  const auto path = containment_path(reread, key);
  EXPECT_EQ(group_types(path), (std::vector<std::int32_t>{0, 2, 3, 6, 9}));
  ASSERT_EQ(path.size(), 5u);
  EXPECT_EQ(path[1]->header.label_as_u32(), 3u);
  EXPECT_EQ(path[2]->header.label_as_u32(), 5u);
  EXPECT_EQ(path[3]->header.label_as_u32(), 0x900u);
  EXPECT_EQ(path[4]->header.label_as_u32(), 0x900u);
  EXPECT_EQ(count_records(reread, base_key(0x900)), 1u);
}

TEST(CopyCellChildren, InteriorPersistentAndTemporaryShareOneCellChildrenGroup) {
  const auto persistent = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{8, "REFR", 0x800}}), "Source.esp");
  const auto temporary = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x801}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, persistent, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  ASSERT_EQ(fo4::copy_record_as_override(target, temporary, base_key(0x801), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  EXPECT_EQ(group_types(containment_path(reread, base_key(0x800))),
            (std::vector<std::int32_t>{0, 2, 3, 6, 8}));
  EXPECT_EQ(group_types(containment_path(reread, base_key(0x801))),
            (std::vector<std::int32_t>{0, 2, 3, 6, 9}));
  EXPECT_EQ(count_records(reread, base_key(0x900)), 1u);
  const auto path = containment_path(reread, base_key(0x801));
  ASSERT_EQ(path.size(), 5u);
  EXPECT_EQ(path[3]->children.size(), 2u);
}

TEST(CopyCellChildren, ExistingCellAndTemporaryGroupAreReused) {
  const auto first = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  const auto second = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x801}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, first, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  ASSERT_EQ(fo4::copy_record_as_override(target, second, base_key(0x801), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  EXPECT_EQ(count_records(reread, base_key(0x900)), 1u);
  const auto path = containment_path(reread, base_key(0x801));
  ASSERT_EQ(path.size(), 5u);
  EXPECT_EQ(path[3]->children.size(), 1u);
  EXPECT_EQ(path[4]->children.size(), 2u);
}

TEST(CopyCellChildren, ExteriorChainRecreatesWorldAndBlockGroups) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::exterior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto key = base_key(0x800);

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  const auto path = containment_path(reread, key);
  EXPECT_EQ(group_types(path), (std::vector<std::int32_t>{0, 1, 4, 5, 6, 9}));
  ASSERT_EQ(path.size(), 6u);
  EXPECT_EQ(path[1]->header.label_as_u32(), 0xA00u);
  EXPECT_EQ(path[2]->header.label_as_u32(), 0x00010002u);
  EXPECT_EQ(path[3]->header.label_as_u32(), 0x00010002u);
  EXPECT_EQ(path[4]->header.label_as_u32(), 0x900u);
  ASSERT_EQ(reread.groups.size(), 1u);
  const auto& world = reread.groups.front();
  ASSERT_EQ(world.children.size(), 2u);
  EXPECT_NE(std::get_if<fo4::Record>(&world.children[0]), nullptr);
  const auto* world_children = std::get_if<fo4::Group>(&world.children[1]);
  ASSERT_NE(world_children, nullptr);
  EXPECT_EQ(world_children->header.group_type, 1);
}

TEST(CopyCellChildren, WorldspacePersistentCellSitsDirectlyInWorldChildren) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::persistent, 0x900, 0, {{8, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto key = base_key(0x800);

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  const auto path = containment_path(reread, key);
  EXPECT_EQ(group_types(path), (std::vector<std::int32_t>{0, 1, 6, 8}));
  ASSERT_EQ(path.size(), 4u);
  ASSERT_EQ(path[1]->children.size(), 2u);
  EXPECT_NE(std::get_if<fo4::Record>(&path[1]->children[0]), nullptr);
  EXPECT_EQ(count_records(reread, base_key(0x900)), 1u);
}

TEST(CopyCellChildren, OnlyAcceptedKindsInsertAtTheirGroupTypes) {
  struct Case {
    Leaf leaf;
    fo4::RecordCopyStatus status;
  };
  const Case cases[] = {
      {{9, "NAVM", 0x800}, fo4::RecordCopyStatus::inserted},
      {{8, "PGRE", 0x800}, fo4::RecordCopyStatus::inserted},
      {{8, "ACHR", 0x800}, fo4::RecordCopyStatus::inserted},
      {{8, "NAVM", 0x800}, fo4::RecordCopyStatus::source_containment_unsupported},
      {{7, "REFR", 0x800}, fo4::RecordCopyStatus::source_containment_unsupported},
      {{7, "INFO", 0x800}, fo4::RecordCopyStatus::source_containment_unsupported},
  };
  for (const auto& test_case : cases) {
    const auto source = fo4::read_plugin(
        nested_source(Shape::interior, 0x900, 0, {test_case.leaf}), "Source.esp");
    auto target = fo4::read_plugin(empty_target(), "Target.esp");
    const auto before = fo4::write_plugin(target);
    EXPECT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
              test_case.status)
        << test_case.leaf.signature;
    if (test_case.status != fo4::RecordCopyStatus::inserted) {
      EXPECT_EQ(fo4::write_plugin(target), before) << test_case.leaf.signature;
    }
  }
}

TEST(CopyCellChildren, FailClosedWithoutParentCellOrMatchingLabels) {
  const auto valid = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  auto good = fo4::read_plugin(empty_target(), "Target.esp");
  ASSERT_EQ(fo4::copy_record_as_override(good, valid, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);

  for (const auto& bytes : {source_without_parent_cell(), source_with_mismatched_labels()}) {
    const auto source = fo4::read_plugin(bytes, "Source.esp");
    auto target = fo4::read_plugin(empty_target(), "Target.esp");
    const auto before = fo4::write_plugin(target);
    EXPECT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
              fo4::RecordCopyStatus::source_containment_unsupported);
    EXPECT_EQ(fo4::write_plugin(target), before);
    EXPECT_TRUE(target.groups.empty());
    EXPECT_TRUE(target.header.masters.empty());
  }
}

TEST(CopyCellChildren, FailClosedWhenParentCellLivesInAnotherBlock) {
  const auto valid = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  auto fresh = fo4::read_plugin(empty_target(), "Target.esp");
  ASSERT_EQ(fo4::copy_record_as_override(fresh, valid, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);

  auto target = fo4::read_plugin(target_with_cell_in_other_block(), "Target.esp");
  const auto before = fo4::write_plugin(target);
  EXPECT_EQ(fo4::copy_record_as_override(target, valid, base_key(0x800), false),
            fo4::RecordCopyStatus::source_containment_unsupported);
  EXPECT_EQ(fo4::write_plugin(target), before);
}

TEST(CopyCellChildren, PartialFormCellFlagIsKeptFromSource) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, fo4::RecordFlags::partial_form, {{9, "REFR", 0x800}}),
      "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  const auto* cell = fo4::find_record(reread, base_key(0x900));
  ASSERT_NE(cell, nullptr);
  EXPECT_TRUE(cell->header.flags.test(fo4::RecordFlags::partial_form));
}

TEST(CopyCellChildren, ParentCellOwnedByOtherMasterAddsThatMaster) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::interior, 0x01000900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(fo4::RecordFlags::master), "Patch.esm");
  const auto other_cell =
      fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}};

  ASSERT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esm");
  ASSERT_EQ(reread.header.masters.size(), 2u);
  const auto has_other = std::any_of(
      reread.header.masters.begin(), reread.header.masters.end(),
      [](const auto& master) { return master.key.filename() == "Other.esm"; });
  EXPECT_TRUE(has_other);
  EXPECT_NE(fo4::find_record(reread, other_cell), nullptr);
  ASSERT_EQ(reread.header.overrides.size(), 1u);
  EXPECT_EQ(reread.header.overrides[0],
            fo4::encode_plugin_form_id(base_key(0x800), fo4::plugin_form_id_context(reread)));
}

TEST(CopyCellChildren, RepeatCopyIsAlreadyExistsThenOverwritesInPlace) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto key = base_key(0x800);

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  EXPECT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::already_exists);
  EXPECT_EQ(fo4::copy_record_as_override(target, source, key, true),
            fo4::RecordCopyStatus::replaced);
  const auto reread = reread_plugin(target);
  EXPECT_EQ(reread.groups.size(), 1u);
  EXPECT_EQ(group_types(containment_path(reread, key)),
            (std::vector<std::int32_t>{0, 2, 3, 6, 9}));
  EXPECT_EQ(count_records(reread, key), 1u);
}

TEST(CopyCellChildren, PersistentCellPrecedesBlockGroupsInWorldChildren) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::persistent, 0x900, 0, {{8, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(target_with_world_group(true), "Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  ASSERT_EQ(reread.groups.size(), 1u);
  const auto* world_children = std::get_if<fo4::Group>(&reread.groups.front().children[1]);
  ASSERT_NE(world_children, nullptr);
  ASSERT_EQ(world_children->children.size(), 3u);
  const auto* cell = std::get_if<fo4::Record>(&world_children->children[0]);
  ASSERT_NE(cell, nullptr);
  EXPECT_EQ(cell->header.signature.to_string(), "CELL");
  EXPECT_EQ(child_group_types(*world_children), (std::vector<std::int32_t>{6, 4}));
}

TEST(CopyCellChildren, CellChildGroupsStayInAscendingTypeOrder) {
  const auto temporary = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{9, "REFR", 0x800}}), "Source.esp");
  const auto persistent = fo4::read_plugin(
      nested_source(Shape::interior, 0x900, 0, {{8, "REFR", 0x801}}), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, temporary, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  ASSERT_EQ(fo4::copy_record_as_override(target, persistent, base_key(0x801), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  const auto path = containment_path(reread, base_key(0x801));
  ASSERT_EQ(path.size(), 5u);
  EXPECT_EQ(child_group_types(*path[3]), (std::vector<std::int32_t>{8, 9}));
}

TEST(CopyCellChildren, RefusalAfterMasterAddedLeavesHeaderAndBytesUnchanged) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::persistent, 0x01000900, 0, {{8, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(target_with_world_outside_world_group(), "Target.esp");
  const auto before = fo4::write_plugin(target);
  const auto masters_before = target.header.masters.size();

  EXPECT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::source_containment_unsupported);
  EXPECT_EQ(target.header.masters.size(), masters_before);
  EXPECT_EQ(fo4::write_plugin(target), before);
}

TEST(CopyCellChildren, RefusalWhenWorldExistsOutsideWorldGroup) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::persistent, 0x900, 0, {{8, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(target_with_world_outside_world_group(), "Target.esp");
  const auto before = fo4::write_plugin(target);

  EXPECT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::source_containment_unsupported);
  EXPECT_EQ(fo4::write_plugin(target), before);
  EXPECT_EQ(target.groups.size(), 1u);
}

TEST(CopyCellChildren, RefusalWhenChildGroupIsNotAdjacentToCell) {
  const auto source = fo4::read_plugin(source_with_record_before_cell_children(), "Source.esp");
  auto target = fo4::read_plugin(empty_target(), "Target.esp");
  const auto before = fo4::write_plugin(target);

  EXPECT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::source_containment_unsupported);
  EXPECT_EQ(fo4::write_plugin(target), before);
}

TEST(CopyCellChildren, PersistentCopyReusesExistingWorldAndWorldChildren) {
  const auto source = fo4::read_plugin(
      nested_source(Shape::persistent, 0x900, 0, {{8, "REFR", 0x800}}), "Source.esp");
  auto target = fo4::read_plugin(target_with_world_group(false), "Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  ASSERT_EQ(reread.groups.size(), 1u);
  const auto& world = reread.groups.front();
  EXPECT_EQ(count_child_records(world, "WRLD"), 1u);
  EXPECT_EQ(child_group_types(world), (std::vector<std::int32_t>{1}));
  const auto path = containment_path(reread, base_key(0x800));
  EXPECT_EQ(group_types(path), (std::vector<std::int32_t>{0, 1, 6, 8}));
  ASSERT_EQ(path.size(), 4u);
  EXPECT_EQ(path[1]->children.size(), 2u);
  EXPECT_EQ(count_records(reread, base_key(0x900)), 1u);
}

TEST(CopyCellChildren, ParentCellKeepsEdidAndRemapsFormIdSubrecord) {
  const auto source = fo4::read_plugin(interior_source_with_image_space(), "Source.esp");
  fo4::test::ByteWriter header;
  write_header(header, 0, {"Other.esm", "Base.esm"});
  auto target = fo4::read_plugin(header.bytes, "Target.esp");
  const auto other_cell =
      fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}};

  ASSERT_EQ(fo4::copy_record_as_override(target, source, base_key(0x800), false),
            fo4::RecordCopyStatus::inserted);
  const auto reread = reread_plugin(target);
  const auto* cell = fo4::find_record(reread, other_cell);
  const auto* source_cell = fo4::find_record(source, other_cell);
  ASSERT_NE(cell, nullptr);
  ASSERT_NE(source_cell, nullptr);
  EXPECT_EQ(subrecord_data(*cell, "EDID"), subrecord_data(*source_cell, "EDID"));
  fo4::test::ByteWriter expected;
  expected.u32(0x00000950);
  EXPECT_EQ(subrecord_data(*cell, "XCIM"), expected.bytes);
}
