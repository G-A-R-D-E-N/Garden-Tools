#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;
using Subs = std::vector<std::pair<std::string_view, Bytes>>;

constexpr const char* k_plugin = "Patch.esp";

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

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

Bytes slice(const Bytes& data, std::size_t offset, std::size_t size) {
  if (data.size() < offset + size) return {};
  return Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
               data.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

fo4::FormKey key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:%s", local, k_plugin);
  return fo4::FormKey::parse(text);
}

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                std::uint16_t form_version, const Subs& subs) {
  const auto record = fo4::test::begin_record(out, signature, 0, form_id, form_version);
  for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
}

ByteWriter build_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto astp_group = fo4::test::begin_group(out, "ASTP", 0);
  add_record(out, "ASTP", 0x801, 131, {{"EDID", zstr("Assoc")}, {"MPRT", zstr("Father")}, {"DATA", u32_bytes(0)}});
  fo4::test::end_group(out, astp_group);

  const auto lgtm_group = fo4::test::begin_group(out, "LGTM", 0);
  add_record(out, "LGTM", 0x802, 131, {{"EDID", zstr("Full")}, {"DATA", Bytes(136)}, {"DALC", Bytes(32)}});
  add_record(out, "LGTM", 0x803, 131, {{"EDID", zstr("Short")}, {"DATA", Bytes(92)}, {"DALC", Bytes(32)}});
  add_record(out, "LGTM", 0x804, 20, {{"EDID", zstr("Old")}, {"DATA", Bytes(136)}, {"DALC", Bytes(24)}});
  fo4::test::end_group(out, lgtm_group);

  const Subs parts{{"NAM0", u32_bytes(1)}, {"NAM0", u32_bytes(2)}, {"NAM1", zstr("a.nif")},
                   {"TNAM", u32_bytes(0)}};
  const auto hdpt_group = fo4::test::begin_group(out, "HDPT", 0);
  for (std::uint32_t id = 0x810; id <= 0x814; ++id) add_record(out, "HDPT", id, 131, parts);
  fo4::test::end_group(out, hdpt_group);
  return out;
}

std::vector<std::string> order(const fo4::Plugin& plugin, const fo4::FormKey& record_key) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, record_key);
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

Bytes payload(const fo4::Plugin& plugin, const fo4::FormKey& record_key, std::string_view name) {
  const auto* record = fo4::find_record(plugin, record_key);
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == sig(name)) return subrecord.data;
  }
  ADD_FAILURE() << name;
  return {};
}

bool authoring_clean(const fo4::Plugin& plugin, const fo4::FormKey& record_key) {
  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record_key)) {
    if (issue.severity == fo4::schema::PreflightSeverity::error) return false;
  }
  return true;
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view record, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(sig(record), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

}  // namespace

TEST(WP9SmallRecords, FormListAuthoringCreatesAnEmptyListAndRefusesLinks) {
  const auto out = build_plugin();
  auto plugin = fo4::read_plugin(out.bytes, k_plugin);
  const auto constraint = fo4::schema::record_authoring_constraint(sig("FLST"));
  ASSERT_TRUE(constraint.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 3u);
  EXPECT_EQ(rules[0].signature, sig("EDID"));
  EXPECT_EQ(rules[0].minimum, 1);
  EXPECT_EQ(rules[1].signature, sig("FULL"));
  EXPECT_EQ(rules[1].maximum, 1);
  EXPECT_EQ(rules[2].signature, sig("LNAM"));
  EXPECT_EQ(rules[2].maximum, 65535);
  EXPECT_TRUE(fo4::schema::is_localized_field(sig("FLST"), sig("FULL")));

  const auto created = fo4::create_record(plugin, sig("FLST"), "NewList");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, created.key), (std::vector<std::string>{"EDID"}));
  EXPECT_EQ(payload(plugin, created.key, "EDID"), zstr("NewList"));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("LNAM")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("FULL")),
            fo4::SubrecordAddStatus::no_default);
  EXPECT_TRUE(authoring_clean(plugin, created.key));
}

TEST(WP9SmallRecords, AssociationTypeCreatesFlagsAndRoundTripsItsTitles) {
  const auto out = build_plugin();
  auto plugin = fo4::read_plugin(out.bytes, k_plugin);
  const auto constraint = fo4::schema::record_authoring_constraint(sig("ASTP"));
  ASSERT_TRUE(constraint.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 6u);
  EXPECT_EQ(rules.back().signature, sig("DATA"));
  EXPECT_EQ(rules.back().minimum, 1);
  EXPECT_EQ(rules.back().default_bytes().size(), 4u);

  const auto created = fo4::create_record(plugin, sig("ASTP"), "NewAssociation");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, created.key), (std::vector<std::string>{"EDID", "DATA"}));
  EXPECT_EQ(payload(plugin, created.key, "DATA"), Bytes(4));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("MPRT")),
            fo4::SubrecordAddStatus::no_default);

  EXPECT_EQ(fo4::set_record_string(plugin, key(0x801), "ASTP\\MaleParentTitle", 0, "Sire"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x801), scalar("ASTP", "ASTP\\Flags"), 0, "1"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), k_plugin);
  EXPECT_EQ(payload(reread, key(0x801), "MPRT"), zstr("Sire"));
  EXPECT_EQ(payload(reread, key(0x801), "DATA"), u32_bytes(1));
  EXPECT_TRUE(authoring_clean(reread, key(0x801)));
  const auto flags = scalar("ASTP", "ASTP\\Flags");
  EXPECT_EQ(flags.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(flags.scalar.names.size(), 1u);
}

