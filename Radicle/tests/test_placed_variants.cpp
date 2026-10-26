#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<std::string_view, 8> kVariants{
    "PARW", "PBAR", "PBEA", "PCON", "PFLA", "PGRE", "PHZD", "PMIS"};
constexpr std::uint32_t kCellLocal = 0x800;
constexpr std::uint32_t kAddnLocal = 0x900;
constexpr std::uint32_t kVariantLocal = 0x810;
constexpr std::uint32_t kReferenceLocal = 0x801;

fo4::FormKey local_key(std::uint32_t id) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{id}};
}

fo4::FormKey master_key(std::uint32_t id) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{id}};
}

std::string label_of(std::uint32_t value) {
  std::string out;
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
  return out;
}

void write_u32(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
  fo4::test::ByteWriter data;
  data.u32(value);
  fo4::test::write_subrecord(out, signature, data.bytes);
}

std::uint32_t subrecord_u32(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != fo4::Signature::from_string(signature)) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data(), sizeof(value));
    return value;
  }
  return 0;
}

void write_placed(fo4::test::ByteWriter& out,
                  std::string_view signature,
                  std::uint32_t local,
                  std::uint32_t base) {
  const auto at = fo4::test::begin_record(out, signature, 0, 0x01000000u | local);
  write_u32(out, "NAME", base);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Placed\0", 7});
  fo4::test::ByteWriter transform;
  for (const float value : {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.5f}) transform.f32(value);
  fo4::test::write_subrecord(out, "DATA", transform.bytes);
  fo4::test::ByteWriter enable;
  enable.u32(0);
  enable.u32(0);
  fo4::test::write_subrecord(out, "XESP", enable.bytes);
  fo4::test::ByteWriter owner;
  owner.u32(0x903);
  owner.u32(0);
  owner.u32(0);
  fo4::test::write_subrecord(out, "XOWN", owner.bytes);
  write_u32(out, "XHLT", 100);
  fo4::test::ByteWriter scale;
  scale.f32(2.0f);
  fo4::test::write_subrecord(out, "XSCL", scale.bytes);
  fo4::test::end_record(out, at);
}

void write_addn(fo4::test::ByteWriter& out) {
  const auto at = fo4::test::begin_record(out, "ADDN", 0, 0x01000000u | kAddnLocal);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Addon\0", 6});
  write_u32(out, "DATA", 284);
  write_u32(out, "SNAM", 0x905);
  write_u32(out, "LNAM", 0x906);
  fo4::test::ByteWriter flags;
  flags.u16(10);
  flags.u16(1);
  fo4::test::write_subrecord(out, "DNAM", flags.bytes);
  fo4::test::end_record(out, at);
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto addn_group = fo4::test::begin_group(out, "ADDN", 0);
  write_addn(out);
  fo4::test::end_group(out, addn_group);

  const auto cell_group = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x01000000u | kCellLocal);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Garden\0", 7});
  fo4::test::end_record(out, cell);
  const auto label = label_of(0x01000000u | kCellLocal);
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto persistent = fo4::test::begin_group(out, label, 8);
  write_placed(out, "REFR", kReferenceLocal, 0x900);
  fo4::test::end_group(out, persistent);
  const auto temporary = fo4::test::begin_group(out, label, 9);
  for (std::size_t i = 0; i < kVariants.size(); ++i) {
    write_placed(out, kVariants[i], kVariantLocal + static_cast<std::uint32_t>(i), 0x900);
  }
  fo4::test::end_group(out, temporary);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cell_group);
  return out.bytes;
}

}  // namespace

TEST(PlacedVariants, CellListingDecodesEveryVariantAsVariant) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto refs = fo4::forms::placed_references_for_cell(plugin, local_key(kCellLocal));
  ASSERT_EQ(refs.size(), 1 + kVariants.size());
  EXPECT_EQ(refs[0].kind, fo4::forms::PlacedReferenceKind::object);
  for (std::size_t i = 0; i < kVariants.size(); ++i) {
    const auto& ref = refs[i + 1];
    EXPECT_EQ(ref.kind, fo4::forms::PlacedReferenceKind::variant) << kVariants[i];
    ASSERT_TRUE(ref.base.has_value()) << kVariants[i];
    EXPECT_EQ(*ref.base, master_key(0x900)) << kVariants[i];
    EXPECT_EQ(ref.transform.x, 1.0f);
    EXPECT_EQ(ref.transform.y, 2.0f);
    EXPECT_EQ(ref.transform.z, 3.0f);
    EXPECT_EQ(ref.transform.rz, 0.5f);
    EXPECT_EQ(ref.scale, 2.0f);
    EXPECT_FALSE(ref.persistent);
  }
}

