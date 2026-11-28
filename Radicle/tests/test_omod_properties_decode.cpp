#include "npc_visual_fixture.hpp"

#include "fo4/c_api_records.h"
#include "fo4/c_api_ui_records.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using npc_visual_fixture::Bytes;
using npc_visual_fixture::Rec;
using npc_visual_fixture::build;
using npc_visual_fixture::zstr;

constexpr std::uint32_t k_base_keyword = 0x800;
constexpr std::uint32_t k_own_keyword = 0x801;
constexpr std::uint32_t k_extra_keyword = 0x802;
constexpr std::uint32_t k_shapes = 0x810;
constexpr std::uint32_t k_master_index = 0x811;
constexpr std::uint32_t k_second_master_index = 0x812;
constexpr std::uint32_t k_own_index = 0x813;
constexpr std::uint32_t k_past_own_index = 0x814;
constexpr std::uint32_t k_created_index = 0x815;
constexpr std::uint32_t k_plain_values_unchecked = 0x816;
constexpr std::uint32_t k_count_past_rows = 0x817;
constexpr std::uint32_t k_trailing_bytes = 0x818;
constexpr std::uint32_t k_cut_row = 0x819;
constexpr std::uint32_t k_short_header = 0x81A;
constexpr std::uint32_t k_no_data = 0x81B;
constexpr std::uint32_t k_empty = 0x81C;
constexpr std::uint32_t k_form_id_null = 0x81D;
constexpr std::uint32_t k_wrong_signature = 0x81E;
constexpr std::uint32_t k_vanilla_shape = 0x820;

constexpr std::uint8_t k_int = 0;
constexpr std::uint8_t k_float = 1;
constexpr std::uint8_t k_bool = 2;
constexpr std::uint8_t k_form_int = 4;
constexpr std::uint8_t k_enum = 5;
constexpr std::uint8_t k_form_float = 6;

constexpr std::uint32_t patch_id(std::uint32_t local) { return 0x02000000u | local; }

Bytes omod_row(std::uint8_t type, std::uint8_t function, std::uint16_t property, std::uint32_t value1,
               std::uint32_t value2, float step) {
  fo4::test::ByteWriter out;
  out.u8(type);
  for (int i = 0; i < 3; ++i) out.u8(std::uint8_t{0});
  out.u8(function);
  for (int i = 0; i < 3; ++i) out.u8(std::uint8_t{0});
  out.u16(property);
  out.u16(std::uint16_t{0});
  out.u32(value1);
  out.u32(value2);
  out.f32(step);
  return out.bytes;
}

Bytes omod_data(const std::vector<Bytes>& rows, std::uint32_t declared) {
  fo4::test::ByteWriter out;
  out.u32(std::uint32_t{0});
  out.u32(declared);
  out.u16(std::uint16_t{0});
  out.u32(std::uint32_t{0});
  out.u16(std::uint16_t{0});
  out.u32(std::uint32_t{0});
  out.u32(std::uint32_t{0});
  out.u32(std::uint32_t{0});
  for (const auto& row : rows) out.bytes.insert(out.bytes.end(), row.begin(), row.end());
  return out.bytes;
}

Bytes omod_data(const std::vector<Bytes>& rows) {
  return omod_data(rows, static_cast<std::uint32_t>(rows.size()));
}

Bytes include_row(std::uint32_t mod, std::uint8_t level, std::uint8_t optional, std::uint8_t dont_use_all) {
  fo4::test::ByteWriter out;
  out.u32(mod);
  out.u8(level);
  out.u8(optional);
  out.u8(dont_use_all);
  return out.bytes;
}

Bytes omod_vanilla_shaped(std::uint32_t include_count, std::uint32_t attach_point, std::initializer_list<std::uint32_t> slots,
                          std::vector<Bytes> includes, const std::vector<Bytes>& properties) {
  fo4::test::ByteWriter out;
  out.u32(include_count);
  out.u32(static_cast<std::uint32_t>(properties.size()));
  out.u16(std::uint16_t{0});
  out.u32(1346454871u);
  out.u16(std::uint16_t{0});
  out.u32(attach_point);
  out.u32(static_cast<std::uint32_t>(slots.size()));
  for (const auto slot : slots) out.u32(slot);
  out.u32(std::uint32_t{0});
  for (auto& row : includes) out.bytes.insert(out.bytes.end(), row.begin(), row.end());
  for (const auto& row : properties) out.bytes.insert(out.bytes.end(), row.begin(), row.end());
  return out.bytes;
}

Bytes one_row(std::uint8_t type, std::uint32_t value1) {
  return omod_data({omod_row(type, 0, 7, value1, 0, 0.0f)});
}

