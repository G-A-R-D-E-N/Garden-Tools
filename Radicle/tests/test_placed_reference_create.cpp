#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

float read_f32(const std::vector<std::byte>& data, std::size_t offset) {
  return std::bit_cast<float>(read_u32(data, offset));
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

const fo4::Subrecord* subrecord_named(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig) return &subrecord;
  }
  return nullptr;
}

std::string placed_reference_refusal(fo4::Plugin& plugin,
                                     const fo4::forms::PlacedReferenceCreation& creation) {
  try {
    static_cast<void>(fo4::forms::create_placed_reference(plugin, creation));
  } catch (const fo4::forms::PlacedReferenceError& error) {
    return error.what();
  }
  return {};
}

void erase_cell_subrecords(fo4::Plugin& plugin, const fo4::FormKey& cell, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (record.form_key != cell) return;
    std::erase_if(record.subrecords,
                  [&](const fo4::Subrecord& subrecord) { return subrecord.signature == sig; });
  });
}

}  // namespace

TEST(PlacedReferenceCreate, PlacesTemporaryReferenceInsideExteriorCellGridAndRoundTrips) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = -33,
          .y = 32,
          .editor_id = std::string_view{"PlacedCell"},
      });
  ASSERT_EQ(base, fo4::FormKey::parse("000900:Cells.esp"));
  ASSERT_EQ(cell, fo4::FormKey::parse("000901:Cells.esp"));

  const auto temporary_ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = -135068.0f, .y = 131172.0f, .z = 6.0f, .rx = 0.25f, .ry = -0.5f, .rz = 3.0f},
          .persistent = false,
      });
  EXPECT_EQ(temporary_ref, fo4::FormKey::parse("000902:Cells.esp"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* subblock = find_group(reread, fo4::GroupType::exterior_cell_subblock, 0xFFFB0004u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 2u);
  const auto* cell_children = std::get_if<fo4::Group>(&subblock->children[1]);
  ASSERT_NE(cell_children, nullptr);
  EXPECT_EQ(cell_children->header.label_as_u32(), 0x00000901u);
  ASSERT_EQ(cell_children->children.size(), 1u);

  const auto* temporary_bucket = std::get_if<fo4::Group>(&cell_children->children[0]);
  ASSERT_NE(temporary_bucket, nullptr);
  EXPECT_EQ(temporary_bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  ASSERT_EQ(temporary_bucket->children.size(), 1u);
  const auto* temporary_record = std::get_if<fo4::Record>(&temporary_bucket->children[0]);
  ASSERT_NE(temporary_record, nullptr);
  EXPECT_EQ(temporary_record->form_key, temporary_ref);
  EXPECT_FALSE(temporary_record->header.flags.is_persistent());

  const auto* data = subrecord_named(*temporary_record, "DATA");
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->data.size(), 24u);
  EXPECT_FLOAT_EQ(read_f32(data->data, 0), -135068.0f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 4), 131172.0f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 12), 0.25f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 20), 3.0f);

  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_FALSE(refs[0].persistent);
  EXPECT_FLOAT_EQ(refs[0].transform.x, -135068.0f);
  EXPECT_FLOAT_EQ(refs[0].transform.rz, 3.0f);
}