TEST(PlacedVariants, DecodeSkipsAddonNodeBecauseItIsNotAReference) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto* addn = fo4::find_record(plugin, local_key(kAddnLocal));
  ASSERT_NE(addn, nullptr);
  EXPECT_FALSE(fo4::forms::decode_placed_reference(plugin, *addn).has_value());
}

TEST(PlacedVariants, EveryScalarAndFormLinkRowReadsItsSubrecord) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  std::size_t scalar_rows = 0;
  std::size_t link_rows = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
      for (const auto& sub : record.subrecords) {
        if (sub.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(sub.data, field.scalar);
        EXPECT_FALSE(bytes.empty()) << field.path;
        static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
        ++scalar_rows;
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(record.header.signature)) {
      for (const auto& sub : record.subrecords) {
        if (sub.signature != field.subrecord) continue;
        EXPECT_GE(sub.data.size(), field.offset + 4) << field.path;
        ++link_rows;
      }
    }
  });
  // Constraint: the variant rows add the XESP pair, so a variant matches one row more than REFR.
  EXPECT_EQ(scalar_rows, kVariants.size() * 10u + 9u + 3u);
  EXPECT_EQ(link_rows, kVariants.size() * 3u + 2u + 2u);
}

TEST(PlacedVariants, EditsOneScalarAndOneLinkPerRecordAndRoundTrips) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (std::size_t i = 0; i < kVariants.size(); ++i) {
    const auto signature = fo4::Signature::from_string(kVariants[i]);
    const auto prefix = std::string(kVariants[i]);
    const auto key = local_key(kVariantLocal + static_cast<std::uint32_t>(i));
    const auto health = fo4::schema::scalar_field_descriptor(signature, prefix + "\\HealthPercent");
    const auto scale = fo4::schema::scalar_field_descriptor(signature, prefix + "\\Scale");
    const auto base = fo4::schema::formlink_field_descriptor(signature, prefix + "\\Base");
    const auto owner = fo4::schema::formlink_field_descriptor(signature, prefix + "\\Owner");
    ASSERT_TRUE(health.has_value() && scale.has_value() && base.has_value() && owner.has_value());
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *health, 0, "50"),
              fo4::ScalarMutationStatus::updated);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *scale, 0, "3.5"),
              fo4::ScalarMutationStatus::updated);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *base, 0, master_key(0x901)),
              fo4::FormLinkFieldMutationStatus::updated);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *owner, 0, master_key(0x902)),
              fo4::FormLinkFieldMutationStatus::updated);
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (std::size_t i = 0; i < kVariants.size(); ++i) {
    const auto* record = fo4::find_record(reread, local_key(kVariantLocal + static_cast<std::uint32_t>(i)));
    ASSERT_NE(record, nullptr) << kVariants[i];
    const auto view = fo4::forms::decode_placed_reference(reread, *record);
    ASSERT_TRUE(view.has_value()) << kVariants[i];
    EXPECT_EQ(view->kind, fo4::forms::PlacedReferenceKind::variant) << kVariants[i];
    ASSERT_TRUE(view->base.has_value()) << kVariants[i];
    EXPECT_EQ(*view->base, master_key(0x901)) << kVariants[i];
    EXPECT_EQ(view->scale, 3.5f) << kVariants[i];
    EXPECT_EQ(subrecord_u32(*record, "XHLT"), 50u) << kVariants[i];
    EXPECT_EQ(subrecord_u32(*record, "XOWN"), 0x902u) << kVariants[i];
  }
}

TEST(PlacedVariants, EditsAddonIndexAndSoundLinkAndRoundTrips) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto addn = fo4::Signature::from_string("ADDN");
  const auto index = fo4::schema::scalar_field_descriptor(addn, "ADDN\\Index");
  const auto sound = fo4::schema::formlink_field_descriptor(addn, "ADDN\\Sound");
  ASSERT_TRUE(index.has_value() && sound.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, local_key(kAddnLocal), *index, 0, "300"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, local_key(kAddnLocal), *sound, 0, master_key(0x907)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, local_key(kAddnLocal));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(subrecord_u32(*record, "DATA"), 300u);
  EXPECT_EQ(subrecord_u32(*record, "SNAM"), 0x907u);
}
