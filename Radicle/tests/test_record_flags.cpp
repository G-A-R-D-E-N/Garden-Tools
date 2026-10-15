#include "fo4/forms/placed_reference.hpp"
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

std::string position_data(float x, float y, float z) {
  fo4::test::ByteWriter out;
  out.f32(x);
  out.f32(y);
  out.f32(z);
  out.f32(0.0f);
  out.f32(0.0f);
  out.f32(0.0f);
  return std::string(reinterpret_cast<const char*>(out.bytes.data()), out.bytes.size());
}

std::string as_text(const Bytes& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

const fo4::Subrecord* subrecord_named(const fo4::Record& record, std::string_view name) {
  const auto signature = fo4::Signature::from_string(name);
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

constexpr fo4::RecordFlags::value_type k_visible_when_distant = 1u << 15;

struct ExteriorLayout {
  bool persistent_cell{false};
  bool origin_cell{true};
  bool east_cell{false};
  bool reference_in_persistent{false};
  fo4::GroupType bucket{fo4::GroupType::cell_temporary_children};
  fo4::RecordFlags::value_type ref_flags{0};
  float x{10.0f};
  float y{11.0f};
  bool second_reference{false};
  bool origin_data{true};
};

std::string grid_data(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  out.u32(0);
  return std::string(reinterpret_cast<const char*>(out.bytes.data()), out.bytes.size());
}

void put_reference_children(fo4::test::ByteWriter& out, std::uint32_t cell_form, const ExteriorLayout& layout) {
  const auto children = fo4::test::begin_group(out, label_of(cell_form), 6);
  const auto bucket = fo4::test::begin_group(out, label_of(cell_form), static_cast<std::int32_t>(layout.bucket));
  const auto base = std::string{'\x01', '\x08', '\0', '\0'};
  put_record(out, "REFR", layout.ref_flags, 0x910,
             {{"NAME", base}, {"DATA", position_data(layout.x, layout.y, 12.0f)}});
  if (layout.second_reference) {
    put_record(out, "REFR", 0, 0x911,
               {{"NAME", base}, {"DATA", position_data(layout.x + 100.0f, layout.y, 12.0f)}});
  }
  fo4::test::end_group(out, bucket);
  fo4::test::end_group(out, children);
}

Bytes interior_with_two_temporary_buckets() {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_record(out, "CELL", 0, 0x900, {{"DATA", std::string{'\x01', '\0'}}});
  const auto children = fo4::test::begin_group(out, label_of(0x900), 6);
  const auto persistent = fo4::test::begin_group(out, label_of(0x900), 8);
  put_record(out, "REFR", fo4::RecordFlags::persistent, 0x800,
             {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}, {"DATA", position_data(1.0f, 2.0f, 3.0f)}});
  fo4::test::end_group(out, persistent);
  for (const std::uint32_t form : {0x801u, 0x802u}) {
    const auto temporary = fo4::test::begin_group(out, label_of(0x900), 9);
    put_record(out, "REFR", 0, form,
               {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}, {"DATA", position_data(4.0f, 5.0f, 6.0f)}});
    fo4::test::end_group(out, temporary);
  }
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

Bytes exterior_world(const ExteriorLayout& layout) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = fo4::test::begin_group(out, label_of(0x800), 1);
  if (layout.persistent_cell) {
    put_record(out, "CELL", 0x400, 0x901, {{"DATA", std::string{'\x02', '\0'}}});
    if (layout.reference_in_persistent) put_reference_children(out, 0x901, layout);
  }
  const auto block = fo4::test::begin_group(out, label_of(0), 4);
  const auto subblock = fo4::test::begin_group(out, label_of(0), 5);
  if (layout.origin_cell) {
    Subrecords origin;
    if (layout.origin_data) origin.emplace_back("DATA", std::string{'\x02', '\0'});
    origin.emplace_back("XCLC", grid_data(0, 0));
    put_record(out, "CELL", 0, 0x902, origin);
    if (!layout.reference_in_persistent) put_reference_children(out, 0x902, layout);
  }
  if (layout.east_cell) {
    put_record(out, "CELL", 0, 0x903, {{"DATA", std::string{'\x02', '\0'}}, {"XCLC", grid_data(1, 0)}});
  }
  fo4::test::end_group(out, subblock);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, world_children);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

