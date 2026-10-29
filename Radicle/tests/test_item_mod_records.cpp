#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
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
  std::string_view link_target;
};

constexpr Item k_items[] = {
    {"NOTE", "000810:Patch.esp", "NOTE\\Value", "250", "NOTE\\Model", "NOTE\\PickUpSound", "000808:Patch.esp"},
    {"ENCH", "000811:Patch.esp", "ENCH\\Cost", "7", "", "ENCH\\BaseEnchantment", "000817:Patch.esp"},
    {"AMDL", "000812:Patch.esp", "AMDL\\RunawayRecoilShots", "13","", "", ""},
    {"AORU", "000813:Patch.esp", "AORU\\Radius", "700", "", "", ""},
    {"PKIN", "000814:Patch.esp", "PKIN\\Version", "2", "PKIN\\Filter", "PKIN\\Cell", "00081B:Patch.esp"},
    {"DOBJ", "000815:Patch.esp", "", "", "", "", ""},
    {"INNR", "000816:Patch.esp", "INNR\\Target", "43", "", "", ""},
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

Bytes kwda_ids(std::initializer_list<std::uint32_t> ids) {
  ByteWriter out;
  for (const auto id : ids) {
    out.u32(raw_id(id));
  }
  return out.bytes;
}

Bytes innr_property(float value, std::uint8_t target, std::uint8_t operation) {
  ByteWriter out;
  out.f32(value);
  out.u8(target);
  out.u8(operation);
  return out.bytes;
}

Bytes note_type_dnam() {
  ByteWriter out;
  out.u8(1);
  return out.bytes;
}

Bytes note_data() {
  ByteWriter out;
  out.u32(100);
  out.f32(2.5f);
  return out.bytes;
}

Bytes note_modt(const std::vector<std::uint32_t>& addon_nodes = {42u}) {
  ByteWriter out;
  out.u32(4);
  out.u32(1);
  out.u32(static_cast<std::uint32_t>(addon_nodes.size()));
  out.u32(0);
  out.u32(1);
  out.u32(0x0A0B0C0Du);
  out.sig("dds");
  out.u32(0x11121314u);
  for (const auto node : addon_nodes) {
    out.u32(node);
  }
  out.u32(0x71727374u);
  out.sig("bgsm");
  out.u32(0x81828384u);
  return out.bytes;
}

Bytes ench_enit() {
  ByteWriter out;
  out.i32(5);
  out.u32(0x4u);
  out.u32(0);
  out.i32(10);
  out.u32(0);
  out.u32(6);
  out.f32(1.0f);
  out.u32(0);
  out.u32(0);
  return out.bytes;
}

Bytes amdl_dnam() {
  ByteWriter out;
  for (std::uint32_t index = 0; index < 16; ++index) {
    if (index == 4 || index == 11) {
      out.u32(index + 1);
    } else {
      out.f32(1.0f + static_cast<float>(index));
    }
  }
  return out.bytes;
}

Bytes aor2() {
  ByteWriter out;
  out.f32(600.0f);
  out.f32(1.0f);
  out.f32(30.0f);
  out.u8(1);
  out.u8(0);
  out.u8(0);
  out.u8(0);
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
  write_group(out, "CELL", {{0x81A, {{"EDID", zstr("RadicleCellA")}}},
                            {0x81B, {{"EDID", zstr("RadicleCellB")}}}});
  write_group(out, "NOTE", {{0x810, {{"EDID", zstr("RadicleNote")},
                                     {"OBND", zeros(12)},
                                     {"MODL", zstr("Note.nif")},
                                     {"MODT", note_modt()},
                                     {"ICON", zstr("Icon.dds")},
                                     {"YNAM", u32_bytes(raw_id(0x800))},
                                     {"ZNAM", u32_bytes(raw_id(0x800))},
                                     {"DNAM", note_type_dnam()},
                                     {"DATA", note_data()},
                                     {"PNAM", zstr("Program.psc")}}}});
  write_group(out, "ENCH", {{0x811, {{"EDID", zstr("RadicleEnchant")},
                                     {"OBND", zeros(12)},
                                     {"FULL", zstr("Enchant")},
                                     {"ENIT", ench_enit()}}},
                            {0x817, {{"EDID", zstr("RadicleEnchantB")},
                                     {"OBND", zeros(12)},
                                     {"ENIT", ench_enit()}}}});
  write_group(out, "AMDL", {{0x812, {{"EDID", zstr("RadicleAimModel")},
                                     {"DNAM", amdl_dnam()}}}});
  write_group(out, "AORU", {{0x813, {{"EDID", zstr("RadicleAttraction")},
                                     {"AOR2", aor2()}}}});
  write_group(out, "PKIN", {{0x814, {{"EDID", zstr("RadiclePackIn")},
                                     {"OBND", zeros(12)},
                                     {"FLTR", zstr("Filter.txt")},
                                     {"CNAM", u32_bytes(raw_id(0x81A))},
                                     {"VNAM", u32_bytes(1)}}}});
  write_group(out, "DOBJ", {{0x815, {{"EDID", zstr("RadicleObjects")},
                                     {"DNAM", zeros(8)}}}});
  write_group(out, "INNR", {{0x816, {{"EDID", zstr("RadicleNaming")},
                                     {"UNAM", u32_bytes(29)},
                                     {"VNAM", u32_bytes(2)},
                                     {"WNAM", zstr("RadicleRuleA")},
                                     {"KSIZ", u32_bytes(1)},
                                     {"KWDA", kwda_ids({0x81A})},
                                     {"XNAM", innr_property(1.5f, 4, 0)},
                                     {"YNAM", u16_bytes(0)},
                                     {"WNAM", zstr("RadicleRuleB")},
                                     {"KSIZ", u32_bytes(2)},
                                     {"KWDA", kwda_ids({0x81A, 0x81B})},
                                     {"XNAM", innr_property(1.5f, 4, 0)},
                                     {"YNAM", u16_bytes(7)}}}});
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

Bytes subrecord_at(const fo4::Record& record, std::string_view signature, std::size_t ordinal) {
  const auto wanted = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != wanted) continue;
    if (seen++ == ordinal) return sub.data;
  }
  return {};
}

}  // namespace

