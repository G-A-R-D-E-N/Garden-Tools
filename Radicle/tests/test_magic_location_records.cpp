#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Sub {
  std::string_view signature;
  Bytes data;
};

struct Entry {
  std::uint32_t local_id;
  std::vector<Sub> subs;
};

struct Item {
  std::string_view signature;
  std::string_view key;
  std::string_view scalar_path;
  std::string_view scalar_value;
  std::string_view link_path;
  std::string_view link_subrecord;
  std::size_t link_offset;
  std::string_view target;
  std::uint32_t target_raw;
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

constexpr Item k_items[] = {
    {"MGEF", "000901:Patch.esp", "MGEF\\BaseCost", "2.5", "MGEF\\PerkToApply", "DATA", 136,
     "000911:Patch.esp", raw_id(0x911)},
    {"SPEL", "000902:Patch.esp", "SPEL\\ChargeTime", "1.5", "SPEL\\EquipmentType", "ETYP", 0,
     "000921:Patch.esp", raw_id(0x921)},
    {"LCTN", "000903:Patch.esp", "LCTN\\WorldLocationRadius", "120.5", "LCTN\\ParentLocation", "PNAM", 0,
     "000931:Patch.esp", raw_id(0x931)},
};

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes zeros(std::size_t size) { return Bytes(size); }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

void put(Bytes& data, std::size_t offset, const Bytes& value) {
  std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

// source:wbDefinitionsFO4.pas @93cc0bc LCTN ACPR/LCPR offset map: ref 0, world/cell 4, Grid Y 8, Grid X 10 in a 12-byte element.
Bytes persist_element(std::uint32_t ref, std::uint32_t world_or_cell, std::int16_t grid_y,
                      std::int16_t grid_x) {
  ByteWriter out;
  out.u32(ref);
  out.u32(world_or_cell);
  out.u16(static_cast<std::uint16_t>(grid_y));
  out.u16(static_cast<std::uint16_t>(grid_x));
  return out.bytes;
}

// source:wbDefinitionsFO4.pas @93cc0bc LCTN ACSR/LCSR offset map: type 0, ref 4, world/cell 8, Grid Y 12, Grid X 14 in a 16-byte element.
Bytes special_element(std::uint32_t type, std::uint32_t ref, std::uint32_t world_or_cell,
                      std::int16_t grid_y, std::int16_t grid_x) {
  ByteWriter out;
  out.u32(type);
  out.u32(ref);
  out.u32(world_or_cell);
  out.u16(static_cast<std::uint16_t>(grid_y));
  out.u16(static_cast<std::uint16_t>(grid_x));
  return out.bytes;
}

// source:wbDefinitionsFO4.pas @93cc0bc LCTN ACEP/LCEP offset map: ref 0, enable parent 4, flags 8, three unused bytes from 9.
Bytes enable_parent_element(std::uint32_t ref, std::uint32_t parent, std::uint8_t flags) {
  ByteWriter out;
  out.u32(ref);
  out.u32(parent);
  out.u8(flags);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

// source:wbDefinitionsFO4.pas @93cc0bc LCTN worldspace cell offset map: world at 0, packed (Grid Y, Grid X) pairs from 4.
Bytes worldspace_cells(std::uint32_t world, std::int16_t grid_y, std::int16_t grid_x) {
  ByteWriter out;
  out.u32(world);
  out.u16(static_cast<std::uint16_t>(grid_y));
  out.u16(static_cast<std::uint16_t>(grid_x));
  return out.bytes;
}

Bytes concat(const Bytes& first, const Bytes& second) {
  Bytes out = first;
  out.insert(out.end(), second.begin(), second.end());
  return out;
}

Bytes location_arrays() {
  return concat(persist_element(0x00000A10u, 0x00000A11u, 11, 12),
                persist_element(0x00000A12u, 0x00000A13u, -13, 14));
}

Bytes mgef_data() {
  Bytes data = zeros(152);
  put(data, 4, f32_bytes(1.0f));
  put(data, 136, u32_bytes(raw_id(0x910)));
  return data;
}

Bytes spel_spit() {
  ByteWriter out;
  out.u32(100);
  out.u32(0x20000u);
  out.u32(0);
  out.f32(1.0f);
  out.u32(0);
  out.u32(0);
  out.f32(2.0f);
  out.f32(3.0f);
  out.u32(0);
  return out.bytes;
}

void write_group(ByteWriter& out, std::string_view label, const std::vector<Entry>& entries) {
  const auto group = fo4::test::begin_group(out, label, 0);
  for (const auto& entry : entries) {
    const auto record = fo4::test::begin_record(out, label, 0, raw_id(entry.local_id));
    for (const auto& sub : entry.subs) {
      fo4::test::write_subrecord(out, sub.signature, sub.data);
    }
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", zeros(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MagicMaster.esp\0", 16});
  fo4::test::write_subrecord(out, "DATA", zeros(8));
  fo4::test::end_record(out, tes4);

  write_group(out, "PERK", {{0x910, {{"EDID", zstr("RadiclePerkA")}}},
                            {0x911, {{"EDID", zstr("RadiclePerkB")}}}});
  write_group(out, "EQUP", {{0x920, {{"EDID", zstr("RadicleEquipA")}}},
                            {0x921, {{"EDID", zstr("RadicleEquipB")}}}});
  write_group(out, "MGEF", {{0x901, {{"EDID", zstr("RadicleEffect")},
                                     {"DATA", mgef_data()},
                                     {"DNAM", zstr("An effect.")}}}});
  write_group(out, "SPEL", {{0x902, {{"EDID", zstr("RadicleSpell")},
                                     {"OBND", zeros(12)},
                                     {"ETYP", u32_bytes(raw_id(0x920))},
                                     {"DESC", zstr("A spell.")},
                                     {"SPIT", spel_spit()}}}});
  write_group(out, "LCTN", {{0x903, {{"EDID", zstr("RadicleLocation")},
                                     {"PNAM", u32_bytes(raw_id(0x930))},
                                     {"RNAM", f32_bytes(50.0f)},
                                     {"ANAM", f32_bytes(1.0f)}}},
                            {0x904, {{"EDID", zstr("RadicleLocationArrays")},
                                     {"ACPR", persist_element(0x00000A01u, 0x00000A02u, -3, 7)},
                                     {"LCPR", location_arrays()},
                                     {"ACUN", concat(concat(u32_bytes(0x00000A20u),
                                                            u32_bytes(0x00000A21u)),
                                                     u32_bytes(0x00000A22u))},
                                     {"LCUN", concat(concat(u32_bytes(0x00000A23u),
                                                            u32_bytes(0x00000A24u)),
                                                     u32_bytes(0))},
                                     {"ACSR", special_element(0x00000A30u, 0x00000A31u, 0x00000A32u, 21, 22)},
                                     {"LCSR", special_element(0x00000A33u, 0x00000A34u, 0x00000A35u, -21, 23)},
                                     {"RCSR", u32_bytes(0x00000A36u)},
                                     {"ACEC", worldspace_cells(0x00000A40u, 3, 4)},
                                     {"LCEC", worldspace_cells(0x00000A41u, 5, 6)},
                                     {"LCEC", worldspace_cells(0x00000A42u, 7, 8)},
                                     {"RCEC", worldspace_cells(0x00000A43u, 9, 10)},
                                     {"ACID", concat(u32_bytes(0x00000A50u), u32_bytes(0x00000A51u))},
                                     {"LCID", u32_bytes(0x00000A52u)},
                                     {"ACEP", enable_parent_element(0x00000A60u, 0x00000A61u, 0x02)},
                                     {"LCEP", concat(enable_parent_element(0x00000A62u, 0x00000A63u, 0x01),
                                                     enable_parent_element(0x00000A64u, 0x00000A65u, 0x00))}}},
                            {0x930, {{"EDID", zstr("RadicleParentA")}}},
                            {0x931, {{"EDID", zstr("RadicleParentB")}}}});
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(MagicLocationSynthetic, EveryScalarRowDecodesOnTheFixture) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr) << item.signature;
    std::size_t decoded = 0;
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      const auto* sub = find_subrecord(*record, field.scalar.subrecord);
      if (sub == nullptr) continue;
      const auto bytes = fo4::schema::scalar_field_bytes(sub->data, field.scalar);
      if (bytes.empty()) continue;
      EXPECT_NO_THROW(static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar)))
          << field.path;
      ++decoded;
    }
    EXPECT_GT(decoded, 0u) << item.signature;
  }
}

