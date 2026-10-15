#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
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

struct ScalarRow {
  std::string_view record;
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  std::size_t minimum_payload_size;
};

struct NoteRow {
  std::string_view path;
  std::string_view target;
  std::int64_t note_type;
};

constexpr ScalarRow k_scalar_rows[] = {
    {"IPCT", "IPCT\\DecalFlags", "DODT", 29, fo4::schema::ScalarStorage::u8, 0},
    {"IPCT", "IPCT\\DecalColorBlue", "DODT", 34, fo4::schema::ScalarStorage::u8, 0},
    {"KYWD", "KYWD\\ColorAlpha", "CNAM", 3, fo4::schema::ScalarStorage::u8, 0},
    {"LCRT", "LCRT\\ColorRed", "CNAM", 0, fo4::schema::ScalarStorage::u8, 0},
    {"LCTN", "LCTN\\ColorGreen", "CNAM", 1, fo4::schema::ScalarStorage::u8, 0},
    {"MATO", "MATO\\NormalDampener", "DATA", 28, fo4::schema::ScalarStorage::f32, 32},
    {"MATO", "MATO\\SinglePassColorRed", "DATA", 32, fo4::schema::ScalarStorage::f32, 36},
    {"MATO", "MATO\\SinglePassColorBlue", "DATA", 40, fo4::schema::ScalarStorage::f32, 44},
    {"MATO", "MATO\\SinglePass", "DATA", 44, fo4::schema::ScalarStorage::u32, 48},
};

constexpr NoteRow k_note_rows[] = {
    {"NOTE\\SoundTarget", "SNDR", 0},
    {"NOTE\\VoiceTarget", "SCEN", 1},
    {"NOTE\\TerminalTarget", "TERM", 3},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes payload(std::size_t size, std::initializer_list<std::pair<std::size_t, std::uint8_t>> set) {
  Bytes out(size);
  for (const auto& [offset, value] : set) out[offset] = std::byte{value};
  return out;
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

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

Bytes make_note_plugin(std::uint8_t note_type, const Bytes& snam) {
  ByteWriter out;
  write_header(out);
  write_group(out, "SCEN", {{0x0B01, {{"EDID", zstr("RadicleScene")}}}});
  std::vector<Sub> subs{{"EDID", zstr("RadicleNote")}, {"DNAM", Bytes{std::byte{note_type}}}};
  if (!snam.empty()) subs.push_back({"SNAM", snam});
  write_group(out, "NOTE", {{0x0B02, subs}});
  return out.bytes;
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);

  write_group(out, "IPCT", {{0x0A01, {{"EDID", zstr("RadicleImpact")},
                                       {"DODT", payload(36, {{28, 0x05}, {29, 0x03}, {32, 0x11},
                                                             {33, 0x22}, {34, 0x33}})}}}});
  write_group(out, "MATO", {{0x0A02, {{"EDID", zstr("RadicleMaterialA")},
                                       {"DATA", payload(32, {{30, 0x80}, {31, 0x3F}})}}},
                            {0x0A03, {{"EDID", zstr("RadicleMaterialB")},
                                       {"DATA", payload(48, {{44, 0x01}})}}}});
  write_group(out, "LIGH", {{0x0A04, {{"EDID", zstr("RadicleLight")},
                                       {"PTRN", u32_bytes(0x00000B10)}}}});
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view record, std::string_view key,
                   std::string_view path) {
  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

fo4::FormLinkFieldMutationStatus note_link(fo4::Plugin& plugin, std::string_view path,
                                           const fo4::FormKey& target) {
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("NOTE"), path);
  return fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000B02:Patch.esp"), field.value(),
                                        0, target);
}

std::size_t note_snam_hits(const fo4::Plugin& plugin) {
  const auto note = fo4::FormKey::parse("000B02:Patch.esp");
  std::size_t count = 0;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == note && hit.subrecord == fo4::Signature::from_string("SNAM")) ++count;
  }
  return count;
}

}  // namespace

TEST(WP9MiscRowsB, NewScalarRowsHaveXEditOffsetsStorageAndPayloadFloor) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(row.record), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.minimum_payload_size, row.minimum_payload_size);
    EXPECT_TRUE(field->scalar.embedded);
  }
}

