#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

fo4::Signature sig(std::string_view text) {
  return fo4::Signature::from_string(text);
}

std::vector<std::byte> empty_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

const fo4::Subrecord* subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& item : record.subrecords) {
    if (item.signature == sig(signature)) return &item;
  }
  return nullptr;
}

std::optional<fo4::schema::FormLinkListDescriptor> items_descriptor() {
  return fo4::schema::formlink_list_descriptor(sig("FLST"), "FLST\\Items");
}

std::vector<fo4::FormKey> list_values(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto descriptor = items_descriptor();
  EXPECT_TRUE(descriptor.has_value());
  if (!descriptor.has_value()) return {};
  const auto values = fo4::schema::record_formlink_list_values(plugin, key, *descriptor);
  EXPECT_TRUE(values.has_value());
  return values.value_or(std::vector<fo4::FormKey>{});
}

}

// Detects: keyword color and type must survive record creation and serialization.
TEST(KeywordCreation, ExplicitRulesWriteColorAndType) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");
  const auto constraint = fo4::schema::record_authoring_constraint(sig("KYWD"));
  ASSERT_TRUE(constraint.has_value());
  const auto generated = fo4::schema::authoring_subrecords(*constraint);
  std::vector<fo4::schema::SubrecordAuthoringConstraint> rules(generated.begin(),
                                                               generated.end());
  static const std::uint8_t color[4] = {0x33, 0x66, 0xCC, 0xFF};
  static const std::uint8_t type[4] = {6, 0, 0, 0};
  bool saw_color = false;
  bool saw_type = false;
  for (auto& rule : rules) {
    if (rule.signature == sig("CNAM")) {
      rule.minimum = 1;
      rule.maximum = 1;
      rule.default_data = color;
      rule.default_size = 4;
      saw_color = true;
    }
    if (rule.signature == sig("TNAM")) {
      rule.minimum = 1;
      rule.maximum = 1;
      rule.default_data = type;
      rule.default_size = 4;
      saw_type = true;
    }
  }
  ASSERT_TRUE(saw_color);
  ASSERT_TRUE(saw_type);

  const auto created = fo4::create_record(plugin, sig("KYWD"), "RadDiseaseKeyword", rules);
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);

  const auto disk = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(disk, created.key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature, sig("KYWD"));

  const auto* editor = subrecord(*record, "EDID");
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(editor->data.data())),
            "RadDiseaseKeyword");

  const auto* color_sub = subrecord(*record, "CNAM");
  ASSERT_NE(color_sub, nullptr);
  ASSERT_EQ(color_sub->data.size(), 4u);
  EXPECT_EQ(color_sub->data[0], std::byte{0x33});
  EXPECT_EQ(color_sub->data[1], std::byte{0x66});
  EXPECT_EQ(color_sub->data[2], std::byte{0xCC});
  EXPECT_EQ(color_sub->data[3], std::byte{0xFF});

  const auto* type_sub = subrecord(*record, "TNAM");
  ASSERT_NE(type_sub, nullptr);
  ASSERT_EQ(type_sub->data.size(), 4u);
  EXPECT_EQ(type_sub->data[0], std::byte{6});
}

// Detects: an ALCH record must retain required defaults and its magic effect after serialization.
TEST(AlchemyCreation, SchemaDefaultsPlusAnEffectSurviveARoundTrip) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");
  const auto magic = fo4::create_record(plugin, sig("MGEF"), "RadCureMagic");
  ASSERT_EQ(magic.status, fo4::RecordCreateStatus::created);

  const auto created = fo4::create_record(plugin, sig("ALCH"), "RadCure");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);
  for (const auto* signature : {"OBND", "DATA", "ENIT"}) {
    EXPECT_NE(subrecord(*record, signature), nullptr) << signature;
  }

  ASSERT_EQ(fo4::insert_record_list_element(plugin, created.key, "ALCH\\Effects", 0, nullptr),
            fo4::FieldStatus::ok);
  ASSERT_EQ(fo4::write_record_field(plugin, created.key, "ALCH\\EffectBaseEffect", 0, magic.key.to_string()),
            fo4::FieldStatus::ok);
  ASSERT_EQ(fo4::write_record_field(plugin, created.key, "ALCH\\EffectMagnitude", 0, "-10"), fo4::FieldStatus::ok);

  const auto disk = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* persisted = fo4::find_record(disk, created.key);
  ASSERT_NE(persisted, nullptr);
  std::size_t count = 99;
  ASSERT_EQ(fo4::record_list_size(disk, created.key, "ALCH\\Effects", count), fo4::FieldStatus::ok);
  ASSERT_EQ(count, 1u);
  std::string text;
  ASSERT_EQ(fo4::read_record_field(disk, *persisted, "ALCH\\EffectBaseEffect", 0, text), fo4::FieldStatus::ok);
  EXPECT_EQ(text, magic.key.to_string());
  ASSERT_EQ(fo4::read_record_field(disk, *persisted, "ALCH\\EffectMagnitude", 0, text), fo4::FieldStatus::ok);
  EXPECT_FLOAT_EQ(std::stof(text), -10.0f);
}

// Detects: a newly created form list must retain its entries after serialization.
TEST(FormListCreation, CreatedListAcceptsEntriesAndPersistsThem) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");
  const auto keyword = fo4::create_record(plugin, sig("KYWD"), "RadDiseaseKeyword");
  ASSERT_EQ(keyword.status, fo4::RecordCreateStatus::created);
  const auto list = fo4::create_record(plugin, sig("FLST"), "RadDiseaseList");
  ASSERT_EQ(list.status, fo4::RecordCreateStatus::created);

  const auto descriptor = items_descriptor();
  ASSERT_TRUE(descriptor.has_value());
  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, list.key, *descriptor), 0u);
  ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, list.key, *descriptor, 0,
                                                             keyword.key),
            fo4::schema::FormLinkListMutationStatus::updated);
  ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, list.key, *descriptor, 1,
                                                             keyword.key),
            fo4::schema::FormLinkListMutationStatus::updated);
  ASSERT_EQ(list_values(plugin, list.key).size(), 2u);

  auto disk = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto persisted = list_values(disk, list.key);
  ASSERT_EQ(persisted.size(), 2u);
  EXPECT_EQ(persisted[0], keyword.key);
  EXPECT_EQ(persisted[1], keyword.key);

  ASSERT_EQ(fo4::schema::remove_record_formlink_list_element(disk, list.key, *descriptor, 0),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(list_values(disk, list.key).size(), 1u);
}
