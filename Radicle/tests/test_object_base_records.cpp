#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "authoring_order.hpp"
#include "object_base_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace object_base_fixture;

TEST(ObjectBaseRecordsSynthetic, EveryScalarRowDecodesOnTheFixture) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
                                             {"OBTS", zeros(18)},
                                             {"STOP", Bytes{}}});
  const auto key = fo4::FormKey::parse("000810:Patch.esp");
  ASSERT_NE(only_record(ordered, "FURN"), nullptr);
  for (const auto& issue : fo4::schema::preflight_authoring_record(ordered, key)) {
    EXPECT_NE(issue.code, "subrecord_order_invalid") << issue.path;
  }

  const auto swapped = template_record_plugin("FURN", 0x810, {{"EDID", zstr("RadicleFurn")},
                                             {"OBND", zeros(12)},
                                             {"OBTE", u32_bytes(1)},
                                             {"OBTS", zeros(18)},
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
  const auto preflight_key = fo4::FormKey::parse("000820:Patch.esp");
  for (const auto& issue : fo4::schema::preflight_authoring_record(two_stages, preflight_key)) {
    EXPECT_NE(issue.code, "subrecord_order_invalid") << issue.path;
  }

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

  const auto preflight_issues = fo4::schema::preflight_authoring_record(inverted, preflight_key);
  EXPECT_TRUE(std::any_of(preflight_issues.begin(), preflight_issues.end(),
                          [](const fo4::schema::PreflightIssue& issue) {
                            return issue.code == "subrecord_order_invalid";
                          }));
}

TEST(ObjectBaseRecordsSynthetic, ModelRowsDecodeTheirValueAndFlags) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
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
  auto plugin = template_record_plugin("DOOR", 0x812, {{"EDID", zstr("RadicleDoor")},
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

namespace {

constexpr std::string_view k_obts_paths[] = {
    "ObjectTemplateIncludeCount", "ObjectTemplatePropertyCount", "ObjectTemplateLevelMin",
    "ObjectTemplateLevelMax",     "ObjectTemplateParentCombinationIndex",
    "ObjectTemplateDefault"};

fo4::Plugin object_template_plugin(std::string_view signature, std::uint32_t local_id,
                                   const std::vector<Sub>& combinations) {
  std::vector<Sub> subs;
  subs.push_back({"EDID", zstr("RadicleObjectTemplate")});
  for (const auto& combination : combinations) {
    subs.push_back(combination);
  }
  return template_record_plugin(signature, local_id, subs);
}

}  // namespace

TEST(ObjectBaseRecordsSynthetic, ObjectTemplateHeaderRowsCoverNpcAndFurn) {
  for (const auto signature : {"NPC_", "FURN"}) {
    const auto record = fo4::Signature::from_string(signature);
    for (const auto name : k_obts_paths) {
      const auto path = std::string{signature} + "\\" + std::string{name};
      const auto field = fo4::schema::scalar_field_descriptor(record, path);
      ASSERT_TRUE(field.has_value()) << path;
      EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("OBTS")) << path;
      EXPECT_TRUE(field->scalar.address.repeated) << path;
      EXPECT_EQ(field->scalar.minimum_payload_size, 18u) << path;
    }
    const auto constraint = fo4::schema::record_authoring_constraint(record);
    ASSERT_TRUE(constraint.has_value());
    const auto rules = fo4::schema::authoring_subrecords(*constraint);
    const auto obts = rule_index(rules, "OBTS");
    ASSERT_LT(obts, rules.size());
    EXPECT_TRUE(rules[obts].coupled);
    const auto level_min = fo4::schema::scalar_field_descriptor(record, std::string{signature} + "\\ObjectTemplateLevelMin");
    const auto parent = fo4::schema::scalar_field_descriptor(record, std::string{signature} + "\\ObjectTemplateParentCombinationIndex");
    const auto defaulted = fo4::schema::scalar_field_descriptor(record, std::string{signature} + "\\ObjectTemplateDefault");
    ASSERT_TRUE(level_min.has_value());
    ASSERT_TRUE(parent.has_value());
    ASSERT_TRUE(defaulted.has_value());
    EXPECT_EQ(level_min->scalar.offset, 8u);
    EXPECT_EQ(parent->scalar.offset, 12u);
    EXPECT_EQ(defaulted->scalar.offset, 14u);
    const auto true_name = fo4::schema::scalar_value_name(defaulted->scalar, 1);
    ASSERT_TRUE(true_name.has_value());
    EXPECT_EQ(*true_name, "True");
  }
}

TEST(ObjectBaseRecordsSynthetic, CoupledObjectTemplatePayloadKeepsItsIncludeCount) {
// Constraint: the IncludeCount row must stay write-refused, because a writable count desynced the stored count from the include run.
  for (const auto signature : {"NPC_", "FURN"}) {
    const auto key = fo4::FormKey::parse("000852:Patch.esp");
    auto plugin = object_template_plugin(signature, 0x852,
                                         {{"OBTE", u32_bytes(1)},
                                          {"OBTF", zeros(0)},
                                          {"OBTS", obts_header(2, 0, 5, 10, -1, 0, 1, 2, 0)},
                                          {"STOP", zeros(0)}});
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("OBTS")),
              fo4::SubrecordAddStatus::not_authorable);
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, fo4::Signature::from_string("OBTS"), 0),
              fo4::SubrecordRemoveStatus::not_authorable);

    const auto level_min = fo4::schema::scalar_field_descriptor(
        fo4::Signature::from_string(signature), std::string{signature} + "\\ObjectTemplateLevelMin");
    ASSERT_TRUE(level_min.has_value());
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_min, 0, "9"),
              fo4::ScalarMutationStatus::updated);

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto* record = only_record(reread, signature);
    ASSERT_NE(record, nullptr);
    const auto* obts = find_subrecord(*record, fo4::Signature::from_string("OBTS"));
    ASSERT_NE(obts, nullptr);
    ASSERT_GE(obts->data.size(), 18u);
    EXPECT_EQ(std::to_integer<std::uint8_t>(obts->data[0]), 2u);
    EXPECT_EQ(std::to_integer<std::uint8_t>(obts->data[8]), 9u);
  }
}

