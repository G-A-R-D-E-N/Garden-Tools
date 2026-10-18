#include "fo4/plugin/vmad_writer.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace fo4;
namespace fixture = fo4::test::vmad;

VmadProperty property(std::string name, std::uint8_t type) {
  VmadProperty value;
  value.name = std::move(name);
  value.type = type;
  value.flags = 1;
  return value;
}

VmadStructMember member(std::string name, std::uint8_t type) {
  VmadStructMember value;
  value.name = std::move(name);
  value.type = type;
  return value;
}

VmadStruct scalar_members() {
  VmadStruct out;
  auto object = member("obj", 1);
  object.flags = 3;
  object.object = VmadObjectElement{RawFormId{0x801}, -1};
  auto text = member("text", 2);
  text.text = std::string("member");
  auto integer = member("int", 3);
  integer.integer = -7;
  auto real = member("float", 4);
  real.real = 1.25f;
  auto boolean = member("bool", 5);
  boolean.boolean = true;
  out.members = {object, text, integer, real, boolean};
  return out;
}

std::vector<std::byte> quest_tail() {
  test::ByteWriter tail;
  tail.u8(3);
  tail.u16(1);
  tail.u16(0);
  tail.u16(12);
  tail.u16(0x1234);
  tail.i32(-3);
  tail.u8(7);
  fixture::name(tail, "FragmentScript");
  fixture::name(tail, "Fragment_12");
  tail.u16(0);
  return tail.bytes;
}

std::vector<std::byte> slice(const std::vector<std::byte>& data, std::size_t begin,
                             std::size_t end) {
  return {data.begin() + static_cast<std::ptrdiff_t>(begin),
          data.begin() + static_cast<std::ptrdiff_t>(end)};
}

}

TEST(VmadStructure, AddAndRemoveScriptKeepEveryOtherByte) {
  auto plugin = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto* other = find_record(plugin, fixture::key(0x801));
  ASSERT_NE(other, nullptr);
  const auto other_bytes = test::record_original_bytes(plugin, *other);

  ASSERT_EQ(add_vmad_script(plugin, fixture::key(), 1, "Added", 3),
            VmadStructureMutationStatus::updated);
  const auto data = fixture::subrecord(plugin).data;
  const auto scripts = read_vmad_scripts(data);
  ASSERT_EQ(scripts.scripts.size(), 2u);
  EXPECT_EQ(data[4], std::byte{2});
  EXPECT_EQ(data[5], std::byte{0});
  EXPECT_EQ(scripts.scripts[0].name, "Script");
  ASSERT_EQ(scripts.scripts[0].properties.size(), 12u);
  EXPECT_EQ(scripts.scripts[1].name, "Added");
  EXPECT_EQ(scripts.scripts[1].flags, 3);
  EXPECT_TRUE(scripts.scripts[1].properties.empty());
  EXPECT_EQ(scripts.trailing_bytes, 0u);

  test::ByteWriter entry;
  fixture::name(entry, "Added");
  entry.u8(3);
  entry.u16(0);
  EXPECT_EQ(slice(data, scripts.scripts[1].offset, data.size()), entry.bytes);
  EXPECT_EQ(slice(data, 6, scripts.scripts[1].offset), slice(before, 6, before.size()));
  EXPECT_EQ(slice(data, 0, 4), slice(before, 0, 4));

  ASSERT_EQ(add_vmad_script(plugin, fixture::key(), 0, "Front", 0),
            VmadStructureMutationStatus::updated);
  const auto front = read_vmad_scripts(fixture::subrecord(plugin).data);
  ASSERT_EQ(front.scripts.size(), 3u);
  EXPECT_EQ(front.scripts[0].name, "Front");
  EXPECT_EQ(front.scripts[1].name, "Script");
  EXPECT_EQ(front.scripts[2].name, "Added");
  EXPECT_EQ(remove_vmad_script(plugin, fixture::key(), 0), VmadStructureMutationStatus::updated);
  EXPECT_EQ(remove_vmad_script(plugin, fixture::key(), 1), VmadStructureMutationStatus::updated);
  EXPECT_EQ(fixture::subrecord(plugin).data, before);

  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 4, std::int32_t{-200}),
            ScalarMutationStatus::updated);
  EXPECT_EQ(read_vmad_scripts(fixture::subrecord(plugin).data).scripts[0].properties[4].integer,
            -200);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 1, 0, std::int32_t{1}),
            ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(test::record_original_bytes(plugin, *find_record(plugin, fixture::key(0x801))),
            other_bytes);
}