TEST(ItemModRecordsSynthetic, EveryScalarRowDecodesOnTheFixture) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(item.key));
    ASSERT_NE(record, nullptr) << item.signature;
    const auto fields = fo4::schema::scalar_fields(signature);
    std::size_t decoded = 0;
    for (const auto& field : fields) {
      const auto* sub = find_subrecord(*record, field.scalar.subrecord);
      ASSERT_NE(sub, nullptr) << field.path;
      const auto bytes = fo4::schema::scalar_field_bytes(sub->data, field.scalar);
      ASSERT_FALSE(bytes.empty()) << field.path;
      EXPECT_NO_THROW(static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar)))
          << field.path;
      ++decoded;
    }
    EXPECT_EQ(decoded, fields.size()) << item.signature;
  }
}

TEST(ItemModRecordsSynthetic, ModelInfoSegmentRowsReadAndWriteAtTheirSegmentBase) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000810:Patch.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  const auto read_row = [](const fo4::Record& from, std::string_view path) {
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("NOTE"), path);
    EXPECT_TRUE(field.has_value()) << path;
    if (!field) return std::string{};
    const auto* sub = find_subrecord(from, field->scalar.subrecord);
    EXPECT_NE(sub, nullptr) << path;
    if (sub == nullptr) return std::string{};
    return fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(sub->data, field->scalar),
                                      field->scalar);
  };

  EXPECT_EQ(read_row(*record, "NOTE\\ModelInfoTextureFileHash"), std::to_string(0x0A0B0C0Du));
  EXPECT_EQ(read_row(*record, "NOTE\\ModelInfoAddonNode"), "42");
  EXPECT_EQ(read_row(*record, "NOTE\\ModelInfoMaterialFileHash"), std::to_string(0x71727374u));

  const auto addon = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("NOTE"),
                                                          "NOTE\\ModelInfoAddonNode");
  ASSERT_TRUE(addon.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *addon, 0, "7"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* saved = fo4::find_record(reread, key);
  ASSERT_NE(saved, nullptr);
  EXPECT_EQ(read_row(*saved, "NOTE\\ModelInfoAddonNode"), "7");
  EXPECT_EQ(read_row(*saved, "NOTE\\ModelInfoTextureFileHash"), std::to_string(0x0A0B0C0Du));
  EXPECT_EQ(read_row(*saved, "NOTE\\ModelInfoMaterialFileHash"), std::to_string(0x71727374u));
  const auto* modt = find_subrecord(*saved, fo4::Signature::from_string("MODT"));
  ASSERT_NE(modt, nullptr);
  EXPECT_EQ(modt->data, note_modt({7u}));

  EXPECT_TRUE(fo4::schema::scalar_field_bytes(note_modt({}), addon->scalar).empty());
}

