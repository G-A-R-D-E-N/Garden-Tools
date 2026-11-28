#include "fo4/c_api_env.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
};

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

std::vector<std::byte> make_vmad_arrays() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "ArrayScript");
  out.u8(0);
  out.u16(8);
  wstring(out, "Targets");
  out.u8(11);
  out.u8(1);
  out.u32(2);
  out.u16(0);
  out.u16(0xFFFF);
  out.u32(0x805);
  out.u16(0);
  out.u16(3);
  out.u32(0x806);
  wstring(out, "Labels");
  out.u8(12);
  out.u8(1);
  out.u32(2);
  wstring(out, "");
  wstring(out, "Alpha");
  wstring(out, "Numbers");
  out.u8(13);
  out.u8(1);
  out.u32(3);
  out.u32(0);
  out.u32(0xFFFFFFFF);
  out.u32(0x7FFFFFFF);
  wstring(out, "Ratios");
  out.u8(14);
  out.u8(1);
  out.u32(2);
  out.f32(1.5f);
  out.f32(-0.0f);
  wstring(out, "Switches");
  out.u8(15);
  out.u8(1);
  out.u32(3);
  out.u8(0);
  out.u8(1);
  out.u8(2);
  wstring(out, "Empty");
  out.u8(13);
  out.u8(1);
  out.u32(0);
  wstring(out, "Count");
  out.u8(3);
  out.u8(1);
  out.u32(7);
  wstring(out, "Holder");
  out.u8(7);
  out.u8(1);
  out.u32(1);
  wstring(out, "Inner");
  out.u8(3);
  out.u8(1);
  out.u32(5);
  return out.bytes;
}

std::vector<std::byte> make_vmad_structs() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "StructScript");
  out.u8(0);
  out.u16(2);
  wstring(out, "Point");
  out.u8(7);
  out.u8(1);
  out.u32(3);
  wstring(out, "Target");
  out.u8(1);
  out.u8(0);
  out.u16(0);
  out.u16(3);
  out.u32(0x806);
  wstring(out, "Label");
  out.u8(2);
  out.u8(0);
  wstring(out, "Alpha");
  wstring(out, "Count");
  out.u8(3);
  out.u8(0);
  out.u32(0xFFFFFFFCu);
  wstring(out, "Pairs");
  out.u8(17);
  out.u8(0);
  out.u32(2);
  out.u32(1);
  wstring(out, "Flag");
  out.u8(5);
  out.u8(0);
  out.u8(1);
  out.u32(0);
  return out.bytes;
}

std::vector<std::byte> make_vmad_latin1_struct() {
  fo4::test::ByteWriter out;
  out.u16(6);
  out.u16(2);
  out.u16(1);
  wstring(out, "LatinScript");
  out.u8(0);
  out.u16(2);
  wstring(out, "Holder");
  out.u8(7);
  out.u8(0);
  out.u32(1);
  wstring(out, "Caf\xE9");
  out.u8(2);
  out.u8(0);
  wstring(out, "Caf\xC3\xA9");
  wstring(out, "Labels");
  out.u8(12);
  out.u8(1);
  out.u32(1);
  wstring(out, "Caf\xC3\xA9");
  return out.bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  for (const auto& record : records) {
    const auto group = fo4::test::begin_group(out, record.signature, 0);
    const auto header = fo4::test::begin_record(out, record.signature, 0, record.form_id);
    for (const auto& sub : record.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, header);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

class PlacedScriptElementTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_script_elements_" + std::to_string(id));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path root_;
};

}  // namespace

