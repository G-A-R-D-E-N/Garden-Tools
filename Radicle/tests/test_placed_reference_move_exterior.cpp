#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
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
constexpr std::int32_t k_visible_distant_bucket =
    static_cast<std::int32_t>(fo4::GroupType::cell_visible_distant_children);

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

void write_header(fo4::test::ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

void write_worldspace(fo4::test::ByteWriter& out) {
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
}

void write_exterior_cell(fo4::test::ByteWriter& out, std::uint32_t flags, std::uint32_t form_id,
                         bool grid) {
  const auto cell = fo4::test::begin_record(out, "CELL", flags, form_id, 131);
  fo4::test::ByteWriter cell_flags;
  cell_flags.u16(0x0002);
  fo4::test::write_subrecord(out, "DATA", cell_flags.bytes);
  if (grid) {
    fo4::test::ByteWriter xclc;
    xclc.i32(1);
    xclc.i32(0);
    xclc.u32(0);
    fo4::test::write_subrecord(out, "XCLC", xclc.bytes);
  }
  fo4::test::end_record(out, cell);
}

void write_reference(fo4::test::ByteWriter& out, std::uint32_t flags, std::uint32_t form_id) {
  const auto ref = fo4::test::begin_record(out, "REFR", flags, form_id, 131);
  fo4::test::ByteWriter name;
  name.u32(0x00000900u);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter transform;
  for (int i = 0; i < 6; ++i) transform.f32(10.0f + static_cast<float>(i));
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  fo4::test::end_record(out, ref);
}

struct ExteriorShape {
  fo4::GroupType bucket{fo4::GroupType::cell_temporary_children};
  fo4::GroupType children{fo4::GroupType::cell_children};
  fo4::GroupType owner{fo4::GroupType::exterior_cell_subblock};
  std::uint32_t reference_flags{0};
  bool cell_record{true};
  bool bucket_group{true};
  bool worldspace_record{true};
};

std::vector<std::byte> exterior_plugin(const ExteriorShape& shape) {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  if (shape.worldspace_record) write_worldspace(out);
  const auto world_children = begin_group_raw(out, 0x00000800u, fo4::GroupType::world_children);
  const bool in_block = shape.owner == fo4::GroupType::exterior_cell_subblock;
  const auto block = in_block ? begin_group_raw(out, 0u, fo4::GroupType::exterior_cell_block)
                              : std::size_t{0};
  const auto owner = begin_group_raw(out, 0u, shape.owner);
  if (shape.cell_record) write_exterior_cell(out, 0u, 0x00000901u, true);
  const auto children = begin_group_raw(out, 0x00000901u, shape.children);
  if (shape.bucket_group) {
    const auto bucket = begin_group_raw(out, 0x00000901u, shape.bucket);
    write_reference(out, shape.reference_flags, 0x00000902u);
    end_group_raw(out, bucket);
  } else {
    write_reference(out, shape.reference_flags, 0x00000902u);
  }
  end_group_raw(out, children);
  end_group_raw(out, owner);
  if (in_block) end_group_raw(out, block);
  end_group_raw(out, world_children);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

std::vector<std::byte> persistent_cell_with_grid_reference_plugin() {
  fo4::test::ByteWriter out;
  write_header(out);
  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  write_worldspace(out);
  const auto world_children = begin_group_raw(out, 0x00000800u, fo4::GroupType::world_children);
  write_exterior_cell(out, 0x00000400u, 0x00000901u, false);
  const auto block = begin_group_raw(out, 0u, fo4::GroupType::exterior_cell_block);
  const auto subblock = begin_group_raw(out, 0u, fo4::GroupType::exterior_cell_subblock);
  write_exterior_cell(out, 0u, 0x00000902u, true);
  const auto children = begin_group_raw(out, 0x00000902u, fo4::GroupType::cell_children);
  const auto bucket = begin_group_raw(out, 0x00000902u, fo4::GroupType::cell_temporary_children);
  write_reference(out, 0x00000400u, 0x00000903u);
  write_reference(out, 0u, 0x00000904u);
  end_group_raw(out, bucket);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
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

std::int32_t bucket_type_of(const fo4::Plugin& plugin, const fo4::FormKey& cell,
                            const fo4::FormKey& reference) {
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

void expect_move_refused(const std::vector<std::byte>& bytes) {
  auto plugin = fo4::read_plugin(bytes, "Exterior.esp");
  const auto ref = fo4::FormKey::parse("000902:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::forms::move_placed_reference(plugin, ref, {.x = 4106.0f})),
               fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

void expect_move_refused_with_status(const std::vector<std::byte>& bytes,
                                     const fo4::forms::PositionRotation& transform,
                                     fo4::forms::PlacedReferenceMoveStatus expected) {
  auto plugin = fo4::read_plugin(bytes, "Exterior.esp");
  const auto ref = fo4::FormKey::parse("000902:Exterior.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, transform), expected);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

}  // namespace

TEST(PlacedReferenceMoveExterior, PersistentBucketWithAClearFlagRebucketsToTemporary) {
  auto plugin = fo4::read_plugin(
      exterior_plugin({.bucket = fo4::GroupType::cell_persistent_children}), "Exterior.esp");
  const auto cell = fo4::FormKey::parse("000901:Exterior.esp");
  const auto ref = fo4::FormKey::parse("000902:Exterior.esp");
  ASSERT_EQ(bucket_type_of(plugin, cell, ref), k_persistent_bucket);

  const fo4::forms::PositionRotation moved_transform{
      .x = 4106.0f, .y = 20.0f, .z = 30.0f, .rx = 13.0f, .ry = 14.0f, .rz = 15.0f};
  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, moved_transform),
            fo4::forms::PlacedReferenceMoveStatus::moved);

  EXPECT_EQ(bucket_type_of(plugin, cell, ref), k_temporary_bucket);
  const auto refs = fo4::forms::placed_references_for_cell(plugin, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].record->form_key, ref);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_EQ(refs[0].base, fo4::FormKey::parse("000900:Exterior.esp"));
  EXPECT_FLOAT_EQ(refs[0].transform.x, 4106.0f);
  EXPECT_FLOAT_EQ(refs[0].transform.rz, 15.0f);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_EQ(bucket_type_of(reread, cell, ref), k_temporary_bucket);
  const auto reloaded = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reloaded.size(), 1u);
  EXPECT_EQ(reloaded[0].record->form_key, ref);
  EXPECT_FLOAT_EQ(reloaded[0].transform.x, 4106.0f);
}

TEST(PlacedReferenceMoveExterior, SameCellMoveFromTheVisibleDistantBucketRebucketsToTemporary) {
  auto plugin = fo4::read_plugin(
      exterior_plugin({.bucket = fo4::GroupType::cell_visible_distant_children}), "Exterior.esp");
  const auto cell = fo4::FormKey::parse("000901:Exterior.esp");
  const auto ref = fo4::FormKey::parse("000902:Exterior.esp");
  ASSERT_EQ(bucket_type_of(plugin, cell, ref), k_visible_distant_bucket);

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 4106.0f, .y = 20.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::moved);

  EXPECT_EQ(bucket_type_of(plugin, cell, ref), k_temporary_bucket);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_EQ(bucket_type_of(reread, cell, ref), k_temporary_bucket);
  const auto reloaded = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(reloaded.size(), 1u);
  EXPECT_FLOAT_EQ(reloaded[0].transform.x, 4106.0f);
}

TEST(PlacedReferenceMoveExterior, SameCellMoveWithAMatchingBucketWritesOnlyData) {
  auto plugin = fo4::read_plugin(exterior_plugin({}), "Exterior.esp");
  const auto ref = fo4::FormKey::parse("000902:Exterior.esp");
  const fo4::forms::PositionRotation transform{.x = 4106.0f, .y = 20.0f, .z = 30.0f};

  auto transform_only = plugin;
  ASSERT_TRUE(fo4::forms::set_placed_reference_transform(transform_only, ref, transform));

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, transform),
            fo4::forms::PlacedReferenceMoveStatus::moved);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(transform_only));
}

