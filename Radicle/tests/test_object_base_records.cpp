#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "authoring_order.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
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
  std::string_view text_path;
  std::string_view link_path;
  std::uint32_t link_start;
  std::string_view link_target;
};

constexpr Item k_items[] = {
    {"ACTI", "000801:Patch.esp", "ACTI\\BoundMaxX", "7", "ACTI\\Model", "ACTI\\LoopingSound",
     0x800, "000808:Patch.esp"},
    {"CONT", "000802:Patch.esp", "CONT\\Weight", "2.5", "CONT\\Model", "CONT\\OpenSound", 0x800,
     "000808:Patch.esp"},
    {"DOOR", "000803:Patch.esp", "DOOR\\Flags", "2", "DOOR\\Model", "DOOR\\OpenSound", 0x800,
     "000808:Patch.esp"},
    {"FURN", "000804:Patch.esp", "FURN\\BenchType", "7", "FURN\\Model", "FURN\\PreviewTransform",
     0x809, "00080A:Patch.esp"},
    {"MSTT", "000805:Patch.esp", "MSTT\\OnLocalMap", "0", "MSTT\\Model", "MSTT\\LoopingSound",
     0x800, "000808:Patch.esp"},
    {"TACT", "000806:Patch.esp", "TACT\\BoundMinX", "-4", "TACT\\Model", "TACT\\LoopingSound",
     0x800, "000808:Patch.esp"},
    {"TERM", "000807:Patch.esp", "TERM\\Flags", "4", "TERM\\Model", "TERM\\PreviewTransform",
     0x809, "00080A:Patch.esp"},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

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

Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes f32_bytes(float value) {
  ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes u8_bytes(std::uint8_t value) {
  ByteWriter out;
  out.u8(value);
  return out.bytes;
}

Bytes container_data() {
  ByteWriter out;
  out.u8(1);
  out.f32(1.5f);
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

  write_group(out, "SNDR", {{0x800, {{"EDID", zstr("RadicleSoundA")}}},
                            {0x808, {{"EDID", zstr("RadicleSoundB")}}}});
  write_group(out, "TRNS", {{0x809, {{"EDID", zstr("RadicleTransformA")}}},
                            {0x80A, {{"EDID", zstr("RadicleTransformB")}}}});
  write_group(out, "ACTI", {{0x801, {{"EDID", zstr("RadicleActi")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Light.nif")},
                                     {"SNAM", u32_bytes(raw_id(0x800))},
                                     {"FNAM", u16_bytes(1)}}}});
  write_group(out, "CONT", {{0x802, {{"EDID", zstr("RadicleCont")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Crate.nif")},
                                     {"MODC", f32_bytes(0.25f)},
                                     {"MODF", u8_bytes(2)},
                                     {"DATA", container_data()},
                                     {"SNAM", u32_bytes(raw_id(0x800))}}}});
  write_group(out, "DOOR", {{0x803, {{"EDID", zstr("RadicleDoor")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Door.nif")},
                                     {"MODC", f32_bytes(2.5f)},
                                     {"MODF", u8_bytes(1)},
                                     {"SNAM", u32_bytes(raw_id(0x800))},
                                     {"FNAM", u8_bytes(1)}}}});
  write_group(out, "FURN", {{0x804, {{"EDID", zstr("RadicleFurn")},
                                     {"OBND", zeros(12)},
                                     {"PTRN", u32_bytes(raw_id(0x809))},
                                     {"MODL", zstr("Bench.nif")},
                                     {"PNAM", zeros(4)},
                                     {"FNAM", u16_bytes(2)},
                                     {"WBDT", u8_bytes(1)}}}});
  write_group(out, "MSTT", {{0x805, {{"EDID", zstr("RadicleMstt")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Pylon.nif")},
                                     {"DATA", u8_bytes(1)},
                                     {"SNAM", u32_bytes(raw_id(0x800))}}}});
  write_group(out, "TACT", {{0x806, {{"EDID", zstr("RadicleTact")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Talk.nif")},
                                     {"PNAM", zeros(4)},
                                     {"SNAM", u32_bytes(raw_id(0x800))},
                                     {"FNAM", zeros(2)}}}});
  write_group(out, "TERM", {{0x807, {{"EDID", zstr("RadicleTerm")},
                                     {"OBND", zeros(12)},
                                     {"PTRN", u32_bytes(raw_id(0x809))},
                                     {"MODL", zstr("Terminal.nif")},
                                     {"FNAM", u16_bytes(1)}}}});
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

std::size_t rule_index(std::span<const fo4::schema::SubrecordAuthoringConstraint> rules,
                       std::string_view name) {
  const auto signature = fo4::Signature::from_string(name);
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (rules[index].signature == signature) return index;
  }
  return rules.size();
}

fo4::Plugin template_record_plugin(std::string_view signature, std::uint32_t local_id,
                                   const std::vector<Sub>& subs) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, local_id);
  for (const auto& sub : subs) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

const fo4::Record* only_record(const fo4::Plugin& plugin, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  const fo4::Record* found = nullptr;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == wanted) found = &record;
  });
  return found;
}

fo4::Plugin destructible_acti_plugin(const std::vector<Sub>& stages) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x820);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleActi"));
  fo4::test::write_subrecord(out, "OBND", zeros(12));
  for (const auto& sub : stages) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

const fo4::Record* only_acti(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000820:Patch.esp"));
}

}  // namespace

TEST(ObjectBaseRecordsSynthetic, EveryScalarRowDecodesOnTheFixture) {
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

TEST(ObjectBaseRecordsSynthetic, FlagsRowNamesComeFromXEdit) {
  const auto door = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("DOOR"),
                                                         "DOOR\\Flags");
  ASSERT_TRUE(door.has_value());
  const auto hidden = fo4::schema::scalar_value_name(door->scalar, 0x4);
  ASSERT_TRUE(hidden.has_value());
  EXPECT_EQ(*hidden, "Hidden");

  const auto term = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("TERM"),
                                                         "TERM\\Flags");
  ASSERT_TRUE(term.has_value());
  const auto radio = fo4::schema::scalar_value_name(term->scalar, 0x10);
  ASSERT_TRUE(radio.has_value());
  EXPECT_EQ(*radio, "Is a Radio");
}