TEST(PlacedReferenceCreate, RefusesPersistentAndOutOfGridReferencesInExteriorCellsWithoutMutation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = -33,
          .y = 32,
          .editor_id = std::nullopt});
  const auto before = fo4::write_plugin(plugin);

  const auto persistent_refusal = placed_reference_refusal(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = -135068.0f, .y = 131172.0f},
          .persistent = true,
      });
  EXPECT_NE(persistent_refusal.find("worldspace persistent cell"), std::string::npos);

  const auto outside_refusal = placed_reference_refusal(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 1.0f, .y = 2.0f, .z = 3.0f},
          .persistent = false,
      });
  EXPECT_NE(outside_refusal.find("outside its exterior CELL grid"), std::string::npos);

  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PlacedReferenceCreate, PlacesPersistentReferenceInInteriorCellAndRoundTrips) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "Interior");
  const auto persistent_ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 1.0f, .y = 2.0f, .z = 3.0f, .rx = 0.25f, .ry = -0.5f, .rz = 1.75f},
          .persistent = true,
      });
  EXPECT_EQ(persistent_ref, fo4::FormKey::parse("000902:Cells.esp"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* subblock = find_group(reread, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 2u);
  const auto* cell_children = std::get_if<fo4::Group>(&subblock->children[1]);
  ASSERT_NE(cell_children, nullptr);
  ASSERT_EQ(cell_children->children.size(), 1u);
  const auto* persistent_bucket = std::get_if<fo4::Group>(&cell_children->children[0]);
  ASSERT_NE(persistent_bucket, nullptr);
  EXPECT_EQ(persistent_bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  ASSERT_EQ(persistent_bucket->children.size(), 1u);
  const auto* persistent_record = std::get_if<fo4::Record>(&persistent_bucket->children[0]);
  ASSERT_NE(persistent_record, nullptr);
  EXPECT_EQ(persistent_record->form_key, persistent_ref);
  EXPECT_EQ(persistent_record->header.signature.to_string(), "REFR");
  EXPECT_TRUE(persistent_record->header.flags.is_persistent());

  const auto* data = subrecord_named(*persistent_record, "DATA");
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->data.size(), 24u);
  EXPECT_FLOAT_EQ(read_f32(data->data, 0), 1.0f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 12), 0.25f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 16), -0.5f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 20), 1.75f);

  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_TRUE(refs[0].persistent);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(*refs[0].base, base);
  EXPECT_FLOAT_EQ(refs[0].transform.rz, 1.75f);
}

TEST(PlacedReferenceCreate, PersistentBucketIsInsertedBeforeExistingTemporaryBucket) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "Interior");
  static_cast<void>(fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{.cell = cell, .base = base, .persistent = false}));
  static_cast<void>(fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{.cell = cell, .base = base, .persistent = true}));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* cell_children = find_group(reread, fo4::GroupType::cell_children, 0x00000901u);
  ASSERT_NE(cell_children, nullptr);
  ASSERT_EQ(cell_children->children.size(), 2u);
  const auto* persistent_bucket = std::get_if<fo4::Group>(&cell_children->children[0]);
  const auto* temporary_bucket = std::get_if<fo4::Group>(&cell_children->children[1]);
  ASSERT_NE(persistent_bucket, nullptr);
  ASSERT_NE(temporary_bucket, nullptr);
  EXPECT_EQ(persistent_bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  EXPECT_EQ(temporary_bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
}

TEST(PlacedReferenceCreate, InitiallyDisabledSetsRecordFlagAndDefaultsOff) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "Interior");
  const auto disabled = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .persistent = true,
          .initially_disabled = true,
      });
  const auto enabled = fo4::forms::create_placed_reference(
      plugin, fo4::forms::PlacedReferenceCreation{.cell = cell, .base = base});

  const auto* disabled_record = fo4::find_record(plugin, disabled);
  const auto* enabled_record = fo4::find_record(plugin, enabled);
  ASSERT_NE(disabled_record, nullptr);
  ASSERT_NE(enabled_record, nullptr);
  EXPECT_TRUE(disabled_record->header.flags.test(fo4::RecordFlags::initially_disabled));
  EXPECT_TRUE(disabled_record->header.flags.is_persistent());
  EXPECT_FALSE(enabled_record->header.flags.test(fo4::RecordFlags::initially_disabled));
  EXPECT_TRUE(enabled_record->header.flags.is_persistent());
}

TEST(PlacedReferenceCreate, RefusesForeignCellsAndUnownedBasesWithoutMutation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = 1,
          .y = 2,
          .editor_id = std::nullopt});
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{
                       .cell = fo4::FormKey::parse("000901:Master.esm"), .base = base})),
               fo4::forms::PlacedReferenceError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{.cell = world, .base = base})),
               fo4::forms::PlacedReferenceError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{
                       .cell = cell, .base = fo4::FormKey::parse("000010:Fallout4.esm")})),
               fo4::forms::PlacedReferenceError);

  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PlacedReferenceCreate, RefusesMasterCellWithOverrideHint) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto refusal = placed_reference_refusal(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = fo4::FormKey::parse("000901:Master.esm"), .base = base});
  EXPECT_NE(refusal.find("copy the cell as an override first"), std::string::npos);
}

