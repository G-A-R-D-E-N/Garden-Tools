#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_qust = fo4::Signature::from_string("QUST");
constexpr auto k_dnam = fo4::Signature::from_string("DNAM");
constexpr auto k_qtgl = fo4::Signature::from_string("QTGL");

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey glob_key() { return fo4::FormKey::parse("000900:Patch.esp"); }
fo4::FormKey keyword_key() { return fo4::FormKey::parse("000910:Patch.esp"); }
fo4::FormKey spare_keyword_key() { return fo4::FormKey::parse("000911:Patch.esp"); }
fo4::FormKey location_key() { return fo4::FormKey::parse("000920:Patch.esp"); }
fo4::FormKey spare_location_key() { return fo4::FormKey::parse("000921:Patch.esp"); }

struct Piece {
  std::string_view signature;
  Bytes data;
};

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

fo4::schema::FormLinkListDescriptor list(std::string_view path) {
  return fo4::schema::formlink_list_descriptor(k_qust, path).value();
}

Bytes general_data(std::uint16_t flags, std::uint8_t priority, float delay, std::uint8_t type) {
  fo4::test::ByteWriter out;
  out.u16(flags);
  out.u8(priority);
  out.u8(0);
  out.f32(delay);
  out.u8(type);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes ctda_bytes() {
  Bytes payload(32, std::byte{0});
  payload[8] = std::byte{7};
  return payload;
}

Bytes zstring(std::string_view text) {
  fo4::test::ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

std::vector<Piece> general_pieces() {
  return {
      {"EDID", zstring("Q0")},
      {"FULL", zstring("Name")},
      {"DNAM", general_data(0x0001, 30, 12.5f, 6)},
      {"ENAM", u32_bytes(1280067915u)},
      {"LNAM", u32_bytes(0x920)},
      {"XNAM", u32_bytes(0x900)},
      {"QTGL", u32_bytes(0x901)},
      {"QTGL", u32_bytes(0x902)},
      {"FLTR", zstring("FilterText")},
      {"NNAM", zstring("Description")},
      {"SNAM", zstring("QuestMenu.swf")},
      {"GNAM", u32_bytes(0x910)},
  };
}

std::vector<Piece> nested_condition_pieces() {
  return {
      {"EDID", zstring("Q0")},
      {"FULL", zstring("Name")},
      {"DNAM", general_data(0x0001, 30, 12.5f, 6)},
      {"ENAM", u32_bytes(1280067915u)},
      {"LNAM", u32_bytes(0x920)},
      {"XNAM", u32_bytes(0x900)},
      {"QTGL", u32_bytes(0x901)},
      {"FLTR", zstring("FilterText")},
      {"INDX", u32_bytes(0)},
      {"QSDT", u32_bytes(1)},
      {"CTDA", ctda_bytes()},
      {"QOBJ", u32_bytes(10)},
      {"NNAM", zstring("Objective text")},
      {"QSTA", u32_bytes(0)},
      {"CTDA", ctda_bytes()},
      {"NNAM", zstring("Description")},
      {"GNAM", u32_bytes(0x910)},
      {"SNAM", zstring("QuestMenu.swf")},
  };
}

std::vector<std::byte> make_plugin(const std::vector<Piece>& quest) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x912);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto record = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
  for (const auto& piece : quest) {
    fo4::test::write_subrecord(out, piece.signature, piece.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  const auto glob_group = fo4::test::begin_group(out, "GLOB", 0);
  const auto glob_a = fo4::test::begin_record(out, "GLOB", 0, 0x00000900);
  fo4::test::end_record(out, glob_a);
  const auto glob_b = fo4::test::begin_record(out, "GLOB", 0, 0x00000901);
  fo4::test::end_record(out, glob_b);
  const auto glob_c = fo4::test::begin_record(out, "GLOB", 0, 0x00000902);
  fo4::test::end_record(out, glob_c);
  fo4::test::end_group(out, glob_group);

  const auto keyword_group = fo4::test::begin_group(out, "KYWD", 0);
  const auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000910);
  fo4::test::end_record(out, keyword);
  const auto keyword_b = fo4::test::begin_record(out, "KYWD", 0, 0x00000911);
  fo4::test::end_record(out, keyword_b);
  fo4::test::end_group(out, keyword_group);

  const auto location_group = fo4::test::begin_group(out, "LCTN", 0);
  const auto location = fo4::test::begin_record(out, "LCTN", 0, 0x00000920);
  fo4::test::end_record(out, location);
  const auto location_b = fo4::test::begin_record(out, "LCTN", 0, 0x00000921);
  fo4::test::end_record(out, location_b);
  fo4::test::end_group(out, location_group);
  return out.bytes;
}

fo4::Plugin load(const std::vector<Piece>& quest = general_pieces()) {
  return fo4::read_plugin(make_plugin(quest), "Patch.esp");
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

const fo4::Record& record_of(const fo4::Plugin& plugin) {
  return *fo4::find_record(plugin, record_key());
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto field = scalar(path);
  const auto& record = record_of(plugin);
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  const auto slot = resolved.slots.at(index);
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::vector<std::string> signatures(const fo4::Plugin& plugin) {
  std::vector<std::string> names;
  for (const auto& subrecord : record_of(plugin).subrecords) {
    names.push_back(subrecord.signature.to_string());
  }
  return names;
}

Bytes payload_of(const fo4::Plugin& plugin, std::string_view signature) {
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature.to_string() == signature) return subrecord.data;
  }
  ADD_FAILURE() << "missing subrecord " << signature;
  return {};
}

}  // namespace