TEST(VmadStructure, AddedPropertiesReparseAndRemovingThemRestoresThePayload) {
  auto plugin = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto original = read_vmad_scripts(before).scripts[0].properties;
  ASSERT_EQ(original.size(), 12u);

  VmadProperty object = property("NewObject", 1);
  object.object = RawFormId{0x801};
  object.alias = -1;
  VmadProperty text = property("NewText", 2);
  text.text = std::string("hello");
  VmadProperty integer = property("NewInt", 3);
  integer.integer = -123456;
  VmadProperty real = property("NewFloat", 4);
  real.real = 2.5f;
  VmadProperty boolean = property("NewBool", 5);
  boolean.boolean = true;
  VmadProperty struct_value = property("NewStruct", 7);
  struct_value.struct_elements = std::vector<VmadStruct>{scalar_members()};
  VmadProperty objects = property("NewObjects", 11);
  objects.object_elements = std::vector<VmadObjectElement>{{RawFormId{0x801}, 3},
                                                          {RawFormId{0x802}, -1}};
  VmadProperty strings = property("NewStrings", 12);
  strings.text_elements = std::vector<std::string>{"a", "bb"};
  VmadProperty ints = property("NewInts", 13);
  ints.integer_elements = std::vector<std::int32_t>{-1, 2};
  VmadProperty floats = property("NewFloats", 14);
  floats.real_elements = std::vector<float>{0.5f};
  VmadProperty bools = property("NewBools", 15);
  bools.boolean_elements = std::vector<bool>{true, false};
  VmadProperty variable = property("NewVariable", 16);
  variable.element_count = 0;
  VmadProperty structs = property("NewStructs", 17);
  structs.struct_elements = std::vector<VmadStruct>{scalar_members(), VmadStruct{}};
  VmadProperty none = property("NewNone", 0);

  const std::vector<VmadProperty> added = {object, text, integer, real, boolean, struct_value,
                                           objects, strings, ints, floats, bools, variable,
                                           structs, none};
  for (std::size_t index = 0; index < added.size(); ++index) {
    EXPECT_EQ(add_vmad_property(plugin, fixture::key(), 0, 12 + index, added[index]),
              VmadStructureMutationStatus::updated)
        << index;
  }

  const auto reparsed = read_plugin(write_plugin(plugin), "Vmad.esp");
  const auto scripts = read_vmad_scripts(fixture::subrecord(reparsed).data);
  ASSERT_EQ(scripts.scripts.size(), 1u);
  const auto& properties = scripts.scripts[0].properties;
  ASSERT_EQ(properties.size(), 26u);
  for (std::size_t index = 0; index < original.size(); ++index) {
    EXPECT_EQ(properties[index].name, original[index].name) << index;
  }
  EXPECT_EQ(properties[12].name, "NewObject");
  EXPECT_EQ(properties[12].type, 1);
  EXPECT_EQ(properties[12].flags, 1);
  EXPECT_EQ(properties[12].object->value(), 0x801u);
  EXPECT_EQ(properties[12].alias, -1);
  EXPECT_EQ(properties[13].text, "hello");
  EXPECT_EQ(properties[14].integer, -123456);
  EXPECT_EQ(properties[15].real, 2.5f);
  EXPECT_EQ(properties[16].boolean, true);
  EXPECT_EQ(properties[17].type, 7);
  ASSERT_TRUE(properties[17].struct_elements.has_value());
  ASSERT_EQ(properties[17].struct_elements->size(), 1u);
  const auto& members = properties[17].struct_elements->front().members;
  ASSERT_EQ(members.size(), 5u);
  EXPECT_EQ(members[0].name, "obj");
  EXPECT_EQ(members[0].type, 1);
  EXPECT_EQ(members[0].flags, 3);
  EXPECT_EQ(members[0].object->object.value(), 0x801u);
  EXPECT_EQ(members[0].object->alias, -1);
  EXPECT_EQ(members[1].text, "member");
  EXPECT_EQ(members[2].integer, -7);
  EXPECT_EQ(members[3].real, 1.25f);
  EXPECT_EQ(members[4].boolean, true);
  ASSERT_TRUE(properties[18].object_elements.has_value());
  ASSERT_EQ(properties[18].object_elements->size(), 2u);
  EXPECT_EQ(properties[18].object_elements->front().alias, 3);
  EXPECT_EQ(properties[18].object_elements->back().object.value(), 0x802u);
  ASSERT_TRUE(properties[19].text_elements.has_value());
  EXPECT_EQ(*properties[19].text_elements, (std::vector<std::string>{"a", "bb"}));
  ASSERT_TRUE(properties[20].integer_elements.has_value());
  EXPECT_EQ(*properties[20].integer_elements, (std::vector<std::int32_t>{-1, 2}));
  ASSERT_TRUE(properties[21].real_elements.has_value());
  EXPECT_EQ(*properties[21].real_elements, (std::vector<float>{0.5f}));
  ASSERT_TRUE(properties[22].boolean_elements.has_value());
  EXPECT_EQ(*properties[22].boolean_elements, (std::vector<bool>{true, false}));
  EXPECT_EQ(properties[23].type, 16);
  EXPECT_EQ(properties[23].element_count, 0u);
  EXPECT_EQ(properties[24].type, 17);
  ASSERT_TRUE(properties[24].struct_elements.has_value());
  ASSERT_EQ(properties[24].struct_elements->size(), 2u);
  EXPECT_EQ(properties[24].struct_elements->front().members.size(), 5u);
  EXPECT_TRUE(properties[24].struct_elements->back().members.empty());
  EXPECT_EQ(properties[25].type, 0);
  EXPECT_EQ(properties[25].value_size, 0u);

  for (std::size_t index = 0; index < added.size(); ++index) {
    EXPECT_EQ(remove_vmad_property(plugin, fixture::key(), 0, 12),
              VmadStructureMutationStatus::updated)
        << index;
  }
  EXPECT_EQ(fixture::subrecord(plugin).data, before);
}