TEST(MagicLocationSynthetic, FlagsRowNamesComeFromXEdit) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("SPEL"),
                                                          "SPEL\\Flags");
  ASSERT_TRUE(field.has_value());
  const auto name = fo4::schema::scalar_value_name(field->scalar, 0x20000);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, "PC Start Spell");
}

TEST(MagicLocationSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record = fo4::FormKey::parse(item.key);
    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value()) << item.scalar_path;
    EXPECT_EQ(fo4::set_record_scalar(plugin, record, *scalar, item.scalar_value),
              fo4::ScalarMutationStatus::updated)
        << item.scalar_path;
    const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
    ASSERT_TRUE(link.has_value()) << item.link_path;
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *link, fo4::FormKey::parse(item.target)),
              fo4::FormLinkFieldMutationStatus::updated)
        << item.link_path;
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record_key = fo4::FormKey::parse(item.key);
    const auto* record = fo4::find_record(reread, record_key);
    ASSERT_NE(record, nullptr) << item.signature;

    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value());
    const auto* scalar_sub = find_subrecord(*record, scalar->scalar.subrecord);
    ASSERT_NE(scalar_sub, nullptr);
    const auto expected = fo4::schema::encode_scalar(item.scalar_value, scalar->scalar);
    const auto actual = fo4::schema::scalar_field_bytes(scalar_sub->data, scalar->scalar);
    EXPECT_EQ(Bytes(actual.begin(), actual.end()), expected) << item.scalar_path;

    const auto* link_sub = find_subrecord(*record, fo4::Signature::from_string(item.link_subrecord));
    ASSERT_NE(link_sub, nullptr) << item.link_subrecord;
    const auto offset = static_cast<std::ptrdiff_t>(item.link_offset);
    const Bytes stored(link_sub->data.begin() + offset, link_sub->data.begin() + offset + 4);
    EXPECT_EQ(stored, u32_bytes(item.target_raw)) << item.link_path;
  }
}