TEST_F(PlacedScriptElementTest, CApiExposesDecodedScriptArrayElements) {
  const auto data = make_plugin({{"REFR", 0x800, {{"VMAD", make_vmad_arrays()}}},
                                 {"ARMO", 0x805, {}},
                                 {"STAT", 0x806, {}}});
  const auto path = root_ / "Elements.esp";
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                               static_cast<std::streamsize>(data.size()));
  const auto utf8 = path.u8string();
  const std::string text(utf8.begin(), utf8.end());
  const char* raw[] = {text.c_str()};
  radicle_env* pointer = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &pointer), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(pointer, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), {"Elements.esp", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_placed_ref* decoded = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env.get(), record, &decoded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_placed_ref, decltype(&radicle_placed_ref_free)> ref(decoded, radicle_placed_ref_free);

  ASSERT_EQ(ref->script_count, 1u);
  ASSERT_EQ(ref->scripts[0].property_count, 8u);
  const radicle_script_property* props = ref->scripts[0].properties;

  const auto& targets = props[0];
  EXPECT_STREQ(targets.name, "Targets");
  EXPECT_EQ(targets.has_elements, 1);
  ASSERT_EQ(targets.element_value_count, 2u);
  ASSERT_NE(targets.elements, nullptr);
  EXPECT_STREQ(targets.elements[0].object.plugin, "Elements.esp");
  EXPECT_EQ(targets.elements[0].object.local_id, 0x805u);
  EXPECT_EQ(targets.elements[0].alias, -1);
  EXPECT_EQ(targets.elements[0].text, nullptr);
  EXPECT_EQ(targets.elements[1].object.local_id, 0x806u);
  EXPECT_EQ(targets.elements[1].alias, 3);
  EXPECT_EQ(targets.elements[1].integer, 0);

  const auto& labels = props[1];
  EXPECT_EQ(labels.has_elements, 1);
  ASSERT_EQ(labels.element_value_count, 2u);
  EXPECT_STREQ(labels.elements[0].text, "");
  EXPECT_STREQ(labels.elements[1].text, "Alpha");
  EXPECT_EQ(labels.elements[1].object.plugin, nullptr);
  EXPECT_EQ(labels.elements[1].alias, 0);

  const auto& numbers = props[2];
  EXPECT_EQ(numbers.has_elements, 1);
  ASSERT_EQ(numbers.element_value_count, 3u);
  EXPECT_EQ(numbers.elements[0].integer, 0);
  EXPECT_EQ(numbers.elements[1].integer, -1);
  EXPECT_EQ(numbers.elements[2].integer, 2147483647);
  EXPECT_EQ(numbers.elements[2].text, nullptr);

  const auto& ratios = props[3];
  EXPECT_EQ(ratios.has_elements, 1);
  ASSERT_EQ(ratios.element_value_count, 2u);
  EXPECT_EQ(ratios.elements[0].real, 1.5f);
  EXPECT_EQ(std::bit_cast<std::uint32_t>(ratios.elements[1].real), 0x80000000u);
  EXPECT_EQ(ratios.elements[1].integer, 0);

  const auto& switches = props[4];
  EXPECT_EQ(switches.has_elements, 1);
  ASSERT_EQ(switches.element_value_count, 3u);
  EXPECT_EQ(switches.elements[0].boolean, 0);
  EXPECT_EQ(switches.elements[1].boolean, 1);
  EXPECT_EQ(switches.elements[2].boolean, 1);
  EXPECT_EQ(switches.elements[2].real, 0.0f);

  const auto& empty = props[5];
  EXPECT_EQ(empty.has_elements, 1);
  EXPECT_EQ(empty.element_value_count, 0u);

  const auto& count = props[6];
  EXPECT_STREQ(count.name, "Count");
  EXPECT_EQ(count.has_elements, 0);
  EXPECT_EQ(count.elements, nullptr);
  EXPECT_EQ(count.element_value_count, 0u);
  EXPECT_EQ(count.has_integer, 1);
  EXPECT_EQ(count.integer, 7);

  const auto& holder = props[7];
  EXPECT_STREQ(holder.name, "Holder");
  EXPECT_EQ(holder.type, 7);
  EXPECT_EQ(holder.has_element_count, 1);
  EXPECT_EQ(holder.element_count, 1u);
  EXPECT_EQ(holder.has_elements, 0);
  EXPECT_EQ(holder.elements, nullptr);
  EXPECT_EQ(holder.element_value_count, 0u);
  EXPECT_EQ(holder.has_structs, 1);
  ASSERT_EQ(holder.struct_count, 1u);
  ASSERT_EQ(holder.structs[0].member_count, 1u);
  EXPECT_STREQ(holder.structs[0].members[0].name, "Inner");
  EXPECT_EQ(holder.structs[0].members[0].value.integer, 5);
}