TEST(VmadStructure, ReplacingAPropertyWithItselfIsByteIdenticalForEveryType) {
  for (const std::uint16_t format : {std::uint16_t{1}, std::uint16_t{2}}) {
    SCOPED_TRACE("format=" + std::to_string(format));
    auto plugin = read_plugin(fixture::plugin_bytes(fixture::payload(format)), "Vmad.esp");
    const auto before = fixture::subrecord(plugin).data;
    const auto properties = read_vmad_scripts(before).scripts[0].properties;
    ASSERT_EQ(properties.size(), 12u);
    for (std::size_t index = 0; index < properties.size(); ++index) {
      EXPECT_EQ(replace_vmad_property(plugin, fixture::key(), 0, index, properties[index]),
                VmadStructureMutationStatus::unchanged)
          << index;
    }
    EXPECT_EQ(fixture::subrecord(plugin).data, before);
  }
}

TEST(VmadStructure, ReplaceWritesStructValuesAndChangesAPropertyType) {
  auto plugin = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  const auto original = read_vmad_scripts(before).scripts[0].properties;
  ASSERT_EQ(original.size(), 12u);

  VmadProperty struct_value = property("EmptyStruct", 7);
  VmadStruct filler;
  auto named = member("named", 3);
  named.integer = 42;
  filler.members = {named};
  struct_value.struct_elements = std::vector<VmadStruct>{filler};
  ASSERT_EQ(replace_vmad_property(plugin, fixture::key(), 0, 10, struct_value),
            VmadStructureMutationStatus::updated);

  const auto data = fixture::subrecord(plugin).data;
  const auto replaced = read_vmad_scripts(data).scripts[0].properties;
  ASSERT_EQ(replaced.size(), 12u);
  ASSERT_TRUE(replaced[10].struct_elements.has_value());
  ASSERT_EQ(replaced[10].struct_elements->size(), 1u);
  ASSERT_EQ(replaced[10].struct_elements->front().members.size(), 1u);
  EXPECT_EQ(replaced[10].struct_elements->front().members.front().name, "named");
  EXPECT_EQ(replaced[10].struct_elements->front().members.front().integer, 42);
  EXPECT_EQ(replaced[11].name, "EmptyVariableArray");
  EXPECT_EQ(data.size() - before.size(), replaced[10].value_size - original[10].value_size);
  EXPECT_EQ(slice(data, 0, original[10].offset), slice(before, 0, original[10].offset));
  EXPECT_EQ(slice(data, replaced[10].value_offset + replaced[10].value_size, data.size()),
            slice(before, original[10].value_offset + original[10].value_size, before.size()));

  VmadProperty string_value = property("3Value", 2);
  string_value.flags = 7;
  string_value.text = std::string("changed");
  ASSERT_EQ(replace_vmad_property(plugin, fixture::key(), 0, 4, string_value),
            VmadStructureMutationStatus::updated);
  const auto changed = read_vmad_scripts(fixture::subrecord(plugin).data).scripts[0].properties;
  EXPECT_EQ(changed[4].name, "3Value");
  EXPECT_EQ(changed[4].type, 2);
  EXPECT_EQ(changed[4].flags, 7);
  EXPECT_EQ(changed[4].text, "changed");
  EXPECT_EQ(changed[5].name, "3Array");
  ASSERT_EQ(replace_vmad_property(plugin, fixture::key(), 0, 4, original[4]),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(fixture::subrecord(plugin).data, data);

  ASSERT_EQ(replace_vmad_property(plugin, fixture::key(), 0, 10, original[10]),
            VmadStructureMutationStatus::updated);
  EXPECT_EQ(fixture::subrecord(plugin).data, before);
}

TEST(VmadStructure, AddVmadWritesTheEmptyPayloadAfterEditorId) {
  auto plugin = read_plugin(fixture::plugin_bytes_without_vmad(), "Vmad.esp");
  const auto misc = Signature::from_string("MISC");
  const auto* record = find_record(plugin, fixture::key());
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");

  ASSERT_EQ(add_vmad(plugin, fixture::key()), VmadStructureMutationStatus::updated);
  record = find_record(plugin, fixture::key());
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 3u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "VMAD");
  EXPECT_EQ(record->subrecords[2].signature.to_string(), "ZZZZ");
  const std::vector<std::byte> empty{std::byte{6}, std::byte{0}, std::byte{2},
                                     std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_EQ(record->subrecords[1].data, empty);
  EXPECT_TRUE(vmad_coverage(record->subrecords[1].data, misc).complete());
  EXPECT_EQ(add_vmad(plugin, fixture::key()), VmadStructureMutationStatus::already_present);

  ASSERT_EQ(add_vmad_script(plugin, fixture::key(), 0, "Fresh", 1),
            VmadStructureMutationStatus::updated);
  VmadProperty count = property("Count", 3);
  count.integer = 7;
  ASSERT_EQ(add_vmad_property(plugin, fixture::key(), 0, 0, count),
            VmadStructureMutationStatus::updated);

  const auto reparsed = read_plugin(write_plugin(plugin), "Vmad.esp");
  const auto* saved = find_record(reparsed, fixture::key());
  ASSERT_NE(saved, nullptr);
  ASSERT_EQ(saved->subrecords.size(), 3u);
  EXPECT_EQ(saved->subrecords[2].data, record->subrecords[2].data);
  EXPECT_TRUE(vmad_coverage(saved->subrecords[1].data, misc).complete());
  const auto scripts = read_vmad_scripts(saved->subrecords[1].data);
  ASSERT_EQ(scripts.scripts.size(), 1u);
  EXPECT_EQ(scripts.scripts[0].name, "Fresh");
  EXPECT_EQ(scripts.scripts[0].flags, 1);
  ASSERT_EQ(scripts.scripts[0].properties.size(), 1u);
  EXPECT_EQ(scripts.scripts[0].properties[0].name, "Count");
  EXPECT_EQ(scripts.scripts[0].properties[0].integer, 7);
}

TEST(VmadStructure, AddVmadWithoutEditorIdGoesFirstAndRefusesOtherRecords) {
  auto plugin = read_plugin(fixture::plugin_bytes_without_vmad("MISC", false), "Vmad.esp");
  ASSERT_EQ(add_vmad(plugin, fixture::key()), VmadStructureMutationStatus::updated);
  const auto* record = find_record(plugin, fixture::key());
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "VMAD");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "ZZZZ");

  auto other = read_plugin(fixture::plugin_bytes_without_vmad("SNDR"), "Vmad.esp");
  const auto before = write_plugin(other);
  EXPECT_EQ(add_vmad(other, fixture::key()), VmadStructureMutationStatus::not_authorable);
  EXPECT_EQ(add_vmad_script(other, fixture::key(), 0, "X", 0),
            VmadStructureMutationStatus::subrecord_missing);
  EXPECT_EQ(add_vmad(other, fixture::key(0x999)), VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(write_plugin(other), before);
}