TEST(WP9MiscRowsB, DecalAndMaterialRowsReadTheirPayloadBytes) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const Bytes red{std::byte{0x11}};
  const Bytes green{std::byte{0x22}};
  const Bytes blue{std::byte{0x33}};
  const Bytes flags{std::byte{0x03}};
  EXPECT_EQ(scalar_bytes(plugin, "IPCT", "000A01:Patch.esp", "IPCT\\DecalColorRed"), red);
  EXPECT_EQ(scalar_bytes(plugin, "IPCT", "000A01:Patch.esp", "IPCT\\DecalColorGreen"), green);
  EXPECT_EQ(scalar_bytes(plugin, "IPCT", "000A01:Patch.esp", "IPCT\\DecalColorBlue"), blue);
  EXPECT_EQ(scalar_bytes(plugin, "IPCT", "000A01:Patch.esp", "IPCT\\DecalFlags"), flags);

  const Bytes normal_dampener{std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F}};
  EXPECT_EQ(scalar_bytes(plugin, "MATO", "000A02:Patch.esp", "MATO\\NormalDampener"),
            normal_dampener);
  EXPECT_TRUE(scalar_bytes(plugin, "MATO", "000A02:Patch.esp", "MATO\\SinglePassColorRed").empty());
  EXPECT_TRUE(scalar_bytes(plugin, "MATO", "000A02:Patch.esp", "MATO\\SinglePass").empty());

  const Bytes single_pass{std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
  EXPECT_EQ(scalar_bytes(plugin, "MATO", "000A03:Patch.esp", "MATO\\SinglePass"), single_pass);
  EXPECT_EQ(scalar_bytes(plugin, "MATO", "000A03:Patch.esp", "MATO\\SinglePassColorRed").size(),
            4u);
}

TEST(WP9MiscRowsB, NoteTargetRowsAreGuardedByDnamType) {
  for (const auto& row : k_note_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("NOTE"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string("SNAM"));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_FALSE(field->nullable);
    EXPECT_EQ(field->address.when.subrecord, fo4::Signature::from_string("DNAM"));
    EXPECT_EQ(field->address.when.count, 1u);
    EXPECT_EQ(field->address.when.equals[0], row.note_type);
    EXPECT_FALSE(field->address.when.match_absent);
    const auto targets = fo4::schema::formlink_field_targets(*field);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fo4::Signature::from_string(row.target));
  }
}

TEST(WP9MiscRowsB, LightPreviewTransformRetargetsAndRejectsNull) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto field =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("LIGH"), "LIGH\\PreviewTransform");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->subrecord, fo4::Signature::from_string("PTRN"));
  EXPECT_FALSE(field->nullable);
  EXPECT_EQ(field->payload_sizes[0], 4u);
  const auto targets = fo4::schema::formlink_field_targets(*field);
  ASSERT_EQ(targets.size(), 1u);
  EXPECT_EQ(targets[0], fo4::Signature::from_string("TRNS"));

  const auto key = fo4::FormKey::parse("000A04:Patch.esp");
  EXPECT_EQ(sub_bytes(plugin, "000A04:Patch.esp", "PTRN"), u32_bytes(0x00000B10));
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *field, target),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000A04:Patch.esp", "PTRN"), u32_bytes(0x01000A20));

  const fo4::FormKey none{};
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, key, *field, none),
               fo4::schema::FormLinkFieldError);
}

TEST(WP9MiscRowsB, NoteSnamIsGuardedByDnamType) {
  auto voice_note = fo4::read_plugin(make_note_plugin(1, u32_bytes(raw_id(0x0B01))), "Patch.esp");
  const auto scene = fo4::FormKey::parse("000B01:Patch.esp");
  const auto other = fo4::FormKey::parse("000A20:Objects.esp");
  EXPECT_EQ(note_link(voice_note, "NOTE\\VoiceTarget", scene),
            fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_EQ(note_link(voice_note, "NOTE\\SoundTarget", other),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(note_link(voice_note, "NOTE\\TerminalTarget", other),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(note_snam_hits(voice_note), 1u);

  auto program_note = fo4::read_plugin(make_note_plugin(2, {}), "Patch.esp");
  EXPECT_EQ(note_link(program_note, "NOTE\\SoundTarget", other),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(note_link(program_note, "NOTE\\VoiceTarget", other),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(note_link(program_note, "NOTE\\TerminalTarget", other),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);
  EXPECT_EQ(note_snam_hits(program_note), 0u);
}
