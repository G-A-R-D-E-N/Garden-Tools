#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
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

constexpr auto k_arma = "ARMA";
constexpr auto k_sculpt_path = "ARMA\\SculptData";
constexpr auto k_gender_path = "ARMA\\SculptTargetGender";
constexpr auto k_arma_key = "000D01:Patch.esp";

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
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

std::vector<std::byte> make_plugin(const std::vector<Sub>& sculpt) {
  ByteWriter out;
  write_header(out);
  const auto group = fo4::test::begin_group(out, k_arma, 0);
  const auto record = fo4::test::begin_record(out, k_arma, 0, raw_id(0x0D01));
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleSculptAddon"));
  fo4::test::write_subrecord(out, "DNAM", Bytes(12));
  for (const auto& sub : sculpt) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<Sub> two_entries() {
  return {
      {"BSMP", u32_bytes(0)},
      {"BSMB", zstr("Head")},
      {"BSMS", floats({1.0f, 1.0f, 1.0f})},
      {"BSMP", u32_bytes(1)},
      {"BSMB", zstr("Neck")},
      {"BSMS", floats({2.0f, 2.0f, 2.0f})},
  };
}

fo4::Plugin load(const std::vector<Sub>& sculpt) {
  return fo4::read_plugin(make_plugin(sculpt), "Patch.esp");
}

const fo4::schema::SubrecordGroupDescriptor& sculpt_group() {
  static const auto group =
      fo4::schema::subrecord_group_descriptor(fo4::Signature::from_string(k_arma), k_sculpt_path);
  EXPECT_TRUE(group.has_value());
  return *group;
}

fo4::schema::ScalarFieldDescriptor gender_row() {
  const auto row =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(k_arma), k_gender_path);
  EXPECT_TRUE(row.has_value());
  return *row;
}

std::vector<Bytes> all_sub_bytes(const fo4::Plugin& plugin, std::string_view subrecord) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_arma_key));
  if (record == nullptr) {
    ADD_FAILURE() << k_arma_key;
    return out;
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) out.push_back(sub.data);
  }
  return out;
}

std::vector<std::string> subrecord_order(const fo4::Plugin& plugin) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_arma_key));
  if (record == nullptr) {
    ADD_FAILURE() << k_arma_key;
    return out;
  }
  for (const auto& sub : record->subrecords) out.push_back(sub.signature.to_string());
  return out;
}

}  // namespace

TEST(ARMASculptRows, GroupDescribesOneEntryPerBoneScaleSet) {
  const auto& group = sculpt_group();
  EXPECT_EQ(group.record, fo4::Signature::from_string(k_arma));
  EXPECT_EQ(group.count_subrecord, fo4::Signature{});
  ASSERT_EQ(group.members.size(), 3u);
  EXPECT_EQ(group.members[0].subrecord.to_string(), "BSMP");
  EXPECT_TRUE(group.members[0].required);
  EXPECT_FALSE(group.members[0].repeatable);
  EXPECT_EQ(group.members[0].payload_size, 4u);
  EXPECT_EQ(group.members[1].subrecord.to_string(), "BSMB");
  EXPECT_TRUE(group.members[1].required);
  EXPECT_TRUE(group.members[1].repeatable);
  EXPECT_EQ(group.members[1].payload_size, 0u);
  EXPECT_EQ(group.members[2].subrecord.to_string(), "BSMS");
  EXPECT_TRUE(group.members[2].required);
  EXPECT_TRUE(group.members[2].repeatable);
  EXPECT_EQ(group.members[2].payload_size, 12u);
  EXPECT_EQ(fo4::schema::subrecord_group_for_member(fo4::Signature::from_string(k_arma),
                                                   fo4::Signature::from_string("BSMB"))
                ->path,
            k_sculpt_path);

  const auto plugin = load(two_entries());
  const auto spans = fo4::subrecord_group_spans(plugin, fo4::FormKey::parse(k_arma_key), group);
  ASSERT_TRUE(spans.has_value());
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].first, 2u);
  EXPECT_EQ((*spans)[0].end, 5u);
  EXPECT_EQ((*spans)[1].first, 5u);
  EXPECT_EQ((*spans)[1].end, 8u);
  EXPECT_EQ(fo4::record_field_count(plugin, fo4::FormKey::parse(k_arma_key), k_gender_path),
            std::optional<std::size_t>{2});
  EXPECT_EQ(all_sub_bytes(plugin, "BSMB"), (std::vector<Bytes>{zstr("Head"), zstr("Neck")}));
  EXPECT_EQ(all_sub_bytes(plugin, "BSMS"),
            (std::vector<Bytes>{floats({1.0f, 1.0f, 1.0f}), floats({2.0f, 2.0f, 2.0f})}));
}

