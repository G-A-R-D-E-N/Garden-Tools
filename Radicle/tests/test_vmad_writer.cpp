#include "fo4/plugin/vmad_writer.hpp"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace fo4;
namespace fixture = fo4::test::vmad;

TEST(VmadWriter, ScalarsAndArrayElementsRoundtripBothObjectFormats) {
  for (const std::uint16_t format : {std::uint16_t{1}, std::uint16_t{2}}) {
    auto plugin = read_plugin(fixture::plugin_bytes(fixture::payload(format)), "Vmad.esp");
    const auto* other = find_record(plugin, fixture::key(0x801));
    ASSERT_NE(other, nullptr);
    const auto other_bytes = test::record_original_bytes(plugin, *other);
    const auto before = fixture::subrecord(plugin).data;
    const auto old_scripts = read_vmad_scripts(before);
    const VmadValue values[] = {VmadObjectValue{fixture::key(0x801), 9}, std::string("longer text"),
                                std::int32_t{-200}, 4.25f, true};
    for (std::size_t type = 0; type < 5; ++type) {
      EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, type * 2, values[type]), ScalarMutationStatus::updated);
      EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, type * 2, values[type]), ScalarMutationStatus::unchanged);
      EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, type * 2 + 1, values[type], 0), ScalarMutationStatus::updated);
      EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, type * 2 + 1, values[type], 0), ScalarMutationStatus::unchanged);
    }
    const auto saved = write_plugin(plugin);
    const auto reparsed = read_plugin(saved, "Vmad.esp");
    const auto& data = fixture::subrecord(reparsed).data;
    const auto scripts = read_vmad_scripts(data);
    ASSERT_EQ(scripts.scripts.size(), 1u);
    const auto& properties = scripts.scripts[0].properties;
    EXPECT_EQ(scripts.scripts[0].flags, 3);
    ASSERT_EQ(properties.size(), 12u);
    EXPECT_EQ(properties[0].alias, 9);
    EXPECT_EQ(properties[0].object->value(), 0x801u);
    EXPECT_EQ(properties[2].text, "longer text");
    EXPECT_EQ(properties[4].integer, -200);
    EXPECT_EQ(properties[6].real, 4.25f);
    EXPECT_EQ(properties[8].boolean, true);
    for (std::size_t i = 0; i < properties.size(); ++i) {
      const auto& old = old_scripts.scripts[0].properties[i];
      const auto& now = properties[i];
      EXPECT_EQ(now.type, old.type); EXPECT_EQ(now.name, old.name); EXPECT_EQ(now.flags, old.flags);
      EXPECT_EQ(now.element_count, old.element_count);
      if (i < 10 && i % 2 == 1) {
        const auto old_first = old.value_offset + 4;
        const auto now_first = now.value_offset + 4;
        const std::size_t old_width = i == 1 ? 8 : i == 3 ? 5 : i == 9 ? 1 : 4;
        const std::size_t new_width = i == 3 ? 13 : old_width;
        EXPECT_EQ(std::vector<std::byte>(before.begin() + old_first + old_width, before.begin() + old.value_offset + old.value_size),
                  std::vector<std::byte>(data.begin() + now_first + new_width, data.begin() + now.value_offset + now.value_size));
      }
    }
    const auto padding = properties[0].value_offset + (format == 2 ? 0 : 6);
    EXPECT_EQ(data[padding], std::byte{0}); EXPECT_EQ(data[padding + 1], std::byte{0});
    EXPECT_EQ(find_record(reparsed, fixture::key())->subrecords[1].data,
              find_record(plugin, fixture::key())->subrecords[1].data);
    EXPECT_EQ(test::record_original_bytes(reparsed, *find_record(reparsed, fixture::key(0x801))), other_bytes);
  }
}