Rec omod(std::uint32_t local, const char* edid, Bytes data) {
  return Rec{"OMOD", patch_id(local), {{"EDID", zstr(edid)}, {"DATA", std::move(data)}}};
}

std::vector<Rec> patch_records() {
  Bytes cut = omod_data({omod_row(k_int, 0, 1, 2, 0, 0.0f)});
  cut.resize(cut.size() - 4);
  Bytes trailing = omod_data({});
  trailing.insert(trailing.end(), 4, std::byte{0x7F});
  return {
      Rec{"KYWD", patch_id(k_own_keyword), {{"EDID", zstr("OwnKeyword")}}},
      omod(k_shapes, "Shapes",
           omod_data({omod_row(k_int, 0, 1, 5, 0, 0.0f),
                      omod_row(k_float, 1, 2, 0x3FC00000, 0, 0.25f),
                      omod_row(k_bool, 2, 3, 1, 0, 0.0f),
                      omod_row(k_form_int, 0, 4, k_base_keyword, 9, 0.0f),
                      omod_row(k_form_float, 2, 5, patch_id(k_own_keyword), 0x40000000, 0.5f),
                      omod_row(k_enum, 0, 6, 3, 0, 0.0f)})),
      omod(k_master_index, "MasterIndex", one_row(k_form_int, k_base_keyword)),
      omod(k_second_master_index, "SecondMasterIndex", one_row(k_form_int, 0x01000000u | k_extra_keyword)),
      omod(k_own_index, "OwnIndex", one_row(k_form_int, patch_id(k_own_keyword))),
      omod(k_past_own_index, "PastOwnIndex", one_row(k_form_int, 0x03000000u | k_base_keyword)),
      omod(k_created_index, "CreatedIndex", one_row(k_form_float, 0xFF000800u)),
      omod(k_plain_values_unchecked, "PlainValues",
           omod_data({omod_row(k_int, 0, 1, 0x02000800u, 0, 0.0f), omod_row(k_enum, 0, 2, 0xFF000800u, 0, 0.0f)})),
      omod(k_count_past_rows, "CountPastRows", omod_data({omod_row(k_int, 0, 1, 2, 0, 0.0f)}, 3)),
      omod(k_trailing_bytes, "TrailingBytes", std::move(trailing)),
      omod(k_cut_row, "CutRow", std::move(cut)),
      omod(k_short_header, "ShortHeader", Bytes(3, std::byte{0})),
      Rec{"OMOD", patch_id(k_no_data), {{"EDID", zstr("NoData")}}},
      omod(k_empty, "Empty", omod_data({})),
      omod(k_form_id_null, "NullForm", one_row(k_form_int, 0)),
      omod(k_vanilla_shape, "VanillaShape",
           omod_vanilla_shaped(2, k_base_keyword, {k_base_keyword, 0},
                               {include_row(0, 0, 1, 0), include_row(patch_id(k_own_keyword), 3, 0, 1)},
                               {omod_row(k_float, 1, 28, 0x3E800000, 0x40000000, 0.5f)})),
      Rec{"KYWD", patch_id(k_wrong_signature), {{"EDID", zstr("NotAnOmod")}}},
  };
}

class OmodPropertiesDecode : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_omod_properties_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    const auto base = write("Base.esm", build({}, 0x1, {Rec{"KYWD", k_base_keyword, {{"EDID", zstr("BaseKeyword")}}}}));
    const auto extra = write("Extra.esp", build({}, 0, {Rec{"KYWD", k_extra_keyword, {{"EDID", zstr("ExtraKeyword")}}}}));
    const auto patch = write("Patch.esp", build({"Base.esm", "Extra.esp"}, 0, patch_records()));
    const char* paths[] = {base.c_str(), extra.c_str(), patch.c_str()};
    ASSERT_EQ(radicle_env_load(paths, 3, &env_), RADICLE_STATUS_OK) << radicle_last_error();
  }

  void TearDown() override {
    radicle_env_destroy(env_);
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::string write(const char* name, const Bytes& bytes) {
    const auto path = (root_ / name).string();
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  radicle_status decode(std::uint32_t local, radicle_omod_properties_info** info) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_, radicle_form_key{"Patch.esp", local}, &record), RADICLE_STATUS_OK);
    return radicle_omod_properties_decode(env_, record, info);
  }

  std::filesystem::path root_;
  radicle_env* env_{nullptr};
};

std::uint32_t word(const std::uint8_t (&bytes)[4]) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

}

