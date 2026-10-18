#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000800:Patch.esp";

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes real(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "MSWP", 0);
  const auto record = fo4::test::begin_record(out, "MSWP", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleSwap"));
  fo4::test::write_subrecord(out, "FNAM", zstr("Props"));
  fo4::test::write_subrecord(out, "BNAM", zstr("Props\\Box.BGSM"));
  fo4::test::write_subrecord(out, "SNAM", zstr("Props\\BoxRust.BGSM"));
  fo4::test::write_subrecord(out, "CNAM", real(0.5f));
  fo4::test::write_subrecord(out, "BNAM", zstr("Props\\Lid.BGSM"));
  fo4::test::write_subrecord(out, "SNAM", zstr("Props\\LidRust.BGSM"));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes subrecord_at(const fo4::Plugin& plugin, std::string_view signature, std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) {
    ADD_FAILURE() << k_key;
    return {};
  }
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != signature) continue;
    if (seen++ == ordinal) return sub.data;
  }
  ADD_FAILURE() << signature << ' ' << ordinal;
  return {};
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MSWP"), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

std::string decoded(const fo4::Plugin& plugin, std::string_view path, std::size_t ordinal) {
  const auto field = row(path);
  const auto data = subrecord_at(plugin, field.scalar.subrecord.to_string(), ordinal);
  const auto bytes = fo4::schema::scalar_field_bytes(data, field.scalar);
  return fo4::schema::decode_scalar(bytes, field.scalar);
}

}  // namespace

TEST(MswpRows, RowsUseTheXEditSubrecordsAndStorage) {
  const auto bnam = row("MSWP\\OriginalMaterial");
  EXPECT_EQ(bnam.scalar.subrecord, fo4::Signature::from_string("BNAM"));
  EXPECT_EQ(bnam.scalar.storage, fo4::schema::ScalarStorage::zstring);
  EXPECT_FALSE(bnam.scalar.embedded);
  EXPECT_TRUE(bnam.scalar.address.repeated);

  const auto snam = row("MSWP\\ReplacementMaterial");
  EXPECT_EQ(snam.scalar.subrecord, fo4::Signature::from_string("SNAM"));
  EXPECT_EQ(snam.scalar.storage, fo4::schema::ScalarStorage::zstring);

  const auto cnam = row("MSWP\\ColorRemappingIndex");
  EXPECT_EQ(cnam.scalar.subrecord, fo4::Signature::from_string("CNAM"));
  EXPECT_EQ(cnam.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_FALSE(cnam.scalar.embedded);
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor("MSWP\\FNAM").has_value());
}

TEST(MswpRows, ReadsEverySubstitutionPairAndTheColorIndex) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  EXPECT_EQ(fo4::record_field_count(plugin, record, row("MSWP\\OriginalMaterial")),
            std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::record_field_count(plugin, record, row("MSWP\\ReplacementMaterial")),
            std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::record_field_count(plugin, record, row("MSWP\\ColorRemappingIndex")),
            std::optional<std::size_t>{1});

  EXPECT_EQ(decoded(plugin, "MSWP\\OriginalMaterial", 0), "Props\\Box.BGSM");
  EXPECT_EQ(decoded(plugin, "MSWP\\OriginalMaterial", 1), "Props\\Lid.BGSM");
  EXPECT_EQ(decoded(plugin, "MSWP\\ReplacementMaterial", 0), "Props\\BoxRust.BGSM");
  EXPECT_EQ(decoded(plugin, "MSWP\\ReplacementMaterial", 1), "Props\\LidRust.BGSM");
  EXPECT_EQ(decoded(plugin, "MSWP\\ColorRemappingIndex", 0), "0.5");
}

TEST(MswpRows, WritesOnePairAndLeavesItsNeighboursUntouched) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  EXPECT_EQ(fo4::set_record_string(plugin, record, row("MSWP\\ReplacementMaterial"), 1,
                                   "Props\\LidBlue.BGSM"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record, row("MSWP\\ColorRemappingIndex"), 0, "0.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record, row("MSWP\\OriginalMaterial"), 2,
                                   "Props\\Hinge.BGSM"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_string(plugin, record, row("MSWP\\OriginalMaterial"), 0, "Props\\Box.BGSM"),
            fo4::ScalarMutationStatus::unchanged);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_at(reread, "BNAM", 0), zstr("Props\\Box.BGSM"));
  EXPECT_EQ(subrecord_at(reread, "BNAM", 1), zstr("Props\\Lid.BGSM"));
  EXPECT_EQ(subrecord_at(reread, "SNAM", 0), zstr("Props\\BoxRust.BGSM"));
  EXPECT_EQ(subrecord_at(reread, "SNAM", 1), zstr("Props\\LidBlue.BGSM"));
  EXPECT_EQ(subrecord_at(reread, "CNAM", 0), real(0.25f));
  EXPECT_EQ(subrecord_at(reread, "FNAM", 0), zstr("Props"));
  EXPECT_EQ(subrecord_at(reread, "EDID", 0), zstr("RadicleSwap"));
}

TEST(MswpRows, RefusesANonStringSetterOnAStringRow) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record, row("MSWP\\OriginalMaterial"), 0, "1"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, record, row("MSWP\\ColorRemappingIndex"), 0, "0.5"),
               fo4::schema::ScalarError);
}

TEST(MswpRows, AuthoringCoversTheSubstitutionSubrecordsInOrder) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(k_key))) {
    EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path << ": " << issue.message;
  }
}