TEST(ObjectBaseRecordsSynthetic, ReadsAndEditsOneObjectTemplateCombinationHeader) {
  for (const auto signature : {"NPC_", "FURN"}) {
    const auto key = fo4::FormKey::parse("000850:Patch.esp");
    auto plugin = object_template_plugin(signature, 0x850,
                                         {{"OBTE", u32_bytes(2)},
                                          {"OBTF", zeros(0)},
                                          {"OBTS", obts_header(1, 0, 5, 10, -1, 0, 1, 2, 0)},
                                          {"OBTF", zeros(0)},
                                          {"OBTS", obts_header(0, 3, 20, 30, 7, 1, 4, 8, 0)},
                                          {"STOP", zeros(0)}});
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateLevelMin", 0), "5");
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateLevelMax", 1), "30");
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateParentCombinationIndex", 0), "-1");
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateDefault", 1), "1");

    const auto level_max = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(signature), std::string{signature} + "\\ObjectTemplateLevelMax");
    const auto parent = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(signature), std::string{signature} + "\\ObjectTemplateParentCombinationIndex");
    ASSERT_TRUE(level_max.has_value());
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_max, 1, "42"),
              fo4::ScalarMutationStatus::updated);
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *parent, 0, "9"),
              fo4::ScalarMutationStatus::updated);

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto* record = fo4::find_record(reread, key);
    ASSERT_NE(record, nullptr) << signature;
    std::vector<Bytes> payloads;
    for (const auto& sub : record->subrecords) {
      if (sub.signature == fo4::Signature::from_string("OBTS")) payloads.push_back(sub.data);
    }
    ASSERT_EQ(payloads.size(), 2u) << signature;
    EXPECT_EQ(payloads[0], obts_header(1, 0, 5, 10, 9, 0, 1, 2, 0)) << signature;
    EXPECT_EQ(payloads[1], obts_header(0, 3, 20, 42, 7, 1, 4, 8, 0)) << signature;
    EXPECT_EQ(read_row(reread, key, std::string{signature} + "\\ObjectTemplateLevelMax", 1), "42");
    EXPECT_EQ(read_row(reread, key, std::string{signature} + "\\ObjectTemplateLevelMin", 1), "20");
  }
}

