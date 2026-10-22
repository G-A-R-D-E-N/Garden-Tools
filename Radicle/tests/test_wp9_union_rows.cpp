#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::uint32_t k_localized_flag = 0x80;
constexpr std::string_view k_text = "000801:Patch.esp";
constexpr std::string_view k_count = "000802:Patch.esp";
constexpr std::string_view k_other = "000803:Patch.esp";
constexpr std::string_view k_scale = "000804:Patch.esp";
constexpr std::string_view k_string_id = "000805:Patch.esp";
constexpr std::string_view k_head = "000810:Patch.esp";
constexpr std::string_view k_action = "000820:Patch.esp";

struct Sub {
  std::string_view signature;
  Bytes data;
};

Bytes u32(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes i32(std::int32_t value) {
  fo4::test::ByteWriter out;
  out.i32(value);
  return out.bytes;
}

Bytes f32(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

void write_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  const std::vector<Sub>& subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& subrecord : subrecords) {
    fo4::test::write_subrecord(out, subrecord.signature, std::span<const std::byte>{subrecord.data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(2304);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  write_record(out, "GMST", 0x801, {{"EDID", zstr("sRadicleText")}, {"DATA", zstr("Hello")}});
  write_record(out, "GMST", 0x802, {{"EDID", zstr("iRadicleCount")}, {"DATA", i32(7)}});
  write_record(out, "GMST", 0x803, {{"EDID", zstr("xRadicleOther")}, {"DATA", i32(9)}});
  write_record(out, "GMST", 0x804, {{"EDID", zstr("fRadicleScale")}, {"DATA", f32(1.5f)}});
  write_record(out, "GMST", 0x805, {{"EDID", zstr("sRadicleStringId")}, {"DATA", u32(0x00000AB1)}});
  write_record(out, "HDPT", 0x810,
               {{"EDID", zstr("RadicleHead")},
                {"NAM0", u32(0)}, {"NAM1", zstr("Head/Race.nif")},
                {"NAM0", u32(1)}, {"NAM1", zstr("Head/Tri.tri")},
                {"NAM0", u32(2)}, {"NAM1", zstr("Head/Chargen.nif")}});
  write_record(out, "AACT", 0x820,
               {{"EDID", zstr("RadicleAction")},
                {"CNAM", Bytes{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255}}}});
  return out.bytes;
}

fo4::Plugin load(std::uint32_t tes4_flags) {
  return fo4::read_plugin(make_plugin(tes4_flags), "Patch.esp");
}

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown scalar row " + std::string(path));
  return *field;
}

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::string_view record_key,
                            std::string_view signature) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(record_key));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature.to_string() == signature) out.push_back(subrecord.data);
  }
  return out;
}

bool slot_valid(const fo4::Plugin& plugin, std::string_view record_key, std::string_view path) {
  const auto field = scalar(path);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(record_key));
  return fo4::schema::resolve_field_slots(*record, fo4::schema::scalar_field_shape(field.scalar),
                                          field.scalar.address)
      .slots.at(0)
      .valid;
}

std::string text_value(const fo4::Plugin& plugin, std::string_view record_key, std::string_view path) {
  const auto field = scalar(path);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(record_key));
  const auto resolved = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto& slot = resolved.slots.at(0);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

}  // namespace

TEST(WP9UnionRows, GameSettingDataFollowsTheXEditDecider) {
  auto plugin = load(0);
  EXPECT_EQ(fo4::set_record_string(plugin, fo4::FormKey::parse(k_text), "GMST\\StringValue", 0, "World"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_count), scalar("GMST\\IntegerValue"), 0, "8"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_other), scalar("GMST\\IntegerValue"), 0, "-4"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_text), scalar("GMST\\IntegerValue"), 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_scale), scalar("GMST\\FloatValue"), 0, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, fo4::FormKey::parse(k_count), "GMST\\StringValue", 0, "x"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads(reread, k_text, "DATA"), std::vector<Bytes>{zstr("World")});
  EXPECT_EQ(payloads(reread, k_count, "DATA"), std::vector<Bytes>{i32(8)});
  EXPECT_EQ(payloads(reread, k_other, "DATA"), std::vector<Bytes>{i32(-4)});
  EXPECT_EQ(payloads(reread, k_scale, "DATA"), std::vector<Bytes>{f32(2.5f)});
  EXPECT_TRUE(slot_valid(reread, k_text, "GMST\\StringValue"));
  EXPECT_FALSE(slot_valid(reread, k_text, "GMST\\IntegerValue"));
  EXPECT_TRUE(slot_valid(reread, k_count, "GMST\\IntegerValue"));
  EXPECT_TRUE(slot_valid(reread, k_other, "GMST\\IntegerValue"));
  EXPECT_FALSE(slot_valid(reread, k_other, "GMST\\StringValue"));
  EXPECT_FALSE(slot_valid(reread, k_other, "GMST\\FloatValue"));
  EXPECT_TRUE(slot_valid(reread, k_scale, "GMST\\FloatValue"));
  EXPECT_FALSE(slot_valid(reread, k_scale, "GMST\\IntegerValue"));
}