TEST_F(PlacedScriptElementTest, CApiExposesDecodedStructMembersAndStructArrays) {
  const auto data = make_plugin({{"REFR", 0x800, {{"VMAD", make_vmad_structs()}}},
                                 {"STAT", 0x806, {}}});
  const auto path = root_ / "Structs.esp";
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                               static_cast<std::streamsize>(data.size()));
  const auto utf8 = path.u8string();
  const std::string text(utf8.begin(), utf8.end());
  const char* raw[] = {text.c_str()};
  radicle_env* pointer = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &pointer), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(pointer, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), {"Structs.esp", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_placed_ref* decoded = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env.get(), record, &decoded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_placed_ref, decltype(&radicle_placed_ref_free)> ref(decoded, radicle_placed_ref_free);

  ASSERT_EQ(ref->script_count, 1u);
  ASSERT_EQ(ref->scripts[0].property_count, 2u);
  const radicle_script_property* props = ref->scripts[0].properties;

  const auto& point = props[0];
  EXPECT_STREQ(point.name, "Point");
  EXPECT_EQ(point.has_elements, 0);
  EXPECT_EQ(point.has_structs, 1);
  ASSERT_EQ(point.struct_count, 1u);
  ASSERT_EQ(point.structs[0].member_count, 3u);
  const auto* members = point.structs[0].members;
  EXPECT_STREQ(members[0].name, "Target");
  EXPECT_EQ(members[0].type, 1);
  EXPECT_STREQ(members[0].value.object.plugin, "Structs.esp");
  EXPECT_EQ(members[0].value.object.local_id, 0x806u);
  EXPECT_EQ(members[0].value.alias, 3);
  EXPECT_STREQ(members[1].name, "Label");
  EXPECT_STREQ(members[1].value.text, "Alpha");
  EXPECT_EQ(members[1].value.object.plugin, nullptr);
  EXPECT_STREQ(members[2].name, "Count");
  EXPECT_EQ(members[2].value.integer, -4);

  const auto& pairs = props[1];
  EXPECT_STREQ(pairs.name, "Pairs");
  EXPECT_EQ(pairs.has_structs, 1);
  ASSERT_EQ(pairs.struct_count, 2u);
  ASSERT_EQ(pairs.structs[0].member_count, 1u);
  EXPECT_STREQ(pairs.structs[0].members[0].name, "Flag");
  EXPECT_EQ(pairs.structs[0].members[0].value.boolean, 1);
  EXPECT_EQ(pairs.structs[1].member_count, 0u);
}

TEST_F(PlacedScriptElementTest, CApiConvertsWindows1252NamesKeepsUtf8Text) {
  const auto data = make_plugin({{"REFR", 0x800, {{"VMAD", make_vmad_latin1_struct()}}}});
  const auto path = root_ / "Latin.esp";
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                               static_cast<std::streamsize>(data.size()));
  const auto utf8 = path.u8string();
  const std::string text(utf8.begin(), utf8.end());
  const char* raw[] = {text.c_str()};
  radicle_env* pointer = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &pointer), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env(pointer, radicle_env_destroy);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env.get(), {"Latin.esp", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_placed_ref* decoded = nullptr;
  ASSERT_EQ(radicle_placed_ref_decode(env.get(), record, &decoded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_placed_ref, decltype(&radicle_placed_ref_free)> ref(decoded, radicle_placed_ref_free);

  ASSERT_EQ(ref->script_count, 1u);
  ASSERT_EQ(ref->scripts[0].property_count, 2u);
  const auto& holder = ref->scripts[0].properties[0];
  EXPECT_EQ(holder.has_structs, 1);
  ASSERT_EQ(holder.struct_count, 1u);
  ASSERT_EQ(holder.structs[0].member_count, 1u);
  EXPECT_STREQ(holder.structs[0].members[0].name, "Caf\xC3\xA9");
  EXPECT_STREQ(holder.structs[0].members[0].value.text, "Caf\xC3\xA9");
  const auto& labels = ref->scripts[0].properties[1];
  EXPECT_STREQ(labels.name, "Labels");
  ASSERT_EQ(labels.element_value_count, 1u);
  EXPECT_STREQ(labels.elements[0].text, "Caf\xC3\xA9");
}
