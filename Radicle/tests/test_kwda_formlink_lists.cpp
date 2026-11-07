#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

const fo4::Subrecord* find(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == sig) return &subrecord;
  }
  return nullptr;
}

std::size_t index_of(const fo4::Record& record, std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig) return i;
  }
  return record.subrecords.size();
}

std::uint32_t ksiz_value(const fo4::Record& record) {
  const auto* ksiz = find(record, "KSIZ");
  EXPECT_NE(ksiz, nullptr);
  EXPECT_EQ(ksiz->data.size(), 4u);
  std::uint32_t value = 0;
  if (ksiz != nullptr && ksiz->data.size() == 4) {
    std::memcpy(&value, ksiz->data.data(), sizeof(value));
  }
  return value;
}

std::vector<std::uint32_t> kwda_values(const fo4::Record& record) {
  std::vector<std::uint32_t> out;
  const auto* kwda = find(record, "KWDA");
  if (kwda == nullptr) return out;
  EXPECT_EQ(kwda->data.size() % 4, 0u);
  for (std::size_t offset = 0; offset + 4 <= kwda->data.size(); offset += 4) {
    std::uint32_t value = 0;
    std::memcpy(&value, kwda->data.data() + offset, sizeof(value));
    out.push_back(value);
  }
  return out;
}

// Two KYWD targets (0x801, 0x802) plus a third (0x803) to add, one MISC to prove
// the typed target rejects a non-keyword, a WEAP that already carries a KSIZ/KWDA
// keyword block after its ZNAM, and a WEAP with no keywords (EDID + ZNAM only).
std::vector<std::byte> plugin_with_keywords() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(6);
  hedr.u32(0x804);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto kywd_group = fo4::test::begin_group(out, "KYWD", 0);
  for (const std::uint32_t id : {0x00000801u, 0x00000802u, 0x00000803u}) {
    const auto kywd = fo4::test::begin_record(out, "KYWD", 0, id, 131);
    fo4::test::end_record(out, kywd);
  }
  fo4::test::end_group(out, kywd_group);

  const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
  const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000800, 131);
  fo4::test::end_record(out, misc);
  fo4::test::end_group(out, misc_group);

  const auto weap_group = fo4::test::begin_group(out, "WEAP", 0);

  auto full = fo4::test::begin_record(out, "WEAP", 0, 0x00000900, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("KeyedGun"));
  fo4::test::ByteWriter znam;
  znam.u32(0);
  fo4::test::write_subrecord(out, "ZNAM", znam.bytes);
  fo4::test::ByteWriter ksiz;
  ksiz.u32(2);
  fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
  fo4::test::ByteWriter kwda;
  kwda.u32(0x00000801);
  kwda.u32(0x00000802);
  fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
  fo4::test::end_record(out, full);

  auto empty = fo4::test::begin_record(out, "WEAP", 0, 0x00000901, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view("BareGun"));
  fo4::test::ByteWriter znam2;
  znam2.u32(0);
  fo4::test::write_subrecord(out, "ZNAM", znam2.bytes);
  fo4::test::end_record(out, empty);

  fo4::test::end_group(out, weap_group);
  return out.bytes;
}

fo4::schema::FormLinkListDescriptor keywords_descriptor() {
  const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("WEAP"), "Keywords");
  EXPECT_TRUE(descriptor.has_value());
  return descriptor.value_or(fo4::schema::FormLinkListDescriptor{});
}

std::vector<std::byte> plugin_with_bare_record(
    std::string_view record_sig,
    const std::vector<std::string_view>& subrecords) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x200);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto kywd_group = fo4::test::begin_group(out, "KYWD", 0);
  const auto kywd = fo4::test::begin_record(out, "KYWD", 0, 0x00000001, 131);
  fo4::test::end_record(out, kywd);
  fo4::test::end_group(out, kywd_group);

  const auto group = fo4::test::begin_group(out, record_sig, 0);
  auto rec = fo4::test::begin_record(out, record_sig, 0, 0x00000100, 131);
  for (const auto sub : subrecords) {
    fo4::test::ByteWriter payload;
    payload.u32(0);
    fo4::test::write_subrecord(out, sub, payload.bytes);
  }
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}  // namespace

TEST(KwdaFormLinkList, ResolvesKeywordsByFieldAlias) {
  const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("WEAP"), "Keywords");
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->record, fo4::Signature::from_string("WEAP"));
  EXPECT_EQ(descriptor->subrecord, fo4::Signature::from_string("KWDA"));
  EXPECT_EQ(descriptor->counter, fo4::Signature::from_string("KSIZ"));
  EXPECT_EQ(descriptor->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_EQ(descriptor->path, "WEAP\\Keywords");
  EXPECT_TRUE(descriptor->can_insert);
  EXPECT_FALSE(descriptor->wildcard_target);
  EXPECT_FALSE(descriptor->nullable);
}

