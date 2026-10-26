#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct ScalarRow {
  std::string_view path;
  std::size_t offset;
};

struct PnamRow {
  std::string_view path;
  std::size_t offset;
  std::string_view target;
};

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr std::size_t k_sped_floor = 112;

constexpr ScalarRow k_sped_rows[] = {
    {"MOVT\\LeftStanding", 0},    {"MOVT\\LeftWalk", 4},     {"MOVT\\LeftRun", 8},
    {"MOVT\\LeftSprint", 12},     {"MOVT\\RightStanding", 16}, {"MOVT\\RightWalk", 20},
    {"MOVT\\RightRun", 24},       {"MOVT\\RightSprint", 28}, {"MOVT\\ForwardStanding", 32},
    {"MOVT\\ForwardWalk", 36},    {"MOVT\\ForwardRun", 40},  {"MOVT\\ForwardSprint", 44},
    {"MOVT\\BackStanding", 48},   {"MOVT\\BackWalk", 52},    {"MOVT\\BackRun", 56},
    {"MOVT\\BackSprint", 60},     {"MOVT\\PitchStanding", 64}, {"MOVT\\PitchWalk", 68},
    {"MOVT\\PitchRun", 72},       {"MOVT\\PitchSprint", 76}, {"MOVT\\RollStanding", 80},
    {"MOVT\\RollWalk", 84},       {"MOVT\\RollRun", 88},     {"MOVT\\RollSprint", 92},
    {"MOVT\\YawStanding", 96},    {"MOVT\\YawWalk", 100},    {"MOVT\\YawRun", 104},
    {"MOVT\\YawSprint", 108},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes link_pair(std::uint32_t material, std::uint32_t impact) {
  ByteWriter out;
  out.u32(material);
  out.u32(impact);
  return out.bytes;
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes ramp(std::size_t count) {
  ByteWriter out;
  for (std::size_t i = 0; i < count; ++i) out.f32(static_cast<float>(i + 1));
  return out.bytes;
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

std::vector<std::byte> make_impact_plugin() {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "IPDS", 0);
  const auto record = fo4::test::begin_record(out, "IPDS", 0, raw_id(0x0C01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleImpact"));
  fo4::test::write_subrecord(out, "PNAM", link_pair(0x00000A10, 0x00000A11));
  fo4::test::write_subrecord(out, "PNAM", link_pair(0x01000A20, 0x01000A21));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_movt_plugin(const Bytes& sped, std::uint16_t form_version = 131) {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "MOVT", 0);
  const auto record = fo4::test::begin_record(out, "MOVT", 0, raw_id(0x0C02), form_version);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleMovement"));
  fo4::test::write_subrecord(out, "SPED", sped);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes subrecord_at(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord,
                   std::size_t ordinal) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != subrecord) continue;
    if (seen++ == ordinal) return sub.data;
  }
  ADD_FAILURE() << subrecord << ' ' << ordinal;
  return {};
}

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view path) {
  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MOVT"), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field->scalar), field->scalar.address);
  if (resolved.slots.empty() || !resolved.slots[0].valid) return {};
  const auto slot = resolved.slots[0];
  const auto bytes = fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot,
                                                    field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::uint32_t pnam_link_raw(const fo4::Plugin& plugin, std::string_view key, std::size_t offset,
                            std::size_t ordinal) {
  const auto record = fo4::FormKey::parse(key);
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == record && hit.subrecord.to_string() == "PNAM" && hit.offset == offset &&
        hit.subrecord_ordinal == ordinal) {
      return hit.raw.value();
    }
  }
  ADD_FAILURE() << key << ' ' << offset << ' ' << ordinal;
  return 0;
}

fo4::schema::SubrecordGroupDescriptor pnam_group() {
  const auto group = fo4::schema::subrecord_group_descriptor(
      fo4::Signature::from_string("IPDS"), "IPDS\\Data");
  EXPECT_TRUE(group.has_value());
  return group.value_or(fo4::schema::SubrecordGroupDescriptor{});
}

}  // namespace

TEST(IPDSRows, FormLinkRowsNameTheirPnamOffsetsAndTargets) {
  constexpr PnamRow rows[] = {{"IPDS\\Material", 0, "MATT"}, {"IPDS\\Impact", 4, "IPCT"}};
  for (const auto& row : rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("IPDS"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string("PNAM"));
    EXPECT_EQ(field->offset, row.offset);
    EXPECT_TRUE(field->address.repeated);
    EXPECT_FALSE(field->nullable);
    EXPECT_EQ(field->payload_sizes[0], 8u);
    const auto targets = fo4::schema::formlink_field_targets(*field);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fo4::Signature::from_string(row.target));
  }
}

