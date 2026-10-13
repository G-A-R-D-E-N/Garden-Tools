#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
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

}  // namespace

TEST(PlacedReferenceCreate, PlacesTwoReferencesInPersistentAndTemporaryBucketsAndRoundTrips) {
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

  const auto persistent_ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 1.0f, .y = 2.0f, .z = 3.0f, .rx = 0.25f, .ry = -0.5f, .rz = 1.75f},
          .persistent = true,
      });
  const auto temporary_ref = fo4::forms::create_placed_reference(
      plugin,
      fo4::forms::PlacedReferenceCreation{
          .cell = cell,
          .base = base,
          .transform = {.x = 4.0f, .y = 5.0f, .z = 6.0f, .rx = 0.0f, .ry = 0.0f, .rz = 3.0f},
          .persistent = false,
      });
  EXPECT_EQ(persistent_ref, fo4::FormKey::parse("000902:Cells.esp"));
  EXPECT_EQ(temporary_ref, fo4::FormKey::parse("000903:Cells.esp"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cells.esp");
  const auto* subblock = find_group(reread, fo4::GroupType::exterior_cell_subblock, 0xFFFB0004u);
  ASSERT_NE(subblock, nullptr);
  ASSERT_EQ(subblock->children.size(), 2u);
  const auto* cell_record = std::get_if<fo4::Record>(&subblock->children[0]);
  ASSERT_NE(cell_record, nullptr);
  EXPECT_EQ(cell_record->form_key, cell);

  const auto* cell_children = std::get_if<fo4::Group>(&subblock->children[1]);
  ASSERT_NE(cell_children, nullptr);
  EXPECT_EQ(cell_children->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_children));
  EXPECT_EQ(cell_children->header.label_as_u32(), 0x00000901u);
  ASSERT_EQ(cell_children->children.size(), 2u);

  const auto* persistent_bucket = std::get_if<fo4::Group>(&cell_children->children[0]);
  ASSERT_NE(persistent_bucket, nullptr);
  EXPECT_EQ(persistent_bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_persistent_children));
  EXPECT_EQ(persistent_bucket->header.label_as_u32(), 0x00000901u);
  ASSERT_EQ(persistent_bucket->children.size(), 1u);
  const auto* persistent_record = std::get_if<fo4::Record>(&persistent_bucket->children[0]);
  ASSERT_NE(persistent_record, nullptr);
  EXPECT_EQ(persistent_record->form_key, persistent_ref);
  EXPECT_EQ(persistent_record->header.signature.to_string(), "REFR");
  EXPECT_TRUE(persistent_record->header.flags.is_persistent());

  const auto* temporary_bucket = std::get_if<fo4::Group>(&cell_children->children[1]);
  ASSERT_NE(temporary_bucket, nullptr);
  EXPECT_EQ(temporary_bucket->header.group_type,
            static_cast<std::int32_t>(fo4::GroupType::cell_temporary_children));
  ASSERT_EQ(temporary_bucket->children.size(), 1u);
  const auto* temporary_record = std::get_if<fo4::Record>(&temporary_bucket->children[0]);
  ASSERT_NE(temporary_record, nullptr);
  EXPECT_EQ(temporary_record->form_key, temporary_ref);
  EXPECT_FALSE(temporary_record->header.flags.is_persistent());

  const auto* data = subrecord_named(*persistent_record, "DATA");
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->data.size(), 24u);
  EXPECT_FLOAT_EQ(read_f32(data->data, 0), 1.0f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 12), 0.25f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 16), -0.5f);
  EXPECT_FLOAT_EQ(read_f32(data->data, 20), 1.75f);

  const auto refs = fo4::forms::placed_references_for_cell(reread, cell);
  ASSERT_EQ(refs.size(), 2u);
  EXPECT_TRUE(refs[0].persistent);
  EXPECT_FALSE(refs[1].persistent);
  ASSERT_TRUE(refs[0].base.has_value());
  EXPECT_EQ(*refs[0].base, base);
  EXPECT_FLOAT_EQ(refs[0].transform.x, 1.0f);
  EXPECT_FLOAT_EQ(refs[0].transform.rz, 1.75f);
  EXPECT_FLOAT_EQ(refs[1].transform.x, 4.0f);
  EXPECT_FLOAT_EQ(refs[1].transform.rz, 3.0f);
}

TEST(PlacedReferenceCreate, RefusesForeignCellsAndUnownedBasesWithoutMutation) {
  auto plugin = fo4::read_plugin(plugin_with_worldspace(), "Cells.esp");
  const auto world = fo4::FormKey::parse("000800:Cells.esp");
  const auto base = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  const auto cell = fo4::forms::create_exterior_cell(
      plugin,
      fo4::forms::ExteriorCellCreation{.worldspace = world, .x = 1, .y = 2});
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