TEST(ObjectBaseRecordsSynthetic, ShortObjectTemplateCombinationsHideTheirHeaderRows) {
  const auto key = fo4::FormKey::parse("000851:Patch.esp");
  auto plugin = object_template_plugin("NPC_", 0x851,
                                       {{"OBTE", u32_bytes(1)},
                                        {"OBTF", zeros(0)},
                                        {"OBTS", zeros(17)},
                                        {"STOP", zeros(0)}});
  EXPECT_EQ(read_row(plugin, key, "NPC_\\ObjectTemplateLevelMin", 0), "absent");
  const auto level_min = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("NPC_"), "NPC_\\ObjectTemplateLevelMin");
  ASSERT_TRUE(level_min.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_min, 0, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_min, 1, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* obts = find_subrecord(*record, fo4::Signature::from_string("OBTS"));
  ASSERT_NE(obts, nullptr);
  EXPECT_EQ(obts->data, zeros(17));
}

TEST(ObjectBaseRecordsSynthetic, KeywordedObjectTemplateReadsMinRankAndAltRowsPastTheKeywords) {
  const auto key = fo4::FormKey::parse("000852:Patch.esp");
  for (const auto signature : {"NPC_", "FURN"}) {
    const auto record = fo4::Signature::from_string(signature);
    for (const auto leaf : {"ObjectTemplateMinLevelForRanks", "ObjectTemplateAltLevelsPerTier"}) {
      const auto row = fo4::schema::scalar_field_descriptor(record, std::string{signature} + "\\" + leaf);
      ASSERT_TRUE(row.has_value()) << signature << leaf;
      EXPECT_NE(row->scalar.address.run.count, 0u) << signature << leaf;
    }

    auto plugin = object_template_plugin(signature, 0x852,
                                         {{"OBTE", u32_bytes(1)},
                                          {"OBTF", zeros(0)},
                                          {"OBTS", obts_header(0, 0, 5, 10, -1, 0, 1, 2, 1)},
                                          {"STOP", zeros(0)}});
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateLevelMax", 0), "10");
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateMinLevelForRanks", 0), "1");
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateAltLevelsPerTier", 0), "2");
    const auto level_max = fo4::schema::scalar_field_descriptor(
        record, std::string{signature} + "\\ObjectTemplateLevelMax");
    ASSERT_TRUE(level_max.has_value()) << signature;
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *level_max, 0, "42"),
              fo4::ScalarMutationStatus::updated)
        << signature;

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto* reread_record = fo4::find_record(reread, key);
    ASSERT_NE(reread_record, nullptr) << signature;
    const auto* obts = find_subrecord(*reread_record, fo4::Signature::from_string("OBTS"));
    ASSERT_NE(obts, nullptr) << signature;
    EXPECT_EQ(obts->data, obts_header(0, 0, 5, 42, -1, 0, 1, 2, 1)) << signature;
  }
}

namespace {

fo4::schema::SubrecordGroupDescriptor combinations_group(std::string_view signature) {
  return fo4::schema::subrecord_group_descriptor(fo4::Signature::from_string(signature),
                                                 std::string{signature} + "\\ObjectTemplateCombinations")
      .value();
}

std::vector<std::string> template_signatures(const fo4::Record& record) {
  std::vector<std::string> out;
  bool inside = false;
  for (const auto& sub : record.subrecords) {
    if (sub.signature == fo4::Signature::from_string("OBTE")) inside = true;
    if (inside) out.push_back(sub.signature.to_string());
    if (sub.signature == fo4::Signature::from_string("STOP")) break;
  }
  return out;
}

}  // namespace