TEST(ARMASculptRows, TargetGenderRowAddressesOneEntryByIndex) {
  const auto row = gender_row();
  EXPECT_EQ(row.scalar.subrecord.to_string(), "BSMP");
  EXPECT_EQ(row.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(row.scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  EXPECT_EQ(row.scalar.names.size(), 2u);
  EXPECT_EQ(row.scalar.names[0].name, "Male");
  EXPECT_EQ(row.scalar.names[1].name, "Female");

  auto plugin = load(two_entries());
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_arma_key), row, 0, "1"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_arma_key), row, 1, "0"),
            fo4::ScalarMutationStatus::updated);
  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(all_sub_bytes(reread, "BSMP"), (std::vector<Bytes>{u32_bytes(1), u32_bytes(0)}));
  EXPECT_EQ(all_sub_bytes(reread, "BSMB"), (std::vector<Bytes>{zstr("Head"), zstr("Neck")}));

  EXPECT_EQ(fo4::set_record_scalar(reread, fo4::FormKey::parse(k_arma_key), row, 1, "1"),
            fo4::ScalarMutationStatus::updated);
  auto trimmed = fo4::read_plugin(fo4::write_plugin(reread), "Patch.esp");
  EXPECT_EQ(all_sub_bytes(trimmed, "BSMP"), (std::vector<Bytes>{u32_bytes(1), u32_bytes(1)}));

  EXPECT_EQ(fo4::set_record_scalar(trimmed, fo4::FormKey::parse(k_arma_key), row, 2, "0"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(fo4::set_record_scalar(trimmed, fo4::FormKey::parse(k_arma_key), row, 0, "Both"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(trimmed, fo4::FormKey::parse(k_arma_key), row, 0, "3"),
               fo4::schema::ScalarError);
  EXPECT_EQ(all_sub_bytes(trimmed, "BSMP"), (std::vector<Bytes>{u32_bytes(1), u32_bytes(1)}));
}

TEST(ARMASculptRows, InsertsAndRemovesAWholeSculptEntry) {
  const auto& group = sculpt_group();
  auto plugin = load(two_entries());
  EXPECT_EQ(fo4::insert_record_subrecord_group(plugin, fo4::FormKey::parse(k_arma_key), group, 1),
            fo4::SubrecordGroupMutationStatus::updated);

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_order(reread),
            (std::vector<std::string>{"EDID", "DNAM", "BSMP", "BSMB", "BSMS",
                                      "BSMP", "BSMB", "BSMS", "BSMP", "BSMB", "BSMS"}));
  EXPECT_EQ(all_sub_bytes(reread, "BSMP"),
            (std::vector<Bytes>{u32_bytes(0), u32_bytes(0), u32_bytes(1)}));
  EXPECT_EQ(all_sub_bytes(reread, "BSMB"),
            (std::vector<Bytes>{zstr("Head"), zstr(""), zstr("Neck")}));
  EXPECT_EQ(all_sub_bytes(reread, "BSMS"),
            (std::vector<Bytes>{floats({1.0f, 1.0f, 1.0f}), Bytes(12),
                                floats({2.0f, 2.0f, 2.0f})}));
  EXPECT_EQ(fo4::record_field_count(reread, fo4::FormKey::parse(k_arma_key), k_gender_path),
            std::optional<std::size_t>{3});
  EXPECT_EQ(fo4::set_record_scalar(reread, fo4::FormKey::parse(k_arma_key), gender_row(), 1, "1"),
            fo4::ScalarMutationStatus::updated);

  auto filled = fo4::read_plugin(fo4::write_plugin(reread), "Patch.esp");
  EXPECT_EQ(all_sub_bytes(filled, "BSMP"),
            (std::vector<Bytes>{u32_bytes(0), u32_bytes(1), u32_bytes(1)}));

  EXPECT_EQ(fo4::remove_record_subrecord_group(filled, fo4::FormKey::parse(k_arma_key), group, 1),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(fo4::remove_record_subrecord_group(filled, fo4::FormKey::parse(k_arma_key), group, 2),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
  auto trimmed = fo4::read_plugin(fo4::write_plugin(filled), "Patch.esp");
  EXPECT_EQ(all_sub_bytes(trimmed, "BSMP"), (std::vector<Bytes>{u32_bytes(0), u32_bytes(1)}));
  EXPECT_EQ(all_sub_bytes(trimmed, "BSMB"), (std::vector<Bytes>{zstr("Head"), zstr("Neck")}));
  EXPECT_EQ(all_sub_bytes(trimmed, "BSMS"),
            (std::vector<Bytes>{floats({1.0f, 1.0f, 1.0f}), floats({2.0f, 2.0f, 2.0f})}));
  EXPECT_EQ(subrecord_order(trimmed),
            (std::vector<std::string>{"EDID", "DNAM", "BSMP", "BSMB", "BSMS", "BSMP", "BSMB", "BSMS"}));
}

TEST(ARMASculptRows, RunsWithoutTheirMembersAreRefused) {
  const auto& group = sculpt_group();
  const auto key = fo4::FormKey::parse(k_arma_key);

  auto missing_name = load({
      {"BSMP", u32_bytes(0)},
      {"BSMS", floats({1.0f, 1.0f, 1.0f})},
  });
  EXPECT_THROW(fo4::subrecord_group_spans(missing_name, key, group),
               fo4::schema::SubrecordGroupError);
  EXPECT_THROW(fo4::insert_record_subrecord_group(missing_name, key, group, 1),
               fo4::schema::SubrecordGroupError);

  auto short_delta = load({
      {"BSMP", u32_bytes(0)},
      {"BSMB", zstr("Head")},
      {"BSMS", floats({1.0f, 1.0f})},
  });
  EXPECT_THROW(fo4::subrecord_group_spans(short_delta, key, group),
               fo4::schema::SubrecordGroupError);

  auto loose_name = load({
      {"BSMB", zstr("Head")},
      {"BSMP", u32_bytes(0)},
      {"BSMB", zstr("Neck")},
      {"BSMS", floats({1.0f, 1.0f, 1.0f})},
  });
  EXPECT_THROW(fo4::subrecord_group_spans(loose_name, key, group),
               fo4::schema::SubrecordGroupError);

  auto no_entries = load({});
  const auto spans = fo4::subrecord_group_spans(no_entries, key, group);
  ASSERT_TRUE(spans.has_value());
  EXPECT_TRUE(spans->empty());
  EXPECT_EQ(fo4::insert_record_subrecord_group(no_entries, key, group, 1),
            fo4::SubrecordGroupMutationStatus::index_out_of_range);
}