TEST(ObjectBaseRecordsSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record = fo4::FormKey::parse(item.key);
    const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
    ASSERT_TRUE(scalar.has_value()) << item.scalar_path;
    EXPECT_EQ(fo4::set_record_scalar(plugin, record, *scalar, item.scalar_value),
              fo4::ScalarMutationStatus::updated)
        << item.scalar_path;
    EXPECT_EQ(fo4::set_record_string(plugin, record, item.text_path, 0, "Edited.nif"),
              fo4::ScalarMutationStatus::updated)
        << item.text_path;
    const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
    ASSERT_TRUE(link.has_value()) << item.link_path;
    EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *link,
                                             fo4::FormKey::parse(item.link_target)),
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

    const auto text = fo4::schema::scalar_field_descriptor(signature, item.text_path);
    ASSERT_TRUE(text.has_value());
    const auto* text_sub = find_subrecord(*record, text->scalar.subrecord);
    ASSERT_NE(text_sub, nullptr);
    EXPECT_EQ(text_sub->data, zstr("Edited.nif")) << item.text_path;

    const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
    ASSERT_TRUE(link.has_value());
    const auto* link_sub = find_subrecord(*record, link->subrecord);
    ASSERT_NE(link_sub, nullptr) << item.link_path;
    const auto target = fo4::FormKey::parse(item.link_target).local().value();
    EXPECT_EQ(link_sub->data, u32_bytes(raw_id(target))) << item.link_path;
  }
}

TEST(ObjectBaseRecordsSynthetic, FixtureLinksStartOnTheirFirstTarget) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
    ASSERT_TRUE(link.has_value()) << item.link_path;
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr);
    const auto* link_sub = find_subrecord(*record, link->subrecord);
    ASSERT_NE(link_sub, nullptr) << item.link_path;
    EXPECT_EQ(link_sub->data, u32_bytes(raw_id(item.link_start))) << item.link_path;
  }
}

TEST(ObjectBaseRecordsSynthetic, FixtureRecordsMeetTheirAuthoringMinimums) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto constraint = fo4::schema::record_authoring_constraint(signature);
    ASSERT_TRUE(constraint.has_value()) << item.signature;
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr);
    for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
      const auto count = static_cast<std::size_t>(std::count_if(
          record->subrecords.begin(), record->subrecords.end(),
          [&](const fo4::Subrecord& sub) { return sub.signature == rule.signature; }));
      EXPECT_GE(count, static_cast<std::size_t>(rule.minimum))
          << item.signature << ' ' << rule.signature.to_string();
      EXPECT_LE(count, static_cast<std::size_t>(rule.maximum))
          << item.signature << ' ' << rule.signature.to_string();
    }
  }
}