fo4::Plugin load_exterior(const ExteriorLayout& layout) {
  return fo4::read_plugin(exterior_world(layout), "Exterior.esp");
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

TEST(RecordFlags, ExteriorPersistentSetMovesReferenceToWorldPersistentCell) {
  auto plugin = load_exterior({.persistent_cell = true});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto persistent_cell = fo4::FormKey::parse("000901:Exterior.esp");
  const auto origin = fo4::FormKey::parse("000902:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, persistent_cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, origin).empty());
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
}

TEST(RecordFlags, ExteriorPersistentClearMovesReferenceToGridCellByPosition) {
  auto plugin = load_exterior({.persistent_cell = true,
                               .east_cell = true,
                               .reference_in_persistent = true,
                               .bucket = fo4::GroupType::cell_persistent_children,
                               .ref_flags = fo4::RecordFlags::persistent,
                               .x = 4106.0f,
                               .y = 20.0f});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp")).empty());
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_children, 0x00000901u), nullptr);
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp")).empty());
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000903:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4106.0f);
}

TEST(RecordFlags, ExteriorPersistentClearWithoutGridCellRefusesWithoutChange) {
  auto plugin = load_exterior({.persistent_cell = true,
                               .reference_in_persistent = true,
                               .bucket = fo4::GroupType::cell_persistent_children,
                               .ref_flags = fo4::RecordFlags::persistent,
                               .x = 4106.0f,
                               .y = 20.0f});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::target_cell_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, ExteriorPersistentSetWithoutPersistentCellRefusesWithoutChange) {
  auto plugin = load_exterior({});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::target_cell_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, ExteriorPersistentClearInsideGridCellMovesBucketInPlace) {
  auto plugin = load_exterior({.bucket = fo4::GroupType::cell_persistent_children,
                               .ref_flags = fo4::RecordFlags::persistent});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  EXPECT_NE(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
}

TEST(RecordFlags, ExteriorVisibleWhenDistantToggleNeverMoves) {
  auto plugin = load_exterior({});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, k_visible_when_distant), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_NE(refs[0].record->header.flags.value() & k_visible_when_distant, 0u);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, ExteriorPersistentClearRelocatesFromWrongGridCellTemporaryBucket) {
  auto plugin = load_exterior({.east_cell = true,
                               .ref_flags = fo4::RecordFlags::persistent,
                               .x = 4106.0f,
                               .y = 20.0f});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp")).empty());
  EXPECT_EQ(find_group(reread, fo4::GroupType::cell_children, 0x00000902u), nullptr);
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000903:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4106.0f);
}

TEST(RecordFlags, ExteriorPersistentClearInTemporaryBucketOfSameCellStaysPut) {
  auto plugin = load_exterior({.ref_flags = fo4::RecordFlags::persistent});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, 0), fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, ExteriorPersistentSetInsideWorldPersistentCellMovesBucketInPlace) {
  auto plugin = load_exterior({.persistent_cell = true, .reference_in_persistent = true});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp"));
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  EXPECT_NE(find_group(reread, fo4::GroupType::cell_children, 0x00000901u), nullptr);
}

TEST(RecordFlags, ExteriorPersistentSetMovesOnlyEditedReferenceAndKeepsSourceBucket) {
  auto plugin = load_exterior({.persistent_cell = true, .second_reference = true});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto second = fo4::FormKey::parse("000911:Exterior.esp");
  ASSERT_EQ(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
            fo4::RecordFlagStatus::changed);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  const auto origin_refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000902:Exterior.esp"));
  ASSERT_EQ(origin_refs.size(), 1u);
  EXPECT_EQ(origin_refs[0].record->form_key, second);
  EXPECT_FALSE(origin_refs[0].persistent);
  const auto* bucket = parent_group(reread, second);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  const auto persistent_refs = fo4::forms::placed_references_for_cell(reread, fo4::FormKey::parse("000901:Exterior.esp"));
  ASSERT_EQ(persistent_refs.size(), 1u);
  EXPECT_EQ(persistent_refs[0].record->form_key, ref);
  EXPECT_TRUE(persistent_refs[0].persistent);
}