TEST(PlacedReferenceMoveExterior, PersistentReferenceUnderAGridCellMovesToThePersistentCell) {
  auto plugin = fo4::read_plugin(persistent_cell_with_grid_reference_plugin(), "Exterior.esp");
  const auto persistent_cell = fo4::FormKey::parse("000901:Exterior.esp");
  const auto grid_cell = fo4::FormKey::parse("000902:Exterior.esp");
  const auto ref = fo4::FormKey::parse("000903:Exterior.esp");
  const auto other = fo4::FormKey::parse("000904:Exterior.esp");
  ASSERT_EQ(bucket_type_of(plugin, grid_cell, ref), k_temporary_bucket);

  EXPECT_EQ(fo4::forms::move_placed_reference(plugin, ref, {.x = 4106.0f, .y = 20.0f, .z = 30.0f}),
            fo4::forms::PlacedReferenceMoveStatus::relocated);

  EXPECT_EQ(bucket_type_of(plugin, persistent_cell, ref), k_persistent_bucket);
  const auto moved = fo4::forms::placed_references_for_cell(plugin, persistent_cell);
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(moved[0].record->form_key, ref);
  EXPECT_TRUE(moved[0].persistent);
  EXPECT_FLOAT_EQ(moved[0].transform.x, 4106.0f);

  const auto remaining = fo4::forms::placed_references_for_cell(plugin, grid_cell);
  ASSERT_EQ(remaining.size(), 1u);
  EXPECT_EQ(remaining[0].record->form_key, other);
  EXPECT_FLOAT_EQ(remaining[0].transform.x, 10.0f);
  EXPECT_EQ(bucket_type_of(plugin, grid_cell, other), k_temporary_bucket);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Exterior.esp");
  EXPECT_EQ(bucket_type_of(reread, persistent_cell, ref), k_persistent_bucket);
  const auto still_there = fo4::forms::placed_references_for_cell(reread, grid_cell);
  ASSERT_EQ(still_there.size(), 1u);
  EXPECT_EQ(still_there[0].record->form_key, other);
  const auto reloaded = fo4::forms::placed_references_for_cell(reread, persistent_cell);
  ASSERT_EQ(reloaded.size(), 1u);
  EXPECT_EQ(reloaded[0].record->form_key, ref);
  EXPECT_TRUE(reloaded[0].persistent);
  EXPECT_FLOAT_EQ(reloaded[0].transform.x, 4106.0f);
}