TEST(ObjectBaseRecordsSynthetic, ObjectTemplateCountAndCombinationsDescriptorsCoverEveryTemplateRecord) {
  for (const auto signature : {"NPC_", "FURN", "ARMO", "WEAP"}) {
    const auto record = fo4::Signature::from_string(signature);
    const auto count =
        fo4::schema::scalar_field_descriptor(record, std::string{signature} + "\\ObjectTemplateCount");
    ASSERT_TRUE(count.has_value()) << signature;
    EXPECT_EQ(count->scalar.subrecord, fo4::Signature::from_string("OBTE")) << signature;
    EXPECT_EQ(count->scalar.storage, fo4::schema::ScalarStorage::u32) << signature;

    const auto group = combinations_group(signature);
    EXPECT_EQ(group.count_subrecord, fo4::Signature::from_string("OBTE")) << signature;
    EXPECT_FALSE(group.requires_existing_count) << signature;
    EXPECT_EQ(group.block.closer, fo4::Signature::from_string("STOP")) << signature;
    EXPECT_EQ(group.block.leader_min_size, 18u) << signature;
    EXPECT_EQ(group.block.link_offset, 12u) << signature;
    EXPECT_EQ(group.block.link_none, 0xFFFFu) << signature;
    ASSERT_EQ(group.members.size(), 3u) << signature;
    EXPECT_EQ(group.members[0].subrecord, fo4::Signature::from_string("OBTF")) << signature;
    EXPECT_TRUE(group.members[0].leading) << signature;
    EXPECT_FALSE(group.members[0].required) << signature;
    EXPECT_EQ(group.members[1].subrecord, fo4::Signature::from_string("FULL")) << signature;
    EXPECT_TRUE(group.members[1].leading) << signature;
    EXPECT_EQ(group.members[2].subrecord, fo4::Signature::from_string("OBTS")) << signature;
    EXPECT_TRUE(group.members[2].required) << signature;
    EXPECT_FALSE(group.members[2].leading) << signature;
    ASSERT_EQ(group.members[2].default_payload.size(), 18u) << signature;
    EXPECT_EQ(group.members[2].default_payload[12], 0xFFu) << signature;
    EXPECT_EQ(group.members[2].default_payload[13], 0xFFu) << signature;

    EXPECT_EQ(fo4::schema::subrecord_group_for_member(record, fo4::Signature::from_string("OBTS"))->path,
              std::string{signature} + "\\ObjectTemplateCombinations")
        << signature;
    EXPECT_EQ(fo4::schema::subrecord_group_for_member(record, fo4::Signature::from_string("OBTF"))->path,
              std::string{signature} + "\\ObjectTemplateCombinations")
        << signature;
  }
}

TEST(ObjectBaseRecordsSynthetic, InsertsAndRemovesObjectTemplateCombinationsWithCounterUpkeep) {
  for (const auto signature : {"NPC_", "FURN", "ARMO", "WEAP"}) {
    const auto key = fo4::FormKey::parse("000853:Patch.esp");
    auto plugin = object_template_plugin(
        signature, 0x853,
        {{"OBTE", u32_bytes(2)},
         {"OBTF", zeros(0)},
         {"FULL", u32_bytes(0x00123456)},
         {"OBTS", obts_header(1, 0, 5, 10, -1, 0, 1, 2, 0)},
         {"OBTS", obts_header(0, 3, 20, 30, -1, 1, 4, 8, 0)},
         {"STOP", zeros(0)}});
    const auto group = combinations_group(signature);
    const auto count = fo4::schema::scalar_field_descriptor(
        fo4::Signature::from_string(signature), std::string{signature} + "\\ObjectTemplateCount");
    ASSERT_TRUE(count.has_value()) << signature;
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateCount", 0), "2")
        << signature;
    EXPECT_THROW(fo4::set_record_scalar(plugin, key, *count, 0, "9"), fo4::schema::ScalarError)
        << signature;

    EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key, group, 1),
              fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateCount", 0), "3")
        << signature;
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateLevelMin", 1), "0")
        << signature;
    EXPECT_EQ(fo4::set_record_subrecord_group_member(
                  plugin, key, group, 1, fo4::Signature::from_string("OBTF"), true),
              fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, key, group, 0),
              fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(read_row(plugin, key, std::string{signature} + "\\ObjectTemplateCount", 0), "2")
        << signature;
    EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, key, group, 5),
              fo4::SubrecordGroupMutationStatus::index_out_of_range)
        << signature;

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    const auto* record = fo4::find_record(reread, key);
    ASSERT_NE(record, nullptr) << signature;
    // Invariant: insert stages a bare OBTS run, so the OBTF toggle lands on it and the next run stays bare.
    EXPECT_EQ(template_signatures(*record),
              (std::vector<std::string>{"OBTE", "OBTF", "OBTS", "OBTS", "STOP"}))
        << signature;
    EXPECT_EQ(read_row(reread, key, std::string{signature} + "\\ObjectTemplateCount", 0), "2")
        << signature;
    EXPECT_EQ(read_row(reread, key, std::string{signature} + "\\ObjectTemplateLevelMax", 1), "30")
        << signature;
  }
}