TEST(KwdaFormLinkList, TypedTargetAcceptsOnlyKeyword) {
  const auto descriptor = keywords_descriptor();
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(
      descriptor, fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(
      descriptor, fo4::Signature::from_string("MISC")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(
      descriptor, fo4::Signature::from_string("WEAP")));
}

TEST(KwdaFormLinkList, CountsPackedElements) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto descriptor = keywords_descriptor();
  EXPECT_EQ(fo4::schema::record_formlink_list_size(
                plugin, fo4::FormKey::parse("000900:Guns.esp"), descriptor),
            2u);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(
                plugin, fo4::FormKey::parse("000901:Guns.esp"), descriptor),
            0u);
}

TEST(KwdaFormLinkList, AppendsIntoPackedArrayAndBumpsCounter) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto target = fo4::FormKey::parse("000803:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, key, descriptor, 2, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(ksiz_value(*weap), 3u);
  const auto values = kwda_values(*weap);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], 0x00000801u);
  EXPECT_EQ(values[1], 0x00000802u);
  EXPECT_EQ(values[2], 0x00000803u);
}

TEST(KwdaFormLinkList, RemovesMatchingValueAndDecrementsCounter) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto target = fo4::FormKey::parse("000801:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_elements_matching(
                plugin, key, descriptor, target),
            1u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(ksiz_value(*weap), 1u);
  const auto values = kwda_values(*weap);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], 0x00000802u);
}

TEST(KwdaFormLinkList, RemovingLastElementDropsKsizAndKwda) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_elements_matching(
                plugin, key, descriptor, fo4::FormKey::parse("000801:Guns.esp")),
            1u);
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_elements_matching(
                plugin, key, descriptor, fo4::FormKey::parse("000802:Guns.esp")),
            1u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(find(*weap, "KWDA"), nullptr);
  EXPECT_EQ(find(*weap, "KSIZ"), nullptr);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(reread, key, descriptor), 0u);
}

TEST(KwdaFormLinkList, CreatesKeywordBlockAfterAnchorWhenEmpty) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000901:Guns.esp");
  const auto target = fo4::FormKey::parse("000801:Guns.esp");
  const auto descriptor = keywords_descriptor();

  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, key, descriptor), 0u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, key, descriptor, 0, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(ksiz_value(*weap), 1u);
  const auto values = kwda_values(*weap);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], 0x00000801u);
  const auto znam = index_of(*weap, "ZNAM");
  const auto ksiz = index_of(*weap, "KSIZ");
  const auto kwda = index_of(*weap, "KWDA");
  EXPECT_EQ(ksiz, znam + 1);
  EXPECT_EQ(kwda, ksiz + 1);
}

TEST(KwdaFormLinkList, RejectsNonKeywordLocalTarget) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto misc = fo4::FormKey::parse("000800:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, key, descriptor, 2, misc),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
}

TEST(KwdaFormLinkList, OverwritesElementInPlace) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                plugin, key, descriptor, 0, fo4::FormKey::parse("000803:Guns.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(ksiz_value(*weap), 2u);
  const auto values = kwda_values(*weap);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 0x00000803u);
  EXPECT_EQ(values[1], 0x00000802u);
}

TEST(KwdaFormLinkList, SetRejectsNonKeywordLocalTarget) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(
                plugin, key, descriptor, 0, fo4::FormKey::parse("000800:Guns.esp")),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
}

TEST(KwdaFormLinkList, RemovesElementByIndex) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key, descriptor, 0),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(ksiz_value(*weap), 1u);
  const auto values = kwda_values(*weap);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], 0x00000802u);
}

TEST(KwdaFormLinkList, MovesElementWithinArray) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::move_record_formlink_list_element(plugin, key, descriptor, 0, 1),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(ksiz_value(*weap), 2u);
  const auto values = kwda_values(*weap);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 0x00000802u);
  EXPECT_EQ(values[1], 0x00000801u);
}

TEST(KwdaFormLinkList, ClearsAllKeywords) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto key = fo4::FormKey::parse("000900:Guns.esp");
  const auto descriptor = keywords_descriptor();

  EXPECT_EQ(fo4::schema::clear_record_formlink_list(plugin, key, descriptor),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  EXPECT_EQ(find(*weap, "KWDA"), nullptr);
  EXPECT_EQ(find(*weap, "KSIZ"), nullptr);
  EXPECT_EQ(fo4::schema::record_formlink_list_size(reread, key, descriptor), 0u);
}