TEST(ObjectBaseRecordsSynthetic, FurnObjectTemplateRulesMatchNpcAndFollowXmrk) {
  const auto furn = fo4::schema::record_authoring_constraint(fo4::Signature::from_string("FURN"));
  const auto npc = fo4::schema::record_authoring_constraint(fo4::Signature::from_string("NPC_"));
  ASSERT_TRUE(furn.has_value());
  ASSERT_TRUE(npc.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*furn);
  const auto npc_rules = fo4::schema::authoring_subrecords(*npc);

  std::size_t previous = rule_index(rules, "XMRK");
  ASSERT_LT(previous, rules.size());
  for (const auto name : {"APPR", "OBTE", "OBTF", "OBTS", "STOP"}) {
    const auto index = rule_index(rules, name);
    EXPECT_LT(index, rules.size()) << name;
    EXPECT_GT(index, previous) << name;
    previous = index;
    const auto twin = rule_index(npc_rules, name);
    ASSERT_LT(twin, npc_rules.size()) << name;
    EXPECT_EQ(rules[index].minimum, npc_rules[twin].minimum) << name;
    EXPECT_EQ(rules[index].maximum, npc_rules[twin].maximum) << name;
    EXPECT_EQ(rules[index].coupled, npc_rules[twin].coupled) << name;
  }
}

TEST(ObjectBaseRecordsSynthetic, FurnTemplateBlockFollowsTheAuthoringOrderInPreflight) {
  const auto ordered = template_record_plugin("FURN", 0x810, {{"EDID", zstr("RadicleFurn")},
                                             {"OBND", zeros(12)},
                                             {"XMRK", zstr("Marker.nif")},
                                             {"APPR", zeros(4)},
                                             {"OBTE", u32_bytes(1)},
                                             {"OBTF", Bytes{}},
                                             {"OBTS", u32_bytes(1)},
                                             {"STOP", Bytes{}}});
  const auto key = fo4::FormKey::parse("000810:Patch.esp");
  ASSERT_NE(only_record(ordered, "FURN"), nullptr);
  for (const auto& issue : fo4::schema::preflight_authoring_record(ordered, key)) {
    EXPECT_NE(issue.code, "subrecord_order_invalid") << issue.path;
  }

  const auto swapped = template_record_plugin("FURN", 0x810, {{"EDID", zstr("RadicleFurn")},
                                             {"OBND", zeros(12)},
                                             {"OBTE", u32_bytes(1)},
                                             {"OBTS", u32_bytes(1)},
                                             {"OBTF", Bytes{}}});
  ASSERT_NE(only_record(swapped, "FURN"), nullptr);
  const auto issues = fo4::schema::preflight_authoring_record(swapped, key);
  EXPECT_TRUE(std::any_of(issues.begin(), issues.end(), [](const fo4::schema::PreflightIssue& issue) {
    return issue.code == "subrecord_order_invalid";
  }));
}

TEST(ObjectBaseRecordsSynthetic, MultiStageDestructibleFollowsTheAuthoringOrderPerStage) {
  const auto constraint =
      fo4::schema::record_authoring_constraint(fo4::Signature::from_string("ACTI"));
  ASSERT_TRUE(constraint.has_value());
  const auto rules = fo4::schema::authoring_subrecords(*constraint);

  const auto two_stages = destructible_acti_plugin({{"DEST", zeros(8)},
                                                    {"DSTD", zeros(20)},
                                                    {"DSTA", zstr("Stage1")},
                                                    {"DMDL", zstr("Stage1.nif")},
                                                    {"DSTF", Bytes{}},
                                                    {"DSTD", zeros(20)},
                                                    {"DSTA", zstr("Stage2")},
                                                    {"DMDL", zstr("Stage2.nif")},
                                                    {"DSTF", Bytes{}}});
  const auto* acti = only_acti(two_stages);
  ASSERT_NE(acti, nullptr);
  EXPECT_FALSE(fo4::test::authoring_order_violation(*acti, rules).has_value());

  const auto inverted = destructible_acti_plugin({{"DEST", zeros(8)},
                                                  {"DSTD", zeros(20)},
                                                  {"DMDL", zstr("Stage1.nif")},
                                                  {"DSTA", zstr("Stage1")},
                                                  {"DSTF", Bytes{}}});
  const auto* bad = only_acti(inverted);
  ASSERT_NE(bad, nullptr);
  const auto violation = fo4::test::authoring_order_violation(*bad, rules);
  ASSERT_TRUE(violation.has_value());
  EXPECT_EQ(violation->subrecord, fo4::Signature::from_string("DSTA"));
}