TEST(IPDSRows, ReadsAndSetsPairsByOccurrenceThenRereads) {
  constexpr std::string_view key = "000C01:Patch.esp";
  auto plugin = fo4::read_plugin(make_impact_plugin(), "Patch.esp");
  EXPECT_EQ(pnam_link_raw(plugin, key, 0, 0), 0x00000A10u);
  EXPECT_EQ(pnam_link_raw(plugin, key, 4, 0), 0x00000A11u);
  EXPECT_EQ(pnam_link_raw(plugin, key, 0, 1), 0x01000A20u);
  EXPECT_EQ(pnam_link_raw(plugin, key, 4, 1), 0x01000A21u);

  const auto material =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("IPDS"), "IPDS\\Material");
  const auto impact =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("IPDS"), "IPDS\\Impact");
  ASSERT_TRUE(material.has_value());
  ASSERT_TRUE(impact.has_value());
  const auto record = fo4::FormKey::parse(key);
  EXPECT_EQ(fo4::record_field_count(plugin, record, *material), std::optional<std::size_t>{2});
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *material, 1,
                                           fo4::FormKey::parse("000A30:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *impact, 0,
                                           fo4::FormKey::parse("000A40:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *material, 2,
                                           fo4::FormKey::parse("000A30:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_at(reread, key, "PNAM", 0), link_pair(0x00000A10, 0x01000A40));
  EXPECT_EQ(subrecord_at(reread, key, "PNAM", 1), link_pair(0x01000A30, 0x01000A21));
  EXPECT_EQ(pnam_link_raw(reread, key, 4, 0), 0x01000A40u);
  EXPECT_EQ(pnam_link_raw(reread, key, 0, 1), 0x01000A30u);
}

TEST(IPDSRows, DataGroupHoldsOneEightBytePnamPerSpan) {
  const auto group = pnam_group();
  EXPECT_EQ(group.record, fo4::Signature::from_string("IPDS"));
  EXPECT_EQ(group.path, "IPDS\\Data");
  EXPECT_EQ(group.count_subrecord, fo4::Signature{});
  ASSERT_EQ(group.members.size(), 1u);
  EXPECT_EQ(group.members[0].subrecord, fo4::Signature::from_string("PNAM"));
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 8u);
  EXPECT_TRUE(group.members[0].default_payload.empty());

  constexpr std::string_view key = "000C01:Patch.esp";
  auto plugin = fo4::read_plugin(make_impact_plugin(), "Patch.esp");
  const auto spans = fo4::subrecord_group_spans(plugin, fo4::FormKey::parse(key), group);
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].first, 1u);
  EXPECT_EQ((*spans)[0].end, 2u);
  EXPECT_EQ((*spans)[1].first, 2u);
  EXPECT_EQ((*spans)[1].end, 3u);
}