TEST(QustGeneralRows, RowsDescribeTheGeneralDataBlock) {
  const auto flags = scalar("QUST\\Flags");
  EXPECT_EQ(flags.record, k_qust);
  EXPECT_EQ(flags.scalar.subrecord, k_dnam);
  EXPECT_EQ(flags.scalar.offset, 0u);
  EXPECT_EQ(flags.scalar.payload_size, 12u);
  EXPECT_TRUE(flags.scalar.embedded);
  EXPECT_EQ(flags.scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(flags.scalar.presentation, fo4::schema::ScalarPresentation::flags);
  ASSERT_EQ(flags.scalar.names.size(), 16u);
  EXPECT_EQ(flags.scalar.names[11].value, std::int64_t{1} << 11);
  EXPECT_EQ(flags.scalar.names[11].name, "Active");

  const auto priority = scalar("QUST\\Priority");
  EXPECT_EQ(priority.scalar.offset, 2u);
  EXPECT_EQ(priority.scalar.storage, fo4::schema::ScalarStorage::u8);

  const auto delay = scalar("QUST\\DelayTime");
  EXPECT_EQ(delay.scalar.subrecord, k_dnam);
  EXPECT_EQ(delay.scalar.offset, 4u);
  EXPECT_EQ(delay.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(delay.scalar.payload_size, 12u);

  const auto type = scalar("QUST\\Type");
  EXPECT_EQ(type.scalar.offset, 8u);
  EXPECT_EQ(type.scalar.storage, fo4::schema::ScalarStorage::u8);
  EXPECT_EQ(type.scalar.payload_size, 12u);
  EXPECT_EQ(type.scalar.maximum, 255.0);
  ASSERT_EQ(type.scalar.names.size(), 15u);
  EXPECT_EQ(type.scalar.names[6].name, "Miscellaneous");

  const auto event = scalar("QUST\\Event");
  EXPECT_EQ(event.scalar.subrecord, fo4::Signature::from_string("ENAM"));
  EXPECT_FALSE(event.scalar.embedded);
  ASSERT_EQ(event.scalar.names.size(), 38u);
  EXPECT_FALSE(fo4::schema::scalar_value_name(event.scalar, 0).has_value());

  EXPECT_EQ(scalar("QUST\\Name").scalar.subrecord, fo4::Signature::from_string("FULL"));
  EXPECT_EQ(scalar("QUST\\Filter").scalar.subrecord, fo4::Signature::from_string("FLTR"));
  EXPECT_EQ(scalar("QUST\\Description").scalar.subrecord, fo4::Signature::from_string("NNAM"));
  EXPECT_EQ(scalar("QUST\\SwfFile").scalar.subrecord, fo4::Signature::from_string("SNAM"));

  EXPECT_EQ(link("QUST\\Location").subrecord, fo4::Signature::from_string("LNAM"));
  EXPECT_FALSE(link("QUST\\Location").nullable);
  EXPECT_EQ(link("QUST\\QuestCompletionXp").subrecord, fo4::Signature::from_string("XNAM"));
  EXPECT_FALSE(link("QUST\\QuestCompletionXp").nullable);
  EXPECT_EQ(link("QUST\\QuestGroup").subrecord, fo4::Signature::from_string("GNAM"));
  EXPECT_FALSE(link("QUST\\QuestGroup").nullable);
}

TEST(QustGeneralRows, ReadsTheGeneralDataAndLinks) {
  auto plugin = load();
  EXPECT_EQ(read_member(plugin, "QUST\\Name", 0), "Name");
  EXPECT_EQ(read_member(plugin, "QUST\\Flags", 0), "1");
  EXPECT_EQ(read_member(plugin, "QUST\\Priority", 0), "30");
  EXPECT_EQ(read_member(plugin, "QUST\\DelayTime", 0), "12.5");
  EXPECT_EQ(read_member(plugin, "QUST\\Type", 0), "6");
  EXPECT_EQ(read_member(plugin, "QUST\\Event", 0), "1280067915");
  EXPECT_EQ(read_member(plugin, "QUST\\Filter", 0), "FilterText");
  EXPECT_EQ(read_member(plugin, "QUST\\Description", 0), "Description");
  EXPECT_EQ(read_member(plugin, "QUST\\SwfFile", 0), "QuestMenu.swf");

  EXPECT_EQ(fo4::schema::scalar_value_name(scalar("QUST\\Type").scalar, 6), "Miscellaneous");
  EXPECT_EQ(fo4::schema::scalar_value_name(scalar("QUST\\Event").scalar, 1280067915),
            "Kill Actor Event");

  EXPECT_EQ(fo4::schema::formlink_field_target_allowed(link("QUST\\Location"),
                                                       fo4::Signature::from_string("LCTN")),
            true);
  EXPECT_EQ(fo4::schema::formlink_field_target_allowed(link("QUST\\Location"),
                                                       fo4::Signature::from_string("GLOB")),
            false);
}

TEST(QustGeneralRows, WritesEachGeneralFieldAndRoundTripsTheBlock) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Flags"), "2048"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Priority"), "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\DelayTime"), "4.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Type"), "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Event"), "1413894723"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("QUST\\Filter"), 0, "Other"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("QUST\\Description"), 0, "Other desc"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("QUST\\SwfFile"), 0, "Other.swf"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, record_key(), scalar("QUST\\Name"), 0, "Renamed"),
            fo4::ScalarMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(payload_of(after, "DNAM"), general_data(2048, 7, 4.5f, 1));
  EXPECT_EQ(payload_of(after, "ENAM"), u32_bytes(1413894723u));
  EXPECT_EQ(read_member(after, "QUST\\Name", 0), "Renamed");
  EXPECT_EQ(read_member(after, "QUST\\Filter", 0), "Other");
  EXPECT_EQ(read_member(after, "QUST\\Description", 0), "Other desc");
  EXPECT_EQ(read_member(after, "QUST\\SwfFile", 0), "Other.swf");
}