TEST_F(OmodPropertiesDecode, EveryValueShapeDecodesAndOnlyFormShapesResolveValue1) {
  radicle_omod_properties_info* info = nullptr;
  ASSERT_EQ(decode(k_shapes, &info), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(info->property_count, 6u);
  const auto* rows = info->properties;
  EXPECT_EQ(rows[0].value_type, k_int);
  EXPECT_EQ(word(rows[0].value1), 5u);
  EXPECT_EQ(rows[1].value_type, k_float);
  EXPECT_EQ(rows[1].function_type, 1);
  EXPECT_EQ(word(rows[1].value1), 0x3FC00000u);
  EXPECT_FLOAT_EQ(rows[1].step, 0.25f);
  EXPECT_EQ(rows[2].value_type, k_bool);
  EXPECT_EQ(word(rows[2].value1), 1u);
  EXPECT_EQ(rows[3].value_type, k_form_int);
  EXPECT_EQ(word(rows[3].value2), 9u);
  EXPECT_STREQ(rows[3].value1_form.plugin, "Base.esm");
  EXPECT_EQ(rows[3].value1_form.local_id, k_base_keyword);
  EXPECT_EQ(rows[4].value_type, k_form_float);
  EXPECT_EQ(rows[4].property, 5);
  EXPECT_EQ(word(rows[4].value2), 0x40000000u);
  EXPECT_STREQ(rows[4].value1_form.plugin, "Patch.esp");
  EXPECT_EQ(rows[4].value1_form.local_id, k_own_keyword);
  EXPECT_EQ(rows[5].value_type, k_enum);
  EXPECT_EQ(word(rows[5].value1), 3u);
  for (const std::size_t plain : {0u, 1u, 2u, 5u}) EXPECT_EQ(rows[plain].value1_form.plugin, nullptr) << plain;
  radicle_omod_properties_free(info);
}

TEST_F(OmodPropertiesDecode, FormIdMasterIndexBelowAtAndPastTheMasterCount) {
  radicle_omod_properties_info* info = nullptr;
  ASSERT_EQ(decode(k_master_index, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info->properties[0].value1_form.plugin, "Base.esm");
  EXPECT_EQ(info->properties[0].value1_form.local_id, k_base_keyword);
  radicle_omod_properties_free(info);
  ASSERT_EQ(decode(k_second_master_index, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info->properties[0].value1_form.plugin, "Extra.esp");
  EXPECT_EQ(info->properties[0].value1_form.local_id, k_extra_keyword);
  radicle_omod_properties_free(info);
  ASSERT_EQ(decode(k_own_index, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info->properties[0].value1_form.plugin, "Patch.esp");
  EXPECT_EQ(info->properties[0].value1_form.local_id, k_own_keyword);
  radicle_omod_properties_free(info);
  info = nullptr;
  EXPECT_EQ(decode(k_past_own_index, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
  const auto* why = radicle_last_error();
  ASSERT_NE(why, nullptr);
  EXPECT_NE(std::string(why).find("master index 3 is outside this plugin's master table"), std::string::npos);
  EXPECT_EQ(decode(k_created_index, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(info, nullptr);
}

TEST_F(OmodPropertiesDecode, NullFormIdAndNonFormShapesNeverTouchTheMasterTable) {
  radicle_omod_properties_info* info = nullptr;
  ASSERT_EQ(decode(k_form_id_null, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->properties[0].value1_form.plugin, nullptr);
  radicle_omod_properties_free(info);
  ASSERT_EQ(decode(k_plain_values_unchecked, &info), RADICLE_STATUS_OK);
  ASSERT_EQ(info->property_count, 2u);
  EXPECT_EQ(word(info->properties[0].value1), 0x02000800u);
  EXPECT_EQ(info->properties[1].value1_form.plugin, nullptr);
  radicle_omod_properties_free(info);
  ASSERT_EQ(decode(k_empty, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->property_count, 0u);
  radicle_omod_properties_free(info);
}

TEST_F(OmodPropertiesDecode, VanillaShapedPayloadWithZeroIncludeAndParentSlotsStillDecodesProperties) {
  radicle_omod_properties_info* info = nullptr;
  ASSERT_EQ(decode(k_vanilla_shape, &info), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(info->property_count, 1u);
  EXPECT_EQ(info->properties[0].value_type, k_float);
  EXPECT_EQ(word(info->properties[0].value1), 0x3E800000u);
  radicle_omod_properties_free(info);
}

TEST_F(OmodPropertiesDecode, MiscountedTruncatedAndMissingDataFailClosed) {
  for (const auto local : {k_count_past_rows, k_trailing_bytes, k_cut_row, k_short_header, k_no_data, k_wrong_signature}) {
    radicle_omod_properties_info* info = nullptr;
    EXPECT_EQ(decode(local, &info), RADICLE_STATUS_PARSE_ERROR) << std::hex << local;
    EXPECT_EQ(info, nullptr) << std::hex << local;
  }
}
