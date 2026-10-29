#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Subrecords = std::vector<std::pair<std::string_view, Bytes>>;

constexpr auto k_updated = fo4::ScalarMutationStatus::updated;
constexpr auto k_link_updated = fo4::FormLinkFieldMutationStatus::updated;

constexpr std::array<std::string_view, 7> k_batch{"STAT", "LIGH", "BNDS", "LAYR", "LGTM", "MSWP", "TXST"};

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

bool in_batch(fo4::Signature signature) {
  return std::any_of(k_batch.begin(), k_batch.end(),
                     [&](std::string_view name) { return signature == sig(name); });
}

Bytes u8le(std::uint8_t value) {
  fo4::test::ByteWriter out;
  out.u8(value);
  return out.bytes;
}

Bytes u16le(std::uint16_t value) {
  fo4::test::ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes u32le(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32le(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  fo4::test::ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

Bytes patched(Bytes data, std::size_t offset, const Bytes& value) {
  std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
  return data;
}

Bytes light_data() {
  return concat({u32le(0), u32le(0), u8le(255), u8le(200), u8le(100), u8le(0), u32le(8), f32le(1.0f), f32le(90.0f),
                 f32le(10.0f), f32le(0.0f), f32le(0.0f), f32le(0.0f), f32le(0.0f), f32le(0.0f), f32le(0.0f),
                 f32le(2.0f), f32le(0.5f), u32le(0), f32le(0.0f)});
}

void write_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  const Subrecords& subrecords) {
  const auto header = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& [name, data] : subrecords) fo4::test::write_subrecord(out, name, std::span<const std::byte>(data));
  fo4::test::end_record(out, header);
}

void write_group(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                 const Subrecords& subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  write_record(out, signature, form_id, subrecords);
  fo4::test::end_group(out, group);
}

Bytes make_plugin() {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  write_group(out, "STAT", 0x01000800,
              {{"MODL", zstr("Meshes\\Stat.nif")},
               {"DNAM", concat({f32le(90.0f), u32le(0), f32le(1.0f), f32le(1.0f)})}});
  write_group(out, "LIGH", 0x01000801,
              {{"DATA", light_data()}, {"FNAM", f32le(1.0f)}, {"NAM0", zstr("Textures\\Gobo.dds")}, {"LNAM", u32le(0)}});
  write_group(out, "BNDS", 0x01000802,
              {{"DNAM", concat({f32le(1.0f), u16le(4), u16le(0), f32le(1.0f), f32le(1.0f), f32le(1.0f), f32le(0.5f),
                                f32le(0.25f)})},
               {"TNAM", u32le(0)}});
  write_group(out, "LAYR", 0x01000803, {{"PNAM", u32le(0)}});
  write_group(out, "LGTM", 0x01000804, {{"DATA", Bytes(136)}, {"DALC", Bytes(32)}});
  write_group(out, "MSWP", 0x01000805, {{"BNAM", zstr("Materials\\A.bgsm")}, {"SNAM", zstr("Materials\\B.bgsm")}});
  write_group(out, "TXST", 0x01000806,
              {{"TX00", zstr("Textures\\Diffuse.dds")}, {"MNAM", zstr("Materials\\Test.bgsm")}, {"DODT", Bytes(36)},
               {"DNAM", u16le(1)}});
  return out.bytes;
}

Bytes payload(const fo4::Plugin& plugin, std::string_view key_text, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key_text));
  if (record == nullptr) return {};
  for (const auto& sub : record->subrecords) {
    if (sub.signature == sig(signature)) return sub.data;
  }
  return {};
}

}  // namespace

TEST(StaticLightSynthetic, EveryBatchRowDecodesOnItsRecord) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  std::size_t decoded = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (!in_batch(record.header.signature)) return;
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
      for (const auto& sub : record.subrecords) {
        if (sub.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(sub.data, field.scalar);
        if (bytes.empty()) continue;
        static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
        ++decoded;
      }
    }
  });
  EXPECT_GE(decoded, 59u);
}

TEST(StaticLightSynthetic, EditsOneFieldPerRecordAndRereadsIt) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000800:Patch.esp"),
                                   *fo4::schema::scalar_field_descriptor("STAT\\MaxAngle"), 0, "45"),
            k_updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000800:Patch.esp"),
                                           *fo4::schema::formlink_field_descriptor("STAT\\Material"),
                                           fo4::FormKey::parse("000123:Fallout4.esm")),
            k_link_updated);
  EXPECT_EQ(fo4::set_record_string(plugin, fo4::FormKey::parse("000800:Patch.esp"), "STAT\\Model", 0,
                                   "Meshes\\Other.nif"),
            k_updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000801:Patch.esp"),
                                   *fo4::schema::scalar_field_descriptor("LIGH\\Radius"), 0, "512"),
            k_updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000802:Patch.esp"),
                                   *fo4::schema::scalar_field_descriptor("BNDS\\DefaultSlices"), 0, "6"),
            k_updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000803:Patch.esp"),
                                           *fo4::schema::formlink_field_descriptor("LAYR\\Parent"),
                                           fo4::FormKey::parse("000123:Fallout4.esm")),
            k_link_updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000804:Patch.esp"),
                                   *fo4::schema::scalar_field_descriptor("LGTM\\FogPower"), 0, "2"),
            k_updated);
  EXPECT_EQ(fo4::set_record_string(plugin, fo4::FormKey::parse("000806:Patch.esp"), "TXST\\Diffuse", 0,
                                   "Textures\\New_d.dds"),
            k_updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, "000800:Patch.esp", "DNAM"),
            concat({f32le(45.0f), u32le(0x00000123), f32le(1.0f), f32le(1.0f)}));
  EXPECT_EQ(payload(reread, "000800:Patch.esp", "MODL"), zstr("Meshes\\Other.nif"));
  EXPECT_EQ(payload(reread, "000801:Patch.esp", "DATA"), patched(light_data(), 4, u32le(512)));
  EXPECT_EQ(payload(reread, "000802:Patch.esp", "DNAM"),
            patched(concat({f32le(1.0f), u16le(4), u16le(0), f32le(1.0f), f32le(1.0f), f32le(1.0f), f32le(0.5f),
                            f32le(0.25f)}),
                    4, u16le(6)));
  EXPECT_EQ(payload(reread, "000803:Patch.esp", "PNAM"), u32le(0x00000123));
  EXPECT_EQ(payload(reread, "000804:Patch.esp", "DATA"), patched(Bytes(136), 36, f32le(2.0f)));
  EXPECT_EQ(payload(reread, "000806:Patch.esp", "TX00"), zstr("Textures\\New_d.dds"));
}

TEST(StaticLightSynthetic, MaterialSwapArraysStayUntouchedAndFlatFnamHasNoRow) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor("MSWP\\FNAM").has_value());
  EXPECT_TRUE(fo4::schema::formlink_fields(sig("MSWP")).empty());
  EXPECT_EQ(payload(plugin, "000805:Patch.esp", "BNAM"), zstr("Materials\\A.bgsm"));
  EXPECT_EQ(payload(plugin, "000805:Patch.esp", "SNAM"), zstr("Materials\\B.bgsm"));
}