TEST(VmadWriter, RefusalsAreAtomicAndMissingIndicesAreDistinct) {
  auto plugin = read_plugin(fixture::plugin_bytes(), "Vmad.esp");
  const auto before = write_plugin(plugin);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(0x999), 0, 4, std::int32_t{1}), ScalarMutationStatus::record_missing);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 1, 4, std::int32_t{1}), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 12, std::int32_t{1}), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 5, std::int32_t{1}, 2), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 4, std::int32_t{1}, 1), ScalarMutationStatus::subrecord_missing);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 4, true), schema::ScalarError);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 10, std::int32_t{1}), schema::ScalarError);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 11, std::int32_t{1}), schema::ScalarError);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 6, std::numeric_limits<float>::infinity()), schema::ScalarError);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 2, std::string(65536, 'a')), schema::ScalarError);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 0, VmadObjectValue{fixture::key(0x802), -1}), schema::ScalarError);
  EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 0, VmadObjectValue{fixture::key(0x801, "Missing.esm"), -1}), FormIdError);
  EXPECT_EQ(write_plugin(plugin), before);
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 0, VmadObjectValue{FormKey{}, -1}), ScalarMutationStatus::updated);
  EXPECT_EQ(read_vmad_scripts(fixture::subrecord(plugin).data).scripts[0].properties[0].object->value(), 0u);
}

TEST(VmadWriter, UnknownMalformedDuplicateAndFutureLayoutsRefuseMutation) {
  const auto valid = fixture::payload();
  const auto props = read_vmad_scripts(valid).scripts[0].properties;
  for (int mode = 0; mode < 6; ++mode) {
    auto bytes = valid;
    if (mode == 0) bytes.push_back(std::byte{9});
    if (mode == 1) bytes.pop_back();
    if (mode == 2) bytes[0] = std::byte{7};
    if (mode == 3) bytes[2] = std::byte{3};
    if (mode == 4) bytes[props[2].value_offset - 2] = std::byte{6};
    auto plugin = read_plugin(fixture::plugin_bytes(bytes, "REFR", mode == 5), "Vmad.esp");
    const auto before = write_plugin(plugin);
    EXPECT_THROW(set_vmad_property(plugin, fixture::key(), 0, 4, std::int32_t{9}), schema::ScalarError);
    EXPECT_EQ(write_plugin(plugin), before);
  }
}

TEST(VmadWriter, BoolWriteReplacesAnyStoredByteWithZeroOrOne) {
  auto data = fixture::payload();
  const auto offset = read_vmad_scripts(data).scripts[0].properties[8].value_offset;
  data[offset] = std::byte{2};
  auto plugin = read_plugin(fixture::plugin_bytes(data), "Vmad.esp");
  const auto before = fixture::subrecord(plugin).data;
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 8, true), ScalarMutationStatus::updated);
  EXPECT_EQ(fixture::subrecord(plugin).data[offset], std::byte{1});
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 8, false), ScalarMutationStatus::updated);
  EXPECT_EQ(fixture::subrecord(plugin).data[offset], std::byte{0});
  auto expected = before;
  expected[offset] = std::byte{0};
  EXPECT_EQ(fixture::subrecord(plugin).data, expected);
}

TEST(VmadWriter, LengthChangingStringPreservesKnownQuestFragmentTail) {
  auto data = fixture::payload();
  test::ByteWriter tail; tail.u8(3); tail.u16(1); tail.u16(0);
  tail.u16(12); tail.u16(0x1234); tail.i32(-3); tail.u8(7);
  fixture::name(tail, "FragmentScript"); fixture::name(tail, "Fragment_12"); tail.u16(0);
  data.insert(data.end(), tail.bytes.begin(), tail.bytes.end());
  auto plugin = read_plugin(fixture::plugin_bytes(data, "QUST"), "Vmad.esp");
  ASSERT_TRUE(vmad_coverage(data, sig_qust).complete());
  EXPECT_EQ(set_vmad_property(plugin, fixture::key(), 0, 2, std::string(65535, 'a')), ScalarMutationStatus::updated);
  auto reparsed = read_plugin(write_plugin(plugin), "Vmad.esp");
  const auto& saved = fixture::subrecord(reparsed).data;
  EXPECT_TRUE(vmad_coverage(saved, sig_qust).complete());
  EXPECT_EQ(std::vector<std::byte>(saved.end() - tail.bytes.size(), saved.end()), tail.bytes);
  EXPECT_EQ(read_vmad_scripts(saved).scripts[0].properties[2].text->size(), 65535u);
}

}