TEST(VmadStructure, StructureEditsRefuseBadIndicesAndUnencodableValuesAtomically) {
  auto plugin = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto before = write_plugin(plugin);

  EXPECT_EQ(add_vmad_script(plugin, fixture::key(0x999), 0, "X", 0),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(remove_vmad_script(plugin, fixture::key(0x999), 0),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(add_vmad_property(plugin, fixture::key(0x999), 0, 0, property("P", 3)),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(remove_vmad_property(plugin, fixture::key(0x999), 0, 0),
            VmadStructureMutationStatus::record_missing);
  EXPECT_EQ(replace_vmad_property(plugin, fixture::key(0x999), 0, 0, property("P", 3)),
            VmadStructureMutationStatus::record_missing);

  EXPECT_EQ(remove_vmad_script(plugin, fixture::key(), 1),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(add_vmad_script(plugin, fixture::key(), 2, "X", 0),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(add_vmad_property(plugin, fixture::key(), 1, 0, property("P", 3)),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(add_vmad_property(plugin, fixture::key(), 0, 13, property("P", 3)),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(remove_vmad_property(plugin, fixture::key(), 0, 12),
            VmadStructureMutationStatus::index_out_of_range);
  EXPECT_EQ(replace_vmad_property(plugin, fixture::key(), 0, 12, property("P", 3)),
            VmadStructureMutationStatus::index_out_of_range);

  EXPECT_THROW(add_vmad_script(plugin, fixture::key(), 0, std::string(65536, 'a'), 0),
               schema::ScalarError);
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, property(std::string(65536, 'a'), 3)),
               schema::ScalarError);
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, property("Bad", 6)),
               schema::ScalarError);
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, property("Bad", 18)),
               schema::ScalarError);
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, property("Bad", 4)),
               schema::ScalarError);
  VmadProperty infinite = property("Bad", 4);
  infinite.real = std::numeric_limits<float>::infinity();
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, infinite), schema::ScalarError);
  VmadProperty structless = property("Bad", 7);
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, structless), schema::ScalarError);
  VmadProperty nested = property("Bad", 7);
  VmadStruct inner;
  inner.members = {member("inner", 7)};
  nested.struct_elements = std::vector<VmadStruct>{inner};
  EXPECT_THROW(replace_vmad_property(plugin, fixture::key(), 0, 10, nested), schema::ScalarError);
  VmadProperty nonempty = property("Bad", 16);
  nonempty.element_count = 1;
  EXPECT_THROW(replace_vmad_property(plugin, fixture::key(), 0, 11, nonempty), schema::ScalarError);
  VmadProperty no_values = property("Bad", 13);
  EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, no_values), schema::ScalarError);

  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(VmadStructure, MalformedDuplicateAndUnsupportedPayloadsRefuseStructureEdits) {
  const auto valid = fixture::payload();
  const auto properties = read_vmad_scripts(valid).scripts[0].properties;
  ASSERT_EQ(properties.size(), 12u);

  for (int mode = 0; mode < 5; ++mode) {
    SCOPED_TRACE("mode=" + std::to_string(mode));
    auto bytes = valid;
    if (mode == 0) bytes[properties[2].value_offset - 2] = std::byte{6};
    if (mode == 1) bytes.push_back(std::byte{0xAB});
    if (mode == 2) bytes[0] = std::byte{7};
    if (mode == 3) bytes[2] = std::byte{3};
    if (mode == 4) bytes.pop_back();
    auto plugin = read_plugin(fixture::plugin_bytes(bytes), "Vmad.esp");
    const auto before = write_plugin(plugin);
    EXPECT_THROW(add_vmad_script(plugin, fixture::key(), 0, "X", 0), schema::ScalarError);
    EXPECT_THROW(remove_vmad_script(plugin, fixture::key(), 0), schema::ScalarError);
    EXPECT_THROW(add_vmad_property(plugin, fixture::key(), 0, 12, property("P", 3)),
                 schema::ScalarError);
    EXPECT_THROW(remove_vmad_property(plugin, fixture::key(), 0, 0), schema::ScalarError);
    EXPECT_THROW(replace_vmad_property(plugin, fixture::key(), 0, 0, property("P", 3)),
                 schema::ScalarError);
    EXPECT_EQ(write_plugin(plugin), before);
  }

  auto duplicate = read_plugin(fixture::plugin_bytes(valid, "REFR", true), "Vmad.esp");
  EXPECT_THROW(add_vmad_script(duplicate, fixture::key(), 0, "X", 0), schema::ScalarError);
  EXPECT_EQ(add_vmad(duplicate, fixture::key()), VmadStructureMutationStatus::already_present);
}