// Constraint: the OBTE count bounds the run scan, so the record's own FULL after STOP is not a combination run.
TEST(ObjectBaseRecordsSynthetic, ObjectTemplateRunScanStopsAtItsCountedRuns) {
  const auto key = fo4::FormKey::parse("000856:Patch.esp");
  auto plugin = object_template_plugin(
      "NPC_", 0x856,
      {{"OBTE", u32_bytes(1)},
       {"OBTS", obts_header(1, 0, 5, 10, -1, 0, 1, 2, 0)},
       {"STOP", zeros(0)},
       {"CNAM", zeros(4)},
       {"FULL", u32_bytes(0x00123456)}});
  const auto spans = fo4::subrecord_group_spans(plugin, key, combinations_group("NPC_"));
  ASSERT_TRUE(spans.has_value());
  EXPECT_EQ(spans->size(), 1u);
  EXPECT_EQ(read_row(plugin, key, "NPC_\\ObjectTemplateLevelMin", 0), "5");
  EXPECT_EQ(read_row(plugin, key, "NPC_\\ObjectTemplateLevelMin", 1), "absent");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(read_row(reread, key, "NPC_\\ObjectTemplateLevelMin", 0), "5");
  EXPECT_EQ(read_row(reread, key, "NPC_\\ObjectTemplateLevelMin", 1), "absent");
}

// Constraint: a record without OBTE holds no combination run, so its own FULL neither starts one nor throws.
TEST(ObjectBaseRecordsSynthetic, ObjectTemplateRowsStayAbsentWithoutACount) {
  for (const auto signature : {"NPC_", "FURN"}) {
    const auto key = fo4::FormKey::parse("000857:Patch.esp");
    auto plugin = object_template_plugin(signature, 0x857, {{"FULL", u32_bytes(0x00123456)}});
    const auto spans = fo4::subrecord_group_spans(plugin, key, combinations_group(signature));
    ASSERT_TRUE(spans.has_value()) << signature;
    EXPECT_TRUE(spans->empty()) << signature;
    const auto path = std::string{signature} + "\\ObjectTemplateLevelMin";
    EXPECT_EQ(read_row(plugin, key, path, 0), "absent") << signature;
    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
    EXPECT_EQ(read_row(reread, key, path, 0), "absent") << signature;
  }
}

TEST(ObjectBaseRecordsSynthetic, CombinationEditsRefuseACountThatDisagreesWithTheRuns) {
  for (const auto signature : {"NPC_", "FURN", "ARMO", "WEAP"}) {
    const auto key = fo4::FormKey::parse("000855:Patch.esp");
    auto plugin = object_template_plugin(
        signature, 0x855,
        {{"OBTE", u32_bytes(3)},
         {"OBTF", zeros(0)},
         {"OBTS", obts_header(1, 0, 5, 10, -1, 0, 1, 2, 0)},
         {"OBTF", zeros(0)},
         {"OBTS", obts_header(0, 3, 20, 30, 7, 1, 4, 8, 0)},
         {"STOP", zeros(0)}});
    const auto before = fo4::write_plugin(plugin);
    EXPECT_THROW(fo4::insert_record_subrecord_group(plugin, key, combinations_group(signature), 0),
                 fo4::schema::SubrecordGroupError)
        << signature;
    EXPECT_THROW(fo4::remove_record_subrecord_group(plugin, key, combinations_group(signature), 0),
                 fo4::schema::SubrecordGroupError)
        << signature;
    EXPECT_EQ(fo4::write_plugin(plugin), before) << signature;
  }
}