TEST(WP9UnionRows, GuardFailingTextPayloadIsNotAWidthError) {
  auto plugin = load(0);
  const auto text_key = fo4::FormKey::parse(k_text);
  for (const auto path : {"GMST\\IntegerValue", "GMST\\FloatValue", "GMST\\BooleanValue"}) {
    SCOPED_TRACE(path);
    const auto field = scalar(path);
    const auto* record = fo4::find_record(plugin, text_key);
    ASSERT_NE(record, nullptr);
    const auto resolved = fo4::schema::resolve_field_slots(
        *record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
    EXPECT_FALSE(resolved.malformed);
    EXPECT_FALSE(resolved.misaligned);
    EXPECT_FALSE(slot_valid(plugin, k_text, path));
    EXPECT_EQ(fo4::record_field_count(plugin, text_key, path), 1u);
    EXPECT_EQ(fo4::set_record_scalar(plugin, text_key, field, 0, "1"),
              fo4::ScalarMutationStatus::subrecord_missing);
  }
  EXPECT_EQ(text_value(plugin, k_text, "GMST\\StringValue"), "Hello");
  EXPECT_EQ(fo4::set_record_string(plugin, text_key, "GMST\\StringValue", 0, "Bye"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(text_value(reread, k_text, "GMST\\StringValue"), "Bye");
  for (const auto& issue : fo4::schema::preflight_plugin(reread)) {
    EXPECT_NE(issue.path, "GMST\\IntegerValue") << issue.code;
    EXPECT_NE(issue.path, "GMST\\FloatValue") << issue.code;
    EXPECT_NE(issue.path, "GMST\\BooleanValue") << issue.code;
  }
}

TEST(WP9UnionRows, LocalizedGameSettingStringIsRefusedAndNotReadAsText) {
  auto plugin = load(k_localized_flag);
  ASSERT_TRUE(plugin.header.tes4_flags.is_localized());
  EXPECT_TRUE(slot_valid(plugin, k_string_id, "GMST\\StringValue"));
  EXPECT_THROW(static_cast<void>(fo4::set_record_string(plugin, fo4::FormKey::parse(k_string_id),
                                                        "GMST\\StringValue", 0, "World")),
               fo4::schema::ScalarError);
  EXPECT_EQ(payloads(plugin, k_string_id, "DATA"), std::vector<Bytes>{u32(0x00000AB1)});
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_count), scalar("GMST\\IntegerValue"), 0, "8"),
            fo4::ScalarMutationStatus::updated);
  for (const auto& issue : fo4::schema::preflight_plugin(plugin)) {
    EXPECT_NE(issue.path, "GMST\\StringValue") << issue.code;
  }
}

TEST(WP9UnionRows, HeadPartPairsEditByIndex) {
  auto plugin = load(0);
  EXPECT_EQ(fo4::record_field_count(plugin, fo4::FormKey::parse(k_head), "HDPT\\PartType"), 3u);
  EXPECT_EQ(fo4::record_field_count(plugin, fo4::FormKey::parse(k_head), "HDPT\\PartFilename"), 3u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_head), scalar("HDPT\\PartType"), 1, "2"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, fo4::FormKey::parse(k_head), "HDPT\\PartFilename", 1, "Head/Edited.tri"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads(reread, k_head, "NAM0"), (std::vector<Bytes>{u32(0), u32(2), u32(2)}));
  EXPECT_EQ(payloads(reread, k_head, "NAM1"),
            (std::vector<Bytes>{zstr("Head/Race.nif"), zstr("Head/Edited.tri"), zstr("Head/Chargen.nif")}));
}

TEST(WP9UnionRows, ActionColorChannelsReadAndWriteTheirBytes) {
  auto plugin = load(0);
  EXPECT_TRUE(slot_valid(plugin, k_action, "AACT\\ColorBlue"));
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_action), scalar("AACT\\ColorRed"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_action), scalar("AACT\\ColorAlpha"), 0, "128"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads(reread, k_action, "CNAM"),
            (std::vector<Bytes>{Bytes{std::byte{5}, std::byte{20}, std::byte{30}, std::byte{128}}}));
  EXPECT_TRUE(slot_valid(reread, k_action, "AACT\\ColorGreen"));
}