TEST(VmadStructure, StructureEditsPreserveTheQuestFragmentTail) {
  const auto tail = quest_tail();
  auto data = fixture::payload();
  data.insert(data.end(), tail.begin(), tail.end());
  auto plugin = read_plugin(fixture::plugin_bytes(data, "QUST"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(data, sig_qust).complete());

  ASSERT_EQ(add_vmad_script(plugin, fixture::key(), 1, "Extra", 0),
            VmadStructureMutationStatus::updated);
  VmadProperty count = property("StageCount", 3);
  count.integer = 3;
  ASSERT_EQ(add_vmad_property(plugin, fixture::key(), 1, 0, count),
            VmadStructureMutationStatus::updated);

  const auto reparsed = read_plugin(write_plugin(plugin), "Vmad.esp");
  const auto& saved = fixture::subrecord(reparsed).data;
  EXPECT_TRUE(vmad_coverage(saved, sig_qust).complete());
  EXPECT_EQ(slice(saved, saved.size() - tail.size(), saved.size()), tail);
  const auto scripts = read_vmad_scripts(saved);
  ASSERT_EQ(scripts.scripts.size(), 2u);
  EXPECT_EQ(scripts.scripts[0].name, "Script");
  EXPECT_EQ(scripts.scripts[1].name, "Extra");
  EXPECT_EQ(scripts.trailing_bytes, tail.size());

  EXPECT_EQ(remove_vmad_script(plugin, fixture::key(), 0), VmadStructureMutationStatus::updated);
  EXPECT_EQ(remove_vmad_script(plugin, fixture::key(), 0), VmadStructureMutationStatus::updated);
  const auto emptied = read_plugin(write_plugin(plugin), "Vmad.esp");
  const auto& now = fixture::subrecord(emptied).data;
  EXPECT_TRUE(vmad_coverage(now, sig_qust).complete());
  EXPECT_EQ(now.size(), 6u + tail.size());
  EXPECT_EQ(slice(now, 6, now.size()), tail);
}