TEST(MagicLocationSynthetic, FixtureRecordsPassAuthoringPreflight) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto issues =
        fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(item.key));
    EXPECT_TRUE(issues.empty()) << item.signature;
  }
}

namespace {

const fo4::Signature k_lctn = fo4::Signature::from_string("LCTN");
const fo4::FormKey k_arrays = fo4::FormKey::parse("000904:Patch.esp");

struct LctnScalarRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  std::size_t stride;
};

struct LctnLinkRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  std::size_t stride;
  std::string_view allowed;
  std::string_view rejected;
  bool nullable;
};

constexpr auto k_i16 = fo4::schema::ScalarStorage::i16;
constexpr auto k_flags = fo4::schema::ScalarStorage::u8;

// source:wbDefinitionsFO4.pas @93cc0bc LCTN Grid Y offset 8 or 12 comes before Grid X offset 10 or 14.
constexpr LctnScalarRow k_array_scalar_rows[] = {
    {"LCTN\\AddedEnableParentFlags", "ACEP", 8, k_flags, 12},
    {"LCTN\\AddedPersistReferenceGridX", "ACPR", 10, k_i16, 12},
    {"LCTN\\AddedPersistReferenceGridY", "ACPR", 8, k_i16, 12},
    {"LCTN\\AddedSpecialReferenceGridX", "ACSR", 14, k_i16, 16},
    {"LCTN\\AddedSpecialReferenceGridY", "ACSR", 12, k_i16, 16},
    {"LCTN\\MasterEnableParentFlags", "LCEP", 8, k_flags, 12},
    {"LCTN\\MasterPersistReferenceGridX", "LCPR", 10, k_i16, 12},
    {"LCTN\\MasterPersistReferenceGridY", "LCPR", 8, k_i16, 12},
    {"LCTN\\MasterSpecialReferenceGridX", "LCSR", 14, k_i16, 16},
    {"LCTN\\MasterSpecialReferenceGridY", "LCSR", 12, k_i16, 16},
};