TEST(QustGeneralRows, TypeIsOneByteWideAndDelayTimeIsAFloatAtOffsetFour) {
  auto quest = general_pieces();
  quest[2].data[3] = std::byte{0x7F};
  quest[2].data[9] = std::byte{0xAA};
  quest[2].data[10] = std::byte{0xBB};
  quest[2].data[11] = std::byte{0xCC};
  auto plugin = load(quest);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\DelayTime"), "3.25"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Type"), "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Type"), "300"),
               fo4::schema::ScalarError);
  const auto after = reparse(plugin);
  EXPECT_EQ(read_member(after, "QUST\\DelayTime", 0), "3.25");
  EXPECT_EQ(read_member(after, "QUST\\Type", 0), "7");

  const auto payload = payload_of(after, "DNAM");
  ASSERT_EQ(payload.size(), 12u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[3]), 0x7F);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[8]), 7);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[9]), 0xAA);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[10]), 0xBB);
  EXPECT_EQ(std::to_integer<std::uint8_t>(payload[11]), 0xCC);
  EXPECT_EQ(Bytes(payload.begin() + 4, payload.begin() + 8),
            (std::vector<std::byte>{std::byte{0x00}, std::byte{0x00}, std::byte{0x50}, std::byte{0x40}}));
}

TEST(QustGeneralRows, RefusesGeneralValuesOutsideTheRowRange) {
  auto plugin = load();
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Priority"), "256"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Flags"), "65536"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QustGeneralRows, AShortDnamPayloadExposesNothingAndRefusesWrites) {
  auto quest = general_pieces();
  quest[2].data = u32_bytes(0);
  auto plugin = load(quest);
  const auto before = fo4::write_plugin(plugin);
  const auto dnam = payload_of(plugin, "DNAM");
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(dnam, scalar("QUST\\Flags").scalar).empty());
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(dnam, scalar("QUST\\Priority").scalar).empty());
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("QUST\\Priority"), "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QustGeneralRows, GeneralLinksEditAndRefuseNull) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("QUST\\QuestGroup"),
                                           keyword_key()),
            fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("QUST\\Location"),
                                           location_key()),
            fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("QUST\\QuestGroup"),
                                           spare_keyword_key()),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), link("QUST\\Location"),
                                           spare_location_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(payload_of(after, "GNAM"), u32_bytes(0x911));
  EXPECT_EQ(payload_of(after, "LNAM"), u32_bytes(0x921));

  auto refusing = load();
  const auto before = fo4::write_plugin(refusing);
  EXPECT_THROW(fo4::set_record_formlink_field(refusing, record_key(), link("QUST\\QuestGroup"),
                                              fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(fo4::write_plugin(refusing), before);
}

TEST(QustGeneralRows, TextDisplayGlobalsListReadsEditsAndResizes) {
  const auto descriptor = list("QUST\\TextDisplayGlobals");
  EXPECT_EQ(descriptor.record, k_qust);
  EXPECT_EQ(descriptor.subrecord, k_qtgl);
  EXPECT_TRUE(descriptor.can_insert);
  EXPECT_FALSE(descriptor.nullable);

  auto plugin = load();
  EXPECT_EQ(fo4::schema::record_formlink_list_size(plugin, record_key(), descriptor), 2u);
  const auto values = fo4::schema::record_formlink_list_values(plugin, record_key(), descriptor);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("000901:Patch.esp"));
  EXPECT_EQ((*values)[1], fo4::FormKey::parse("000902:Patch.esp"));

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, record_key(), descriptor, 1,
                                                          glob_key()),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record_key(), descriptor, 2,
                                                             fo4::FormKey::parse("000902:Patch.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, record_key(), descriptor, 0),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after),
            (std::vector<std::string>{"EDID", "FULL", "DNAM", "ENAM", "LNAM", "XNAM", "QTGL",
                                      "QTGL", "FLTR", "NNAM", "SNAM", "GNAM"}));
  EXPECT_EQ(fo4::schema::record_formlink_list_size(after, record_key(), descriptor), 2u);
  const auto reread = fo4::schema::record_formlink_list_values(after, record_key(), descriptor);
  ASSERT_TRUE(reread.has_value());
  EXPECT_EQ((*reread)[0], fo4::FormKey::parse("000900:Patch.esp"));
  EXPECT_EQ((*reread)[1], fo4::FormKey::parse("000902:Patch.esp"));
}