TEST(ItemModRecordsSynthetic, FlagsRowNamesComeFromXEdit) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ENCH"),
                                                          "ENCH\\Flags");
  ASSERT_TRUE(field.has_value());
  const auto name = fo4::schema::scalar_value_name(field->scalar, 4);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, "Extend Duration On Recast");
}

TEST(ItemModRecordsSynthetic, EditsOneRowPerRecordAndRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record = fo4::FormKey::parse(item.key);
    if (!item.scalar_path.empty()) {
      const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
      ASSERT_TRUE(scalar.has_value()) << item.scalar_path;
      EXPECT_EQ(fo4::set_record_scalar(plugin, record, *scalar, item.scalar_value),
                fo4::ScalarMutationStatus::updated)
          << item.scalar_path;
    }
    if (!item.text_path.empty()) {
      EXPECT_EQ(fo4::set_record_string(plugin, record, item.text_path, 0, "Edited.nif"),
                fo4::ScalarMutationStatus::updated)
          << item.text_path;
    }
    if (!item.link_path.empty()) {
      const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
      ASSERT_TRUE(link.has_value()) << item.link_path;
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *link,
                                               fo4::FormKey::parse(item.link_target)),
                fo4::FormLinkFieldMutationStatus::updated)
          << item.link_path;
    }
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto hits = fo4::collect_form_links(reread);
  for (const auto& item : k_items) {
    const auto signature = fo4::Signature::from_string(item.signature);
    const auto record_key = fo4::FormKey::parse(item.key);
    const auto* record = fo4::find_record(reread, record_key);
    ASSERT_NE(record, nullptr) << item.signature;

    if (!item.scalar_path.empty()) {
      const auto scalar = fo4::schema::scalar_field_descriptor(signature, item.scalar_path);
      ASSERT_TRUE(scalar.has_value());
      const auto* scalar_sub = find_subrecord(*record, scalar->scalar.subrecord);
      ASSERT_NE(scalar_sub, nullptr);
      const auto expected = fo4::schema::encode_scalar(item.scalar_value, scalar->scalar);
      const auto actual = fo4::schema::scalar_field_bytes(scalar_sub->data, scalar->scalar);
      EXPECT_EQ(Bytes(actual.begin(), actual.end()), expected) << item.scalar_path;
    }

    if (!item.text_path.empty()) {
      const auto text = fo4::schema::scalar_field_descriptor(signature, item.text_path);
      ASSERT_TRUE(text.has_value());
      const auto* text_sub = find_subrecord(*record, text->scalar.subrecord);
      ASSERT_NE(text_sub, nullptr);
      EXPECT_EQ(text_sub->data, zstr("Edited.nif")) << item.text_path;
    }

    if (!item.link_path.empty()) {
      const auto link = fo4::schema::formlink_field_descriptor(signature, item.link_path);
      ASSERT_TRUE(link.has_value());
      const auto target = fo4::FormKey::parse(item.link_target);
      const bool found = std::any_of(hits.begin(), hits.end(), [&](const fo4::FormLinkHit& hit) {
        return hit.record == record_key && hit.subrecord == link->subrecord && hit.key == target;
      });
      EXPECT_TRUE(found) << item.link_path;
    }
  }
}