TEST(IPDSRows, InsertsAPairFillsItThroughTheRowsAndRemovesIt) {
  constexpr std::string_view key = "000C01:Patch.esp";
  const auto record = fo4::FormKey::parse(key);
  const auto material =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("IPDS"), "IPDS\\Material");
  const auto impact =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("IPDS"), "IPDS\\Impact");
  ASSERT_TRUE(material.has_value());
  ASSERT_TRUE(impact.has_value());

  auto plugin = fo4::read_plugin(make_impact_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, record, pnam_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *material, 1,
                                           fo4::FormKey::parse("000A50:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *impact, 1,
                                           fo4::FormKey::parse("000A51:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(reread, record, *material), std::optional<std::size_t>{3});
  EXPECT_EQ(subrecord_at(reread, key, "PNAM", 0), link_pair(0x00000A10, 0x00000A11));
  EXPECT_EQ(subrecord_at(reread, key, "PNAM", 1), link_pair(0x01000A50, 0x01000A51));
  EXPECT_EQ(subrecord_at(reread, key, "PNAM", 2), link_pair(0x01000A20, 0x01000A21));
  EXPECT_EQ(pnam_link_raw(reread, key, 0, 1), 0x01000A50u);

  auto trimmed = fo4::read_plugin(fo4::write_plugin(reread), "Patch.esp");
  EXPECT_EQ(fo4::remove_record_subrecord_group(trimmed, record, pnam_group(), 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord_group(trimmed, record, pnam_group(), 2),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);

  auto after = fo4::read_plugin(fo4::write_plugin(trimmed), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(after, record, *material), std::optional<std::size_t>{2});
  EXPECT_EQ(subrecord_at(after, key, "PNAM", 0), link_pair(0x00000A10, 0x00000A11));
  EXPECT_EQ(subrecord_at(after, key, "PNAM", 1), link_pair(0x01000A20, 0x01000A21));
}

TEST(IPDSRows, ShortPnamFailsThePairRowsAndTheGroupClosed) {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, "IPDS", 0);
  const auto record = fo4::test::begin_record(out, "IPDS", 0, raw_id(0x0C01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleShortPnam"));
  fo4::test::write_subrecord(out, "PNAM", Bytes(4));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey::parse("000C01:Patch.esp");
  const auto material =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("IPDS"), "IPDS\\Material");
  ASSERT_TRUE(material.has_value());
  EXPECT_THROW(static_cast<void>(fo4::record_field_count(plugin, key, *material)),
               fo4::schema::FormLinkFieldError);
  EXPECT_THROW(static_cast<void>(fo4::subrecord_group_spans(plugin, key, pnam_group())),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(static_cast<void>(fo4::insert_record_subrecord_group(plugin, key, pnam_group(), 0)),
               fo4::schema::SubrecordGroupError);
}

TEST(MOVTRows, ScalarRowsHaveXEditOffsetsStorageAndTheSpedFloor) {
  for (const auto& row : k_sped_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MOVT"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("SPED"));
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, f32);
    EXPECT_EQ(field->scalar.minimum_payload_size, k_sped_floor);
    EXPECT_TRUE(field->scalar.embedded);
  }
}

TEST(MOVTRows, ReadsStandingWalkRunAndSprintFromA112ByteSped) {
  auto plugin = fo4::read_plugin(make_movt_plugin(ramp(28)), "Patch.esp");
  const std::string_view key = "000C02:Patch.esp";
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\LeftStanding"), floats({1.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\LeftSprint"), floats({4.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\RightWalk"), floats({6.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\BackRun"), floats({15.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\YawSprint"), floats({28.0f}));
}

TEST(MOVTRows, ShorterSpedPayloadsHideTheSpeedRows) {
  constexpr std::size_t sizes[] = {40, 44, 68};
  constexpr std::string_view key = "000C02:Patch.esp";
  for (const auto size : sizes) {
    SCOPED_TRACE(size);
    auto plugin = fo4::read_plugin(make_movt_plugin(ramp(size / 4)), "Patch.esp");
    for (const auto& row : k_sped_rows) {
      SCOPED_TRACE(row.path);
      EXPECT_TRUE(scalar_bytes(plugin, key, row.path).empty());
    }
  }
}

TEST(MOVTRows, SpedBelowFormVersion104StaysHidden) {
  auto plugin = fo4::read_plugin(make_movt_plugin(ramp(28), 100), "Patch.esp");
  const auto left =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MOVT"), "MOVT\\LeftStanding");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000C02:Patch.esp"), *left, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(MOVTRows, WritesSpeedRowAndRereads) {
  auto plugin = fo4::read_plugin(make_movt_plugin(ramp(28)), "Patch.esp");
  const auto run =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MOVT"), "MOVT\\RightRun");
  ASSERT_TRUE(run.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse("000C02:Patch.esp"), *run, "7.25"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const std::string_view key = "000C02:Patch.esp";
  EXPECT_EQ(scalar_bytes(reread, key, "MOVT\\RightRun"), floats({7.25f}));
  EXPECT_EQ(scalar_bytes(reread, key, "MOVT\\RightWalk"), floats({6.0f}));
  EXPECT_EQ(scalar_bytes(reread, key, "MOVT\\RightSprint"), floats({8.0f}));
  EXPECT_EQ(subrecord_at(reread, key, "SPED", 0).size(), k_sped_floor);
}

TEST(MOVTRows, LegacySpeedRowsCoverThe40And44ByteLayouts) {
  constexpr std::string_view key = "000C02:Patch.esp";
  const auto forty = fo4::read_plugin(make_movt_plugin(ramp(10), 24), "Patch.esp");
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\LeftWalkBelow60"), floats({1.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\LeftRunBelow60"), floats({2.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\RightWalkBelow60"), floats({3.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\RightRunBelow60"), floats({4.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\ForwardWalkBelow60"), floats({5.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\ForwardRunBelow60"), floats({6.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\BackWalkBelow60"), floats({7.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\BackRunBelow60"), floats({8.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\PitchRollYawWalkBelow60"), floats({9.0f}));
  EXPECT_EQ(scalar_bytes(forty, key, "MOVT\\PitchRollYawRunBelow60"), floats({10.0f}));
  EXPECT_TRUE(scalar_bytes(forty, key, "MOVT\\Unknown40Below60").empty());
  EXPECT_TRUE(scalar_bytes(forty, key, "MOVT\\LeftStanding").empty());

  const auto forty_four = fo4::read_plugin(make_movt_plugin(ramp(11), 31), "Patch.esp");
  EXPECT_EQ(scalar_bytes(forty_four, key, "MOVT\\PitchRollYawRunBelow60"), floats({10.0f}));
  EXPECT_EQ(scalar_bytes(forty_four, key, "MOVT\\Unknown40Below60"), floats({11.0f}));
}

TEST(MOVTRows, MiddleSpeedRowsCoverThe68ByteLayout) {
  constexpr std::string_view key = "000C02:Patch.esp";
  auto plugin = fo4::read_plugin(make_movt_plugin(ramp(17), 90), "Patch.esp");
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\LeftWalkFrom60"), floats({1.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\LeftRunFrom60"), floats({2.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\RightWalkFrom60"), floats({3.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\RightRunFrom60"), floats({4.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\ForwardWalkFrom60"), floats({5.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\ForwardRunFrom60"), floats({6.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\BackWalkFrom60"), floats({7.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\BackRunFrom60"), floats({8.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\PitchWalkFrom60"), floats({9.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\PitchRunFrom60"), floats({10.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\RollWalkFrom60"), floats({11.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\RollRunFrom60"), floats({12.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\YawWalkFrom60"), floats({13.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\YawRunFrom60"), floats({14.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\Unknown56From60"), floats({15.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\Unknown60From60"), floats({16.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "MOVT\\Unknown64From60"), floats({17.0f}));
  EXPECT_TRUE(scalar_bytes(plugin, key, "MOVT\\LeftWalkBelow60").empty());
  EXPECT_TRUE(scalar_bytes(plugin, key, "MOVT\\LeftStanding").empty());
}

TEST(MOVTRows, EachSpeedLayoutHidesOutsideItsFormVersionWindow) {
  constexpr std::string_view key = "000C02:Patch.esp";

  const auto late = fo4::read_plugin(make_movt_plugin(ramp(28), 131), "Patch.esp");
  EXPECT_EQ(scalar_bytes(late, key, "MOVT\\LeftStanding"), floats({1.0f}));
  EXPECT_TRUE(scalar_bytes(late, key, "MOVT\\LeftWalkBelow60").empty());
  EXPECT_TRUE(scalar_bytes(late, key, "MOVT\\LeftWalkFrom60").empty());

  const auto at104 = fo4::read_plugin(make_movt_plugin(ramp(28), 104), "Patch.esp");
  EXPECT_EQ(scalar_bytes(at104, key, "MOVT\\LeftStanding"), floats({1.0f}));
  EXPECT_TRUE(scalar_bytes(at104, key, "MOVT\\LeftWalkFrom60").empty());

  const auto at60 = fo4::read_plugin(make_movt_plugin(ramp(17), 60), "Patch.esp");
  EXPECT_EQ(scalar_bytes(at60, key, "MOVT\\LeftWalkFrom60"), floats({1.0f}));
  EXPECT_TRUE(scalar_bytes(at60, key, "MOVT\\LeftWalkBelow60").empty());

  const auto at59 = fo4::read_plugin(make_movt_plugin(ramp(11), 59), "Patch.esp");
  EXPECT_EQ(scalar_bytes(at59, key, "MOVT\\LeftWalkBelow60"), floats({1.0f}));
  EXPECT_TRUE(scalar_bytes(at59, key, "MOVT\\LeftWalkFrom60").empty());

  const auto oversized = fo4::read_plugin(make_movt_plugin(ramp(17), 24), "Patch.esp");
  EXPECT_EQ(scalar_bytes(oversized, key, "MOVT\\LeftWalkBelow60"), floats({1.0f}));
  EXPECT_TRUE(scalar_bytes(oversized, key, "MOVT\\LeftWalkFrom60").empty());
}

TEST(MOVTRows, WritesALegacySpeedRowAndRefusesTheOtherLayouts) {
  auto plugin = fo4::read_plugin(make_movt_plugin(ramp(10), 27), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C02:Patch.esp");
  const auto from60 =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MOVT"), "MOVT\\LeftWalkFrom60");
  const auto below60 = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("MOVT"),
                                                            "MOVT\\LeftWalkBelow60");
  ASSERT_TRUE(from60.has_value());
  ASSERT_TRUE(below60.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *from60, "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *below60, "3"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(scalar_bytes(reread, "000C02:Patch.esp", "MOVT\\LeftWalkBelow60"), floats({3.0f}));
  EXPECT_EQ(scalar_bytes(reread, "000C02:Patch.esp", "MOVT\\LeftRunBelow60"), floats({2.0f}));
  EXPECT_EQ(subrecord_at(reread, "000C02:Patch.esp", "SPED", 0).size(), std::size_t{40});
}