namespace {

using Row = std::pair<std::string, Bytes>;
using Rows = std::vector<Row>;

Rows rows_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  Rows rows;
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return rows;
  for (const auto& sub : record->subrecords) rows.emplace_back(sub.signature.to_string(), sub.data);
  return rows;
}

Bytes blank_obts() { return obts_header(0, 0, 0, 0, -1, 0, 0, 0, 0); }

std::vector<Sub> linked_block() {
  return {{"OBTE", u32_bytes(3)},
          {"OBTS", obts_header(0, 0, 1, 0, -1, 0, 0, 0, 0)},
          {"OBTF", zeros(0)},
          {"FULL", u32_bytes(0x3A1)},
          {"OBTS", obts_header(1, 0, 2, 0, 0, 0, 0, 0, 0)},
          {"OBTF", zeros(0)},
          {"OBTS", obts_header(0, 0, 3, 0, 1, 0, 0, 0, 0)},
          {"STOP", zeros(0)}};
}

std::vector<Sub> free_block() {
  return {{"OBTE", u32_bytes(3)},
          {"OBTS", obts_header(0, 0, 1, 0, -1, 0, 0, 0, 0)},
          {"OBTF", zeros(0)},
          {"FULL", u32_bytes(0x3A1)},
          {"OBTS", obts_header(1, 0, 2, 0, -1, 0, 0, 0, 0)},
          {"OBTF", zeros(0)},
          {"OBTS", obts_header(0, 0, 3, 0, -1, 0, 0, 0, 0)},
          {"STOP", zeros(0)}};
}

const std::vector<Sub> k_followers = {{"DNAM", Bytes(4, std::byte{7})}, {"FNAM", Bytes(2, std::byte{9})}};

fo4::Plugin template_host(std::string_view signature, const std::vector<Sub>& block) {
  auto subs = block;
  subs.insert(subs.end(), k_followers.begin(), k_followers.end());
  return object_template_plugin(signature, 0x860, subs);
}

Rows expected_rows(const std::vector<Sub>& block) {
  Rows rows{{"EDID", zstr("RadicleObjectTemplate")}};
  for (const auto& sub : block) rows.emplace_back(std::string{sub.signature}, sub.data);
  for (const auto& sub : k_followers) rows.emplace_back(std::string{sub.signature}, sub.data);
  return rows;
}

template <typename Edit>
fo4::schema::SubrecordGroupBlockError::Kind refusal_of(fo4::Plugin& plugin, Edit&& edit) {
  const auto before = fo4::write_plugin(plugin);
  try {
    edit();
  } catch (const fo4::schema::SubrecordGroupBlockError& error) {
    EXPECT_EQ(fo4::write_plugin(plugin), before);
    return error.kind();
  }
  ADD_FAILURE() << "the block edit was not refused";
  return fo4::schema::SubrecordGroupBlockError::Kind::malformed;
}

}  // namespace

TEST(ObjectBaseRecordsSynthetic, BlockInsertCreatesCounterCombinationAndStopAtTheAuthoringPosition) {
  struct Case {
    const char* signature;
    std::vector<Sub> subs;
    std::vector<std::string> expected;
  };
  const std::vector<Case> cases = {
      {"ARMO", {{"DATA", Bytes(8, std::byte{1})}, {"FNAM", Bytes(8, std::byte{2})}},
       {"EDID", "DATA", "FNAM", "OBTE", "OBTS", "STOP"}},
      {"FURN", {}, {"EDID", "OBTE", "OBTS", "STOP"}},
      {"NPC_", {{"CNAM", Bytes(4, std::byte{3})}}, {"EDID", "OBTE", "OBTS", "STOP", "CNAM"}},
      {"WEAP", k_followers, {"EDID", "OBTE", "OBTS", "STOP", "DNAM", "FNAM"}},
  };
  for (const auto& item : cases) {
    const auto key = fo4::FormKey::parse("000860:Patch.esp");
    auto plugin = object_template_plugin(item.signature, 0x860, item.subs);
    const auto group = combinations_group(item.signature);
    EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key, group, 0), fo4::SubrecordGroupMutationStatus::updated)
        << item.signature;
    const auto rows = rows_of(plugin, key);
    std::vector<std::string> signatures;
    for (const auto& row : rows) signatures.push_back(row.first);
    EXPECT_EQ(signatures, item.expected) << item.signature;
    for (const auto& row : rows) {
      if (row.first == "OBTE") {
        EXPECT_EQ(row.second, u32_bytes(1)) << item.signature;
      }
      if (row.first == "OBTS") {
        EXPECT_EQ(row.second, blank_obts()) << item.signature;
      }
      if (row.first == "STOP") {
        EXPECT_TRUE(row.second.empty()) << item.signature;
      }
    }
    EXPECT_EQ(read_row(plugin, key, std::string{item.signature} + "\\ObjectTemplateCount", 0), "1") << item.signature;
    EXPECT_EQ(read_row(plugin, key, std::string{item.signature} + "\\ObjectTemplateParentCombinationIndex", 0), "-1")
        << item.signature;
  }
}