TEST(WP9SmallRecords, LightingTailRowsFollowFormVersionAndPayloadLength) {
  const auto out = build_plugin();
  auto plugin = fo4::read_plugin(out.bytes, k_plugin);
  const auto fog_max = scalar("LGTM", "LGTM\\FogMax");
  EXPECT_EQ(fog_max.scalar.offset, 76u);
  EXPECT_EQ(fog_max.scalar.min_form_version, 30);
  EXPECT_EQ(fog_max.scalar.minimum_payload_size, 80u);
  EXPECT_EQ(scalar("LGTM", "LGTM\\UnusedAmbientXPlusRed").scalar.min_form_version, 0);
  const auto fresnel = scalar("LGTM", "LGTM\\AmbientFresnelPower");
  EXPECT_EQ(fresnel.scalar.subrecord, sig("DALC"));
  EXPECT_EQ(fresnel.scalar.minimum_payload_size, 32u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x802), fog_max, 0, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x802), fresnel, 0, "1"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x803), scalar("LGTM", "LGTM\\FarHeightRange"), 0, "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x803), scalar("LGTM", "LGTM\\NearHeightRange"), 0, "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x803), scalar("LGTM", "LGTM\\FogFarColorRed"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x804), fog_max, 0, "2"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x804), scalar("LGTM", "LGTM\\AmbientSpecularRed"), 0, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x804), scalar("LGTM", "LGTM\\UnusedAmbientXPlusRed"), 0, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(0x804), scalar("LGTM", "LGTM\\AmbientXPlusRed"), 0, "9"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), k_plugin);
  EXPECT_EQ(slice(payload(reread, key(0x802), "DATA"), 76, 4), f32_bytes(2.0f));
  EXPECT_EQ(slice(payload(reread, key(0x802), "DALC"), 28, 4), f32_bytes(1.0f));
  EXPECT_EQ(slice(payload(reread, key(0x803), "DATA"), 72, 1), (Bytes{std::byte{5}}));
  EXPECT_EQ(payload(reread, key(0x803), "DATA").size(), 92u);
  EXPECT_EQ(slice(payload(reread, key(0x804), "DATA"), 40, 1), (Bytes{std::byte{9}}));
  EXPECT_EQ(slice(payload(reread, key(0x804), "DALC"), 0, 1), (Bytes{std::byte{9}}));
  EXPECT_EQ(payload(reread, key(0x804), "DALC").size(), 24u);
}

TEST(WP9SmallRecords, LightingTemplateCreatesWithAuthoredTailAndDalc) {
  const auto out = build_plugin();
  auto plugin = fo4::read_plugin(out.bytes, k_plugin);
  const auto created = fo4::create_record(plugin, sig("LGTM"), "NewLight");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  EXPECT_EQ(order(plugin, created.key), (std::vector<std::string>{"EDID", "DATA", "DALC"}));
  EXPECT_EQ(payload(plugin, created.key, "DATA").size(), 136u);
  EXPECT_EQ(payload(plugin, created.key, "DALC").size(), 32u);
  EXPECT_EQ(slice(payload(plugin, created.key, "DATA"), 28, 4), f32_bytes(1.0f));
  EXPECT_EQ(slice(payload(plugin, created.key, "DATA"), 96, 4), f32_bytes(10000.0f));
  EXPECT_EQ(slice(payload(plugin, created.key, "DALC"), 28, 4), f32_bytes(1.0f));
  EXPECT_TRUE(authoring_clean(plugin, created.key));
  EXPECT_EQ(fo4::set_record_scalar(plugin, created.key, scalar("LGTM", "LGTM\\FarHeightRange"), 0, "7"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), k_plugin);
  EXPECT_EQ(slice(payload(reread, created.key, "DATA"), 76, 4), f32_bytes(1.0f));
  EXPECT_EQ(slice(payload(reread, created.key, "DATA"), 132, 4), f32_bytes(7.0f));
  EXPECT_TRUE(authoring_clean(reread, created.key));
}

TEST(WP9SmallRecords, HeadPartPairEditsFollowTheExplicitEditableRules) {
  const auto out = build_plugin();
  auto plugin = fo4::read_plugin(out.bytes, k_plugin);
  constexpr std::uint8_t k_part_type[] = {0, 0, 0, 0};
  constexpr std::uint8_t k_part_file[] = {0};
  const fo4::schema::SubrecordAuthoringConstraint rules[] = {
      {sig("NAM0"), 0, 65535, k_part_type, 4, true, false},
      {sig("NAM1"), 0, 65535, k_part_file, 1, true, false},
      {sig("TNAM"), 0, 1, nullptr, 0, false, false},
  };
  const std::vector<std::string> imbalanced{"NAM0", "NAM0", "NAM1", "TNAM"};

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x810), sig("NAM0"), rules),
            fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(order(plugin, key(0x810)), imbalanced);

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(0x811), sig("NAM1"), rules),
            fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, key(0x811)),
            (std::vector<std::string>{"NAM0", "NAM0", "NAM1", "NAM1", "TNAM"}));

  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x812), sig("NAM0"), 0, rules),
            fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, key(0x812)), (std::vector<std::string>{"NAM0", "NAM1", "TNAM"}));

  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x813), sig("NAM1"), 0, rules),
            fo4::SubrecordRemoveStatus::unknown_order);
  EXPECT_EQ(order(plugin, key(0x813)), imbalanced);

  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(0x814), sig("TNAM"), 0, rules),
            fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, key(0x814)), (std::vector<std::string>{"NAM0", "NAM0", "NAM1"}));
}
