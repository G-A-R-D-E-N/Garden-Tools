#include "fo4/plugin/reader.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

void object_v2(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  out.u16(0);
  out.u16(0xFFFF);
  out.u32(form_id);
}

void object_v1(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  out.u32(form_id);
  out.u16(0xFFFF);
  out.u16(0);
}

fo4::test::ByteWriter vmad_with_properties(std::int16_t object_format, std::uint16_t property_count,
                                           const fo4::test::ByteWriter& properties) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(static_cast<std::uint16_t>(object_format));
  vmad.u16(1);
  wstring(vmad, "ArrayScript");
  vmad.u8(0);
  vmad.u16(property_count);
  vmad.bytes.insert(vmad.bytes.end(), properties.bytes.begin(), properties.bytes.end());
  return vmad;
}

void begin_property(fo4::test::ByteWriter& out, std::string_view name, std::uint8_t type) {
  wstring(out, name);
  out.u8(type);
  out.u8(1);
}

int array_kinds_set(const fo4::VmadProperty& property) {
  return static_cast<int>(property.object_elements.has_value()) +
         static_cast<int>(property.text_elements.has_value()) +
         static_cast<int>(property.integer_elements.has_value()) +
         static_cast<int>(property.real_elements.has_value()) +
         static_cast<int>(property.boolean_elements.has_value());
}
}

TEST(VmadArrayElements, IntArrayDecodesEveryElement) {
  fo4::test::ByteWriter props;
  begin_property(props, "Ints", 13);
  props.u32(3);
  props.i32(0);
  props.i32(-1);
  props.i32(2147483647);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  EXPECT_EQ(property.element_count, 3u);
  ASSERT_TRUE(property.integer_elements.has_value());
  EXPECT_EQ(*property.integer_elements, (std::vector<std::int32_t>{0, -1, 2147483647}));
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, FloatArrayPreservesNegativeZeroBits) {
  fo4::test::ByteWriter props;
  begin_property(props, "Reals", 14);
  props.u32(2);
  props.f32(1.5f);
  props.f32(-0.0f);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  EXPECT_EQ(property.element_count, 2u);
  ASSERT_TRUE(property.real_elements.has_value());
  ASSERT_EQ(property.real_elements->size(), 2u);
  EXPECT_EQ((*property.real_elements)[0], 1.5f);
  std::uint32_t bits = 0;
  std::memcpy(&bits, &(*property.real_elements)[1], 4);
  EXPECT_EQ(bits, 0x80000000u);
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, BoolArrayTreatsAnyNonzeroByteAsTrue) {
  fo4::test::ByteWriter props;
  begin_property(props, "Flags", 15);
  props.u32(3);
  props.u8(0x00);
  props.u8(0x01);
  props.u8(0x02);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  ASSERT_TRUE(property.boolean_elements.has_value());
  EXPECT_EQ(*property.boolean_elements, (std::vector<bool>{false, true, true}));
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, StringArrayDecodesEmptyAndNonEmptyElements) {
  fo4::test::ByteWriter props;
  begin_property(props, "Names", 12);
  props.u32(2);
  wstring(props, "");
  wstring(props, "Alpha");
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  ASSERT_TRUE(property.text_elements.has_value());
  EXPECT_EQ(*property.text_elements, (std::vector<std::string>{"", "Alpha"}));
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, ObjectArrayFormat2KeepsFormIdsAndAliases) {
  fo4::test::ByteWriter props;
  begin_property(props, "Refs", 11);
  props.u32(2);
  object_v2(props, 0x0200009A);
  props.u16(0);
  props.u16(3);
  props.u32(0x02000ABC);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  EXPECT_EQ(property.element_count, 2u);
  ASSERT_TRUE(property.object_elements.has_value());
  ASSERT_EQ(property.object_elements->size(), 2u);
  EXPECT_EQ((*property.object_elements)[0].object.value(), 0x0200009Au);
  EXPECT_EQ((*property.object_elements)[0].alias, -1);
  EXPECT_EQ((*property.object_elements)[1].object.value(), 0x02000ABCu);
  EXPECT_EQ((*property.object_elements)[1].alias, 3);
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, ObjectArrayFormat1DecodesSingleElement) {
  fo4::test::ByteWriter props;
  begin_property(props, "Ref", 11);
  props.u32(1);
  object_v1(props, 0x0200009A);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(1, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  ASSERT_TRUE(property.object_elements.has_value());
  ASSERT_EQ(property.object_elements->size(), 1u);
  EXPECT_EQ((*property.object_elements)[0].object.value(), 0x0200009Au);
  EXPECT_EQ((*property.object_elements)[0].alias, -1);
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, EmptyIntArrayIsEngagedAndEmpty) {
  fo4::test::ByteWriter props;
  begin_property(props, "Ints", 13);
  props.u32(0);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  EXPECT_EQ(property.element_count, 0u);
  ASSERT_TRUE(property.integer_elements.has_value());
  EXPECT_TRUE(property.integer_elements->empty());
  EXPECT_EQ(array_kinds_set(property), 1);
}

TEST(VmadArrayElements, UnexposedObjectArrayWordStopsTypedReaderKeepingEarlierProperty) {
  fo4::test::ByteWriter props;
  begin_property(props, "Before", 3);
  props.i32(7);
  begin_property(props, "Refs", 11);
  props.u32(2);
  object_v2(props, 0x0200009A);
  props.u16(1);
  props.u16(3);
  props.u32(0x02000ABC);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 2, props).bytes);
  EXPECT_TRUE(result.unsupported);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  EXPECT_EQ(result.scripts[0].properties[0].name, "Before");
  EXPECT_EQ(result.scripts[0].properties[0].integer, 7);
}

TEST(VmadArrayElements, PropertyAfterArrayStillDecodesInSync) {
  fo4::test::ByteWriter props;
  begin_property(props, "Ints", 13);
  props.u32(2);
  props.i32(4);
  props.i32(5);
  begin_property(props, "After", 3);
  props.i32(9);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 2, props).bytes);
  EXPECT_FALSE(result.unsupported);
  EXPECT_EQ(result.trailing_bytes, 0u);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 2u);
  EXPECT_EQ(*result.scripts[0].properties[0].integer_elements, (std::vector<std::int32_t>{4, 5}));
  EXPECT_EQ(result.scripts[0].properties[1].name, "After");
  EXPECT_EQ(result.scripts[0].properties[1].integer, 9);
}

TEST(VmadArrayElements, StructPropertyKeepsCountOnlyAndNoArrayElements) {
  fo4::test::ByteWriter props;
  begin_property(props, "Struct", 7);
  props.u32(1);
  begin_property(props, "Member", 3);
  props.i32(5);
  const auto result = fo4::read_vmad_scripts(vmad_with_properties(2, 1, props).bytes);
  ASSERT_EQ(result.scripts.size(), 1u);
  ASSERT_EQ(result.scripts[0].properties.size(), 1u);
  const auto& property = result.scripts[0].properties[0];
  EXPECT_EQ(property.element_count, 1u);
  EXPECT_EQ(array_kinds_set(property), 0);
  EXPECT_EQ(result.trailing_bytes, 0u);
}