TEST(ObjectBaseRecordsSynthetic, BlockInsertAppendsBeforeStopAndKeepsEveryOtherRowByteForByte) {
  for (const auto signature : {"NPC_", "FURN", "ARMO", "WEAP"}) {
    const auto key = fo4::FormKey::parse("000860:Patch.esp");
    auto plugin = template_host(signature, linked_block());
    EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, key, combinations_group(signature), 3),
              fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    auto block = linked_block();
    block[0].data = u32_bytes(4);
    block.insert(block.end() - 1, Sub{"OBTS", blank_obts()});
    EXPECT_EQ(rows_of(plugin, key), expected_rows(block)) << signature;
  }
  const auto key = fo4::FormKey::parse("000860:Patch.esp");
  auto emptied = template_host("WEAP", {{"OBTE", u32_bytes(0)}, {"STOP", zeros(0)}});
  EXPECT_EQ(fo4::insert_record_subrecord_group(emptied, key, combinations_group("WEAP"), 0),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(rows_of(emptied, key), expected_rows({{"OBTE", u32_bytes(1)}, {"OBTS", blank_obts()}, {"STOP", zeros(0)}}));
}

TEST(ObjectBaseRecordsSynthetic, BlockRemoveKeepsTheRestAndDropsTheWholeBlockWithTheLastCombination) {
  for (const auto signature : {"NPC_", "FURN", "ARMO", "WEAP"}) {
    const auto key = fo4::FormKey::parse("000860:Patch.esp");
    const auto group = combinations_group(signature);
    auto middle = template_host(signature, free_block());
    EXPECT_EQ(fo4::remove_record_subrecord_group(middle, key, group, 1), fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(rows_of(middle, key),
              expected_rows({{"OBTE", u32_bytes(2)},
                             {"OBTS", obts_header(0, 0, 1, 0, -1, 0, 0, 0, 0)},
                             {"OBTF", zeros(0)},
                             {"OBTS", obts_header(0, 0, 3, 0, -1, 0, 0, 0, 0)},
                             {"STOP", zeros(0)}}))
        << signature;

    auto first = template_host(signature, free_block());
    EXPECT_EQ(fo4::remove_record_subrecord_group(first, key, group, 0), fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(rows_of(first, key),
              expected_rows({{"OBTE", u32_bytes(2)},
                             {"OBTF", zeros(0)},
                             {"FULL", u32_bytes(0x3A1)},
                             {"OBTS", obts_header(1, 0, 2, 0, -1, 0, 0, 0, 0)},
                             {"OBTF", zeros(0)},
                             {"OBTS", obts_header(0, 0, 3, 0, -1, 0, 0, 0, 0)},
                             {"STOP", zeros(0)}}))
        << signature;

    auto last = template_host(signature, {{"OBTE", u32_bytes(1)},
                                          {"OBTF", zeros(0)},
                                          {"FULL", u32_bytes(5)},
                                          {"OBTS", obts_header(0, 0, 1, 0, -1, 0, 0, 0, 0)},
                                          {"STOP", zeros(0)}});
    EXPECT_EQ(fo4::remove_record_subrecord_group(last, key, group, 0), fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(rows_of(last, key), expected_rows({})) << signature;

    auto none = template_host(signature, {});
    EXPECT_EQ(fo4::remove_record_subrecord_group(none, key, group, 0),
              fo4::SubrecordGroupMutationStatus::index_out_of_range)
        << signature;
    EXPECT_EQ(rows_of(none, key), expected_rows({})) << signature;
  }
}

TEST(ObjectBaseRecordsSynthetic, BlockEditsRefuseWhileAnotherCombinationNamesTheIndexAsParent) {
  using Kind = fo4::schema::SubrecordGroupBlockError::Kind;
  for (const auto signature : {"NPC_", "FURN", "ARMO", "WEAP"}) {
    const auto key = fo4::FormKey::parse("000860:Patch.esp");
    const auto group = combinations_group(signature);
    auto plugin = template_host(signature, linked_block());
    EXPECT_EQ(refusal_of(plugin, [&] { fo4::remove_record_subrecord_group(plugin, key, group, 0); }), Kind::referenced)
        << signature;
    EXPECT_EQ(refusal_of(plugin, [&] { fo4::remove_record_subrecord_group(plugin, key, group, 1); }), Kind::referenced)
        << signature;
    EXPECT_EQ(refusal_of(plugin, [&] { fo4::insert_record_subrecord_group(plugin, key, group, 1); }), Kind::referenced)
        << signature;

    EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, key, group, 2), fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(fo4::remove_record_subrecord_group(plugin, key, group, 1), fo4::SubrecordGroupMutationStatus::updated)
        << signature;
    EXPECT_EQ(rows_of(plugin, key), expected_rows({{"OBTE", u32_bytes(1)},
                                                   {"OBTS", obts_header(0, 0, 1, 0, -1, 0, 0, 0, 0)},
                                                   {"STOP", zeros(0)}}))
        << signature;
  }
}

TEST(ObjectBaseRecordsSynthetic, BlockEditsRefuseMalformedBlocksAndRecordsOutOfOrder) {
  using Kind = fo4::schema::SubrecordGroupBlockError::Kind;
  const Bytes header = blank_obts();
  const std::vector<std::vector<Sub>> blocks = {
      {{"OBTE", u32_bytes(2)}, {"OBTS", header}, {"STOP", zeros(0)}},
      {{"OBTE", u32_bytes(1)}, {"OBTS", header}},
      {{"OBTE", u32_bytes(1)}, {"KSIZ", u32_bytes(0)}, {"OBTS", header}, {"STOP", zeros(0)}},
      {{"OBTE", u32_bytes(1)}, {"OBTS", header}, {"FULL", u32_bytes(1)}, {"STOP", zeros(0)}},
      {{"OBTE", u32_bytes(0)}, {"OBTE", u32_bytes(0)}, {"STOP", zeros(0)}},
      {{"OBTE", u32_bytes(0)}, {"OBTF", zeros(0)}, {"STOP", zeros(0)}},
      {{"OBTE", u32_bytes(0)}, {"FULL", u32_bytes(1)}, {"STOP", zeros(0)}},
      {{"OBTE", u32_bytes(1)}, {"OBTS", Bytes(5, std::byte{0})}, {"STOP", zeros(0)}},
      {{"OBTE", Bytes(2, std::byte{0})}, {"STOP", zeros(0)}},
  };
  const auto key = fo4::FormKey::parse("000860:Patch.esp");
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    auto plugin = template_host("WEAP", blocks[index]);
    const auto group = combinations_group("WEAP");
    EXPECT_EQ(refusal_of(plugin, [&] { fo4::insert_record_subrecord_group(plugin, key, group, 0); }), Kind::malformed)
        << index;
    EXPECT_EQ(refusal_of(plugin, [&] { fo4::remove_record_subrecord_group(plugin, key, group, 0); }), Kind::malformed)
        << index;
  }
  for (const auto signature : {"NPC_", "FURN", "WEAP"}) {
    auto stray = object_template_plugin(signature, 0x860, {{"ZZZZ", Bytes(1, std::byte{1})}});
    const auto group = combinations_group(signature);
    EXPECT_EQ(refusal_of(stray, [&] { fo4::insert_record_subrecord_group(stray, key, group, 0); }),
              Kind::no_insertion_point)
        << signature;
  }
}