TEST(PlacedReferenceMoveExterior, RefusesAReferenceThatIsNotInACellBucket) {
  expect_move_refused(exterior_plugin({.bucket_group = false}));
}

TEST(PlacedReferenceMoveExterior, RefusesAReferenceThatIsNotBeneathACellChildGroup) {
  expect_move_refused(exterior_plugin({.children = fo4::GroupType::topic_children}));
}

TEST(PlacedReferenceMoveExterior, RefusesAReferenceWhoseCellRecordIsMissing) {
  expect_move_refused(exterior_plugin({.cell_record = false}));
}

TEST(PlacedReferenceMoveExterior, RefusesAnExteriorCellOutsideAWorldspace) {
  expect_move_refused(exterior_plugin({.owner = fo4::GroupType::exterior_cell_block}));
}

TEST(PlacedReferenceMoveExterior, RefusesAnExteriorCellWithNoWorldspaceRecord) {
  expect_move_refused(exterior_plugin({.worldspace_record = false}));
}

TEST(PlacedReferenceMoveExterior, RefusesAPersistentReferenceWithNoPersistentCellInTheWorldspace) {
  expect_move_refused_with_status(
      exterior_plugin({.reference_flags = 0x00000400u}), {.x = 4106.0f, .y = 20.0f, .z = 30.0f},
      fo4::forms::PlacedReferenceMoveStatus::target_cell_missing);
}

TEST(PlacedReferenceMoveExterior, RefusesAClearFlagRecordInAPersistentBucketWithNoGridCell) {
  expect_move_refused_with_status(
      exterior_plugin({.bucket = fo4::GroupType::cell_persistent_children}), {.x = 32768.0f},
      fo4::forms::PlacedReferenceMoveStatus::target_cell_missing);
}