// source:wbDefinitionsFO4.pas @93cc0bc LCTN wbArrayS elements use stride and offset; ACE C/LCEC/RCEC are wbRArrayS, one subrecord per world.
constexpr LctnLinkRow k_array_link_rows[] = {
    {"LCTN\\AddedEnableParent", "ACEP", 4, 12, "ACHR", "LCRT", false},
    {"LCTN\\AddedEnableParentReference", "ACEP", 0, 12, "ACHR", "LCRT", false},
    {"LCTN\\AddedInitiallyDisabledReference", "ACID", 0, 4, "REFR", "LCRT", false},
    {"LCTN\\AddedPersistReference", "ACPR", 0, 12, "ACHR", "LCRT", false},
    {"LCTN\\AddedPersistReferenceWorldOrCell", "ACPR", 4, 12, "WRLD", "LCRT", false},
    {"LCTN\\AddedSpecialReference", "ACSR", 4, 16, "REFR", "LCRT", false},
    {"LCTN\\AddedSpecialReferenceType", "ACSR", 0, 16, "LCRT", "ACHR", false},
    {"LCTN\\AddedSpecialReferenceWorldOrCell", "ACSR", 8, 16, "CELL", "LCRT", false},
    {"LCTN\\AddedUniqueActor", "ACUN", 4, 12, "ACHR", "LCRT", false},
    {"LCTN\\AddedUniqueLocation", "ACUN", 8, 12, "LCTN", "LCRT", true},
    {"LCTN\\AddedUniqueNpc", "ACUN", 0, 12, "NPC_", "LCRT", false},
    {"LCTN\\AddedWorldspaceCellWorld", "ACEC", 0, 0, "WRLD", "CELL", false},
    {"LCTN\\MasterEnableParent", "LCEP", 4, 12, "REFR", "LCRT", false},
    {"LCTN\\MasterEnableParentReference", "LCEP", 0, 12, "ACHR", "LCRT", false},
    {"LCTN\\MasterInitiallyDisabledReference", "LCID", 0, 4, "PHZD", "LCRT", false},
    {"LCTN\\MasterPersistReference", "LCPR", 0, 12, "ACHR", "LCRT", false},
    {"LCTN\\MasterPersistReferenceWorldOrCell", "LCPR", 4, 12, "CELL", "LCRT", false},
    {"LCTN\\MasterSpecialReference", "LCSR", 4, 16, "PHZD", "LCRT", false},
    {"LCTN\\MasterSpecialReferenceType", "LCSR", 0, 16, "LCRT", "ACHR", false},
    {"LCTN\\MasterSpecialReferenceWorldOrCell", "LCSR", 8, 16, "WRLD", "LCRT", false},
    {"LCTN\\MasterUniqueActor", "LCUN", 4, 12, "ACHR", "LCRT", false},
    {"LCTN\\MasterUniqueLocation", "LCUN", 8, 12, "LCTN", "LCRT", true},
    {"LCTN\\MasterUniqueNpc", "LCUN", 0, 12, "NPC_", "LCRT", false},
    {"LCTN\\MasterWorldspaceCellWorld", "LCEC", 0, 0, "WRLD", "CELL", false},
    {"LCTN\\RemovedSpecialReference", "RCSR", 0, 4, "REFR", "LCRT", false},
    {"LCTN\\RemovedWorldspaceCellWorld", "RCEC", 0, 0, "WRLD", "CELL", false},
};

const fo4::Record& arrays_record(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, k_arrays);
  if (record == nullptr) throw std::runtime_error("missing LCTN array fixture record");
  return *record;
}

Bytes payload_of(const fo4::Record& record, std::string_view signature, std::size_t ordinal = 0) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& sub : record.subrecords) {
    if (sub.signature != wanted) continue;
    if (ordinal != 0) {
      ordinal -= 1;
      continue;
    }
    return Bytes(sub.data.begin(), sub.data.end());
  }
  return {};
}

std::optional<std::uint32_t> link_slot(const fo4::Record& record,
                                       const fo4::schema::FormLinkFieldDescriptor& field,
                                       std::size_t index) {
  fo4::schema::FieldShape shape;
  shape.subrecord = field.subrecord;
  shape.offset = field.offset;
  shape.width = sizeof(std::uint32_t);
  const auto resolved = fo4::schema::resolve_field_slots(record, shape, field.address);
  if (index >= resolved.slots.size()) return std::nullopt;
  const auto slot = resolved.slots[index];
  if (!slot.valid) return std::nullopt;
  const auto& data = record.subrecords[slot.subrecord].data;
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + slot.offset, sizeof(value));
  return value;
}

