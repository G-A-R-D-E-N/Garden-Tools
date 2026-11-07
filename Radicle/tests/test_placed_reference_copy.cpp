#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
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

void write_transform(fo4::test::ByteWriter& data, float base) {
  for (int index = 0; index < 6; ++index) {
    data.f32(base + static_cast<float>(index));
  }
}

void write_cell(fo4::test::ByteWriter& out, std::uint32_t raw) {
  const auto cell = fo4::test::begin_record(out, "CELL", 0, raw);
  const std::vector<std::byte> data{std::byte{1}, std::byte{0}};
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::end_record(out, cell);
}

void write_reference(fo4::test::ByteWriter& out,
                     std::uint32_t raw,
                     std::uint32_t base_raw,
                     float transform_base,
                     std::string_view signature = "REFR") {
  const auto refr = fo4::test::begin_record(out, signature, 0, raw);
  fo4::test::ByteWriter name;
  name.u32(base_raw);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter data;
  write_transform(data, transform_base);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter scale;
  scale.f32(1.25f);
  fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  fo4::test::end_record(out, refr);
}

std::vector<std::byte> interior_source(std::string_view signature = "REFR") {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});

  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto temporary = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);
  write_reference(out, 0x00000801u, 0x00000802u, 10.0f, signature);
  end_group_raw(out, temporary);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> exterior_source() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});

  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000900u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000900u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto persistent = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_persistent_children);
  write_reference(out, 0x00000801u, 0x00000802u, 20.0f);
  end_group_raw(out, persistent);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> target_with_other_master() {
  fo4::test::ByteWriter out;
  write_header(out, {"Other.esm"});
  return out.bytes;
}

std::vector<std::byte> target_with_interior_cell() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

void write_actor(fo4::test::ByteWriter& out, std::uint32_t raw, std::uint32_t base_raw,
                 float transform_base) {
  const auto achr = fo4::test::begin_record(out, "ACHR", 0, raw);
  fo4::test::ByteWriter name;
  name.u32(base_raw);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter data;
  write_transform(data, transform_base);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter scale;
  scale.f32(1.25f);
  fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  fo4::test::end_record(out, achr);
}

std::vector<std::byte> exterior_actor_source() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000900u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000900u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto persistent = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_persistent_children);
  write_actor(out, 0x00000801u, 0x00000802u, 20.0f);
  end_group_raw(out, persistent);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> target_with_exterior_cell() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"});
  const auto top = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000900u);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000900u, fo4::GroupType::world_children);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::exterior_cell_subblock);
  write_cell(out, 0x00000800u);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::size_t count_records(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  std::size_t count = 0;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->form_key == key) ++count;
  }
  return count;
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

void expect_base_master_second(const fo4::Plugin& plugin) {
  ASSERT_EQ(plugin.header.masters.size(), 2u);
  EXPECT_EQ(plugin.header.masters[0].key.filename(), "Other.esm");
  EXPECT_EQ(plugin.header.masters[1].key.filename(), "Base.esm");
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record,
                                     std::string_view signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature.to_string() == signature) return &subrecord;
  }
  return nullptr;
}

}  // namespace

TEST(PlacedReferenceCopy, CopiesInteriorCellHierarchyAndRemapsCellLabels) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  expect_base_master_second(target);
  EXPECT_NE(fo4::find_record(target, cell), nullptr);
  EXPECT_NE(fo4::find_record(target, reference), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_children, 0x01000800u), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_temporary_children, 0x01000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 1u);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:Base.esm");
  EXPECT_FLOAT_EQ(refs[0].transform.x, 10.0f);
  EXPECT_FLOAT_EQ(refs[0].scale, 1.25f);

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  expect_base_master_second(reread);
  const auto reread_refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->form_key, reference);
  ASSERT_TRUE(reread_refs[0].base.has_value());
  EXPECT_EQ(reread_refs[0].base->to_string(), "000802:Base.esm");
}

TEST(PlacedReferenceCopy, CopiesExteriorWorldspaceAnchorAndRemapsWorldLabel) {
  const auto source = fo4::read_plugin(exterior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto world = fo4::FormKey::parse("000900:Base.esm");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  expect_base_master_second(target);
  EXPECT_NE(fo4::find_record(target, world), nullptr);
  EXPECT_NE(fo4::find_record(target, cell), nullptr);
  EXPECT_NE(fo4::find_record(target, reference), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::world_children, 0x01000900u), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_children, 0x01000800u), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_persistent_children, 0x01000800u), nullptr);

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  expect_base_master_second(reread);
  EXPECT_NE(fo4::find_record(reread, world), nullptr);
  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_EQ(refs[0].record->form_key, reference);
}

TEST(PlacedReferenceCopy, ExistingOverrideIsNoOpUnlessOverwriteIsRequested) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  ASSERT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  const auto before = fo4::write_plugin(target);
  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::already_exists);
  EXPECT_EQ(fo4::write_plugin(target), before);

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference, true),
      fo4::forms::PlacedReferenceCopyStatus::replaced);
  EXPECT_NE(fo4::find_record(target, reference), nullptr);
}