TEST(ItemModRecordsSynthetic, InnrNamingRuleRowsAddressEachName) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto innr = fo4::Signature::from_string("INNR");
  const auto key = fo4::FormKey::parse("000816:Patch.esp");

  const auto rulesets = fo4::schema::scalar_field_descriptor(innr, "INNR\\RulesetCount");
  const auto text = fo4::schema::scalar_field_descriptor(innr, "INNR\\NameText");
  const auto count = fo4::schema::scalar_field_descriptor(innr, "INNR\\KeywordCount");
  const auto index = fo4::schema::scalar_field_descriptor(innr, "INNR\\NameIndex");
  const auto value = fo4::schema::scalar_field_descriptor(innr, "INNR\\PropertyValue");
  const auto target = fo4::schema::scalar_field_descriptor(innr, "INNR\\PropertyTarget");
  const auto operation = fo4::schema::scalar_field_descriptor(innr, "INNR\\PropertyOperator");
  ASSERT_TRUE(rulesets.has_value());
  ASSERT_TRUE(text.has_value());
  ASSERT_TRUE(count.has_value());
  ASSERT_TRUE(index.has_value());
  ASSERT_TRUE(value.has_value());
  ASSERT_TRUE(target.has_value());
  ASSERT_TRUE(operation.has_value());

  EXPECT_EQ(fo4::record_field_count(plugin, key, *rulesets), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *text), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *count), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *index), 2u);
  EXPECT_EQ(fo4::record_field_count(plugin, key, *value), 2u);

  const auto* before = fo4::find_record(plugin, key);
  ASSERT_NE(before, nullptr);
  EXPECT_EQ(subrecord_at(*before, "WNAM", 0), zstr("RadicleRuleA"));
  EXPECT_EQ(subrecord_at(*before, "WNAM", 1), zstr("RadicleRuleB"));
  EXPECT_EQ(subrecord_at(*before, "KSIZ", 1), u32_bytes(2));
  EXPECT_EQ(subrecord_at(*before, "YNAM", 1), u16_bytes(7));
  EXPECT_EQ(subrecord_at(*before, "XNAM", 1), innr_property(1.5f, 4, 0));

  const auto xnam = subrecord_at(*before, "XNAM", 0);
  const auto ynam = subrecord_at(*before, "YNAM", 1);
  const auto ksiz = subrecord_at(*before, "KSIZ", 1);
  const auto value_bytes = fo4::schema::scalar_field_bytes(xnam, value->scalar);
  EXPECT_EQ(Bytes(value_bytes.begin(), value_bytes.end()), u32_bytes(0x3FC00000u));
  EXPECT_EQ(fo4::schema::decode_scalar(value_bytes, value->scalar), "1.5");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(xnam, target->scalar),
                                       target->scalar), "4");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(xnam, operation->scalar),
                                       operation->scalar), "0");
  EXPECT_EQ(fo4::schema::decode_scalar(ynam, index->scalar), "7");
  EXPECT_EQ(fo4::schema::decode_scalar(ksiz, count->scalar), "2");

  const auto untouched = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *index, 2, "4"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_string(plugin, key, *text, 2, "RadicleRuleD"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *rulesets, 0, "9"), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *count, 0, "3"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(untouched));

  EXPECT_EQ(fo4::set_record_string(plugin, key, *text, 1, "RadicleRuleC"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *index, 1, "9"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* after = fo4::find_record(reread, key);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(subrecord_at(*after, "WNAM", 0), zstr("RadicleRuleA"));
  EXPECT_EQ(subrecord_at(*after, "WNAM", 1), zstr("RadicleRuleC"));
  EXPECT_EQ(subrecord_at(*after, "YNAM", 0), u16_bytes(0));
  EXPECT_EQ(subrecord_at(*after, "YNAM", 1), u16_bytes(9));
  EXPECT_EQ(subrecord_at(*after, "KSIZ", 1), u32_bytes(2));
  EXPECT_EQ(subrecord_at(*after, "KWDA", 0), kwda_ids({0x81A}));
  EXPECT_EQ(subrecord_at(*after, "KWDA", 1), kwda_ids({0x81A, 0x81B}));
  EXPECT_EQ(subrecord_at(*after, "XNAM", 1), innr_property(1.5f, 4, 0));
  EXPECT_EQ(subrecord_at(*after, "VNAM", 0), u32_bytes(2));
}

TEST(ItemModRecordsSynthetic, FixtureRecordsPassAuthoringPreflight) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (const auto& item : k_items) {
    const auto issues =
        fo4::schema::preflight_authoring_record(plugin, fo4::FormKey::parse(item.key));
    EXPECT_TRUE(issues.empty()) << item.signature;
  }
}
