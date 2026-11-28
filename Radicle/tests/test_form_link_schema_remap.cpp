#include "fo4/formid.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_link_schema.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t k_own_record = 0x00000802u;
constexpr std::uint32_t k_fallout_link = 0x00000800u;
constexpr std::uint32_t k_remapped_link = 0x01000800u;
constexpr std::uint32_t k_own_old = 0x01000800u;
constexpr std::uint32_t k_own_new = 0x01000801u;
constexpr std::uint32_t k_sentinel = 0x00000900u;

struct TestSubrecord {
  std::string signature;
  std::vector<std::byte> data;
};

std::vector<std::byte> zeroed(std::size_t size) {
  return std::vector<std::byte>(size, std::byte{0});
}

void put_value(std::vector<std::byte>& data, std::size_t offset, std::uint64_t value,
               std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) {
    data[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::size_t link_offset(const fo4::schema::FormLinkFieldDescriptor& field) {
  return field.address.array_offset + field.offset;
}

std::vector<const fo4::schema::FormLinkFieldDescriptor*> rows_for(std::string_view record,
                                                                  std::string_view subrecord) {
  std::vector<const fo4::schema::FormLinkFieldDescriptor*> rows;
  for (const auto& field : fo4::schema::k_generated_formlink_fields) {
    if (field.record.to_string() == record && field.subrecord.to_string() == subrecord) {
      rows.push_back(&field);
    }
  }
  return rows;
}

bool handler_owns(const fo4::Record& record, const fo4::Subrecord& sub) {
  return fo4::struct_payload_formid_offsets(record, sub, false).has_value() ||
         fo4::mixed_formid_offsets(record, sub, false).has_value();
}

bool owned_by_other_branch(const fo4::schema::FormLinkFieldDescriptor& field) {
  const auto record = field.record.to_string();
  const auto subrecord = field.subrecord.to_string();
  return record == "INFO" || record == "PACK" || (record == "NAVI" && subrecord == "NVMI") ||
         (record == "IDLE" && subrecord == "ANAM");
}

std::vector<TestSubrecord> row_subrecords(const fo4::schema::FormLinkFieldDescriptor& field) {
  const auto& address = field.address;
  const auto link = link_offset(field);
  auto size = field.payload_sizes[0] != 0 ? field.payload_sizes[0] : link + 4;
  if (field.payload_sizes[0] == 0 && address.stride != 0) {
    size = (size + address.stride - 1) / address.stride * address.stride;
  }
  std::vector<TestSubrecord> subs;
  if (address.when.count != 0) {
    const auto width = fo4::schema::discriminator_width(address.when.storage);
    auto value = zeroed(address.when.offset + width);
    put_value(value, address.when.offset, static_cast<std::uint64_t>(address.when.equals[0]),
              width);
    subs.push_back({address.when.subrecord.to_string(), std::move(value)});
  }
  auto target = zeroed(size);
  put_value(target, link, k_fallout_link, 4);
  subs.push_back({field.subrecord.to_string(), std::move(target)});
  return subs;
}

fo4::Plugin single_record_plugin(std::string_view name, std::string_view record_signature,
                                 const std::vector<TestSubrecord>& subrecords,
                                 const std::vector<std::string>& masters,
                                 std::uint32_t form_id) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, masters);
  const auto group = fo4::test::begin_group(out, record_signature, 0);
  const auto record = fo4::test::begin_record(out, record_signature, 0, form_id);
  for (const auto& sub : subrecords) {
    fo4::test::write_subrecord(out, sub.signature, sub.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, std::string(name));
}

std::size_t hits_at(const std::vector<fo4::FormLinkHit>& hits, std::string_view subrecord,
                    std::size_t offset) {
  return static_cast<std::size_t>(
      std::count_if(hits.begin(), hits.end(), [&](const fo4::FormLinkHit& hit) {
        return hit.subrecord.to_string() == subrecord && hit.offset == offset;
      }));
}

const fo4::Subrecord* find_sub(const fo4::Record& record, fo4::Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(FormLinkSchemaRemap, SchemaOffsetsCoverGeneratedRowsOutsideOwnedBranches) {
  std::size_t checked = 0;
  for (const auto& field : fo4::schema::k_generated_formlink_fields) {
    if (owned_by_other_branch(field)) continue;
    SCOPED_TRACE(std::string(field.path));
    ++checked;
    auto plugin = single_record_plugin("Schema.esp", field.record.to_string(),
                                             row_subrecords(field), {}, k_own_record);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000802:Schema.esp"));
    ASSERT_NE(record, nullptr);
    const auto offsets = fo4::schema::schema_formlink_offsets(*record);
    const auto target = record->subrecords.size() - 1;
    ASSERT_LT(target, offsets.size());
    if (handler_owns(*record, record->subrecords[target])) {
      EXPECT_TRUE(offsets[target].empty());
      continue;
    }
    EXPECT_TRUE(std::find(offsets[target].begin(), offsets[target].end(), link_offset(field)) !=
                offsets[target].end());
  }
  EXPECT_GT(checked, 0u);
}

TEST(FormLinkSchemaRemap, GeneratedRowsNeverTargetDeliberateNonFormIdSubrecords) {
  for (const auto& field : fo4::schema::k_generated_formlink_fields) {
    SCOPED_TRACE(std::string(field.path));
    auto plugin = single_record_plugin("Schema.esp", field.record.to_string(),
                                             row_subrecords(field), {}, k_own_record);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000802:Schema.esp"));
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(fo4::schema::deliberate_non_formid_subrecord(*record, record->subrecords.back()));
  }
}

TEST(FormLinkSchemaRemap, RemapChangesSchemaRowLinksAndLeavesNonLinkBytes) {
  const auto rows = rows_for("WEAP", "DNAM");
  ASSERT_FALSE(rows.empty());
  const auto size = rows.front()->payload_sizes[0];
  const auto gap = size - 4;
  auto dnam = zeroed(size);
  for (const auto* row : rows) {
    EXPECT_TRUE(link_offset(*row) + 4 <= gap || link_offset(*row) >= gap + 4);
    put_value(dnam, link_offset(*row), k_fallout_link, 4);
  }
  put_value(dnam, gap, k_sentinel, 4);

  auto plugin = single_record_plugin("Remap.esp", "WEAP", {{"DNAM", dnam}},
                                     {"Fallout4.esm", "Other.esm"}, 0x02000802u);
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("Other.esm"),
                                                     fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Remap.esp");
  const auto* weap = fo4::find_record(reread, fo4::FormKey::parse("000802:Remap.esp"));
  ASSERT_NE(weap, nullptr);
  const auto* sub = find_sub(*weap, fo4::Signature::from_string("DNAM"));
  ASSERT_NE(sub, nullptr);
  const auto hits = fo4::collect_form_links(reread);
  for (const auto* row : rows) {
    EXPECT_EQ(read_u32(sub->data, link_offset(*row)), k_remapped_link);
    EXPECT_EQ(hits_at(hits, "DNAM", link_offset(*row)), 1u);
  }
  EXPECT_EQ(read_u32(sub->data, gap), k_sentinel);
  EXPECT_EQ(hits_at(hits, "DNAM", gap), 0u);
}

TEST(FormLinkSchemaRemap, LinkAlsoListedByGlobalTableIsVisitedOnce) {
  const auto vend = rows_for("FACT", "VEND");
  const auto xnam = rows_for("FACT", "XNAM");
  ASSERT_FALSE(vend.empty());
  ASSERT_FALSE(xnam.empty());
  auto subs = row_subrecords(*vend.front());
  const auto extra = row_subrecords(*xnam.front());
  subs.insert(subs.end(), extra.begin(), extra.end());
  const auto hits =
      fo4::collect_form_links(single_record_plugin("Schema.esp", "FACT", subs, {}, k_own_record));
  EXPECT_EQ(hits_at(hits, "VEND", link_offset(*vend.front())), 1u);
  EXPECT_EQ(hits_at(hits, "XNAM", link_offset(*xnam.front())), 1u);
}

TEST(FormLinkSchemaRemap, PayloadSizeGatesSchemaRows) {
  const auto rows = rows_for("WEAP", "DNAM");
  ASSERT_FALSE(rows.empty());
  const auto size = rows.front()->payload_sizes[0];
  const auto at = link_offset(*rows.front());
  auto wrong = zeroed(size + 4);
  put_value(wrong, at, k_fallout_link, 4);
  auto right = zeroed(size);
  put_value(right, at, k_fallout_link, 4);
  EXPECT_EQ(hits_at(fo4::collect_form_links(single_record_plugin(
                        "Schema.esp", "WEAP", {{"DNAM", wrong}}, {}, k_own_record)),
                    "DNAM", at),
            0u);
  EXPECT_EQ(hits_at(fo4::collect_form_links(single_record_plugin(
                        "Schema.esp", "WEAP", {{"DNAM", right}}, {}, k_own_record)),
                    "DNAM", at),
            1u);
}

TEST(FormLinkSchemaRemap, RekeyChangesSchemaRowLinks) {
  const auto rows = rows_for("WEAP", "DNAM");
  ASSERT_FALSE(rows.empty());
  const auto size = rows.front()->payload_sizes[0];
  auto dnam = zeroed(size);
  for (const auto* row : rows) put_value(dnam, link_offset(*row), k_own_old, 4);
  auto plugin = single_record_plugin("Rekey.esp", "WEAP", {{"DNAM", dnam}}, {"Fallout4.esm"},
                                     0x01000802u);
  fo4::rekey_form_links(plugin, fo4::FormKey::parse("000800:Rekey.esp"),
                        fo4::FormKey::parse("000801:Rekey.esp"));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Rekey.esp");
  const auto* weap = fo4::find_record(reread, fo4::FormKey::parse("000802:Rekey.esp"));
  ASSERT_NE(weap, nullptr);
  const auto* sub = find_sub(*weap, fo4::Signature::from_string("DNAM"));
  ASSERT_NE(sub, nullptr);
  for (const auto* row : rows) {
    EXPECT_EQ(read_u32(sub->data, link_offset(*row)), k_own_new);
  }
}

TEST(FormLinkSchemaRemap, OversizedAmmoDnamBytesAreNotRemapped) {
  const auto rows = rows_for("AMMO", "DNAM");
  ASSERT_FALSE(rows.empty());
  auto dnam = zeroed(rows.front()->payload_sizes[0] + 4);
  for (const auto* row : rows) put_value(dnam, link_offset(*row), k_fallout_link, 4);
  const auto original = dnam;
  auto plugin = single_record_plugin("Remap.esp", "AMMO", {{"DNAM", dnam}},
                                     {"Fallout4.esm", "Other.esm"}, 0x02000802u);
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("Other.esm"),
                                                     fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Remap.esp");
  const auto* ammo = fo4::find_record(reread, fo4::FormKey::parse("000802:Remap.esp"));
  ASSERT_NE(ammo, nullptr);
  const auto* sub = find_sub(*ammo, fo4::Signature::from_string("DNAM"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->data, original);
}

TEST(FormLinkSchemaRemap, FailedWhenGateSkipsSchemaRow) {
  const auto rows = rows_for("SNDR", "BNAM");
  ASSERT_FALSE(rows.empty());
  const auto match = static_cast<std::uint64_t>(rows.front()->address.when.equals[0]);
  auto bnam = zeroed(4);
  put_value(bnam, 0, k_fallout_link, 4);
  for (const auto gate : {match, match + 1}) {
    auto cnam = zeroed(4);
    put_value(cnam, 0, gate, 4);
    auto plugin = single_record_plugin("Schema.esp", "SNDR",
                                             {{"CNAM", cnam}, {"BNAM", bnam}}, {}, k_own_record);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000802:Schema.esp"));
    ASSERT_NE(record, nullptr);
    const auto offsets = fo4::schema::schema_formlink_offsets(*record);
    ASSERT_EQ(offsets.size(), 2u);
    EXPECT_EQ(offsets[1].empty(), gate != match);
  }
}

TEST(FormLinkSchemaRemap, DuplicateNonRepeatedAmmoDnamYieldsNoSchemaHits) {
  const auto rows = rows_for("AMMO", "DNAM");
  ASSERT_FALSE(rows.empty());
  const auto size = rows.front()->payload_sizes[0];
  auto dnam = zeroed(size);
  for (const auto* row : rows) put_value(dnam, link_offset(*row), k_fallout_link, 4);
  const auto hits = fo4::collect_form_links(single_record_plugin(
      "Schema.esp", "AMMO", {{"DNAM", dnam}, {"DNAM", dnam}}, {}, k_own_record));
  for (const auto* row : rows) {
    EXPECT_EQ(hits_at(hits, "DNAM", link_offset(*row)), 0u);
  }
}

TEST(FormLinkSchemaRemap, AlchAddictionRowAddressesEnitOffsetEight) {
  const auto rows = rows_for("ALCH", "ENIT");
  const auto match = std::find_if(rows.begin(), rows.end(), [](const auto* field) {
    return field->path == "ALCH\\Addiction";
  });
  ASSERT_NE(match, rows.end());
  EXPECT_EQ((*match)->address.array_offset, 0u);
  EXPECT_EQ((*match)->offset, 8u);
  EXPECT_EQ((*match)->payload_sizes[0], 20u);
  ASSERT_EQ((*match)->target_count, 1u);
  EXPECT_EQ(fo4::schema::k_generated_formlink_field_targets[(*match)->first_target].to_string(),
            "SPEL");
  auto enit = zeroed(20);
  put_value(enit, 8, k_fallout_link, 4);
  const auto hits = fo4::collect_form_links(
      single_record_plugin("Schema.esp", "ALCH", {{"ENIT", enit}}, {}, k_own_record));
  EXPECT_EQ(hits_at(hits, "ENIT", 8), 1u);
}

TEST(FormLinkSchemaRemap, BookTeachesRowIsMaskGuardedByDnamFlags) {
  const auto rows = rows_for("BOOK", "DNAM");
  const auto match = std::find_if(rows.begin(), rows.end(), [](const auto* field) {
    return field->path == "BOOK\\Teaches";
  });
  ASSERT_NE(match, rows.end());
  EXPECT_EQ((*match)->offset, 1u);
  EXPECT_EQ((*match)->payload_sizes[0], 13u);
  EXPECT_EQ((*match)->address.when.bits, 0x15u);
  ASSERT_EQ((*match)->target_count, 3u);

  const auto slots_for = [&](std::uint8_t flags) {
    auto dnam = zeroed(13);
    put_value(dnam, 0, flags, 1);
    put_value(dnam, 1, k_fallout_link, 4);
    auto plugin = single_record_plugin("Schema.esp", "BOOK", {{"DNAM", dnam}}, {}, k_own_record);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000802:Schema.esp"));
    EXPECT_NE(record, nullptr);
    fo4::schema::FieldShape shape{};
    shape.subrecord = (*match)->subrecord;
    shape.offset = (*match)->offset;
    shape.width = 4;
    shape.payload_size = 13;
    return fo4::schema::resolve_field_slots(*record, shape, (*match)->address);
  };
  const auto spell = slots_for(0x04);
  ASSERT_EQ(spell.slots.size(), 1u);
  EXPECT_TRUE(spell.slots.front().valid);
  const auto not_teach = slots_for(0x02);
  ASSERT_EQ(not_teach.slots.size(), 1u);
  EXPECT_FALSE(not_teach.slots.front().valid);
}

TEST(FormLinkSchemaRemap, TeachingFlagsDecideWhetherTheTeachesLinkIsAttributed) {
  const auto fields = fo4::schema::formlink_fields(fo4::Signature::from_string("BOOK"));
  const auto subrecord = fo4::Signature::from_string("DNAM");
  const auto attribute = [&](std::uint8_t flags) {
    auto dnam = zeroed(13);
    put_value(dnam, 0, flags, 1);
    put_value(dnam, 1, k_fallout_link, 4);
    auto plugin = single_record_plugin("Schema.esp", "BOOK", {{"DNAM", dnam}}, {}, k_own_record);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000802:Schema.esp"));
    EXPECT_NE(record, nullptr);
    return fo4::schema::formlink_field_at(fields, *record, subrecord, 0, 1);
  };
  const auto teaching = attribute(0x04);
  ASSERT_TRUE(teaching.has_value());
  EXPECT_EQ(teaching->path, "BOOK\\Teaches");
  EXPECT_FALSE(attribute(0x02).has_value());
}

TEST(FormLinkSchemaRemap, DiscriminatorMaskAndUncappedValueSets) {
  static constexpr std::int64_t k_extra_archetypes[] = {40, 45, 46};
  auto data = zeroed(152);
  put_value(data, 64, 46, 4);
  auto plugin = single_record_plugin("Schema.esp", "MGEF", {{"DATA", data}}, {}, k_own_record);
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000802:Schema.esp"));
  ASSERT_NE(record, nullptr);

  fo4::schema::FieldDiscriminator archetypes{};
  archetypes.subrecord = fo4::Signature::from_string("DATA");
  archetypes.offset = 64;
  archetypes.storage = fo4::schema::DiscriminatorStorage::u32;
  archetypes.count = 8;
  archetypes.equals = {12, 17, 18, 25, 34, 35, 36, 39};
  archetypes.extra = k_extra_archetypes;
  archetypes.extra_count = 3;
  EXPECT_TRUE(fo4::schema::record_discriminator_matches(*record, archetypes));

  fo4::schema::FieldDiscriminator mask{};
  mask.subrecord = fo4::Signature::from_string("DATA");
  mask.offset = 64;
  mask.storage = fo4::schema::DiscriminatorStorage::u32;
  mask.bits = 0x15;
  EXPECT_TRUE(fo4::schema::record_discriminator_matches(*record, mask));
  mask.bits = 0x01;
  EXPECT_FALSE(fo4::schema::record_discriminator_matches(*record, mask));
}

TEST(FormLinkSchemaRemap, MalformedStructPayloadYieldsNoSchemaHits) {
  const auto rows = rows_for("EXPL", "DATA");
  ASSERT_FALSE(rows.empty());
  auto data = zeroed(40);
  for (const auto* row : rows) put_value(data, link_offset(*row), k_fallout_link, 4);
  const auto hits = fo4::collect_form_links(
      single_record_plugin("Schema.esp", "EXPL", {{"DATA", data}}, {}, k_own_record));
  for (const auto* row : rows) {
    EXPECT_EQ(hits_at(hits, "DATA", link_offset(*row)), 0u);
  }
}