std::string scalar_text(const fo4::Record& record,
                        const fo4::schema::ScalarFieldDescriptor& field,
                        std::size_t index) {
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (index >= resolved.slots.size()) return {};
  const auto slot = resolved.slots[index];
  if (!slot.valid) return {};
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(LctnReferenceArrays, RowsCarryTheXEditLayoutAndTargetSet) {
  EXPECT_EQ(fo4::schema::scalar_fields(k_lctn).size(), 16u);
  EXPECT_EQ(fo4::schema::formlink_fields(k_lctn).size(), 30u);
  for (const auto& row : k_array_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(k_lctn, row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.address.stride, row.stride);
    EXPECT_FALSE(field->scalar.address.repeated);
    EXPECT_TRUE(field->scalar.embedded);
    EXPECT_EQ(field->scalar.payload_size, 0u);
  }
  for (const auto& row : k_array_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::formlink_field_descriptor(k_lctn, row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, row.offset);
    EXPECT_EQ(field->address.stride, row.stride);
    EXPECT_EQ(field->address.repeated, row.stride == 0);
    EXPECT_EQ(field->nullable, row.nullable);
    EXPECT_EQ(field->payload_sizes[0], 0u);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.allowed)));
    EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.rejected)));
  }
  const auto grid_y = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterPersistReferenceGridY");
  const auto grid_x = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterPersistReferenceGridX");
  ASSERT_TRUE(grid_y.has_value());
  ASSERT_TRUE(grid_x.has_value());
  EXPECT_EQ(grid_x->scalar.offset - grid_y->scalar.offset, 2u);
  const auto flags = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterEnableParentFlags");
  ASSERT_TRUE(flags.has_value());
  const auto opposite = fo4::schema::scalar_value_name(flags->scalar, 1);
  const auto pop_in = fo4::schema::scalar_value_name(flags->scalar, 2);
  ASSERT_TRUE(opposite.has_value());
  ASSERT_TRUE(pop_in.has_value());
  EXPECT_EQ(*opposite, "Set Enable State to Opposite of Parent");
  EXPECT_EQ(*pop_in, "Pop In");
  EXPECT_FALSE(fo4::schema::scalar_value_name(flags->scalar, 4).has_value());
}

TEST(LctnReferenceArrays, EachElementAddressesItsOwnSlot) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto& record = arrays_record(plugin);
  const auto persist = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterPersistReference");
  const auto grid_y = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterPersistReferenceGridY");
  const auto grid_x = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterPersistReferenceGridX");
  ASSERT_TRUE(persist.has_value());
  ASSERT_TRUE(grid_y.has_value());
  ASSERT_TRUE(grid_x.has_value());
  ASSERT_EQ(fo4::record_field_count(plugin, k_arrays, *persist), 2u);
  EXPECT_EQ(link_slot(record, *persist, 0), 0x00000A10u);
  EXPECT_EQ(link_slot(record, *persist, 1), 0x00000A12u);
  EXPECT_FALSE(link_slot(record, *persist, 2).has_value());
  EXPECT_EQ(scalar_text(record, *grid_y, 0), "11");
  EXPECT_EQ(scalar_text(record, *grid_y, 1), "-13");
  EXPECT_EQ(scalar_text(record, *grid_x, 0), "12");
  EXPECT_EQ(scalar_text(record, *grid_x, 1), "14");
  EXPECT_EQ(scalar_text(record, *grid_y, 2), "");
  const auto special_grid = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\AddedSpecialReferenceGridY");
  ASSERT_TRUE(special_grid.has_value());
  EXPECT_EQ(scalar_text(record, *special_grid, 0), "21");
  const auto master_flags = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterEnableParentFlags");
  const auto added_flags = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\AddedEnableParentFlags");
  ASSERT_TRUE(master_flags.has_value());
  ASSERT_TRUE(added_flags.has_value());
  EXPECT_EQ(scalar_text(record, *master_flags, 0), "1");
  EXPECT_EQ(scalar_text(record, *master_flags, 1), "0");
  EXPECT_EQ(scalar_text(record, *added_flags, 0), "2");
  const auto disabled = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\AddedInitiallyDisabledReference");
  ASSERT_TRUE(disabled.has_value());
  ASSERT_EQ(fo4::record_field_count(plugin, k_arrays, *disabled), 2u);
  EXPECT_EQ(link_slot(record, *disabled, 0), 0x00000A50u);
  EXPECT_EQ(link_slot(record, *disabled, 1), 0x00000A51u);
}