TEST(KwdaFormLinkList, SparseWeapPlacesKeywordsCanonicallyNotAtEnd) {
  auto plugin = fo4::read_plugin(
      plugin_with_bare_record("WEAP", {"OBND", "DNAM"}), "Guns.esp");
  const auto key = fo4::FormKey::parse("000100:Guns.esp");
  const auto target = fo4::FormKey::parse("000001:Guns.esp");
  const auto descriptor = keywords_descriptor();

  ASSERT_EQ(fo4::schema::record_formlink_list_size(plugin, key, descriptor), 0u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, key, descriptor, 0, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Guns.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  const auto obnd = index_of(*weap, "OBND");
  const auto ksiz = index_of(*weap, "KSIZ");
  const auto kwda = index_of(*weap, "KWDA");
  const auto dnam = index_of(*weap, "DNAM");
  EXPECT_EQ(ksiz, obnd + 1);
  EXPECT_EQ(kwda, ksiz + 1);
  EXPECT_LT(kwda, dnam);
}

TEST(KwdaFormLinkList, EveryKeywordRecordAnchorsBeforeItsSuccessor) {
  struct Case {
    std::string_view record;
    std::string_view successor;
  };
  static constexpr Case kCases[] = {
    {"ACTI", "PRPS"}, {"ALCH", "MODL"}, {"AMMO", "DATA"}, {"ARMO", "DESC"},
    {"ARTO", "MODL"}, {"BOOK", "FIMD"}, {"CONT", "FTYP"}, {"DOOR", "NTRM"},
    {"FLOR", "PRPS"}, {"FURN", "PRPS"}, {"IDLM", "IDLF"}, {"INGR", "MODL"},
    {"KEYM", "DATA"}, {"LCTN", "PNAM"}, {"LIGH", "DEST"}, {"MGEF", "DATA"},
    {"MISC", "FIMD"}, {"MSTT", "PRPS"}, {"NPC_", "APPR"}, {"RACE", "PRPS"},
    {"SCEN", "NNAM"}, {"SPEL", "ETYP"}, {"TACT", "PNAM"}, {"TERM", "PRPS"},
    {"WEAP", "DESC"},
  };
  ASSERT_EQ(std::size(kCases), 25u);

  for (const auto& test_case : kCases) {
    SCOPED_TRACE(std::string(test_case.record));
    const auto descriptor = fo4::schema::formlink_list_descriptor_by_field(
        fo4::Signature::from_string(test_case.record), "Keywords");
    ASSERT_TRUE(descriptor.has_value());

    auto plugin = fo4::read_plugin(
        plugin_with_bare_record(test_case.record, {test_case.successor}), "Kw.esp");
    const auto key = fo4::FormKey::parse("000100:Kw.esp");
    const auto target = fo4::FormKey::parse("000001:Kw.esp");

    ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(
                  plugin, key, *descriptor, 0, target),
              fo4::schema::FormLinkListMutationStatus::updated);

    const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Kw.esp");
    const auto* record = fo4::find_record(reread, key);
    ASSERT_NE(record, nullptr);
    const auto ksiz = index_of(*record, "KSIZ");
    const auto kwda = index_of(*record, "KWDA");
    const auto succ = index_of(*record, test_case.successor);
    EXPECT_EQ(kwda, ksiz + 1);
    EXPECT_LT(kwda, succ);
  }
}

TEST(KwdaFormLinkList, AnchorsAfterEditorIdWhenItIsTheOnlyField) {
  auto plugin = fo4::read_plugin(
      plugin_with_bare_record("WEAP", {"EDID"}), "Kw.esp");
  const auto key = fo4::FormKey::parse("000100:Kw.esp");
  const auto target = fo4::FormKey::parse("000001:Kw.esp");
  const auto descriptor = keywords_descriptor();

  ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(
                plugin, key, descriptor, 0, target),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Kw.esp");
  const auto* weap = fo4::find_record(reread, key);
  ASSERT_NE(weap, nullptr);
  const auto edid = index_of(*weap, "EDID");
  const auto ksiz = index_of(*weap, "KSIZ");
  const auto kwda = index_of(*weap, "KWDA");
  EXPECT_EQ(ksiz, edid + 1);
  EXPECT_EQ(kwda, ksiz + 1);
}

TEST(KwdaFormLinkList, InnrIsNotAFlatKeywordRecord) {
  EXPECT_FALSE(fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("INNR"), "Keywords").has_value());
  EXPECT_FALSE(fo4::schema::formlink_list_descriptor(
      fo4::Signature::from_string("INNR"), "INNR\\Keywords").has_value());
}

TEST(SigBaseObjects, MatchesXeditUnionAndRejectsNonBaseRecords) {
  EXPECT_TRUE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("ACTI")));
  EXPECT_TRUE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("NPC_")));
  EXPECT_TRUE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("STAT")));
  EXPECT_TRUE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("FLST")));
  EXPECT_TRUE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("LVSP")));
  EXPECT_FALSE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("QUST")));
  EXPECT_FALSE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("KYWD")));
  EXPECT_FALSE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("FACT")));
  EXPECT_TRUE(fo4::schema::is_sig_base_object(fo4::Signature::from_string("ENCH")));
}

TEST(KwdaFormLinkList, ReadsPackedValuesAndDistinguishesEmptyFromMissingRecord) {
  auto plugin = fo4::read_plugin(plugin_with_keywords(), "Guns.esp");
  const auto descriptor = keywords_descriptor();
  const auto values = fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse("000900:Guns.esp"), descriptor);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("000801:Guns.esp"));
  EXPECT_EQ((*values)[1], fo4::FormKey::parse("000802:Guns.esp"));
  const auto empty = fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse("000901:Guns.esp"), descriptor);
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->empty());
  EXPECT_FALSE(fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse("000999:Guns.esp"), descriptor));
}