TEST(RecordFlags, ExteriorReferenceInCellWithoutDataThrowsWithoutChange) {
  auto plugin = load_exterior({.origin_data = false});
  const auto ref = fo4::FormKey::parse("000910:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_flags(plugin, ref, fo4::RecordFlags::persistent),
               fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, InteriorRelocationThatFailsLeavesPluginUnchanged) {
  auto plugin = fo4::read_plugin(interior_with_two_temporary_buckets(), "Interior.esp");
  const auto ref = fo4::FormKey::parse("000800:Interior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_flags(plugin, ref, 0), fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  const auto* record = fo4::find_record(plugin, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->header.flags.is_persistent());
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

TEST(RecordFlags, UndeleteClearsDeletedDisablesAndParentsToPlayerReference) {
  auto plugin = load(CellPlugin{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_flags = fo4::RecordFlags::deleted | fo4::RecordFlags::compressed,
      .ref_subs =
          {
              {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
              {"DATA", position_data(1.0f, 2.0f, 3.0f)},
              {"XESP", std::string{'\x00', '\x08', '\0', '\0', '\0', '\0', '\0', '\0'}},
              {"XTEL", std::string(32, '\0')},
              {"XSCL", std::string{'\0', '\0', '\x80', '\x3f'}},
          },
  });
  const auto ref = fo4::FormKey::parse("000800:Fallout4.esm");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, ref), fo4::UndeleteStatus::undeleted);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->header.flags.is_compressed());
  EXPECT_TRUE(record->header.flags.test(fo4::RecordFlags::initially_disabled));
  const auto* xesp = subrecord_named(*record, "XESP");
  ASSERT_NE(xesp, nullptr);
  EXPECT_EQ(as_text(xesp->data), std::string("\x14\0\0\0\x01\0\0\0", 8));
  EXPECT_EQ(subrecord_named(*record, "XTEL"), nullptr);
  EXPECT_NE(subrecord_named(*record, "XSCL"), nullptr);
  const auto transform = fo4::forms::decode_placed_reference(reread, *record)->transform;
  EXPECT_FLOAT_EQ(transform.x, 1.0f);
  EXPECT_FLOAT_EQ(transform.y, 2.0f);
  EXPECT_FLOAT_EQ(transform.z, -30000.0f);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(RecordFlags, UndeleteKeepsPersistentReferenceZAndBucket) {
  auto plugin = load(CellPlugin{
      .bucket = fo4::GroupType::cell_persistent_children,
      .ref_flags = fo4::RecordFlags::deleted | fo4::RecordFlags::persistent,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
                   {"DATA", position_data(1.0f, 2.0f, 3.0f)}},
  });
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  ASSERT_EQ(fo4::undelete_and_disable_reference(plugin, ref), fo4::UndeleteStatus::undeleted);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* record = fo4::find_record(reread, ref);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->header.flags.is_deleted());
  EXPECT_TRUE(record->header.flags.is_persistent());
  EXPECT_TRUE(record->header.flags.test(fo4::RecordFlags::initially_disabled));
  EXPECT_FLOAT_EQ(fo4::forms::decode_placed_reference(reread, *record)->transform.z, 3.0f);
  const auto* bucket = parent_group(reread, ref);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type, static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
}

TEST(RecordFlags, UndeleteRefusesNonDeletedReferenceWithoutChange) {
  auto plugin = load({});
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, ref), fo4::UndeleteStatus::not_deleted);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, UndeleteRefusesDeletedStubWithoutDataWithoutChange) {
  auto plugin = load(CellPlugin{
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}},
  });
  const auto ref = fo4::FormKey::parse("000800:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, ref), fo4::UndeleteStatus::incomplete_reference);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, UndeleteRefusesNonReferenceWithoutChange) {
  auto plugin = fo4::read_plugin(weapon_plugin(), "Patch.esp");
  const auto weapon = fo4::FormKey::parse("000800:Base.esm");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, weapon), fo4::UndeleteStatus::not_a_reference);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(RecordFlags, UndeleteRefusesMissingFormKeyWithoutChange) {
  auto plugin = load({});
  const auto missing = fo4::FormKey::parse("000999:Cells.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::undelete_and_disable_reference(plugin, missing), fo4::UndeleteStatus::record_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
