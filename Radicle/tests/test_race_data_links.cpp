#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_race = "RACE";
constexpr auto k_key = "000800:Patch.esp";

struct LinkRow {
  std::string_view path;
  std::size_t offset;
  std::string_view target;
};

constexpr LinkRow k_link_rows[] = {
    {"RACE\\SeverableExplosion", 146, "EXPL"},
    {"RACE\\SeverableDebris", 150, "DEBR"},
    {"RACE\\SeverableImpactDataSet", 154, "IPDS"},
    {"RACE\\ExplodableExplosion", 158, "EXPL"},
    {"RACE\\ExplodableDebris", 162, "DEBR"},
    {"RACE\\ExplodableImpactDataSet", 166, "IPDS"},
    {"RACE\\OnCrippleExplosion", 176, "EXPL"},
    {"RACE\\OnCrippleDebris", 180, "DEBR"},
    {"RACE\\OnCrippleImpactDataSet", 184, "IPDS"},
    {"RACE\\ExplodableSubsegmentExplosion", 188, "EXPL"},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

void put(Bytes& data, std::size_t offset, const Bytes& value) {
  std::copy(value.begin(), value.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::uint32_t raw_at(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

Bytes race_data(std::size_t size) {
  Bytes data(200, std::byte{0});
  put(data, 32, u32_bytes(4));
  for (const auto& row : k_link_rows) {
    put(data, row.offset, u32_bytes(0x01000A20u + static_cast<std::uint32_t>(row.offset)));
  }
  data.resize(size);
  return data;
}

Bytes race_plugin(std::uint16_t form_version, const Bytes& data) {
  ByteWriter out;
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

  const auto group = fo4::test::begin_group(out, k_race, 0);
  const auto record = fo4::test::begin_record(out, k_race, 0, raw_id(0x0800), form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::FormLinkFieldDescriptor row(std::string_view path) {
  const auto descriptor = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string(k_race), path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return descriptor.value_or(fo4::schema::FormLinkFieldDescriptor{});
}

Bytes data_of(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) return {};
  for (const auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string("DATA")) return sub.data;
  }
  return {};
}

bool resolvable(const fo4::Plugin& plugin, std::string_view path) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_key));
  if (record == nullptr) return false;
  const auto field = row(path);
  return fo4::schema::formlink_field_at(
             fo4::schema::formlink_fields(fo4::Signature::from_string(k_race)), *record,
             field.subrecord, 0, field.offset)
      .has_value();
}

std::vector<std::size_t> race_data_link_offsets(const fo4::Plugin& plugin) {
  std::vector<std::size_t> offsets;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.subrecord != fo4::Signature::from_string("DATA")) continue;
    offsets.push_back(hit.offset);
  }
  std::sort(offsets.begin(), offsets.end());
  return offsets;
}

const auto k_target = fo4::FormKey::parse("000A20:Objects.esp");

}  // namespace

TEST(RaceDataLinks, RowsMatchXEditOffsetsTargetsAndVersionGate) {
  for (const auto& expected : k_link_rows) {
    SCOPED_TRACE(expected.path);
    const auto field = row(expected.path);
    EXPECT_EQ(field.subrecord, fo4::Signature::from_string("DATA"));
    EXPECT_EQ(field.offset, expected.offset);
    EXPECT_TRUE(field.nullable);
    EXPECT_EQ(field.payload_sizes[0], 200u);
    EXPECT_EQ(field.payload_sizes[1], 0u);
    EXPECT_EQ(field.min_form_version, 124);
    EXPECT_EQ(field.max_form_version, 0);
    const auto targets = fo4::schema::formlink_field_targets(field);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fo4::Signature::from_string(expected.target));
  }

  const auto severity = row("RACE\\SeverableExplosion");
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(severity, fo4::Signature::from_string("EXPL")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(severity, fo4::Signature::from_string("ARMO")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(severity, fo4::Signature::from_string("DEBR")));
}

TEST(RaceDataLinks, EveryDataLinkOffsetIsClaimedExactlyOnce) {
  auto plugin = fo4::read_plugin(race_plugin(131, race_data(200)), "Patch.esp");
  const auto offsets = race_data_link_offsets(plugin);
  std::vector<std::size_t> expected;
  for (const auto& link : k_link_rows) expected.push_back(link.offset);
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(offsets, expected);
}

TEST(RaceDataLinks, LinksAreHiddenBelowTheFormVersionGate) {
  for (const auto version : {std::uint16_t{109}, std::uint16_t{123}}) {
    auto plugin = fo4::read_plugin(race_plugin(version, race_data(196)), "Patch.esp");
    const auto before = data_of(plugin);
    for (const auto& expected : k_link_rows) {
      SCOPED_TRACE(expected.path);
      EXPECT_FALSE(resolvable(plugin, expected.path));
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_key),
                                               row(expected.path), k_target),
                fo4::FormLinkFieldMutationStatus::subrecord_missing);
    }
    EXPECT_EQ(data_of(plugin), before);
  }

  auto plugin = fo4::read_plugin(race_plugin(124, race_data(200)), "Patch.esp");
  for (const auto& expected : k_link_rows) {
    SCOPED_TRACE(expected.path);
    EXPECT_TRUE(resolvable(plugin, expected.path));
  }
}

TEST(RaceDataLinks, WritesEveryDataLinkThenRereads) {
  auto plugin = fo4::read_plugin(race_plugin(131, race_data(200)), "Patch.esp");
  for (const auto& expected : k_link_rows) {
    SCOPED_TRACE(expected.path);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_key),
                                             row(expected.path), k_target),
              fo4::FormLinkFieldMutationStatus::updated);
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_key),
                                             row(expected.path), k_target),
              fo4::FormLinkFieldMutationStatus::unchanged);
  }

  auto expected = race_data(200);
  for (const auto& link : k_link_rows) put(expected, link.offset, u32_bytes(0x01000A20u));

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(data_of(reread), expected);
  EXPECT_EQ(raw_at(data_of(reread), 146), 0x01000A20u);
  EXPECT_EQ(raw_at(data_of(reread), 188), 0x01000A20u);

  const auto null_key = fo4::FormKey{};
  EXPECT_EQ(fo4::set_record_formlink_field(reread, fo4::FormKey::parse(k_key),
                                           row("RACE\\OnCrippleDebris"), null_key),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(raw_at(data_of(reread), 180), 0u);
  EXPECT_EQ(raw_at(data_of(reread), 184), 0x01000A20u);
}

TEST(RaceDataLinks, RefusesAnUnexpectedDataPayloadSize) {
  auto plugin = fo4::read_plugin(race_plugin(131, race_data(196)), "Patch.esp");
  const auto before = data_of(plugin);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_key),
                                              row("RACE\\SeverableExplosion"), k_target),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(data_of(plugin), before);
}