TEST(QustGeneralRows, TextDisplayGlobalsInsertBeforeFltrWhenTheListIsEmpty) {
  auto quest = general_pieces();
  quest.erase(quest.begin() + 6, quest.begin() + 8);
  auto plugin = load(quest);
  const auto descriptor = list("QUST\\TextDisplayGlobals");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record_key(), descriptor, 0,
                                                             glob_key()),
            fo4::schema::FormLinkListMutationStatus::updated);
  const auto after = reparse(plugin);
  EXPECT_EQ(signatures(after),
            (std::vector<std::string>{"EDID", "FULL", "DNAM", "ENAM", "LNAM", "XNAM", "QTGL",
                                      "FLTR", "NNAM", "SNAM", "GNAM"}));
  EXPECT_EQ(payload_of(after, "QTGL"), u32_bytes(0x900));
}

TEST(QustGeneralRows, AuthoringCreatesAQuestWithTheGeneralDataDefaults) {
  const auto constraint = fo4::schema::record_authoring_constraint(k_qust);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->placement, fo4::schema::RecordPlacement::top_level);
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  ASSERT_EQ(rules.size(), 11u);
  EXPECT_EQ(rules[3].signature, k_dnam);
  EXPECT_EQ(rules[3].minimum, 1);
  ASSERT_EQ(rules[3].default_bytes().size(), 12u);
  EXPECT_EQ(rules[3].default_bytes()[0], 0);

  auto plugin = load();
  const auto created = fo4::create_record(plugin, k_qust, "NewQuest");
  EXPECT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto after = reparse(plugin);
  const auto* record = fo4::find_record(after, created.key);
  ASSERT_NE(record, nullptr);
  std::vector<std::string> names;
  for (const auto& subrecord : record->subrecords) names.push_back(subrecord.signature.to_string());
  EXPECT_EQ(names, (std::vector<std::string>{"EDID", "DNAM"}));
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == k_dnam) {
      EXPECT_EQ(subrecord.data, Bytes(12));
    }
  }
}