TEST(PlacedReferenceCopyNew, CreatesBucketAndNewRecordInExistingInteriorCell) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_EQ(result.new_reference.owner().filename(), "Patch.esp");
  EXPECT_NE(result.new_reference, reference);
  EXPECT_NE(fo4::find_record(target, result.new_reference), nullptr);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_temporary_children, 0x00000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, result.new_reference);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:Base.esm");
  EXPECT_FLOAT_EQ(refs[0].scale, 1.25f);

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  const auto reread_refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->form_key, result.new_reference);
}

TEST(PlacedReferenceCopyNew, FailsClosedWhenParentCellMissing) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::parent_cell_missing);
  EXPECT_EQ(fo4::find_record(target, fo4::FormKey::parse("000800:Base.esm")), nullptr);
}

TEST(PlacedReferenceCopyNew, PreservesExistingBucketReferences) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");
  ASSERT_EQ(fo4::forms::copy_placed_reference_as_override(target, source, reference),
            fo4::forms::PlacedReferenceCopyStatus::inserted);

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_NE(result.new_reference, reference);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 2u);
  bool has_override = false;
  bool has_new = false;
  for (const auto& r : refs) {
    if (r.record->form_key == reference) has_override = true;
    if (r.record->form_key == result.new_reference) has_new = true;
  }
  EXPECT_TRUE(has_override);
  EXPECT_TRUE(has_new);
}

TEST(PlacedReferenceCopyNew, ReportsMissingSource) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto result = fo4::forms::copy_placed_reference_as_new(
      target, source, fo4::FormKey::parse("0009AA:Base.esm"));
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::source_missing);
}

TEST(PlacedReferenceCopyNew, RejectsUnsupportedSignature) {
  const auto source = fo4::read_plugin(interior_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto result = fo4::forms::copy_placed_reference_as_new(
      target, source, fo4::FormKey::parse("000800:Base.esm"));
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::unsupported_signature);
}

TEST(PlacedReferenceCopyNew, CreatesExteriorPersistentActorUnderExistingCell) {
  const auto source = fo4::read_plugin(exterior_actor_source(), "Source.esp");
  auto target = fo4::read_plugin(target_with_exterior_cell(), "Patch.esp");
  const auto world = fo4::FormKey::parse("000900:Base.esm");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_EQ(result.new_reference.owner().filename(), "Patch.esp");
  EXPECT_NE(result.new_reference, reference);

  EXPECT_EQ(count_records(target, world), 1u);
  EXPECT_EQ(count_records(target, cell), 1u);
  EXPECT_EQ(count_records(target, result.new_reference), 1u);
  EXPECT_NE(find_group(target, fo4::GroupType::cell_persistent_children, 0x00000800u), nullptr);

  const auto refs = fo4::forms::placed_references_for_cell(target, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, result.new_reference);
  EXPECT_EQ(refs[0].record->header.signature, fo4::Signature::from_string("ACHR"));
  EXPECT_TRUE(refs[0].persistent);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(refs[0].base->to_string(), "000802:Base.esm");

  auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  EXPECT_EQ(count_records(reread, world), 1u);
  EXPECT_EQ(count_records(reread, cell), 1u);
  const auto reread_refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reread_refs.size(), 1u);
  EXPECT_EQ(reread_refs[0].record->header.signature, fo4::Signature::from_string("ACHR"));
  EXPECT_EQ(reread_refs[0].record->form_key, result.new_reference);
}

TEST(PlacedReferenceCopyNew, CopiesPlacedVariantRecord) {
  const auto source = fo4::read_plugin(interior_source("PGRE"), "Source.esp");
  auto target = fo4::read_plugin(target_with_interior_cell(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  const auto result = fo4::forms::copy_placed_reference_as_new(target, source, reference);
  EXPECT_EQ(result.status, fo4::forms::PlacedReferenceCopyNewStatus::created);
  EXPECT_EQ(result.new_reference.owner().filename(), "Patch.esp");

  const auto* copied = fo4::find_record(target, result.new_reference);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->header.signature, fo4::Signature::from_string("PGRE"));
  const auto* source_record = fo4::find_record(source, reference);
  ASSERT_NE(source_record, nullptr);
  const auto* copied_name = find_subrecord(*copied, "NAME");
  const auto* source_name = find_subrecord(*source_record, "NAME");
  ASSERT_NE(copied_name, nullptr);
  ASSERT_NE(source_name, nullptr);
  EXPECT_EQ(copied_name->data, source_name->data);
  const auto* copied_data = find_subrecord(*copied, "DATA");
  const auto* source_data = find_subrecord(*source_record, "DATA");
  ASSERT_NE(copied_data, nullptr);
  ASSERT_NE(source_data, nullptr);
  EXPECT_EQ(copied_data->data, source_data->data);

  const auto reread = fo4::read_plugin(fo4::write_plugin(target), "Patch.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, result.new_reference);
}

TEST(PlacedReferenceCopy, OverrideCopiesPlacedVariantRecord) {
  const auto source = fo4::read_plugin(interior_source("PGRE"), "Source.esp");
  auto target = fo4::read_plugin(target_with_other_master(), "Patch.esp");
  const auto reference = fo4::FormKey::parse("000801:Base.esm");

  EXPECT_EQ(
      fo4::forms::copy_placed_reference_as_override(target, source, reference),
      fo4::forms::PlacedReferenceCopyStatus::inserted);
  const auto* copied = fo4::find_record(target, reference);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->header.signature, fo4::Signature::from_string("PGRE"));
}