TEST(LctnReferenceArrays, WritingOneElementLeavesTheOtherBytesAlone) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto persist = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterPersistReference");
  const auto grid_x = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterPersistReferenceGridX");
  ASSERT_TRUE(persist.has_value());
  ASSERT_TRUE(grid_x.has_value());
  const auto before = payload_of(arrays_record(plugin), "LCPR");
  ASSERT_EQ(before.size(), 24u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *persist, 1,
                                           fo4::FormKey::parse("00000B00:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto after_ref = payload_of(arrays_record(plugin), "LCPR");
  ASSERT_EQ(after_ref.size(), 24u);
  for (std::size_t at = 0; at < 24; ++at) {
    if (at >= 12 && at < 16) continue;
    EXPECT_EQ(after_ref[at], before[at]) << "byte " << at;
  }
  EXPECT_EQ(link_slot(arrays_record(plugin), *persist, 0), 0x00000A10u);
  EXPECT_EQ(link_slot(arrays_record(plugin), *persist, 1), 0x00000B00u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, k_arrays, *grid_x, 1, "-7"),
            fo4::ScalarMutationStatus::updated);
  const auto after_grid = payload_of(arrays_record(plugin), "LCPR");
  ASSERT_EQ(after_grid.size(), 24u);
  EXPECT_EQ(after_grid[22], std::byte{0xF9});
  EXPECT_EQ(after_grid[23], std::byte{0xFF});
  for (std::size_t at = 0; at < 22; ++at) EXPECT_EQ(after_grid[at], after_ref[at]) << "byte " << at;
  EXPECT_EQ(scalar_text(arrays_record(plugin), *grid_x, 0), "12");
  EXPECT_EQ(scalar_text(arrays_record(plugin), *grid_x, 1), "-7");
  EXPECT_EQ(fo4::set_record_scalar(plugin, k_arrays, *grid_x, 2, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(LctnReferenceArrays, EveryArrayRowWritesItsOwnSlotAndRereads) {
  for (const auto& row : k_array_link_rows) {
    SCOPED_TRACE(row.path);
    auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
    const auto field = fo4::schema::formlink_field_descriptor(k_lctn, row.path);
    ASSERT_TRUE(field.has_value());
    const auto before = payload_of(arrays_record(plugin), row.subrecord);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *field, 0,
                                             fo4::FormKey::parse("00000B01:Fallout4.esm")),
              fo4::FormLinkFieldMutationStatus::updated);
    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto& written = arrays_record(reread);
    EXPECT_EQ(link_slot(written, *field, 0), 0x00000B01u);
    const auto after = payload_of(written, row.subrecord);
    ASSERT_EQ(after.size(), before.size());
    std::size_t changed = 0;
    for (std::size_t at = 0; at < after.size(); ++at) {
      if (after[at] != before[at]) ++changed;
    }
    EXPECT_EQ(changed, 4u);
  }
  for (const auto& row : k_array_scalar_rows) {
    SCOPED_TRACE(row.path);
    auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
    const auto field = fo4::schema::scalar_field_descriptor(k_lctn, row.path);
    ASSERT_TRUE(field.has_value());
    const auto before = payload_of(arrays_record(plugin), row.subrecord);
    const std::string value = row.storage == k_flags ? "3" : "-4";
    EXPECT_EQ(fo4::set_record_scalar(plugin, k_arrays, *field, 0, value),
              fo4::ScalarMutationStatus::updated);
    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto& written = arrays_record(reread);
    EXPECT_EQ(scalar_text(written, *field, 0), value);
    const auto after = payload_of(written, row.subrecord);
    ASSERT_EQ(after.size(), before.size());
    std::size_t changed = 0;
    for (std::size_t at = 0; at < after.size(); ++at) {
      if (after[at] != before[at]) ++changed;
    }
    EXPECT_EQ(changed, row.storage == k_flags ? 1u : 2u);
  }
}

TEST(LctnReferenceArrays, RepeatedWorldspaceCellsAddressOneSlotPerSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto lcec = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterWorldspaceCellWorld");
  const auto acec = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\AddedWorldspaceCellWorld");
  ASSERT_TRUE(lcec.has_value());
  ASSERT_TRUE(acec.has_value());
  EXPECT_TRUE(lcec->address.repeated);
  ASSERT_EQ(fo4::record_field_count(plugin, k_arrays, *lcec), 2u);
  ASSERT_EQ(fo4::record_field_count(plugin, k_arrays, *acec), 1u);
  EXPECT_EQ(link_slot(arrays_record(plugin), *lcec, 0), 0x00000A41u);
  EXPECT_EQ(link_slot(arrays_record(plugin), *lcec, 1), 0x00000A42u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *lcec, 1,
                                           fo4::FormKey::parse("00000B02:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto& written = arrays_record(reread);
  EXPECT_EQ(link_slot(written, *lcec, 0), 0x00000A41u);
  EXPECT_EQ(link_slot(written, *lcec, 1), 0x00000B02u);
  EXPECT_EQ(payload_of(written, "LCEC", 0), worldspace_cells(0x00000A41u, 5, 6));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *lcec, 2,
                                           fo4::FormKey::parse("00000B03:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
}

TEST(LctnReferenceArrays, NullIsRejectedWhereXEditNamesNoNullTarget) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto persist = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterPersistReference");
  const auto cell = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\AddedPersistReferenceWorldOrCell");
  const auto location = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterUniqueLocation");
  ASSERT_TRUE(persist.has_value());
  ASSERT_TRUE(cell.has_value());
  ASSERT_TRUE(location.has_value());
  const auto before = payload_of(arrays_record(plugin), "LCPR");
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(plugin, k_arrays, *persist, 0, fo4::FormKey{})),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(static_cast<void>(fo4::set_record_formlink_field(plugin, k_arrays, *cell, 0, fo4::FormKey{})),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(payload_of(arrays_record(plugin), "LCPR"), before);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *location, 0,
                                           fo4::FormKey::parse("00000B04:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *location, 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(link_slot(arrays_record(reread), *location, 0), 0u);
}

TEST(LctnReferenceArrays, RaggedArrayPayloadsHideEveryRow) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  fo4::visit_records(plugin, [](fo4::Record& record) {
    if (record.form_key.local().value() != 0x904u) return;
    for (auto& sub : record.subrecords) {
      if (sub.signature == fo4::Signature::from_string("LCPR")) sub.data.resize(18);
      if (sub.signature == fo4::Signature::from_string("LCSR")) sub.data.resize(20);
    }
  });
  const auto& record = arrays_record(plugin);
  const auto persist = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterPersistReference");
  const auto grid_y = fo4::schema::scalar_field_descriptor(k_lctn, "LCTN\\MasterPersistReferenceGridY");
  const auto special = fo4::schema::formlink_field_descriptor(k_lctn, "LCTN\\MasterSpecialReference");
  ASSERT_TRUE(persist.has_value());
  ASSERT_TRUE(grid_y.has_value());
  ASSERT_TRUE(special.has_value());
  EXPECT_EQ(fo4::record_field_count(plugin, k_arrays, *persist), 0u);
  EXPECT_EQ(scalar_text(record, *grid_y, 0), "");
  EXPECT_FALSE(link_slot(record, *persist, 0).has_value());
  const auto before = payload_of(record, "LCPR");
  EXPECT_EQ(fo4::set_record_scalar(plugin, k_arrays, *grid_y, 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *persist, 0,
                                           fo4::FormKey::parse("00000B00:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, k_arrays, *special, 0,
                                           fo4::FormKey::parse("00000B00:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  const auto& unchanged = arrays_record(plugin);
  EXPECT_EQ(payload_of(unchanged, "LCPR"), before);
  EXPECT_EQ(payload_of(unchanged, "LCSR").size(), 20u);
}