TEST(QustGeneralRows, AuthoringOrderAcceptsNestedStageAndObjectiveSubrecords) {
  auto plugin = load(nested_condition_pieces());
  const auto issues = fo4::schema::preflight_authoring_record(plugin, record_key());
  for (const auto& issue : issues) {
    EXPECT_NE(issue.code, "subrecord_order_invalid") << issue.path;
    EXPECT_NE(issue.code, "subrecord_cardinality_invalid") << issue.path;
  }
  const auto constraint = fo4::schema::record_authoring_constraint(k_qust);
  ASSERT_TRUE(constraint.has_value());
  for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
    EXPECT_NE(rule.signature, fo4::Signature::from_string("CTDA"));
    EXPECT_NE(rule.signature, fo4::Signature::from_string("NNAM"));
  }
}

TEST(QustGeneralRows, ObjectiveDisplayTextIsNotTheQuestDescription) {
  const std::vector<Piece> head = {
      {"EDID", zstring("Q0")},
      {"FULL", zstring("Name")},
      {"DNAM", general_data(0x0001, 30, 12.5f, 6)},
      {"NNAM", zstring("Description")},
  };
  const std::vector<Piece> objectives = {
      {"QOBJ", Bytes{std::byte{1}, std::byte{0}}},
      {"NNAM", zstring("Objective one")},
      {"QSTA", Bytes(8, std::byte{0})},
      {"QOBJ", Bytes{std::byte{2}, std::byte{0}}},
      {"NNAM", zstring("Objective two")},
      {"QSTA", Bytes(8, std::byte{0})},
  };

  auto pieces = head;
  pieces.insert(pieces.end(), objectives.begin(), objectives.end());
  auto plugin = load(pieces);
  for (const auto& issue : fo4::schema::preflight_record(record_of(plugin))) {
    EXPECT_NE(issue.code, "duplicate_scalar_subrecord") << issue.path;
  }
  EXPECT_EQ(read_member(plugin, "QUST\\Description", 0), "Description");

  auto duplicated = head;
  duplicated.push_back({"NNAM", zstring("Second description")});
  duplicated.insert(duplicated.end(), objectives.begin(), objectives.end());
  const auto duplicate_plugin = load(duplicated);
  std::size_t flagged = 0;
  for (const auto& issue : fo4::schema::preflight_record(record_of(duplicate_plugin))) {
    if (issue.code == "duplicate_scalar_subrecord" && issue.path == "QUST\\Description") ++flagged;
  }
  EXPECT_EQ(flagged, 1u);
}

TEST(QustGeneralRows, AuthoringRefusesSignaturesOutsideTheGeneralBlock) {
  auto plugin = load(general_pieces());
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, record_key(), fo4::Signature::from_string("QOBJ")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, record_key(), k_dnam, 0),
            fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