TEST(PlacedReferenceCreate, RefusesLocalBaseThatIsNotInThisPlugin) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto cell = fo4::forms::create_interior_cell(plugin, "Interior");
  const auto before = fo4::write_plugin(plugin);
  const auto refusal = placed_reference_refusal(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell, .base = fo4::FormKey::parse("000905:Cells.esp")});
  EXPECT_NE(refusal.find("is not a record in this plugin"), std::string::npos);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PlacedReferenceCreate, RefusesCellWithoutDataWithoutMutation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = 1,
          .y = 2,
          .editor_id = std::nullopt});
  erase_cell_subrecords(plugin, cell, "DATA");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{.cell = cell, .base = base})),
               fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PlacedReferenceCreate, RefusesExteriorCellWithoutXclc) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{
          .worldspace = world,
          .x = 1,
          .y = 2,
          .editor_id = std::nullopt});
  erase_cell_subrecords(plugin, cell, "XCLC");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{.cell = cell, .base = base})),
               fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PlacedReferenceCreate, RefusesNonFiniteTransform) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "Interior");
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{
                       .cell = cell,
                       .base = base,
                       .transform = {.x = std::numeric_limits<float>::quiet_NaN()},
                   })),
               fo4::forms::PlacedReferenceError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_placed_reference(
                   plugin,
                   fo4::forms::PlacedReferenceCreation{
                       .cell = cell,
                       .base = base,
                       .transform = {.z = std::numeric_limits<float>::infinity()},
                   })),
               fo4::forms::PlacedReferenceError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(PlacedReferenceCreate, CreatesPlacedActorInInteriorCellAndRoundTrips) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("NPC_"));
  ASSERT_EQ(base, fo4::FormKey::parse("000900:Cells.esp"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "ActorCell");
  const auto actor = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 10.0f, .y = 20.0f, .z = 30.0f, .rx = 0.5f, .ry = 0.0f, .rz = 1.5f},
          .persistent = true,
          .kind = fo4::forms::PlacedReferenceKind::npc,
      });
  EXPECT_EQ(actor, fo4::FormKey::parse("000902:Cells.esp"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* subblock = find_group(reread, fo4::GroupType::interior_cell_subblock, 0u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 2u);
  const auto* cell_children = std::get_if<fo4::Group>(&subblock->children[1]);
  ASSERT_NE(cell_children, nullptr);
  ASSERT_EQ(cell_children->children.size(), 1u);
  const auto* bucket = std::get_if<fo4::Group>(&cell_children->children[0]);
  ASSERT_NE(bucket, nullptr);
  EXPECT_EQ(bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  ASSERT_EQ(bucket->children.size(), 1u);
  const auto* record = std::get_if<fo4::Record>(&bucket->children[0]);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature, fo4::Signature::from_string("ACHR"));
  EXPECT_EQ(record->form_key, actor);
  EXPECT_TRUE(record->header.flags.is_persistent());

  const auto* name = subrecord_named(*record, "NAME");
  ASSERT_NE(name, nullptr);
  ASSERT_EQ(name->data.size(), 4u);
  EXPECT_EQ(read_u32(name->data, 0), 0x00000900u);

  const auto* data = subrecord_named(*record, "DATA");
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->data.size(), 24u);
  EXPECT_FLOAT_EQ(read_f32(data->data, 0), 10.0f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 8), 30.0f);

  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 1u);
  EXPECT_EQ(refs[0].kind, fo4::forms::PlacedReferenceKind::npc);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(*refs[0].base, base);
  EXPECT_FLOAT_EQ(refs[0].transform.z, 30.0f);
}

TEST(PlacedReferenceCreate, RefusesVariantKindAndNonNpcBaseWithoutMutation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto actor_base = fo4::create_local_record(plugin, fo4::Signature::from_string("NPC_"));
  const auto object_base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_interior_cell(plugin, "Refusing");
  const auto before = fo4::write_plugin(plugin);

  const auto variant_refusal = placed_reference_refusal(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell, .base = actor_base, .kind = fo4::forms::PlacedReferenceKind::variant});
  EXPECT_NE(variant_refusal.find("variants cannot be created yet"), std::string::npos);

  const auto base_refusal = placed_reference_refusal(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell, .base = object_base, .kind = fo4::forms::PlacedReferenceKind::npc});
  EXPECT_NE(base_refusal.find("not an NPC_ record"), std::string::npos);

  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
