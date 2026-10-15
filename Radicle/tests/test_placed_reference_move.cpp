#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
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

constexpr std::int32_t k_persistent_bucket =
    static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children);
constexpr std::int32_t k_temporary_bucket =
    static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children);

std::vector<std::byte> plugin_with_worldspace() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

std::size_t begin_group_raw(fo4::test::ByteWriter& out, std::uint32_t label, fo4::GroupType type) {
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

std::vector<std::byte> persistent_reference_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x903);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  const auto world_children = begin_group_raw(out, 0x00000800u, fo4::GroupType::world_children);
  const auto cell = fo4::test::begin_record(out, "CELL", 0x00000400u, 0x00000901u, 131);
  fo4::test::ByteWriter cell_flags;
  cell_flags.u16(0x0002);
  fo4::test::write_subrecord(out, "DATA", cell_flags.bytes);
  fo4::test::end_record(out, cell);
  const auto cell_children = begin_group_raw(out, 0x00000901u, fo4::GroupType::cell_children);
  const auto persistent = begin_group_raw(out, 0x00000901u, fo4::GroupType::cell_persistent_children);
  const auto ref = fo4::test::begin_record(out, "REFR", 0x00000400u, 0x00000902u, 131);
  fo4::test::ByteWriter name;
  name.u32(0x00000900u);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter transform;
  for (int i = 0; i < 6; ++i) transform.f32(10.0f + static_cast<float>(i));
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  fo4::test::end_record(out, ref);
  end_group_raw(out, persistent);
  end_group_raw(out, cell_children);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, wrld_group);
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

const fo4::Group* cell_children_of(const fo4::Plugin& plugin, const fo4::FormKey& cell) {
  const auto label = fo4::encode_plugin_form_id(cell, fo4::plugin_form_id_context(plugin)).value();
  return find_group(plugin, fo4::GroupType::cell_children, label);
}

std::int32_t bucket_type_of(const fo4::Plugin& plugin, const fo4::FormKey& cell, const fo4::FormKey& reference) {
  const auto* children = cell_children_of(plugin, cell);
  if (children == nullptr) return -1;
  for (const auto& child : children->children) {
    const auto* bucket = std::get_if<fo4::Group>(&child);
    if (bucket == nullptr) continue;
    for (const auto& member : bucket->children) {
      const auto* record = std::get_if<fo4::Record>(&member);
      if (record != nullptr && record->form_key == reference) return bucket->header.group_type;
    }
  }
  return -1;
}

}  // namespace

TEST(PlacedReferenceMove, SameCellMoveWritesDataAndKeepsCell) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Moves.esp");
  const auto world = fo4::FormKey::parse("000800:Moves.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{.worldspace = world, .x = 0, .y = 0});
  const auto ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f},
          .persistent = false,
      });

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 4000.0f, .y = 4095.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::moved);

  const auto refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, ref);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4000.0f);
  EXPECT_FLOAT_EQ(refs[0].transform.y, 4095.0f);
  EXPECT_EQ(bucket_type_of(plugin, cell, ref), k_temporary_bucket);
}

TEST(PlacedReferenceMove, CrossCellMoveRelocatesIntoTemporaryGroupAndRoundTrips) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Moves.esp");
  const auto world = fo4::FormKey::parse("000800:Moves.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto source = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{.worldspace = world, .x = 0, .y = 0});
  const auto target = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{.worldspace = world, .x = 1, .y = 0});
  const auto ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = source,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f},
          .persistent = false,
      });

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 4106.0f, .y = 20.0f, .z = 30.0f, .rz = 3.0f}),
            fo4::forms::PlacedReferenceMoveStatus::relocated);

  EXPECT_TRUE(fo4::forms::placed_references_for_cell(plugin, source).empty());
  EXPECT_EQ(cell_children_of(plugin, source), nullptr);
  EXPECT_EQ(bucket_type_of(plugin, target, ref), k_temporary_bucket);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Moves.esp");
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(reread, source).empty());
  const auto refs = fo4::forms::placed_references_for_cell(reread, target);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, ref);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4106.0f);
  EXPECT_FLOAT_EQ(refs[0].transform.rz, 3.0f);
  EXPECT_EQ(bucket_type_of(reread, target, ref), k_temporary_bucket);
}

TEST(PlacedReferenceMove, PersistentReferenceOnlyGetsDataWritten) {
  auto plugin = fo4::read_plugin(persistent_reference_plugin(), "Persist.esp");
  const auto cell = fo4::FormKey::parse("000901:Persist.esp");
  const auto ref = fo4::FormKey::parse("000902:Persist.esp");

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 8200.0f, .y = 20.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::moved);

  const auto refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 8200.0f);
  EXPECT_EQ(bucket_type_of(plugin, cell, ref), k_persistent_bucket);
}

TEST(PlacedReferenceMove, InteriorReferenceOnlyGetsDataWritten) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Moves.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "Vault");
  const auto ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f},
          .persistent = false,
      });

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 8200.0f, .y = 20.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::moved);

  const auto refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 8200.0f);
  EXPECT_EQ(bucket_type_of(plugin, cell, ref), k_temporary_bucket);
}

TEST(PlacedReferenceMove, MissingTargetCellLeavesPluginUnchanged) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Moves.esp");
  const auto world = fo4::FormKey::parse("000800:Moves.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto source = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{.worldspace = world, .x = 0, .y = 0});
  const auto ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = source,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f},
          .persistent = false,
      });
  const auto before = fo4::write_plugin(plugin);

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 12298.0f, .y = 20.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::target_cell_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, fo4::FormKey::parse("000999:Moves.esp"), {.x = 10.0f}),
            fo4::forms::PlacedReferenceMoveStatus::reference_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  const auto refs = fo4::forms::placed_references_for_cell(plugin, source);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 10.0f);
}

TEST(PlacedReferenceMove, NegativeCoordinatesUseFloorGrid) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Moves.esp");
  const auto world = fo4::FormKey::parse("000800:Moves.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto source = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{.worldspace = world, .x = 0, .y = 0});
  const auto target = fo4::forms::create_exterior_cell(
      plugin, fo4::forms::ExteriorCellCreation{.worldspace = world, .x = -1, .y = -1});
  const auto ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = source,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f},
          .persistent = false,
      });

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = -0.5f, .y = -0.5f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::relocated);
  EXPECT_TRUE(fo4::forms::placed_references_for_cell(plugin, source).empty());
  const auto refs = fo4::forms::placed_references_for_cell(plugin, target);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, ref);
  EXPECT_FLOAT_EQ(refs[0].transform.x, -0.5f);

  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = -4096.5f, .y = -0.5f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::target_cell_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