TEST(ObjectBaseRecordsSynthetic, ModelRowsDecodeTheirValueAndFlags) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto door_signature = fo4::Signature::from_string("DOOR");
  const auto cont_signature = fo4::Signature::from_string("CONT");
  const auto* door = fo4::find_record(plugin, fo4::FormKey::parse("000803:Patch.esp"));
  const auto* cont = fo4::find_record(plugin, fo4::FormKey::parse("000802:Patch.esp"));
  ASSERT_NE(door, nullptr);
  ASSERT_NE(cont, nullptr);

  const auto color = fo4::schema::scalar_field_descriptor(door_signature, "DOOR\\ColorRemapping");
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(color->scalar.subrecord, fo4::Signature::from_string("MODC"));
  EXPECT_EQ(color->scalar.storage, fo4::schema::ScalarStorage::f32);
  const auto* modc = find_subrecord(*door, fo4::Signature::from_string("MODC"));
  ASSERT_NE(modc, nullptr);
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(modc->data, color->scalar),
                                      color->scalar),
            "2.5");

  const auto flags = fo4::schema::scalar_field_descriptor(door_signature, "DOOR\\ModelFlags");
  ASSERT_TRUE(flags.has_value());
  EXPECT_EQ(flags->scalar.subrecord, fo4::Signature::from_string("MODF"));
  EXPECT_EQ(flags->scalar.presentation, fo4::schema::ScalarPresentation::flags);
  const auto* modf = find_subrecord(*door, fo4::Signature::from_string("MODF"));
  ASSERT_NE(modf, nullptr);
  const auto face_bones = fo4::schema::scalar_value_name(flags->scalar, 1);
  ASSERT_TRUE(face_bones.has_value());
  EXPECT_EQ(*face_bones, "Has FaceBones Model");
  EXPECT_FALSE(fo4::schema::scalar_value_name(flags->scalar, 4).has_value());

  const auto cont_flags = fo4::schema::scalar_field_descriptor(cont_signature, "CONT\\ModelFlags");
  ASSERT_TRUE(cont_flags.has_value());
  const auto* cont_modf = find_subrecord(*cont, fo4::Signature::from_string("MODF"));
  ASSERT_NE(cont_modf, nullptr);
  const auto first_person = fo4::schema::scalar_value_name(cont_flags->scalar, 2);
  ASSERT_TRUE(first_person.has_value());
  EXPECT_EQ(*first_person, "Has 1stPerson Model");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(cont_modf->data, cont_flags->scalar),
                                      cont_flags->scalar),
            "2");

  const auto stat = template_record_plugin("STAT", 0x811, {{"EDID", zstr("RadicleStat")},
                                                          {"MODC", f32_bytes(1.5f)},
                                                          {"MODF", u8_bytes(1)}});
  const auto* stat_record = only_record(stat, "STAT");
  ASSERT_NE(stat_record, nullptr);
  const auto stat_color = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("STAT"),
                                                              "STAT\\ColorRemapping");
  ASSERT_TRUE(stat_color.has_value());
  const auto* stat_modc = find_subrecord(*stat_record, fo4::Signature::from_string("MODC"));
  ASSERT_NE(stat_modc, nullptr);
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(stat_modc->data, stat_color->scalar),
                                      stat_color->scalar),
            "1.5");
}

TEST(ObjectBaseRecordsSynthetic, WrongSizedModelPayloadsAreRefused) {
  const auto plugin = template_record_plugin("DOOR", 0x812, {{"EDID", zstr("RadicleDoor")},
                                                             {"MODC", u8_bytes(1)},
                                                             {"MODF", u16_bytes(1)}});
  const auto* record = only_record(plugin, "DOOR");
  ASSERT_NE(record, nullptr);
  const auto color = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("DOOR"),
                                                         "DOOR\\ColorRemapping");
  const auto flags = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("DOOR"),
                                                         "DOOR\\ModelFlags");
  ASSERT_TRUE(color.has_value());
  ASSERT_TRUE(flags.has_value());
  const auto* modc = find_subrecord(*record, fo4::Signature::from_string("MODC"));
  const auto* modf = find_subrecord(*record, fo4::Signature::from_string("MODF"));
  ASSERT_NE(modc, nullptr);
  ASSERT_NE(modf, nullptr);
  EXPECT_THROW(static_cast<void>(fo4::schema::scalar_field_bytes(modc->data, color->scalar)),
               fo4::schema::ScalarError);
  EXPECT_THROW(static_cast<void>(fo4::schema::scalar_field_bytes(modf->data, flags->scalar)),
               fo4::schema::ScalarError);
}
